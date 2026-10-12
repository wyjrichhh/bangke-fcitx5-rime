#include "yamlconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

#include <string.h>

#include "theme/inifile.h"
#include "theme/stylepatch.h"
#include "theme/paths.h"
#include "theme/scheme.h"

namespace {

// 只有两个方案接好了 ai_predict 的 translator/filter 链，AI 配置只写它们。
const QStringList kKnownSchemas = {QStringLiteral("luna_pinyin"),
                                   QStringLiteral("rime_ice")};

// 找到形如 "  key:"（块键）或 "  key: value"（标量键）那一行。返回索引与缩进。
struct KeyLine {
  int index = -1;
  int indent = -1;
};

KeyLine findKey(const QStringList& lines, const QString& key) {
  QRegularExpression re("^(\\s*)" + QRegularExpression::escape(key) + "\\s*:");
  for (int i = 0; i < lines.size(); ++i) {
    auto m = re.match(lines[i]);
    if (m.hasMatch()) {
      return KeyLine{i, m.captured(1).size()};
    }
  }
  return KeyLine{};
}

int leadingSpaces(const QString& line) {
  int i = 0;
  while (i < line.size() && line[i].isSpace()) ++i;
  return i;
}

// 取 "key: value" 的 value，剥掉行内注释与引号。
QString scalarValue(const QString& line) {
  int colon = line.indexOf(':');
  if (colon < 0) return QString();
  QString v = line.mid(colon + 1);
  int hash = v.indexOf('#');
  if (hash >= 0) v = v.left(hash);
  v = v.trimmed();
  if (v.size() >= 2 &&
      ((v.front() == '\'' && v.back() == '\'') ||
       (v.front() == '"' && v.back() == '"'))) {
    v = v.mid(1, v.size() - 2);
  }
  return v;
}

QString readScalar(const QStringList& lines, const QString& key) {
  KeyLine k = findKey(lines, key);
  return k.index >= 0 ? scalarValue(lines[k.index]) : QString();
}

// 读某个块（如 ai_predict:）里的子标量键。
QString readScalarInBlock(const QStringList& lines, const QString& blockKey,
                          const QString& subKey) {
  KeyLine blk = findKey(lines, blockKey);
  if (blk.index < 0) return QString();
  for (int i = blk.index + 1; i < lines.size(); ++i) {
    const QString& l = lines[i];
    if (l.trimmed().isEmpty()) continue;
    if (leadingSpaces(l) <= blk.indent) break;
    if (l.trimmed().startsWith(subKey + ":")) return scalarValue(l);
  }
  return QString();
}

QStringList readSchemaList(const QStringList& lines) {
  KeyLine blk = findKey(lines, "schema_list");
  if (blk.index < 0) return QStringList();
  QStringList out;
  QRegularExpression re("^\\s*-\\s*schema\\s*:\\s*([^\\s#]+)");
  for (int i = blk.index + 1; i < lines.size(); ++i) {
    const QString& l = lines[i];
    if (l.trimmed().isEmpty()) continue;
    if (leadingSpaces(l) <= blk.indent) break;
    auto m = re.match(l);
    if (m.hasMatch()) out << m.captured(1);
  }
  return out;
}

QStringList readLines(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QStringList();
  QStringList lines;
  QTextStream in(&f);
  while (!in.atEnd()) lines << in.readLine();
  return lines;
}

bool writeLines(const QString& path, const QStringList& lines) {
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  QTextStream out(&f);
  for (const QString& l : lines) out << l << "\n";
  return true;
}

// 在 patch: 之后插一行（无 patch 块则补一个）。
void insertAfterPatch(QStringList& lines, const QString& newLine) {
  KeyLine p = findKey(lines, "patch");
  if (p.index >= 0) {
    lines.insert(p.index + 1, newLine);
  } else {
    lines << "patch:";
    lines << newLine;
  }
}

void setScalar(QStringList& lines, const QString& key, const QString& value) {
  KeyLine k = findKey(lines, key);
  QString indent = k.indent >= 0 ? QString(k.indent, ' ') : QString("  ");
  QString newLine = indent + key + ": " + value;
  if (k.index >= 0) {
    lines[k.index] = newLine;
  } else {
    insertAfterPatch(lines, newLine);
  }
}

void setScalarInBlock(QStringList& lines, const QString& blockKey,
                      const QString& subKey, const QString& value) {
  KeyLine blk = findKey(lines, blockKey);
  if (blk.index < 0) {
    insertAfterPatch(lines, QString("  ") + blockKey + ":");
    blk = findKey(lines, blockKey);
  }
  int subIdx = -1;
  int blockEnd = blk.index;
  for (int i = blk.index + 1; i < lines.size(); ++i) {
    const QString& l = lines[i];
    if (l.trimmed().isEmpty()) continue;
    if (leadingSpaces(l) <= blk.indent) break;
    if (l.trimmed().startsWith(subKey + ":")) subIdx = i;
    blockEnd = i;
  }
  QString subIndent = QString(blk.indent + 2, ' ');
  QString newLine = subIndent + subKey + ": " + value;
  if (subIdx >= 0) {
    lines[subIdx] = newLine;
  } else {
    // 新键追加到块末尾（而非块顶），既有键原位、新键按调用顺序排尾。
    lines.insert(blockEnd + 1, newLine);
  }
}

void setSchemaList(QStringList& lines, const QStringList& schemas) {
  KeyLine blk = findKey(lines, "schema_list");
  if (blk.index < 0) {
    insertAfterPatch(lines, QString("  schema_list:"));
    blk = findKey(lines, "schema_list");
  }
  int i = blk.index + 1;
  while (i < lines.size()) {
    const QString& l = lines[i];
    if (l.trimmed().isEmpty()) {
      ++i;
      continue;
    }
    if (leadingSpaces(l) <= blk.indent) break;
    if (l.trimmed().startsWith("- schema")) {
      lines.removeAt(i);
      continue;
    }
    ++i;
  }
  QString indent = QString(blk.indent + 2, ' ');
  for (int j = schemas.size() - 1; j >= 0; --j) {
    lines.insert(blk.index + 1, indent + "- schema: " + schemas[j]);
  }
}

// ── 字形开关的运行时定位 ─────────────────────────────────────────
// rime 的 patch 对列表只支持 @N 序号寻址（按名字寻址会报 incompatible node
// type），而序号随 schema 版本/用户改动漂移。所以保存时从 schema 文件解析出
// traditionalization / simplification 开关的真实序号，再写 @N/reset。

struct SwitchInfo {
  int index = -1;
  QStringList states;
};

SwitchInfo findGlyphSwitch(const QStringList& schemaLines) {
  SwitchInfo info;
  KeyLine blk = findKey(schemaLines, "switches");
  if (blk.index < 0) return info;
  QRegularExpression itemRe("^\\s*-\\s+(.*)$");
  QRegularExpression nameRe("name\\s*:\\s*\"?([A-Za-z0-9_]+)\"?");
  QRegularExpression statesInlineRe("states\\s*:\\s*\\[(.*)\\]");
  auto parseStates = [&info](const QString& body) {
    for (const QString& p : body.split(',')) {
      QString s = p.trimmed();
      if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.mid(1, s.size() - 2);
      if (!s.isEmpty()) info.states << s;
    }
  };
  int idx = -1;
  bool inTarget = false;
  bool collectingStates = false;
  int statesIndent = -1;
  for (int i = blk.index + 1; i < schemaLines.size(); ++i) {
    const QString& l = schemaLines[i];
    const QString trimmed = l.trimmed();
    if (trimmed.isEmpty()) continue;
    const int ind = leadingSpaces(l);
    if (ind <= blk.indent) break;
    // states 块列表形式的子项（- 简）：先于 item 判断，否则会被当成新开关。
    if (collectingStates && ind > statesIndent && trimmed.startsWith("-")) {
      QString s = trimmed.mid(1).trimmed();
      if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        s = s.mid(1, s.size() - 2);
      if (!s.isEmpty()) info.states << s;
      continue;
    }
    collectingStates = false;
    auto im = itemRe.match(l);
    if (im.hasMatch()) {
      ++idx;
      inTarget = false;
      auto nm = nameRe.match(im.captured(1));
      if (nm.hasMatch() && (nm.captured(1) == "traditionalization" ||
                            nm.captured(1) == "simplification")) {
        inTarget = true;
        info.index = idx;
      }
      auto st = statesInlineRe.match(im.captured(1));
      if (inTarget && st.hasMatch()) parseStates(st.captured(1));
      if (inTarget && !st.hasMatch() && im.captured(1).startsWith("states")) {
        collectingStates = true;
        statesIndent = ind;
      }
      continue;
    }
    if (!inTarget) continue;
    auto st = statesInlineRe.match(l);
    if (st.hasMatch()) {
      parseStates(st.captured(1));
    } else if (trimmed.startsWith("states")) {
      collectingStates = true;
      statesIndent = ind;
    }
  }
  return info;
}

// custom 自己通过 @before/@after 往 switches 插过元素时，源文件里的序号要平移。
int adjustForCustomInsertions(const QStringList& customLines, int target) {
  if (target < 0) return target;
  QRegularExpression beforeRe("'?switches/@before (\\d+)'?\\s*:");
  QRegularExpression afterRe("'?switches/@after (\\d+)'?\\s*:");
  for (const QString& l : customLines) {
    auto bm = beforeRe.match(l);
    if (bm.hasMatch()) {
      const int p = bm.captured(1).toInt();
      if (target >= p) ++target;
      continue;
    }
    auto am = afterRe.match(l);
    if (am.hasMatch()) {
      const int p = am.captured(1).toInt();
      if (target > p) ++target;
    }
  }
  return target;  // @next 追加到末尾，不影响既有序号。
}

// states 标签里哪个下标是简体：[简,繁]→0；[漢字,汉字]→1。识别不到返回 -1。
int simplifiedStateIndex(const QStringList& states) {
  for (int i = 0; i < states.size(); ++i) {
    if (states[i].contains(QStringLiteral("简")) ||
        states[i].contains(QStringLiteral("汉")) ||
        states[i].contains(QStringLiteral("体")))
      return i;
  }
  return -1;
}

QString schemaFilePath(const QString& dir, const QString& name) {
  const QString user = dir + "/" + name + ".schema.yaml";
  if (QFile::exists(user)) return user;
  return "/usr/share/rime-data/" + name + ".schema.yaml";
}

}  // namespace

