#ifndef FCITX5_RIME_CONFIG_THEME_PATHS_H_
#define FCITX5_RIME_CONFIG_THEME_PATHS_H_

#include <QString>

namespace bangke::paths {

QString dataHome();     // ~/.local/share
QString configHome();   // ~/.config
QString themeRoot();    // <dataHome>/fcitx5/themes
QString themeDir(const QString& name);
QString classicUiConf();  // <configHome>/fcitx5/conf/classicui.conf

// 我们自己的状态目录：生成哈希、同步锁。跟用户配置分开，卸载时整目录删掉即可。
QString stateDir();  // <stateHome>/bangke-pinyin

// Omarchy 的当前主题目录。BK_OMARCHY_THEME_DIR 可覆盖 —— 离线出图、单元测试
// 都靠它喂 fixture，不必真有一台 Omarchy。
QString omarchyThemeDir();  // <stateHome>/omarchy/current/theme

// 设置数据与配色都在 rime 用户配置里（与 mac/Windows 同构）。
QString rimeUserDir();       // <dataHome>/fcitx5/bangke
QString sharedBangkeYaml();  // 出厂值：用户目录里的 bangke.yaml（由 seed 铺开）
QString customBangkeYaml();  // 用户改动：bangke.custom.yaml

// 打包时铺在 /usr/share/fcitx5-rime/seed/ 下的出厂副本。用户目录里没有
// bangke.yaml 时（装包时 postinst 没跑到这个用户）用它自举。
QString seedBangkeYaml();

}  // namespace bangke::paths

#endif  // FCITX5_RIME_CONFIG_THEME_PATHS_H_
