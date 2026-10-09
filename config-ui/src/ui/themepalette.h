#ifndef FCITX5_RIME_CONFIG_UI_THEMEPALETTE_H_
#define FCITX5_RIME_CONFIG_UI_THEMEPALETTE_H_

#include <QColor>
#include <QString>

class QWidget;

namespace bangke {

// 设置程序自身的观感 token。自绘控件不吃 QSS，所以颜色必须能从这里静态取到，
// 而不是只写在样式表字符串里。
namespace palette {

// 深浅色只判一次（按系统的 GNOME 设置），**不要按控件取**：
// 控件调色板会被 QSS 改写，而 QSS 又是由这里的颜色生成的 —— 两边各自判断
// 就会出现「底用浅色画、字用深色方案画」的白底白字（实测踩过）。
void init();
bool isDark(const QWidget* widget);
bool dark();

QColor window(const QWidget* widget);
QColor sidebar(const QWidget* widget);
QColor card(const QWidget* widget);
QColor separator(const QWidget* widget);
QColor text(const QWidget* widget);
QColor secondaryText(const QWidget* widget);
QColor accent(const QWidget* widget);
QColor sidebarSelection(const QWidget* widget);  // 选中行：accent 22%
QColor well(const QWidget* widget);              // 滑杆槽 / 开关轨道未选中态

QString styleSheet(const QWidget* widget);

}  // namespace palette
}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_UI_THEMEPALETTE_H_