QString YamlConfig::rimeUserDir() {
  QString dataHome = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  if (dataHome.isEmpty()) dataHome = QDir::homePath() + "/.local/share";
  return dataHome + "/fcitx5/rime";
}

QString YamlConfig::brandName() {
  // 打包脚本会 sed 这个文件的 Name=（见 repack 脚本的 IM_NAME）。
  QFile f("/usr/share/fcitx5/inputmethod/rime.conf");
  if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QTextStream in(&f);
    while (!in.atEnd()) {
      const QString l = in.readLine();
      if (l.startsWith("Name=")) return l.mid(5).trimmed();
    }
  }
  return QStringLiteral("Rime");
}

namespace {
// rime addon 自己的配置（「在应用内显示拼音预编辑」在那里）。
QString rimeAddonConfPath() {
  QString cfgHome =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  if (cfgHome.isEmpty()) cfgHome = QDir::homePath() + "/.config";
  return cfgHome + "/fcitx5/conf/rime.conf";
}

bool readIniBool(const QString& path, const QString& key, bool fallback) {
  const QString value = bangke::ini::readValue(path, key);
  if (value.isEmpty()) return fallback;
  return value.compare("True", Qt::CaseInsensitive) == 0;
}

bangke::StyleOptions readStyle() {
  return bangke::BangkeConfig::load(bangke::paths::sharedBangkeYaml(),
                                   bangke::paths::customBangkeYaml())
      .style();
}
}  // namespace

