#ifndef FCITX5_RIME_CONFIG_THEME_COLOR_H_
#define FCITX5_RIME_CONFIG_THEME_COLOR_H_

#include <QColor>
#include <QString>

namespace bangke {

// Rime 色值：0xBBGGRR（6 位）或 0xAABBGGRR（8 位），字节序是 BGR ——
// 与 mac/Windows 侧同一约定，配色数据可跨端逐字搬运。非 Rime 形态时
// 退回 QColor 自己的解析（#RRGGBB / 名字），解析不了返回无效色。
QColor parseRimeColor(const QString& spec);

// Classic UI theme.conf uses #RRGGBBAA, unlike QColor’s #AARRGGBB.
QColor parseFcitxColor(const QString& spec);

// mac 的 fallback 混合：(前景×2 + 背景)/3，逐通道含 alpha。
// 未显式给序号色的方案靠它让序号比正文浅一点。
QColor blendMac(const QColor& fg, const QColor& bg);

// 序列化成 classicui 主题要的形态：fcitx5 把 8 位 hex 解析成 #RRGGBBAA
// （先 RGB 后 alpha），与 CSS/Qt 相反；不透明时写 6 位最稳。
QString toThemeColor(const QColor& color);

// 序列化成 Rime 配置文件里的形态：0xBBGGRR / 0xAABBGGRR（BGR 字节序）。
// 配置文件里避免用 '#' 开头 —— 补丁解析器会把行内 '#' 当注释起点。
QString toRimeColor(const QColor& color);

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_THEME_COLOR_H_
