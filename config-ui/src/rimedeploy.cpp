#include "rimedeploy.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMessage>

// rime addon 通过自己的 DBus 接口 org.fcitx.Fcitx.Rime1（/rime）暴露 Deploy 方法，
// 直接调 RimeEngine::deploy() 做 fullcheck 重建（rimeservice.cpp）。失败时兜底提示。
QString RimeDeploy::triggerDeploy() {
  QDBusInterface iface(QStringLiteral("org.fcitx.Fcitx5"), QStringLiteral("/rime"),
                       QStringLiteral("org.fcitx.Fcitx.Rime1"),
                       QDBusConnection::sessionBus());
  if (!iface.isValid()) {
    return QStringLiteral("未检测到运行中的 fcitx5，请启动输入法后重试，或切换输入法使配置生效。");
  }
  QDBusMessage reply = iface.call(QStringLiteral("Deploy"));
  if (reply.type() == QDBusMessage::ErrorMessage) {
    return QStringLiteral("已保存，但无法自动触发部署（%1）。请切换输入法或重启 fcitx5 使配置生效。")
        .arg(reply.errorMessage());
  }
  // classic UI 的候选词排列等 fcitx5 自身配置不走 rime deploy，触发一次配置重载。
  QDBusInterface controller(QStringLiteral("org.fcitx.Fcitx5"),
                            QStringLiteral("/controller"),
                            QStringLiteral("org.fcitx.Fcitx.Controller1"),
                            QDBusConnection::sessionBus());
  if (controller.isValid()) {
    controller.call(QStringLiteral("ReloadConfig"));
  }
  return QString();
}
