#include "themewriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPainter>
#include <QPolygonF>
#include <QStringList>
#include <QTextStream>

#include "color.h"
#include "inifile.h"
#include "ninegrid.h"
#include "paths.h"

namespace bangke {
namespace {

QStringList readLines(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  QTextStream ts(&f);
  ts.setCodec("UTF-8");
  QStringList out;
  while (!ts.atEnd()) out << ts.readLine();
  return out;
}

bool writeLines(const QString& path, const QStringList& lines) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  QTextStream ts(&f);
  ts.setCodec("UTF-8");
  for (int i = 0; i < lines.size(); ++i) {
    ts << lines.at(i);
    if (i + 1 < lines.size()) ts << '\n';
  }
  return true;
}

QString boolText(bool value) { return value ? QStringLiteral("True")
                                            : QStringLiteral("False"); }

void appendMargin(QStringList* out, const QString& section, const QMargins& m) {
  *out << QStringLiteral("[%1]").arg(section);
  *out << QStringLiteral("Left=%1").arg(m.left());
  *out << QStringLiteral("Right=%1").arg(m.right());
  *out << QStringLiteral("Top=%1").arg(m.top());
  *out << QStringLiteral("Bottom=%1").arg(m.bottom());
  *out << QString();
}

// 翻页箭头。classicui 只在主题给了 PrevPage/NextPage 图时才画按钮，
// 所以"显示翻页按钮"这个开关就是"写不写这两张图"。
QImage makePagingImage(bool next, int size, const QColor& color) {
  const int extent = qMax(8, size);
  QImage image(extent, extent, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  const qreal inset = extent * 0.28;
  QPolygonF triangle;
  if (next) {
    triangle << QPointF(inset, inset) << QPointF(extent - inset, extent / 2.0)
             << QPointF(inset, extent - inset);
  } else {
    triangle << QPointF(extent - inset, inset) << QPointF(inset, extent / 2.0)
             << QPointF(extent - inset, extent - inset);
  }
  painter.drawPolygon(triangle);
  painter.end();
  return image;
}

QString themeConfText(const PanelSpec& spec, bool showPaging,
                      const QColor& border) {
  QStringList out;
  out << QStringLiteral("[Metadata]");
  out << QStringLiteral("Name=BangKe");
  out << QStringLiteral("Name[zh_CN]=蚌壳拼音");
  out << QStringLiteral("Version=1");
  out << QStringLiteral("ScaleWithDPI=True");
  out << QString();

  out << QStringLiteral("[InputPanel]");
  out << QStringLiteral("NormalColor=%1").arg(toThemeColor(spec.normalText));
  // accent 只上色不铺底：透明背景时 classicui 跳过背景填充。
  out << QStringLiteral("HighlightColor=%1").arg(toThemeColor(spec.highlightText));
  out << QStringLiteral("HighlightBackgroundColor=%1")
             .arg(toThemeColor(spec.highlightFill));
  out << QStringLiteral("HighlightCandidateColor=%1")
             .arg(toThemeColor(spec.highlightCandidateText));
  out << QStringLiteral("FullWidthHighlight=%1")
             .arg(boolText(spec.fullWidthHighlight));
  out << QStringLiteral("PageButtonAlignment=Last Candidate");
  out << QString();

  appendMargin(&out, QStringLiteral("InputPanel/TextMargin"), spec.textMargin);
  appendMargin(&out, QStringLiteral("InputPanel/ContentMargin"), spec.contentMargin);

  // 面板底图与高亮**只用纯色，不用 Image**。
  //
  // 原来是 Image=background.png / highlight.png（九宫格切片）来画圆角。实测在
  // Hyprland/Wayland 下：带 Image 时**候选面板整个画不出来**，去掉 Image 改纯色就
  // 正常；把切片中心从 2px 放大到 16px、把 PNG 从 1-bit 换成 RGBA 都无效 —— 是这条
  // 路径本身在 Wayland 下不可用，不是尺寸或格式问题。
  // fcitx5 **自带主题**在这两处也只用 Color（Image 仅用于翻页箭头那类小图标），
  // 这里跟它对齐。代价：Linux 上没有圆角了（纯色画不出圆角）。
  out << QStringLiteral("[InputPanel/Background]");
  out << QStringLiteral("Color=%1").arg(toThemeColor(spec.panelBackground));
  out << QStringLiteral("BorderColor=%1").arg(toThemeColor(border));
  out << QString();
  appendMargin(&out, QStringLiteral("InputPanel/Background/Margin"),
               spec.backgroundMargin);

  out << QStringLiteral("[InputPanel/Highlight]");
  out << QStringLiteral("Color=%1").arg(toThemeColor(spec.highlightBackground));
  out << QString();
  appendMargin(&out, QStringLiteral("InputPanel/Highlight/Margin"),
               spec.highlightMargin);
  // 点击区 = 高亮区缩进 ClickMargin；给 0 让整块高亮都可点。
  appendMargin(&out, QStringLiteral("InputPanel/Highlight/ClickMargin"),
               QMargins(0, 0, 0, 0));

  if (showPaging) {
    out << QStringLiteral("[InputPanel/PrevPage]");
    out << QStringLiteral("Image=prev.png");
    out << QString();
    appendMargin(&out, QStringLiteral("InputPanel/PrevPage/ClickMargin"),
                 QMargins(2, 2, 2, 2));
    out << QStringLiteral("[InputPanel/NextPage]");
    out << QStringLiteral("Image=next.png");
    out << QString();
    appendMargin(&out, QStringLiteral("InputPanel/NextPage/ClickMargin"),
                 QMargins(2, 2, 2, 2));
  }

  // 右键菜单跟着同一套配色，免得深浅两套主题下菜单还留着系统默认色。
  QColor separator = spec.normalText;
  separator.setAlpha(40);
  out << QStringLiteral("[Menu]");
  out << QStringLiteral("NormalColor=%1").arg(toThemeColor(spec.normalText));
  out << QStringLiteral("HighlightCandidateColor=%1")
             .arg(toThemeColor(spec.highlightCandidateText));
  out << QString();
  out << QStringLiteral("[Menu/Background]");
  out << QStringLiteral("Color=%1").arg(toThemeColor(spec.panelBackground));
  out << QString();
  out << QStringLiteral("[Menu/Highlight]");
  out << QStringLiteral("Color=%1").arg(toThemeColor(spec.highlightBackground));
  out << QString();
  out << QStringLiteral("[Menu/Separator]");
  out << QStringLiteral("Color=%1").arg(toThemeColor(separator));
  out << QString();
  return out.join(QLatin1Char('\n'));
}

bool writeThemeDir(const QString& dir, const PanelSpec& spec,
                   QStringList* files, QString* error) {
  if (!QDir().mkpath(dir)) {
    *error = QStringLiteral("无法创建主题目录 %1").arg(dir);
    return false;
  }

  const QColor border =
      spec.borderWidth > 0 ? spec.panelBorder : QColor(Qt::transparent);
  const QImage background = panelBackgroundImage(spec);
  const QImage highlight = panelHighlightImage(spec);

  const QString bgPath = dir + QStringLiteral("/background.png");
  const QString hlPath = dir + QStringLiteral("/highlight.png");
  if (!background.save(bgPath, "PNG") || !highlight.save(hlPath, "PNG")) {
    *error = QStringLiteral("主题图片写入失败：%1").arg(dir);
    return false;
  }
  files->append(bgPath);
  files->append(hlPath);

  if (spec.showPaging) {
    const int size = spec.font.pointSize() > 0 ? spec.font.pointSize() : 10;
    QColor arrow = spec.normalText;
    arrow.setAlpha(128);
    const int extent = qRound(size * 1.5);
    const QString prevPath = dir + QStringLiteral("/prev.png");
    const QString nextPath = dir + QStringLiteral("/next.png");
    makePagingImage(false, extent, arrow).save(prevPath, "PNG");
    makePagingImage(true, extent, arrow).save(nextPath, "PNG");
    files->append(prevPath);
    files->append(nextPath);
  }

  const QString confPath = dir + QStringLiteral("/theme.conf");
  const QString text =
      themeConfText(spec, spec.showPaging, border);
  if (!writeLines(confPath, text.split(QLatin1Char('\n')))) {
    *error = QStringLiteral("主题配置写入失败：%1").arg(confPath);
    return false;
  }
  files->append(confPath);
  return true;
}

}  // namespace

QFont resolveFont(const StyleOptions& style, QString* installedFamily) {
  QFont font;
  const QStringList families = QFontDatabase().families();
  if (families.contains(style.fontFace)) {
    font.setFamily(style.fontFace);
    if (installedFamily) *installedFamily = style.fontFace;
  } else if (installedFamily) {
    installedFamily->clear();
  }
  font.setPointSize(style.fontPoint > 0 ? style.fontPoint : 16);
  return font;
}

QString classicUiFontSpec(const StyleOptions& style) {
  const QStringList installed = QFontDatabase().families();
  const int point = style.fontPoint > 0 ? style.fontPoint : 16;
  QString family = style.fontFace.trimmed();
  if (family.isEmpty() || !installed.contains(family)) {
    family = QStringLiteral("Noto Sans CJK SC");
  }
  // 连兜底字体都没装 —— 宁可整项不写（fcitx5 用自己的默认），也不要写一个
  // 空的 `Font=`：那会让面板高度归零、整个看不见。
  if (!installed.contains(family)) return QString();
  return QStringLiteral("%1 %2").arg(family).arg(point);
}

PanelSpec panelSpecFor(const BangkeConfig& config, bool dark) {
  return panelSpecFor(config, config.style(), dark);
}

PanelSpec panelSpecFor(const BangkeConfig& config, const StyleOptions& style,
                       bool dark) {
  const QString id = (dark && !style.colorSchemeDark.isEmpty())
                         ? style.colorSchemeDark
                         : style.colorScheme;
  return buildPanelSpec(config.resolve(id, dark), style, resolveFont(style));
}

ThemeResult applyTheme(const BangkeConfig& config, const StyleOptions& style,
                       bool writeClassicUiConf, const QString& outRoot) {
  ThemeResult result;
  const PanelSpec light = panelSpecFor(config, style, false);
  const PanelSpec dark = panelSpecFor(config, style, true);

  const QString root =
      outRoot.isEmpty() ? paths::themeRoot() : outRoot;
  QString error;
  if (!writeThemeDir(root + QLatin1Char('/') + QLatin1String(kThemeName), light,
                     &result.files, &error) ||
      !writeThemeDir(root + QLatin1Char('/') + QLatin1String(kThemeDarkName), dark,
                     &result.files, &error)) {
    result.message = error;
    return result;
  }

  if (writeClassicUiConf && outRoot.isEmpty()) {
    const QString conf = paths::classicUiConf();
    ini::setValue(conf, QStringLiteral("Theme"), QLatin1String(kThemeName));
    // 不走「跟随系统深色」。生成器给 BangKe / BangKe-Dark 产出的内容**完全一样**
    // （颜色逐行一致、背景图 md5 相同），这个开关本来就没起作用；而一旦置 True，
    // fcitx5 会改走 DarkTheme 那条加载路径 —— 实测真机（Hyprland/Wayland）下
    // 候选面板会整个画不出来。指回同一个主题没有任何损失。
    ini::setValue(conf, QStringLiteral("DarkTheme"), QLatin1String(kThemeName));
    ini::setValue(conf, QStringLiteral("UseDarkTheme"), QStringLiteral("False"));
    // 字体为空就**整项不写**。
    // 空的 `Font=` 会让 fcitx5 解析出 0 号字，而候选面板高度跟着文字高度走 ——
    // 归零，面板整个看不见。真机上「一保存候选面板就消失」就是这个：保存后
    // classicui.conf 里唯一的非默认键就是空的 Font=。
    // 来源是 pages.cpp 的 `out.fontFace = font_->family();`，字体按钮取不到家族名
    // 时返回空串。不写这一项，fcitx5 会保持它原有的字体，比写空值安全得多。
    const QString fontSpec = classicUiFontSpec(style);
    if (!fontSpec.isEmpty()) {
      ini::setValue(conf, QStringLiteral("Font"), fontSpec);
    }
    ini::setValue(conf, QStringLiteral("Vertical Candidate List"),
                  boolText(style.textOrientation == QLatin1String("vertical")));
    result.files.append(conf);
  }

  result.ok = true;
  return result;
}

QString ensureSharedConfig() {
  const QString user = paths::sharedBangkeYaml();
  if (QFileInfo::exists(user)) return QString();
  const QString seed = paths::seedBangkeYaml();
  if (seed.isEmpty()) {
    return QStringLiteral("找不到出厂配置 bangke.yaml（发行包应装到 "
                          "/usr/share/fcitx5-rime/seed/）");
  }
  if (!QDir().mkpath(paths::rimeUserDir()) || !QFile::copy(seed, user)) {
    return QStringLiteral("无法把出厂配置写到 %1").arg(user);
  }
  return QString();
}

void writeClassicUiConf(const StyleOptions& style) {
  const QString conf = paths::classicUiConf();
  ini::setValue(conf, QStringLiteral("Theme"), QLatin1String(kThemeName));
  ini::setValue(conf, QStringLiteral("DarkTheme"), QLatin1String(kThemeName));
  ini::setValue(conf, QStringLiteral("UseDarkTheme"), QStringLiteral("False"));
  const QString fontSpec = classicUiFontSpec(style);
  if (!fontSpec.isEmpty()) {
    ini::setValue(conf, QStringLiteral("Font"), fontSpec);
  }
  ini::setValue(conf, QStringLiteral("Vertical Candidate List"),
                boolText(style.textOrientation == QLatin1String("vertical")));
}

}  // namespace bangke