RimeData YamlConfig::load() {
  RimeData data;
  const QString dir = rimeUserDir();
  const QStringList def = readLines(dir + "/default.custom.yaml");

  bool ok = false;
  int ps = readScalar(def, "menu/page_size").toInt(&ok);
  if (ok && ps > 0) data.pageSize = ps;
  data.style = readStyle();

  const QStringList active = readSchemaList(def);

  QString v = readScalar(def, "ascii_composer/switch_key/Shift_L");
  if (!v.isEmpty()) data.shiftLAction = v;
  v = readScalar(def, "ascii_composer/switch_key/Shift_R");
  if (!v.isEmpty()) data.shiftRAction = v;

  data.preeditInApp = readIniBool(rimeAddonConfPath(), "PreeditInApplication", true);
  const QStringList iceCustom = readLines(dir + "/rime_ice.custom.yaml");
  // 只有显式写了 "1" 才算开。旧写法是 `!= "0"`，那么「文件里没这个键」会被当成
  // 开 —— 而雾凇 schema 原生 spelling_hints: 8，于是默认就带拼音注释。
  data.spellingHints =
      readScalar(iceCustom, "translator/spelling_hints") == "1";

  // 展示两个内置方案 + schema_list 里已有的（用户自装方案加入列表后即可管理）。
  QStringList names = kKnownSchemas;
  for (const QString& a : active)
    if (!names.contains(a)) names << a;

  for (const QString& name : names) {
    SchemaSettings s;
    s.name = name;
    s.enabled = active.isEmpty() ? true : active.contains(name);
    const QStringList sc = readLines(dir + "/" + name + ".custom.yaml");
    SwitchInfo sw = findGlyphSwitch(readLines(schemaFilePath(dir, name)));
    sw.index = adjustForCustomInsertions(sc, sw.index);
    const int simp = simplifiedStateIndex(sw.states);
    if (sw.index >= 0 && simp >= 0) {
      int rv = readScalar(sc, QString("switches/@%1/reset").arg(sw.index)).toInt(&ok);
      // reset 缺失时用 schema 自身默认（states[0]）。
      s.simplified = ok ? (rv == simp) : (simp == 0);
    }
    s.userDict = readScalar(sc, "translator/enable_user_dict") != "false";
    data.schemas.append(s);
  }

  // AI 设置：两个方案文件由 UI 写同一份，任取 luna 读即可。
  const QStringList luna = readLines(dir + "/luna_pinyin.custom.yaml");
  auto get = [&](const QString& k) { return readScalarInBlock(luna, "ai_predict", k); };
  AISettings& ai = data.ai;
  ai.modelPath = get("model_path");
  auto toInt = [&](const QString& k, int dflt) {
    bool b = false;
    int v = get(k).toInt(&b);
    return b ? v : dflt;
  };
  ai.minInputLength = toInt("min_input_length", ai.minInputLength);
  ai.minContextPromptLength = toInt("min_context_prompt_length", ai.minContextPromptLength);
  ai.contextWindowSize = toInt("context_window_size", ai.contextWindowSize);
  ai.debounceMs = toInt("debounce_ms", ai.debounceMs);
  ai.maxTokens = toInt("max_tokens", ai.maxTokens);
  ai.minHanzi = toInt("min_hanzi", ai.minHanzi);
  ai.targetIndex = toInt("target_index", ai.targetIndex);
  ai.searchRange = toInt("search_range", ai.searchRange);
  bool b = false;
  double q = get("quality").toDouble(&b);
  if (b) ai.quality = q;
  QString dev = get("device");
  if (!dev.isEmpty()) ai.device = dev;
  QString en = get("enabled");
  if (en == "false" || en == "0") ai.enabled = false;

  return data;
}

