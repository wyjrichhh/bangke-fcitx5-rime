#ifndef FCITX5_RIME_CONFIG_THEME_SCHEME_H_
#define FCITX5_RIME_CONFIG_THEME_SCHEME_H_

#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

#include "platformtheme.h"

namespace bangke {

// 全局视觉设置（bangke.yaml 的 style: 段）。键名与 mac 侧一一对应，
// 用户在 bangke.custom.yaml 里覆盖。
struct StyleOptions {
  QString colorScheme = QStringLiteral("native");
  QString colorSchemeDark;  // 空 = 跟随浅色方案
  QString candidateListLayout = QStringLiteral("linear");
  QString textOrientation = QStringLiteral("horizontal");
  QString fontFace = QStringLiteral("Noto Sans CJK SC");
  int fontPoint = 16;
  // 整窗不透明度，1 = 不透明。mac 的设置页显示的是 1 - alpha，
  // 存储与手写配置保持同一个语义。
  double alpha = 1.0;
  int cornerRadius = 7;
  int hilitedCornerRadius = 0;
  int borderWidth = -2;   // ≤0 不画
  int borderHeight = -2;
  int lineSpacing = 5;
  int spacing = 8;        // 预编辑与候选的间距（classicui 上无对应）
  int shadowSize = 0;
  bool inlinePreedit = true;
  bool inlineCandidate = false;
  bool memorizeSize = true;
  bool mutualExclusive = false;
  bool translucency = false;
  bool showPaging = false;
  QString candidateFormat = QStringLiteral("[label]. [candidate] [comment]");
};

// 一个方案解析后的颜色角色。取值顺序照 mac（BangkeTheme.load）：方案没给
// 某个角色时按 mac 的回落链推导，序号用 (文字×2+背景)/3 混出来。
struct SchemeColors {
  QColor back;
  QColor text;             // 拼音串
  QColor candidateText;
  QColor hilitedText;
  QColor hilitedCandidateText;
  QColor hilitedCandidateBack;
  QColor label;
  QColor hilitedLabel;
  QColor comment;
  QColor hilitedComment;
  QColor border;
  QColor accent;           // AI 注释的强调色（accent_text_color），无则同候选文字
};

struct SchemeInfo {
  QString id;
  QString displayName;
};

// bangke.yaml（出厂值）+ bangke.custom.yaml（用户改动）的合并视图。
// 两个文件都是 Rime 配置：出厂文件是嵌套结构，补丁文件是 patch: 下的
// 扁平键路径（style/color_scheme、preset_color_schemes/native/<键>），
// 与 mac/Windows 的用户补丁格式一致。
class BangkeConfig {
 public:
  // platform 为空时（默认）完全退回 mac 的烘焙常量。默认参数**刻意不**去探测桌面：
  // 它要跑子进程（gsettings/hyprctl），不该藏在默认参数里，单元测试也会被带脏。
  // 需要系统主题的调用方显式传 currentPlatformTheme()。
  static BangkeConfig load(const QString& sharedPath, const QString& customPath,
                           PlatformTheme platform = PlatformTheme());

  const QStringList& schemeIds() const { return schemeIds_; }
  QString displayName(const QString& id) const;
  const StyleOptions& style() const { return style_; }
  const PlatformTheme& platform() const { return platform_; }
  bool hasScheme(const QString& id) const { return schemes_.contains(id); }

  // 用户是否在补丁里写过这个键。判断「这一项是不是用户自己改的」用它。
  bool isStyleUserSet(const QString& key) const {
    return stylePatch_.contains(key);
  }

  // 解析某方案的颜色；dark 只影响 native（系统配色有深浅两套。桌面给得出主题时
  // 由桌面决定，dark 不再起作用）。
  SchemeColors resolve(const QString& id, bool dark) const;

 private:
  QHash<QString, QHash<QString, QString>> schemes_;  // id -> 键 -> 原始值
  QStringList schemeIds_;
  StyleOptions style_;
  QHash<QString, QString> sharedStyle_;  // 出厂值（bangke.yaml）
  QHash<QString, QString> stylePatch_;   // 用户补丁（bangke.custom.yaml）
  PlatformTheme platform_;

  void applyPatch(const QString& path);
  void parseShared(const QString& path);
};

// StyleOptions → 配置文件键值（键名与 mac 的 style: 段一一对应）。
// 只列本设置程序管理的键。
QList<QPair<QString, QString>> styleToPatch(const StyleOptions& style);

// 写补丁时的**基准**：出厂值叠上桌面主题给的缺省。
//
// 为什么要单独一个函数：设置页的控件显示的是「出厂值 + 桌面缺省」，而
// writeStylePatch 是拿基准逐键比对来决定「用户改没改过」的。要是拿**原始出厂值**
// 当基准，被桌面缺省过的那些键会被判成「用户改过」，一保存就把 Omarchy 的值写进
// bangke.custom.yaml —— 跟随系统主题就此被永久冻住。让「控件默认显示什么」和
// 「什么算没改过」是同一件事，才对。
StyleOptions baselineStyle(const StyleOptions& factory,
                           const PlatformTheme& platform);

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_THEME_SCHEME_H_
