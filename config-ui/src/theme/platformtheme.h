#ifndef FCITX5_RIME_CONFIG_THEME_PLATFORMTHEME_H_
#define FCITX5_RIME_CONFIG_THEME_PLATFORMTHEME_H_

#include <QColor>
#include <QHash>
#include <QString>

namespace bangke {

// 系统配色从哪儿来。它决定两件事：设置窗是否走 Omarchy 外观、native 方案是
// 跟随桌面还是用 mac 烘焙常量。
enum class ThemeSource { GnomeFallback, BakedNative, Omarchy };

// 以**像素**为单位的几何量。
//
// 为什么不塞进 StyleOptions：`decoration:rounding` 是 Hyprland 的像素整数，
// 而 StyleOptions.cornerRadius 是**点**（跨平台 schema，会被 styleToPatch 序列化
// 进 bangke.custom.yaml，必须与 mac/Windows 字节兼容）。px→pt→px 会掉精度
// （6px→5pt→7px），所以这些值单走一条通道，在 buildPanelSpec 里于 ptToPx 之后注入。
struct PlatformMetrics {
  bool valid = false;       // false = 完全退回现有的 pt 数学，一分不改
  int cornerRadiusPx = -1;  // hyprctl -j getoption decoration:rounding
  int borderWidthPx = -1;   // [popups] border-width（Omarchy 缺省 = Style.space(2)）
  int textPadPx = -1;       // [spacing] control-padding-y
};

// 当前桌面的配色与度量。读到 Omarchy 主题时 valid=true。
//
// 注意有两个概念刻意分开：设置程序自己的观感（window/card/accent 这些，
// **恒**取系统主题），与候选面板的配色（panelRoles，**只在** colorScheme 是
// native 时才用）。用户给面板选了 solarized，面板就是 solarized，设置窗仍是
// Omarchy 原生 —— 不能混成"当前主题"一个概念。
struct PlatformTheme {
  bool valid = false;
  bool dark = true;
  ThemeSource source = ThemeSource::GnomeFallback;
  QString name;

  // 设置程序自己的观感 token。
  QColor window;
  QColor sidebar;
  QColor card;
  QColor well;
  QColor text;
  QColor secondaryText;
  QColor separator;
  QColor accent;
  QColor sidebarSelection;

  // 控件度量与排版（px），来自 [spacing] / [font]。
  int controlHeight = 28;
  int rowGap = 8;
  int controlPadX = 10;
  int controlPadY = 6;
  int labelGap = 4;
  int panelPadding = 18;
  QString uiFontFamily;  // 空 = 用系统默认
  int uiFontPx = 12;

  // 候选面板的角色色。键名与 bangke.yaml 的方案键一一对应
  // （back_color / text_color / hilited_candidate_back_color / …），
  // BangkeConfig::resolve("native") 直接吃这一份。
  QHash<QString, QColor> panelRoles;

  PlatformMetrics metrics;
};

// 读 Omarchy 的当前主题。dir 为空时用 paths::omarchyThemeDir()。
// 读到 mode 才算有效；任何一项读不到都逐字段降级，绝不抛。
PlatformTheme loadPlatformTheme(const QString& dir = QString());

// 完全等于 2026-10 之前的行为：native.h 的烘焙常量 + gsettings 的深浅/强调色。
PlatformTheme bakedPlatformTheme(bool dark);

// 有 Omarchy 用前者，否则后者。`dark` 只在没有 Omarchy 时才有意义。
PlatformTheme currentPlatformTheme();

// 进程级的系统主题：main() 里注入一次，之后 UI 与配置层读同一份 ——
// 同一个进程不会出现两套配色，也不会为每次读取再跑一遍 gsettings/hyprctl。
//
// 探测（IO）只在 loadPlatformTheme() 里；这两个函数只是存取，所以测试与
// --dump-* 可以直接 set 一份 fixture，把整套 UI 跑在造出来的主题上。
void setAppPlatformTheme(const PlatformTheme& theme);
const PlatformTheme& appPlatformTheme();

// 给人看的 dump（--dump-theme-state）。切主题前后 diff 它比肉眼比截图准。
QString platformThemeToText(const PlatformTheme& theme);

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_THEME_PLATFORMTHEME_H_