bool YamlConfig::save(const RimeData& data) {
  const QString dir = rimeUserDir();
  if (!QDir().mkpath(dir)) return false;

  QStringList def = readLines(dir + "/default.custom.yaml");
  if (def.isEmpty()) def = QStringList{"patch:"};
  setScalar(def, "menu/page_size", QString::number(data.pageSize));
  setScalar(def, "ascii_composer/switch_key/Shift_L", data.shiftLAction);
  setScalar(def, "ascii_composer/switch_key/Shift_R", data.shiftRAction);
  if (!bangke::ini::setValue(rimeAddonConfPath(), QStringLiteral("PreeditInApplication"),
                     data.preeditInApp ? QStringLiteral("True")
                                       : QStringLiteral("False")))
    return false;
  bangke::writeStylePatch(
      data.style,
      bangke::BangkeConfig::load(bangke::paths::sharedBangkeYaml(), QString())
          .style());
  // 保持 schema_list 原有顺序，追加新勾选的；未勾选的移除。
  QStringList enabled;
  for (const QString& a : readSchemaList(def)) {
    for (const auto& s : data.schemas)
      if (s.name == a && s.enabled) {
        enabled << a;
        break;
      }
  }
  for (const auto& s : data.schemas)
    if (s.enabled && !enabled.contains(s.name)) enabled << s.name;
  setSchemaList(def, enabled);
  if (!writeLines(dir + "/default.custom.yaml", def)) return false;

  for (const auto& s : data.schemas) {
    QStringList sc = readLines(dir + "/" + s.name + ".custom.yaml");
    if (sc.isEmpty()) {
      if (!kKnownSchemas.contains(s.name)) continue;
      sc = QStringList{"patch:"};
    }

    if (kKnownSchemas.contains(s.name)) {
      const AISettings& ai = data.ai;
      setScalarInBlock(sc, "ai_predict", "enabled", ai.enabled ? "true" : "false");
      setScalarInBlock(sc, "ai_predict", "device", ai.device);
      setScalarInBlock(sc, "ai_predict", "max_tokens", QString::number(ai.maxTokens));
      setScalarInBlock(sc, "ai_predict", "debounce_ms", QString::number(ai.debounceMs));
      setScalarInBlock(sc, "ai_predict", "min_input_length", QString::number(ai.minInputLength));
      setScalarInBlock(sc, "ai_predict", "min_context_prompt_length",
                       QString::number(ai.minContextPromptLength));
      setScalarInBlock(sc, "ai_predict", "context_window_size",
                       QString::number(ai.contextWindowSize));
      setScalarInBlock(sc, "ai_predict", "min_hanzi", QString::number(ai.minHanzi));
      setScalarInBlock(sc, "ai_predict", "quality", QString::number(ai.quality));
      setScalarInBlock(sc, "ai_predict", "target_index", QString::number(ai.targetIndex));
      setScalarInBlock(sc, "ai_predict", "search_range", QString::number(ai.searchRange));
      // 自学习（用户词典/自动调频）按方案独立。
      setScalar(sc, "translator/enable_user_dict", s.userDict ? "true" : "false");
      // 候选词 [拼音] 注释，两个方案统一设置。
      setScalar(sc, "translator/spelling_hints", data.spellingHints ? "1" : "0");
    }

    // 简繁默认值：按 schema 实际结构定位字形开关序号。schema 里没有该开关
    // （纯英文方案等）就什么都不写——宁可不改，也不能写坏整个补丁文件。
    SwitchInfo sw = findGlyphSwitch(readLines(schemaFilePath(dir, s.name)));
    sw.index = adjustForCustomInsertions(sc, sw.index);
    const int simp = simplifiedStateIndex(sw.states);
    if (sw.index >= 0 && simp >= 0) {
      // 先清掉本 UI 历史版本写过的字形 reset（序号可能已漂移），再写当前值。
      QRegularExpression staleRe("^\\s*switches/@\\d+/reset\\s*:");
      QRegularExpression legacyRe("^\\s*switches/traditionalization/reset\\s*:");
      for (int i = sc.size() - 1; i >= 0; --i)
        if (staleRe.match(sc[i]).hasMatch() || legacyRe.match(sc[i]).hasMatch())
          sc.removeAt(i);
      const int other = (simp == 0) ? 1 : 0;
      const int reset = s.simplified ? simp : other;
      setScalar(sc, QString("switches/@%1/reset").arg(sw.index), QString::number(reset));
    }
    if (!writeLines(dir + "/" + s.name + ".custom.yaml", sc)) return false;
  }
  return true;
}
