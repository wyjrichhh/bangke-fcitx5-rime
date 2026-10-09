#ifndef FCITX5_RIME_CONFIG_THEME_MIGRATE_H_
#define FCITX5_RIME_CONFIG_THEME_MIGRATE_H_

#include "scheme.h"

namespace bangke {

// 旧版设置程序把外观写在 classicui.conf 与自建的 themes/BangKe/theme.conf 里；
// 新版存在 bangke.custom.yaml（与 mac 同键名）。首次运行时把用户已有的字体、
// 候选排列与配色认领过来 —— 不做的话，用户打开新版会发现外观被重置成出厂值。
//
// 幂等：bangke.custom.yaml 里已经有 style/ 键就不再迁移。
// 返回是否做了迁移。
// config 必须是**只读出厂文件**的实例（BangkeConfig::load(出厂, 空)）：
// 它同时提供出厂 style 与 21 套配色用于比对。
bool migrateLegacyAppearance(const BangkeConfig& config);

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_THEME_MIGRATE_H_
