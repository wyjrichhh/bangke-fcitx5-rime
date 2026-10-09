#ifndef FCITX5_RIME_CONFIG_THEME_INIFILE_H_
#define FCITX5_RIME_CONFIG_THEME_INIFILE_H_

#include <QString>

// fcitx5 的经典 INI 配置（`~/.config/fcitx5/conf/*.conf`）是逐行文本，
// 键的增删要**字节级保留**其余行（注释、顺序、我们不认识的键）——
// 用户可能自己调过 classicui.conf 的其它选项，设置程序只该动自己管的键。
namespace bangke::ini {

QString readValue(const QString& path, const QString& key,
                  const QString& fallback = QString());

// 顶层键值更新（无则追加）；写入成功返回 true。
bool setValue(const QString& path, const QString& key, const QString& value);

}  // namespace bangke::ini

#endif  // FCITX5_RIME_CONFIG_THEME_INIFILE_H_
