#ifndef FCITX5_RIME_CONFIG_FCITXPROFILE_H_
#define FCITX5_RIME_CONFIG_FCITXPROFILE_H_

#include <QString>

// 确保「蚌壳拼音」(输入法 id = rime) 出现在用户的 fcitx5 输入法列表里。
//
// 为什么需要：装完包只是把 addon 装上 ✗，用户还得自己把 rime 加进 fcitx5 的
// 输入法组才会出现 ✗ —— 普通用户不会做这一步（Windows 侧早有等价的
// EnsureInputMethodRegistered）。postinst 会做一次，设置程序启动时再兜底一次。
//
// 幂等：已经有就不动；绝不重排用户已有的输入法顺序。返回错误信息，空 = 成功。
QString ensureRimeInputMethod();

#endif  // FCITX5_RIME_CONFIG_FCITXPROFILE_H_
