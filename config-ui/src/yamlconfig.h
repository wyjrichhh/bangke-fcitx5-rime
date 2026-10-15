#ifndef FCITX5_RIME_CONFIG_YAMLCONFIG_H_
#define FCITX5_RIME_CONFIG_YAMLCONFIG_H_

#include <QList>
#include <QString>

#include "theme/scheme.h"

// AI 预测的可配置项（对应 rime 配置里 ai_predict: 块下的键）。
struct AISettings {
  bool enabled = true;
  bool filterPunctuation = true;
  QString modelPath;
  int minInputLength = 12;
  int minContextPromptLength = 2;
  int contextWindowSize = 10;
  int debounceMs = 200;
  int maxTokens = 64;
  QString device = "cpu";  // 插件只认 cpu / cuda（见 ct2_backend.cc）
  int minHanzi = 2;
  int targetIndex = 1;
  int searchRange = 10;
};

// 单个方案：是否启用 + 默认字形（简体/繁体）+ 自学习（用户词典/自动调频）。
struct SchemaSettings {
  QString name;
  bool enabled = true;
  bool simplified = true;
  bool userDict = true;
};

// 各配置文件里、本 UI 管理的那部分设置的合并视图。
struct RimeData {
  QList<SchemaSettings> schemas;
  int pageSize = 5;
  // 中英切换键行为（default.custom.yaml 的 ascii_composer/switch_key/）。
  // 值：commit_code / commit_text / inline_ascii / clear / noop。
  QString shiftLAction = QStringLiteral("commit_code");
  QString shiftRAction = QStringLiteral("commit_code");
  // 在应用内显示拼音预编辑（fcitx5 addon 配置 rime-ai.conf 的 PreeditInApplication；
  // 关闭后拼音改在候选框顶部显示）。
  bool preeditInApp = true;
  // 候选词后的 [拼音] 注释（schema 的 translator/spelling_hints）。
  // 默认关：用户不要候选右侧那串拼音。雾凇原生的 schema 给的是 8（前 8 个候选
  // 都带注释），所以必须显式写 0 才能压住，光靠"不写"是不行的。
  bool spellingHints = false;
  // 候选面板外观。存在 rime 用户配置的 bangke.yaml / bangke.custom.yaml 里，
  // 与 mac/Windows 同键名；classicui 主题是它的派生物。
  bangke::StyleOptions style;
  AISettings ai;
};

// 定点读写 rime 用户配置（~/.local/share/fcitx5/rime/*.yaml）。
// 只读写已管理的键，其余行（含注释、translators/filters 等高级项）字节级保留。
class YamlConfig {
 public:
  static QString rimeUserDir();
  // 品牌名：读 fcitx5 注册的 IM 名（inputmethod/rime-ai.conf 的 Name=），
  // 打包脚本改这一个 conf 即全局改名，二进制无需重编。
  static QString brandName();
  static RimeData load();
  static bool save(const RimeData& data);
};

#endif  // FCITX5_RIME_CONFIG_YAMLCONFIG_H_
