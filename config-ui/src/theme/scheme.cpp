#include "scheme.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include "paths.h"
#include <QList>
#include <QPair>
#include <QTextStream>

#include "color.h"
#include "native.h"

namespace bangke {
namespace {

QStringList readLines(const QString& path) {
  if (path.isEmpty()) return {};
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  QTextStream ts(&f);
  ts.setCodec("UTF-8");
  QStringList out;
  while (!ts.atEnd()) out << ts.readLine();
  return out;
}

int indentOf(const QString& line) {
  int n = 0;
  while (n < line.size() && line.at(n) == QLatin1Char(' ')) ++n;
  return n;
}

// 去掉行内注释。'#' 只有「在行首」或「前面是空白、后面也是空白或行尾」
// 才算注释 —— 否则会把 `back_color: #eaf1fa` 这种以 '#' 开头的颜色值吃掉
// （曾导致整行被当成段头，自定义配色静默失效）。
QString stripComment(const QString& line) {
  QChar quote;
  bool escaped=false;
  for (int i=0;i<line.size();++i) {
    const QChar ch=line.at(i);
    if (escaped) {escaped=false;continue;}
    if (quote==QLatin1Char('"') && ch==QLatin1Char('\\')) {escaped=true;continue;}
    if (!quote.isNull()) {if(ch==quote) quote=QChar();continue;}
    if(ch==QLatin1Char('"') || ch==QLatin1Char('\'')) {quote=ch;continue;}
    if(ch==QLatin1Char('#') && (i==0 || line.at(i-1).isSpace()) && (i+1==line.size() || line.at(i+1).isSpace())) return line.left(i);
  }
  return line;
}

QString unquote(const QString& v) {
  const QString s = v.trimmed();
  if (s.size() >= 2 &&
      ((s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\''))) ||
       (s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))))) {
    QString out = s.mid(1, s.size() - 2);
    if (s.startsWith(QLatin1Char('"'))) out.replace(QStringLiteral("\\\""), QStringLiteral("\"")).replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    else out.replace(QStringLiteral("''"), QStringLiteral("'"));
    return out;
  }
  return s;
}

// "key: value" → (key, value, 是否有值)；不是键值行返回 false。
bool splitKeyValue(const QString& line, QString* key, QString* value) {
  const QString stripped = stripComment(line);
  const int colon = stripped.indexOf(QLatin1Char(':'));
  if (colon < 0) return false;
  const QString k = stripped.left(colon).trimmed();
  if (k.isEmpty()) return false;
  *key = k;
  *value = unquote(stripped.mid(colon + 1));
  return true;
}

// 系统语义色兜底：方案没给某个颜色时 mac 会退回 AppKit 语义色，这里用
// native 那两套烘焙色顶上，与 native 方案的观感同源。
QColor semantic(bool dark, const QString& role) {
  const NativePalette& p = dark ? kNativeDark : kNativeLight;
  if (role == QLatin1String("windowBackground")) return argb(p.back);
  if (role == QLatin1String("selectedTextBackground"))
    return argb(p.hilitedCandidateBack);
  if (role == QLatin1String("label")) return argb(p.candidateText);
  return argb(p.text);  // secondaryLabel
}

