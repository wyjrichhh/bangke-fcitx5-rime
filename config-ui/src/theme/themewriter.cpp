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

  out << QStringLiteral("[InputPanel/Background]");
  out << QStringLiteral("Image=background.png");
  out << QStringLiteral("Color=%1").arg(toThemeColor(spec.panelBackground));
  // 边框烘在图里；这里给出色值只为「图片加载失败」的兜底路径不再画意外边框。
  out << QStringLiteral("BorderColor=%1").arg(toThemeColor(border));
  out << QString();
  appendMargin(&out, QStringLiteral("InputPanel/Background/Margin"),
               spec.backgroundMargin);

  out << QStringLiteral("[InputPanel/Highlight]");
  out << QStringLiteral("Image=highlight.png");
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
    ini::setValue(conf, QStringLiteral("DarkTheme"), QLatin1String(kThemeDarkName));
    ini::setValue(conf, QStringLiteral("UseDarkTheme"), QStringLiteral("True"));
    ini::setValue(conf, QStringLiteral("Font"),
                  QStringLiteral("%1 %2").arg(style.fontFace).arg(style.fontPoint));
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

}  // namespace bangke
