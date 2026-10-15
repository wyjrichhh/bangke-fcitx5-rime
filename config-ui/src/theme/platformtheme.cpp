#include "platformtheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include <algorithm>

#include "paths.h"

namespace bangke {
namespace {

// ---------------------------------------------------------------- 配置解析

// colors.toml / shell.toml 都只有「[section] + key = value」这一种形状
// （Omarchy 自己生成的），逐行解析就够，不引 TOML 库。
//
// 扁平化成 "section.key"；根键（colors.toml 的 mode/background/…）就是 "key"。
// 保留原始文本 —— 值可能是颜色、可能是**另一个 token 的名字**
// （shell.toml.tpl 里 `border = "hyprland.active-border"` 就是），也可能是一段
// 梯度，得等到解析阶段再决定怎么读。
class ShellConf {
 public:
  static ShellConf load(const QString& path) {
    ShellConf conf;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return conf;
    QTextStream stream(&file);
    stream.setCodec("UTF-8");

    QString section;
    while (!stream.atEnd()) {
      const QString line = stripComment(stream.readLine()).trimmed();
      if (line.isEmpty()) continue;
      if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
        section = line.mid(1, line.size() - 2).trimmed();
        continue;
      }
      const int eq = line.indexOf(QLatin1Char('='));
      if (eq <= 0) continue;
      const QString key = line.left(eq).trimmed();
      if (key.isEmpty()) continue;
      conf.values_.insert(section.isEmpty() ? key
                                            : section + QLatin1Char('.') + key,
                          unquote(line.mid(eq + 1).trimmed()));
    }
    conf.loaded_ = true;
    return conf;
  }

  bool loaded() const { return loaded_; }
  bool contains(const QString& key) const { return values_.contains(key); }
  QString value(const QString& key, const QString& fallback = QString()) const {
    const auto it = values_.constFind(key);
    return it == values_.constEnd() ? fallback : it.value();
  }

 private:
  // 行内注释要认，但 `background = "#1a1b26"` 里的 '#' 不是注释。
  static QString stripComment(const QString& line) {
    bool inQuote = false;
    for (int i = 0; i < line.size(); ++i) {
      const QChar ch = line.at(i);
      if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
        inQuote = !inQuote;
      } else if (ch == QLatin1Char('#') && !inQuote) {
        return line.left(i);
      }
    }
    return line;
  }

  static QString unquote(const QString& raw) {
    if (raw.size() >= 2 &&
        ((raw.startsWith(QLatin1Char('"')) && raw.endsWith(QLatin1Char('"'))) ||
         (raw.startsWith(QLatin1Char('\'')) && raw.endsWith(QLatin1Char('\''))))) {
      return raw.mid(1, raw.size() - 2);
    }
    return raw;
  }

  QHash<QString, QString> values_;
  bool loaded_ = false;
};

// ---------------------------------------------------------------- 颜色解析

// 梯度是「若干个颜色 + 一个角度」，颜色侧只需要第一站 —— 复刻 Omarchy
// Color.qml 的 firstColorToken()。
QString firstColorToken(const QString& value) {
  static const QRegularExpression angle(
      QStringLiteral("^-?[0-9]+(\\.[0-9]+)?deg$"));
  const QStringList parts =
      value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  for (const QString& part : parts) {
    if (!angle.match(part).hasMatch()) return part;
  }
  return value;
}

int hexPair(const QString& text, int index) {
  return text.mid(index, 2).toInt(nullptr, 16);
}