// 只读 keys 里**出现过**的 style 键，其余一律沿用 base。三层叠加靠重复调用它
// 实现（出厂 → 桌面缺省 → 用户补丁），不必在每一层各写一遍默认值。
StyleOptions styleFrom(const QHash<QString, QString>& keys,
                       const StyleOptions& base) {
  const auto get = [&keys](const QString& key) { return keys.value(key); };
  const auto getInt = [&get](const QString& key, int fallback) {
    bool ok = false;
    const int v = get(key).toInt(&ok);
    return ok ? v : fallback;
  };
  const auto getBool = [&get](const QString& key, bool fallback) {
    const QString v = get(key);
    if (v.isEmpty()) return fallback;
    return v.compare(QLatin1String("false"), Qt::CaseInsensitive) != 0 &&
           v != QLatin1String("0");
  };
  const auto getDouble = [&get](const QString& key, double fallback) {
    bool ok = false;
    const double v = get(key).toDouble(&ok);
    return ok ? v : fallback;
  };

  StyleOptions s = base;
  if (!get("color_scheme").isEmpty()) s.colorScheme = get("color_scheme");
  if (!get("color_scheme_dark").isEmpty())
    s.colorSchemeDark = get("color_scheme_dark");
  if (!get("candidate_list_layout").isEmpty()) s.candidateListLayout = get("candidate_list_layout");
  if (!get("text_orientation").isEmpty())
    s.textOrientation = get("text_orientation");
  if (!get("font_face").isEmpty()) s.fontFace = get("font_face");
  s.fontPoint = getInt("font_point", s.fontPoint);
  s.alpha = qBound(0.0, getDouble("alpha", s.alpha), 1.0);
  s.cornerRadius = getInt("corner_radius", s.cornerRadius);
  s.hilitedCornerRadius = getInt("hilited_corner_radius", s.hilitedCornerRadius);
  s.borderWidth = getInt("border_width", s.borderWidth);
  s.borderHeight = getInt("border_height", s.borderHeight);
  s.lineSpacing = getInt("line_spacing", s.lineSpacing);
  s.spacing = getInt("spacing", s.spacing);
  s.shadowSize = getInt("shadow_size", s.shadowSize);
  s.inlinePreedit = getBool("inline_preedit", s.inlinePreedit);
  s.translucency = getBool("translucency", s.translucency);
  s.inlineCandidate = getBool("inline_candidate", s.inlineCandidate);
  s.memorizeSize = getBool("memorize_size", s.memorizeSize);
  s.mutualExclusive = getBool("mutual_exclusive", s.mutualExclusive);
  s.showPaging = getBool("show_paging", s.showPaging);
  if (!get("candidate_format").isEmpty())
    s.candidateFormat = get("candidate_format");
  return s;
}

}  // namespace

void BangkeConfig::parseShared(const QString& path) {
  const QStringList lines = readLines(path);
  enum Section { None, Style, Schemes } section = None;
  QString currentScheme;

  for (const QString& raw : lines) {
    const QString stripped = stripComment(raw);
    if (stripped.trimmed().isEmpty()) continue;
    const int indent = indentOf(stripped);

    if (indent == 0) {
      currentScheme.clear();
      QString key, value;
      if (!splitKeyValue(stripped, &key, &value)) continue;
      if (key == QLatin1String("style")) {
        section = Style;
      } else if (key == QLatin1String("preset_color_schemes")) {
        section = Schemes;
      } else {
        section = None;
      }
      continue;
    }

    QString key, value;
    if (!splitKeyValue(stripped, &key, &value)) continue;

    if (section == Style && indent == 2) {
      sharedStyle_.insert(key, value);
    } else if (section == Schemes) {
      if (indent == 2) {
        currentScheme = key;
        if (!schemes_.contains(currentScheme)) {
          schemes_.insert(currentScheme, {});
          schemeIds_ << currentScheme;
        }
      } else if (indent >= 4 && !currentScheme.isEmpty()) {
        schemes_[currentScheme].insert(key, value);
      }
    }
  }
}

