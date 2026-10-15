#ifndef FCITX5_RIME_CONFIG_UI_THEMEPALETTE_H_
#define FCITX5_RIME_CONFIG_UI_THEMEPALETTE_H_

#include <QColor>
#include <QString>

#include "theme/platformtheme.h"

class QWidget;

namespace bangke {

// 设置程序自身的观感 token。自绘控件不吃 QSS，所以颜色必须能从这里静态取到，
// 而不是只写在样式表字符串里。
//
// 值的来源是 systemtheme（platformtheme.h）里那份进程级系统主题：桌面给得出主题
// （Omarchy）就用桌面的，否则是 Adwaita/Yaru 那套烘焙常量。这些访问器纯读内存
// —— 它们会被 paintEvent 高频调用。
namespace palette {

bool isDark(const QWidget* widget);
bool dark();

QColor window(const QWidget* widget);
QColor sidebar(const QWidget* widget);
QColor card(const QWidget* widget);
QColor separator(const QWidget* widget);
QColor text(const QWidget* widget);
QColor secondaryText(const QWidget* widget);
QColor accent(const QWidget* widget);
QColor sidebarSelection(const QWidget* widget);  // 选中行
QColor well(const QWidget* widget);              // 滑杆槽 / 开关轨道未选中态

QString styleSheet(const QWidget* widget);

}  // namespace palette
}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_UI_THEMEPALETTE_H_
