#ifndef FCITX5_RIME_CONFIG_THEME_PATHS_H_
#define FCITX5_RIME_CONFIG_THEME_PATHS_H_

#include <QString>

namespace bangke::paths {

QString dataHome();     // ~/.local/share
QString configHome();   // ~/.config
QString themeRoot();    // <dataHome>/fcitx5/themes
QString themeDir(const QString& name);
QString classicUiConf();  // <configHome>/fcitx5/conf/classicui.conf

// 设置数据与配色都在 rime 用户配置里（与 mac/Windows 同构）。
QString rimeUserDir();       // <dataHome>/fcitx5/rime
QString sharedBangkeYaml();  // 出厂值：用户目录里的 bangke.yaml（由 seed 铺开）
QString customBangkeYaml();  // 用户改动：bangke.custom.yaml

// 打包时铺在 /usr/share/fcitx5-rime/seed/ 下的出厂副本。用户目录里没有
// bangke.yaml 时（装包时 postinst 没跑到这个用户）用它自举。
QString seedBangkeYaml();

}  // namespace bangke::paths

#endif  // FCITX5_RIME_CONFIG_THEME_PATHS_H_