// 认这几种写法：#rrggbb、#rrggbbaa、rgb(r,g,b)、rgba(r,g,b,a)、
// rgba(RRGGBBAA)（Hyprland 自己的形态，theme 里到处是它）。
QColor parseShellColor(const QString& token) {
  const QString text = token.trimmed();
  if (text.isEmpty()) return QColor();

  if (text.startsWith(QLatin1Char('#'))) {
    if (text.size() == 9) {
      QColor color(text.left(7));
      if (color.isValid()) color.setAlpha(hexPair(text, 7));
      return color;
    }
    return QColor(text);
  }

  static const QRegularExpression functional(
      QStringLiteral("^rgba?\\(([^)]*)\\)$"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = functional.match(text);
  if (!match.hasMatch()) return QColor();

  const QStringList parts = match.captured(1).split(QLatin1Char(','));
  if (parts.size() == 1) {
    // rgba(RRGGBBAA) 这一种：整段当一个 hex 读。
    const QString hex = parts.first().trimmed();
    if (hex.size() != 6 && hex.size() != 8) return QColor();
    QColor color(QLatin1Char('#') + hex.left(6));
    if (color.isValid() && hex.size() == 8) color.setAlpha(hexPair(hex, 6));
    return color;
  }
  if (parts.size() < 3) return QColor();

  QColor color(parts.at(0).trimmed().toInt(), parts.at(1).trimmed().toInt(),
               parts.at(2).trimmed().toInt());
  if (!color.isValid()) return QColor();
  if (parts.size() >= 4) {
    const QString raw = parts.at(3).trimmed();
    const double value = raw.toDouble();
    // 0..1 的浮点与 0..255 的整数两种都得吃。
    color.setAlpha(qBound(0, qRound(raw.contains(QLatin1Char('.')) || value <= 1.0
                                        ? value * 255.0
                                        : value),
                          255));
  }
  return color;
}

// 按 Omarchy Color.qml 的语义取值：值可能是一个**角色名**（foreground/accent/…），
// 也可能指向 shell.toml 里另一个键（hyprland.active-border），还可能直接是颜色。
// 顺着引用链走，撞到环就用兜底色收场。
class Resolver {
 public:
  Resolver(const ShellConf& conf, const QHash<QString, QColor>& roles)
      : conf_(conf), roles_(roles) {}

  QColor resolve(const QString& value, const QColor& fallback) const {
    QSet<QString> seen;
    QString token = firstColorToken(value);
    QString role = token.trimmed().toLower();
    // 值同时等于键名时停手 —— 这一步照抄 Color.qml，否则自指的键会绕不出来。
    while (conf_.contains(role) && conf_.value(role) != token) {
      if (seen.contains(role)) return fallback;
      seen.insert(role);
      token = firstColorToken(conf_.value(role));
      role = token.trimmed().toLower();
    }
    if (roles_.contains(role)) return roles_.value(role);
    if (role == QLatin1String("transparent")) return QColor(0, 0, 0, 0);
    const QColor parsed = parseShellColor(token);
    return parsed.isValid() ? parsed : fallback;
  }

  QColor pick(const QString& key, const QColor& fallback) const {
    return conf_.contains(key) ? resolve(conf_.value(key), fallback) : fallback;
  }

  double pickAlpha(const QString& key, double fallback) const {
    if (!conf_.contains(key)) return fallback;
    bool ok = false;
    const double value = conf_.value(key).toDouble(&ok);
    return ok ? std::clamp(value, 0.0, 1.0) : fallback;
  }

  int pickInt(const QString& key, int fallback) const {
    if (!conf_.contains(key)) return fallback;
    bool ok = false;
    const int value = conf_.value(key).toInt(&ok);
    return ok ? value : fallback;
  }

  // 颜色 + 它的 -alpha 伴生键（Omarchy 的 composed()）。
  QColor compose(const QString& colorKey, const QString& alphaKey,
                 const QColor& colorFallback, double alphaFallback) const {
    QColor color = pick(colorKey, colorFallback);
    color.setAlphaF(color.alphaF() * pickAlpha(alphaKey, alphaFallback));
    return color;
  }

 private:
  const ShellConf& conf_;
  const QHash<QString, QColor>& roles_;
};

QColor withAlpha(const QColor& color, double factor) {
  QColor out = color;
  out.setAlphaF(std::clamp(color.alphaF() * factor, 0.0, 1.0));
  return out;
}

// Adwaita / Yaru 的中性色。烘焙路径用它，Omarchy 主题缺某个键时也退到它 ——
// 两边同一份值，所以「没有 Omarchy 时观感不变」是构造上成立的，不是巧合。
QColor bakedWindow(bool dark) { return dark ? QColor(0x33, 0x33, 0x33) : QColor(0xFC, 0xFC, 0xFC); }
QColor bakedText(bool dark) { return dark ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x2E, 0x34, 0x36); }

// ---------------------------------------------------------------- 桌面探测

// 系统深浅：先问 GNOME（gsettings），问不到再退回 Qt 调色板。
// 这是 Ubuntu/GNOME 那条线的老逻辑，原样搬过来（原来在 ui/themepalette.cpp）。
bool detectDark() {
  QProcess process;
  process.start(QStringLiteral("gsettings"),
                {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                 QStringLiteral("color-scheme")});
  if (process.waitForFinished(1500)) {
    const QString out = QString::fromLocal8Bit(process.readAllStandardOutput());
    if (out.contains(QLatin1String("dark"))) return true;
    if (out.contains(QLatin1String("light"))) return false;
  }
  if (!QGuiApplication::instance()) return true;
  return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

bool onUbuntu() {
  static const bool ubuntu = []() {
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    return QString::fromUtf8(file.readAll()).contains(QLatin1String("ID=ubuntu"));
  }();
  return ubuntu;
}

// 系统强调色：GNOME 46 起有 accent-color，取不到时在 Ubuntu 上退回 Yaru 的橙
// —— 比 Qt 默认那个蓝更像 Ubuntu 原生。
QColor systemAccent() {
  QProcess process;
  process.start(QStringLiteral("gsettings"),
                {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                 QStringLiteral("accent-color")});
  if (process.waitForFinished(1500)) {
    QString name = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    name.remove(QLatin1Char('\''));
    static const QHash<QString, QColor> kAccents = {
        {QStringLiteral("blue"), QColor(0x35, 0x84, 0xE4)},
        {QStringLiteral("teal"), QColor(0x21, 0x9A, 0x8B)},
        {QStringLiteral("green"), QColor(0x3A, 0x94, 0x4A)},
        {QStringLiteral("yellow"), QColor(0xF5, 0xC2, 0x11)},
        {QStringLiteral("orange"), QColor(0xE9, 0x54, 0x20)},
        {QStringLiteral("red"), QColor(0xE0, 0x1B, 0x24)},
        {QStringLiteral("pink"), QColor(0xD5, 0x61, 0x99)},
        {QStringLiteral("purple"), QColor(0x91, 0x41, 0xAC)},
        {QStringLiteral("slate"), QColor(0x6F, 0x83, 0x96)},
    };
    if (kAccents.contains(name)) return kAccents.value(name);
  }
  if (onUbuntu()) return QColor(0xE9, 0x54, 0x20);  // Yaru 的 Ubuntu 橙
  if (!QGuiApplication::instance()) return QColor(0x35, 0x84, 0xE4);
  const QColor pal = QGuiApplication::palette().color(QPalette::Highlight);
  return pal.isValid() ? pal : QColor(0x35, 0x84, 0xE4);
}

// Hyprland 的窗口圆角。**只在会话里有 Hyprland 时才问**，且失败不致命：
// 拿不到就留 -1，颜色照常生效。
int hyprlandRounding() {
  if (qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) return -1;
  QProcess process;
  process.start(QStringLiteral("hyprctl"),
                {QStringLiteral("-j"), QStringLiteral("getoption"),
                 QStringLiteral("decoration:rounding")});
  if (!process.waitForFinished(1500)) return -1;
  const QJsonObject object =
      QJsonDocument::fromJson(process.readAllStandardOutput()).object();
  const int value = object.value(QStringLiteral("int")).toInt(-1);
  return value >= 0 ? value : -1;
}

QString readFileText(const QString& path) {
  if (path.isEmpty()) return QString();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
  return QString::fromUtf8(file.readAll()).trimmed();
}

QString colorText(const QColor& color) {
  if (!color.isValid()) return QStringLiteral("-");
  return color.alpha() == 255 ? color.name(QColor::HexRgb)
                              : color.name(QColor::HexArgb);
}

}  // namespace

// ---------------------------------------------------------------- 公开接口

PlatformTheme bakedPlatformTheme(bool dark) {
  PlatformTheme theme;
  theme.valid = false;
  theme.dark = dark;
  theme.source = ThemeSource::BakedNative;

  // 中性色取 Adwaita / Yaru 的常用值，而不是自己编的灰 —— 跟系统其它应用
  // 摆在一起才不违和。这几组值就是 2026-10 之前 themepalette.cpp 里的常量，
  // 一个都没动，Ubuntu 线的观感因此逐字节不变。
  theme.window = bakedWindow(dark);
  theme.sidebar = dark ? QColor(0x3D, 0x3D, 0x3D) : QColor(0xF0, 0xF0, 0xF0);
  theme.card = dark ? QColor(0x46, 0x46, 0x46) : QColor(0xFF, 0xFF, 0xFF);
  theme.separator = dark ? QColor(0xFF, 0xFF, 0xFF, 0x2B) : QColor(0x00, 0x00, 0x00, 0x1F);
  theme.text = bakedText(dark);
  theme.secondaryText =
      dark ? QColor(0xFF, 0xFF, 0xFF, 0xC4) : QColor(0x2E, 0x34, 0x36, 0x9E);
  theme.well = dark ? QColor(0x62, 0x62, 0x62) : QColor(0xDC, 0xDC, 0xDC);
  theme.accent = systemAccent();
  theme.sidebarSelection = withAlpha(theme.accent, 0.22);

  // panelRoles 留空 = 「不走系统主题」，resolve() 会退回 native.h 的烘焙常量。
  return theme;
}

PlatformTheme loadPlatformTheme(const QString& dir) {
  const QString themeDir = dir.isEmpty() ? paths::omarchyThemeDir() : dir;
  const ShellConf colors = ShellConf::load(themeDir + QStringLiteral("/colors.toml"));
  const QString mode = colors.value(QStringLiteral("mode"));
  if (!colors.loaded()) return PlatformTheme();
  const QColor base = parseShellColor(colors.value(QStringLiteral("background")));
  const QColor ink = parseShellColor(colors.value(QStringLiteral("foreground")));
  if (!base.isValid() || !ink.isValid()) return PlatformTheme();

  PlatformTheme theme;
  theme.valid = true;
  theme.source = ThemeSource::Omarchy;
  theme.dark = mode.isEmpty() ? base.lightnessF() < ink.lightnessF()
                            : mode.compare(QLatin1String("light"), Qt::CaseInsensitive) != 0;
  theme.name = readFileText(themeDir + QStringLiteral("/theme.name"));

  // 底色五个角色：shell.toml 里用角色名引用它们，值取自 colors.toml。
  const QColor background = parseShellColor(colors.value(QStringLiteral("background")));
  const QColor foreground = parseShellColor(colors.value(QStringLiteral("foreground")));
  const QColor accent = parseShellColor(colors.value(QStringLiteral("accent")));
  const QColor muted = parseShellColor(colors.value(QStringLiteral("muted")));
  const QColor urgent = parseShellColor(colors.value(QStringLiteral("red")));

  QHash<QString, QColor> roles;
  roles.insert(QStringLiteral("background"), background);
  roles.insert(QStringLiteral("foreground"), foreground);
  roles.insert(QStringLiteral("accent"), accent);
  roles.insert(QStringLiteral("muted"), muted);
  roles.insert(QStringLiteral("urgent"), urgent);
  roles.insert(QStringLiteral("text"), foreground);  // Color.qml 里 text 就是 foreground

  const ShellConf shell = ShellConf::load(themeDir + QStringLiteral("/shell.toml"));
  const Resolver resolve(shell, roles);

  // ---- 候选面板的角色色。键名与 bangke.yaml 的方案键一一对应，
  // BangkeConfig::resolve("native") 直接吃这一份。
  //
  // 外壳取 [popups]（面板是一张浮动弹出卡片），选中行取 [menu]（"有一个高亮行的
  // 键盘选择列表"这组语义只存在于 [menu]）。这正是原生工具包给组合框弹层配色的方式。
  const QColor popupText = resolve.pick(QStringLiteral("popups.text"), foreground);
  const QColor popupBorder =
      resolve.compose(QStringLiteral("popups.border"),
                      QStringLiteral("popups.border-alpha"), accent, 1.0);
  const QColor selectedText = resolve.pick(QStringLiteral("menu.selected-text"), accent);
  const QColor selectedBackground =
      resolve.compose(QStringLiteral("menu.selected-background"),
                      QStringLiteral("menu.selected-background-alpha"), foreground, 0.08);

  theme.panelRoles.insert(QStringLiteral("back_color"),
                          resolve.compose(QStringLiteral("popups.background"),
                                          QStringLiteral("popups.background-alpha"),
                                          background, 1.0));
  theme.panelRoles.insert(QStringLiteral("text_color"), popupText);
  theme.panelRoles.insert(QStringLiteral("candidate_text_color"), popupText);
  theme.panelRoles.insert(QStringLiteral("hilited_text_color"), selectedText);
  theme.panelRoles.insert(QStringLiteral("hilited_candidate_text_color"), selectedText);
  theme.panelRoles.insert(QStringLiteral("hilited_candidate_back_color"),
                          selectedBackground);
  // 序号与注释是次要文字，Omarchy 里对应 muted。
  theme.panelRoles.insert(QStringLiteral("label_color"), muted);
  theme.panelRoles.insert(QStringLiteral("comment_text_color"), muted);
  theme.panelRoles.insert(
      QStringLiteral("hilited_candidate_label_color"), withAlpha(selectedText, 0.75));
  theme.panelRoles.insert(QStringLiteral("hilited_comment_text_color"),
                          withAlpha(selectedText, 0.75));
  theme.panelRoles.insert(QStringLiteral("border_color"), popupBorder);
  theme.panelRoles.insert(QStringLiteral("accent_text_color"), accent);

  // ---- 设置程序自己的观感。注意这与上面的 panelRoles 是两件事：面板可以用
  // solarized，设置窗仍然是 Omarchy 原生。
  const QColor darker =
      parseShellColor(colors.value(QStringLiteral("darker_background")));
  const QColor dark = parseShellColor(colors.value(QStringLiteral("dark_background")));
  const QColor lighter =
      parseShellColor(colors.value(QStringLiteral("lighter_background")));
  theme.sidebar = darker.isValid() ? darker : (dark.isValid() ? dark : background);
  theme.card = lighter.isValid() ? lighter : background;
  theme.window = background.isValid() ? background : bakedWindow(theme.dark);
  theme.text = foreground.isValid() ? foreground : bakedText(theme.dark);
  theme.secondaryText = muted.isValid() ? muted : withAlpha(theme.text, 0.7);
  theme.accent = accent.isValid() ? accent : systemAccent();
  theme.separator = resolve.compose(QStringLiteral("popups.border"),
                                    QStringLiteral("popups.border-alpha"),
                                    theme.secondaryText, 0.1);
  theme.sidebarSelection = selectedBackground;
  // 滑杆槽：controls 的 normal-fill-alpha(0.04) 是给"有描边的控件"当填充用的，
  // 单独拿来当槽色太淡，取一档介于它与 selected(0.18) 之间的值。
  theme.well = withAlpha(resolve.pick(QStringLiteral("controls.normal-color"), foreground),
                         0.12);

  // ---- 度量。px 原生，交 buildPanelSpec 在 ptToPx 之后注入。
  theme.metrics.valid = true;
  // 问不到 Hyprland 就留 -1（"没意见"），让用户自己的设置接管 —— 硬定成 0
  // 会把用户的圆角无声抹平，而 Omarchy 的 rounding 到底是 0 还是 6 只有 hyprctl 知道。
  theme.metrics.cornerRadiusPx = hyprlandRounding();
  // 缺省 2 = Omarchy 的 Style.space(2)：Menu.qml / NotificationCard.qml 里
  // surfaceSpec(..., Math.max(1, Style.space(2))) 就是这个值。
  theme.metrics.borderWidthPx = resolve.pickInt(QStringLiteral("popups.border-width"), 2);
  theme.metrics.textPadPx = resolve.pickInt(QStringLiteral("spacing.control-padding-y"), 6);

  theme.controlHeight = resolve.pickInt(QStringLiteral("spacing.control-height"), 28);
  theme.rowGap = resolve.pickInt(QStringLiteral("spacing.row-gap"), 8);
  theme.controlPadX = resolve.pickInt(QStringLiteral("spacing.control-padding-x"), 10);
  theme.controlPadY = theme.metrics.textPadPx;
  theme.labelGap = resolve.pickInt(QStringLiteral("spacing.label-gap"), 4);
  theme.panelPadding = resolve.pickInt(QStringLiteral("spacing.panel-padding"), 18);
  theme.uiFontPx = resolve.pickInt(QStringLiteral("font.base-size"), 12);

  return theme;
}

PlatformTheme currentPlatformTheme() {
  const PlatformTheme omarchy = loadPlatformTheme();
  if (omarchy.valid) return omarchy;
  return bakedPlatformTheme(detectDark());
}

namespace {
PlatformTheme g_appPlatformTheme;
bool g_appPlatformThemeSet = false;
}  // namespace

void setAppPlatformTheme(const PlatformTheme& theme) {
  g_appPlatformTheme = theme;
  g_appPlatformThemeSet = true;
}

const PlatformTheme& appPlatformTheme() {
  // 忘了注入就自补一次，而不是返回一份颜色无效的主题（那会让整个界面变黑）。
  // 探测结果会留在这里，所以最多跑一次。
  if (!g_appPlatformThemeSet) setAppPlatformTheme(currentPlatformTheme());
  return g_appPlatformTheme;
}

QString platformThemeToText(const PlatformTheme& theme) {
  QStringList out;
  const char* source = "gnome-fallback";
  if (theme.source == ThemeSource::Omarchy) source = "omarchy";
  else if (theme.source == ThemeSource::BakedNative) source = "baked-native";
  out << QStringLiteral("source=%1 valid=%2 dark=%3 name=%4")
             .arg(QLatin1String(source),
                  theme.valid ? QStringLiteral("true") : QStringLiteral("false"),
                  theme.dark ? QStringLiteral("true") : QStringLiteral("false"),
                  theme.name.isEmpty() ? QStringLiteral("-") : theme.name);
  out << QStringLiteral("window=%1 sidebar=%2 card=%3 well=%4")
             .arg(colorText(theme.window), colorText(theme.sidebar),
                  colorText(theme.card), colorText(theme.well));
  out << QStringLiteral("text=%1 secondary=%2 separator=%3 accent=%4 selection=%5")
             .arg(colorText(theme.text), colorText(theme.secondaryText),
                  colorText(theme.separator), colorText(theme.accent),
                  colorText(theme.sidebarSelection));
  out << QStringLiteral("metrics.valid=%1 cornerRadiusPx=%2 borderWidthPx=%3 textPadPx=%4")
             .arg(theme.metrics.valid ? QStringLiteral("true") : QStringLiteral("false"))
             .arg(theme.metrics.cornerRadiusPx)
             .arg(theme.metrics.borderWidthPx)
             .arg(theme.metrics.textPadPx);
  out << QStringLiteral("controlHeight=%1 rowGap=%2 controlPadX=%3 labelGap=%4 "
                        "panelPadding=%5 uiFontPx=%6")
             .arg(theme.controlHeight)
             .arg(theme.rowGap)
             .arg(theme.controlPadX)
             .arg(theme.labelGap)
             .arg(theme.panelPadding)
             .arg(theme.uiFontPx);

  QStringList keys = theme.panelRoles.keys();
  std::sort(keys.begin(), keys.end());
  for (const QString& key : keys) {
    out << QStringLiteral("panel.%1=%2").arg(key, colorText(theme.panelRoles.value(key)));
  }
  return out.join(QLatin1Char('\n'));
}

}  // namespace bangke
