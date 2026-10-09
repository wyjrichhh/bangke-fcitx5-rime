#ifndef FCITX5_RIME_CONFIG_THEME_STYLEPATCH_H_
#define FCITX5_RIME_CONFIG_THEME_STYLEPATCH_H_

#include "scheme.h"

namespace bangke {

// 把外观设置写进 bangke.custom.yaml 的 patch:（平铺键路径 style/<键>）。
//
// 只写与出厂值不同的键 —— 出厂值以后调整时，用户没动过的项要能跟着变；
// 旧版设置程序把默认值全写进配置，新出厂就永远不生效（Windows 侧踩过）。
// 其余行（注释、别的键）字节级保留。
//
// factory 必须是**解析后的出厂设置**（BangkeConfig::load(出厂文件, 空) 的 style()）：
// 有些键在 bangke.yaml 里没有、由代码默认值兜底（例如 alpha），
// 拿文件里的原始字符串当基准会把它们误判成「用户改过」而写进补丁。
void writeStylePatch(const StyleOptions& style, const StyleOptions& factory);

// 删掉所有受管键，「恢复默认」用。
void resetStylePatch(const StyleOptions& factory);

// 把一个自定义配色写进 bangke.custom.yaml 的 preset_color_schemes/<id>/，
// 让它在方案列表里与内置的 21 套一样可选中（旧版设置程序的自定义配色、
// 以及迁移时认领到的配色都走这里）。
void writeCustomScheme(const QString& id, const QString& displayName,
                       const QList<QPair<QString, QString>>& colors);

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_THEME_STYLEPATCH_H_
