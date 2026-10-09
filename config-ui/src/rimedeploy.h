#ifndef FCITX5_RIME_CONFIG_RIMEDEPLOY_H_
#define FCITX5_RIME_CONFIG_RIMEDEPLOY_H_

#include <QString>
#include <QVariantMap>

// 触发 fcitx5-rime 的 deploy（fullcheck 重建）。成功返回空串，否则返回给用户的提示。
class RimeDeploy {
 public:
  // 把配置推给**正在运行**的插件。
  //
  // 为什么必须这样：fcitx5 只在启动时读 conf/<addon>.conf，也没有文件监视
  // （Instance::reloadConfig() 只重载全局配置），所以直接写文件对运行中的进程
  // 完全无效 —— 用户会看到「设置改了但没生效，重启才好」。
  // Controller1.SetConfig 会调 addonInstance->setConfig()，当场生效并回写文件。
  // 返回错误信息，空 = 成功。
  static QString setAddonConfig(const QString& addon, const QVariantMap& values);

  static QString triggerDeploy();
};

#endif  // FCITX5_RIME_CONFIG_RIMEDEPLOY_H_