void BangkeConfig::applyPatch(const QString& path) {
  const QStringList lines = readLines(path);
  bool inPatch = false;
  // 补丁里两种写法都要认：扁平键路径（style/color_scheme: x）与嵌套段
  // （style: 下面缩进的 color_scheme: x）。按缩进维护路径栈统一成键路径。
  QList<QPair<int, QString>> stack;

  for (const QString& raw : lines) {
    const QString stripped = stripComment(raw);
    if (stripped.trimmed().isEmpty()) continue;
    const int indent = indentOf(stripped);

    if (indent == 0) {
      stack.clear();
      QString key, value;
      if (!splitKeyValue(stripped, &key, &value)) continue;
      inPatch = (key == QLatin1String("patch"));
      continue;
    }
    if (!inPatch) continue;

    QString key, value;
    if (!splitKeyValue(stripped, &key, &value)) continue;
    while (!stack.isEmpty() && stack.last().first >= indent) stack.removeLast();

    if (value.isEmpty()) {
      stack.append({indent, key});
      continue;
    }

    // 扁平写法里键名自带斜杠（style/color_scheme），嵌套写法靠缩进栈，
    // 拼成完整路径后统一按 '/' 切开。
    QStringList levels;
    for (const auto& level : stack) levels << level.second;
    levels << key;
    const QStringList parts = levels.join(QLatin1Char('/')).split(QLatin1Char('/'));

    if (parts.size() >= 3 &&
        parts.at(0) == QLatin1String("preset_color_schemes")) {
      const QString id = parts.at(1);
      schemes_[id].insert(parts.mid(2).join(QLatin1Char('/')), value);
      if (!schemeIds_.contains(id)) schemeIds_ << id;
    } else if (parts.size() == 2 && parts.at(0) == QLatin1String("style")) {
      stylePatch_.insert(parts.at(1), value);
    }
  }
}

BangkeConfig BangkeConfig::load(const QString& sharedPath,
                                const QString& customPath,
                                PlatformTheme platform) {
  BangkeConfig config;
  config.platform_ = std::move(platform);
  config.parseShared(sharedPath);
  // Ship a complete palette catalogue; locally installed/custom themes win.
  if (config.platform_.source == ThemeSource::Omarchy || QFileInfo::exists(QStringLiteral("/usr/share/bangke-pinyin/omarchy-themes")) || (qgetenv("BANGKE_PANEL_BACKEND") == "kimpanel" || qgetenv("BANGKE_PANEL_BACKEND") == "gnome")) {
    QStringList roots{QStringLiteral("/usr/share/bangke-pinyin/omarchy-themes")};
    const QString override = QString::fromLocal8Bit(qgetenv("BK_OMARCHY_CATALOG_DIR"));
    if (!override.isEmpty()) roots = QStringList{override};
    const QString omarchy = QString::fromLocal8Bit(qgetenv("OMARCHY_PATH"));
    if (!omarchy.isEmpty()) roots << omarchy + QStringLiteral("/themes");
    roots << paths::configHome() + QStringLiteral("/omarchy/themes");
    for (const QString& root : roots) {
      for (const QString& name : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto theme = loadPlatformTheme(root + QLatin1Char('/') + name);
        if (!theme.valid) continue;
        const QString id = QStringLiteral("omarchy_") + name;
        if (!config.schemeIds_.contains(id)) config.schemeIds_ << id;
        auto& scheme = config.schemes_[id];
        scheme.insert(QStringLiteral("name"), QStringLiteral("Omarchy · ") + name);
        for (auto it=theme.panelRoles.constBegin(); it!=theme.panelRoles.constEnd(); ++it)
          if (it.value().isValid()) scheme.insert(it.key(), toRimeColor(it.value()));
      }
    }
  }
  config.applyPatch(customPath);

  // 三层叠加：出厂值（bangke.yaml）→ 桌面主题给的缺省 → 用户补丁。
  // 分两步做而不是揉进一个 get()：baselineStyle 需要先看到合并好的出厂值，
  // 而它算出来的东西又必须正好等于设置页里控件的默认显示值 —— 这样
  // 「什么算用户改过」才和「默认显示什么」是同一件事（见 baselineStyle 的注释）。
  const StyleOptions factory = styleFrom(config.sharedStyle_, StyleOptions());
  const StyleOptions baseline = baselineStyle(factory, config.platform_);
  config.style_ = styleFrom(config.stylePatch_, baseline);
  if ((config.platform_.source == ThemeSource::Omarchy || (qgetenv("BANGKE_PANEL_BACKEND") == "kimpanel" || qgetenv("BANGKE_PANEL_BACKEND") == "gnome")) && !config.stylePatch_.contains("candidate_list_layout")) {
    // Earlier Linux UI used text_orientation for the candidate-list axis.
    config.style_.candidateListLayout = config.style_.textOrientation == "vertical" ? "stacked" : "linear";
    config.style_.textOrientation = "horizontal";
  }
  return config;
}

