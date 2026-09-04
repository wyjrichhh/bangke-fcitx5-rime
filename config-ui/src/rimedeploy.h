#ifndef FCITX5_RIME_CONFIG_RIMEDEPLOY_H_
#define FCITX5_RIME_CONFIG_RIMEDEPLOY_H_

#include <QString>

// 触发 fcitx5-rime 的 deploy（fullcheck 重建）。成功返回空串，否则返回给用户的提示。
class RimeDeploy {
 public:
  static QString triggerDeploy();
};

#endif  // FCITX5_RIME_CONFIG_RIMEDEPLOY_H_
