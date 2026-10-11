#ifndef FCITX5_RIME_CONFIG_THEME_THEMEWRITER_H_
#define FCITX5_RIME_CONFIG_THEME_THEMEWRITER_H_

#include <QFont>
#include <QString>
#include <QStringList>

#include "panelspec.h"
#include "scheme.h"

namespace bangke {

// 主题目录名，同时也是 classicui.conf 的 Theme / DarkTheme 取值。
inline constexpr const char* kThemeName = "BangKe";
inline constexpr const char* kThemeDarkName = "BangKe-Dark";

struct ThemeResult {
  bool ok = false;
  QString message;     // 失败原因；成功时为空
  QStringList files;   // 本次写出的文件
};

// 解析字体：字族没装时预览回落到系统默认（classicui.conf 里仍写配置的字族，
// 由 pango 自己回落）。
QFont resolveFont(const StyleOptions& style, QString* installedFamily = nullptr);

// classicui.conf 的 `Font=` 该写什么。**不要自己拼字符串** —— 两个写入点
// （本文件的 applyTheme 与 mainwindow 的 SetConfig 推送）都走这个函数。
//
// 为什么必须校验：`Font=` 一旦是空的（或家族名没装），fcitx5 会解析出 0 号字，
// 候选面板高度跟着文字高度走 → 归零 → 面板整个看不见。真机上「一保存候选面板
// 就消失」就是这个 —— 而家族名来自设置页的字体按钮，可能是空串。
// 家族名空或未安装时退到 "Noto Sans CJK SC"；连它也没装就返回空串，
// 调用方应当**整项不写**，让 fcitx5 用它自己的默认字体。
QString classicUiFontSpec(const StyleOptions& style);

// 某个外观下的面板规格。预览与落盘共用：
//   dark=false 用 color_scheme，dark=true 用 color_scheme_dark（空则跟随浅色方案）。
PanelSpec panelSpecFor(const BangkeConfig& config, bool dark);

// 同上，但用调用方给的 style（设置页里改一项就重画预览，此时改动还没落盘）。
PanelSpec panelSpecFor(const BangkeConfig& config, const StyleOptions& style,
                       bool dark);

// 生成两套主题目录并写 classicui.conf 的 Theme / DarkTheme / UseDarkTheme /
// Font / Vertical Candidate List（其余键字节级保留）。
// outRoot 非空时写到该目录下（供验收/dump 用），不动用户配置。
ThemeResult applyTheme(const BangkeConfig& config, const StyleOptions& style,
                       bool writeClassicUiConf = true,
                       const QString& outRoot = QString());

// 用户目录缺 bangke.yaml 时从发行包里的 seed 拷一份（postinst 没覆盖到的用户）。
// 返回错误信息，空 = 成功或无需处理。
QString ensureSharedConfig();

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_THEME_THEMEWRITER_H_
