#include "scheme.h"

#include <QFile>
#include <QList>
#include <QPair>
#include <QTextStream>

#include "color.h"
#include "native.h"

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

int indentOf(const QString& line) {
  int n = 0;
  while (n < line.size() && line.at(n) == QLatin1Char(' ')) ++n;
  return n;
}

// 去掉行内注释。'#' 只有「在行首」或「前面是空白、后面也是空白或行尾」
// 才算注释 —— 否则会把 `back_color: #eaf1fa` 这种以 '#' 开头的颜色值吃掉
// （曾导致整行被当成段头，自定义配色静默失效）。
QString stripComment(const QString& line) {
  for (int i = 0; i < line.size(); ++i) {
    if (line.at(i) != QLatin1Char('#')) continue;
    if (i == 0) return QString();
    if (!line.at(i - 1).isSpace()) continue;
    if (i + 1 >= line.size() || line.at(i + 1).isSpace()) return line.left(i);
  }
  return line;
}

QString unquote(const QString& v) {
  const QString s = v.trimmed();
  if (s.size() >= 2 &&
      ((s.startsWith(QLatin1Char('\'')) && s.endsWith(QLatin1Char('\''))) ||
       (s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))))) {
    return s.mid(1, s.size() - 2);
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
                                const QString& customPath) {
  BangkeConfig config;
  config.parseShared(sharedPath);
  config.applyPatch(customPath);

  // 用户补丁优先于出厂值；stylePatch_ 只留用户改过的键，供「恢复默认」判断。
  const auto get = [&config](const QString& key) {
    const QString user = config.stylePatch_.value(key);
    return user.isEmpty() ? config.sharedStyle_.value(key) : user;
  };
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

  StyleOptions s;
  if (!get("color_scheme").isEmpty()) s.colorScheme = get("color_scheme");
  s.colorSchemeDark = get("color_scheme_dark");
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
  s.showPaging = getBool("show_paging", s.showPaging);
  if (!get("candidate_format").isEmpty())
    s.candidateFormat = get("candidate_format");

  config.style_ = s;
  return config;
}

QList<QPair<QString, QString>> styleToPatch(const StyleOptions& style) {
  const auto number = [](int v) { return QString::number(v); };
  const auto boolean = [](bool v) { return QString(v ? "true" : "false"); };
  return {
      {QStringLiteral("color_scheme"), style.colorScheme},
      {QStringLiteral("color_scheme_dark"), style.colorSchemeDark},
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
  };
}

QString BangkeConfig::displayName(const QString& id) const {
  const QString name = schemes_.value(id).value(QStringLiteral("name"));
  return name.isEmpty() ? id : name;
}

SchemeColors BangkeConfig::resolve(const QString& id, bool dark) const {
  const bool native = (id == QLatin1String("native"));
  const QHash<QString, QString> raw = schemes_.value(id);
  const QHash<QString, QColor> nativeColors = nativeSchemeColors(dark);

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
  return c;
}

}  // namespace bangke