StyleOptions baselineStyle(const StyleOptions& factory,
                           const PlatformTheme& platform) {
  if (!platform.metrics.valid) return factory;
  StyleOptions style = factory;
  const auto pxToPt = [](int px) { return qRound(px * 72.0 / 96.0); };
  if (platform.metrics.cornerRadiusPx >= 0)
    style.cornerRadius = pxToPt(platform.metrics.cornerRadiusPx);
  if (platform.metrics.borderWidthPx > 0) {
    const int pt = qMax(1, pxToPt(platform.metrics.borderWidthPx));
    style.borderWidth = pt;
    style.borderHeight = pt;
  }
  return style;
}

QList<QPair<QString, QString>> styleToPatch(const StyleOptions& style) {
  const auto number = [](int v) { return QString::number(v); };
  const auto boolean = [](bool v) { return QString(v ? "true" : "false"); };
  return {
      {QStringLiteral("color_scheme"), style.colorScheme},
      {QStringLiteral("color_scheme_dark"), style.colorSchemeDark},
      {QStringLiteral("candidate_list_layout"), style.candidateListLayout},
      {QStringLiteral("text_orientation"), style.textOrientation},
      {QStringLiteral("font_face"), style.fontFace},
      {QStringLiteral("font_point"), number(style.fontPoint)},
      {QStringLiteral("alpha"), QString::number(style.alpha, 'f', 2)},
      {QStringLiteral("corner_radius"), number(style.cornerRadius)},
      {QStringLiteral("hilited_corner_radius"), number(style.hilitedCornerRadius)},
      {QStringLiteral("border_width"), number(style.borderWidth)},
      {QStringLiteral("border_height"), number(style.borderHeight)},
      {QStringLiteral("line_spacing"), number(style.lineSpacing)},
      {QStringLiteral("shadow_size"), number(style.shadowSize)},
      {QStringLiteral("inline_preedit"), boolean(style.inlinePreedit)},
      {QStringLiteral("show_paging"), boolean(style.showPaging)},
      {QStringLiteral("translucency"), boolean(style.translucency)},
      {QStringLiteral("inline_candidate"), boolean(style.inlineCandidate)},
      {QStringLiteral("memorize_size"), boolean(style.memorizeSize)},
      {QStringLiteral("mutual_exclusive"), boolean(style.mutualExclusive)},
      {QStringLiteral("spacing"), number(style.spacing)},
      {QStringLiteral("candidate_format"), style.candidateFormat},
  };
}

QString BangkeConfig::displayName(const QString& id) const {
  if (id == QLatin1String("native") && (platform_.source == ThemeSource::Omarchy || qgetenv("BANGKE_PANEL_BACKEND") == "kimpanel"))
    return QStringLiteral("跟随 Omarchy 系统主题");
  const QString name = schemes_.value(id).value(QStringLiteral("name"));
  return name.isEmpty() ? id : name;
}

SchemeColors BangkeConfig::resolve(const QString& id, bool dark) const {
  const bool native = (id == QLatin1String("native"));
  const QHash<QString, QString> raw = schemes_.value(id);
  // native 的底色来源：桌面给得出主题就用桌面的（platformtheme.h 把 Omarchy 的
  // [popups]/[menu] 解析成了同一套方案键），否则退回 mac 的烘焙常量。
  QHash<QString, QColor> nativeColors = platform_.panelRoles.isEmpty()
                                           ? bakedNativeSchemeColors(dark)
                                           : platform_.panelRoles;
  // 用户在补丁里自己写过的 preset_color_schemes/native/<键> 覆盖上面那一份。
  // （从前 native 的键是被整个无视的 —— 写完不生效，这里顺手修掉。）
  for (auto it = raw.constBegin(); it != raw.constEnd(); ++it) {
    if (!it.key().endsWith(QLatin1String("_color"))) continue;
    const QColor parsed = parseRimeColor(it.value());
    if (parsed.isValid()) nativeColors.insert(it.key(), parsed);
  }

  // native 按 mac 的语义色解析（完全不读 preset 里的颜色键），其它方案读
  // 自己的键、缺失时退回语义色。
  const auto color = [&](const QString& key, const QString& semanticRole) {
    if (native) {
      if (nativeColors.contains(key)) return nativeColors.value(key);
      return semantic(dark, semanticRole);
    }
    const QColor parsed = parseRimeColor(raw.value(key));
    return parsed.isValid() ? parsed : semantic(dark, semanticRole);
  };
  const auto colorOr = [&](const QString& key, const QColor& fallback) {
    if (native) return nativeColors.value(key, fallback);
    const QColor parsed = parseRimeColor(raw.value(key));
    return parsed.isValid() ? parsed : fallback;
  };
  const auto has = [&](const QString& key) {
    return native ? nativeColors.contains(key) : !raw.value(key).isEmpty();
  };

  SchemeColors c;
  c.back = color(QStringLiteral("back_color"), QStringLiteral("windowBackground"));
  c.text = color(QStringLiteral("text_color"), QStringLiteral("secondaryLabel"));
  c.hilitedText = colorOr(QStringLiteral("hilited_text_color"), c.text);
  c.candidateText = colorOr(QStringLiteral("candidate_text_color"), c.text);
  c.hilitedCandidateText =
      colorOr(QStringLiteral("hilited_candidate_text_color"), c.hilitedText);
  const QColor hilitedBack = color(QStringLiteral("hilited_back_color"),
                                   QStringLiteral("selectedTextBackground"));
  c.hilitedCandidateBack =
      colorOr(QStringLiteral("hilited_candidate_back_color"), hilitedBack);
  // 序号：方案没给则按 mac 的 (前景×2 + 背景)/3 混出来。
  c.label =
      colorOr(QStringLiteral("label_color"), blendMac(c.candidateText, c.back));
  c.hilitedLabel =
      colorOr(QStringLiteral("hilited_candidate_label_color"),
              blendMac(c.hilitedCandidateText, c.hilitedCandidateBack));
  c.comment = colorOr(QStringLiteral("comment_text_color"), c.candidateText);
  c.hilitedComment = colorOr(QStringLiteral("hilited_comment_text_color"),
                             c.hilitedCandidateText);
  c.border = has(QStringLiteral("border_color"))
                 ? color(QStringLiteral("border_color"), QStringLiteral("label"))
                 : QColor();
  c.accent = colorOr(QStringLiteral("accent_text_color"), c.candidateText);
  // Older Classic UI imports used Qt's ARGB parser for Fcitx RGBA values.
  // Existing bangke_custom patches can therefore contain fully transparent text.
  // Keep the stored palette and selection, but guarantee readable text in both
  // preview and runtime until the user edits or replaces this imported scheme.
  if (native || id == QLatin1String("bangke_custom")) {
    const auto visible = [](QColor fg, const QColor& bg) {
      const auto luminance = [](const QColor& x) { return .299*x.redF()+.587*x.greenF()+.114*x.blueF(); };
      if (!fg.isValid() || fg.alphaF() < .3 || qAbs(luminance(fg)-luminance(bg)) < .15)
        return luminance(bg) > .5 ? QColor(30,30,30) : QColor(245,245,245);
      return fg;
    };
    c.candidateText = visible(c.candidateText, c.back);
    c.text = visible(c.text, c.back);
    c.hilitedText = visible(c.hilitedText, c.back);
    c.accent = visible(c.accent, c.back);
    c.hilitedCandidateText = visible(c.hilitedCandidateText, c.hilitedCandidateBack.alphaF() < .3 ? c.back : c.hilitedCandidateBack);
    c.label = visible(c.label, c.back);
    c.comment = visible(c.comment, c.back);
    c.hilitedLabel = visible(c.hilitedLabel, c.hilitedCandidateBack.alphaF() < .3 ? c.back : c.hilitedCandidateBack);
    c.hilitedComment = visible(c.hilitedComment, c.hilitedCandidateBack.alphaF() < .3 ? c.back : c.hilitedCandidateBack);
  }
  return c;
}

}  // namespace bangke
