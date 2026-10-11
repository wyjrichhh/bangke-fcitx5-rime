#include "rimedeploy.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMessage>

QString RimeDeploy::setAddonConfig(const QString& addon, const QVariantMap& values) {
  QDBusInterface controller(QStringLiteral("org.fcitx.Fcitx5"),
                            QStringLiteral("/controller"),
                            QStringLiteral("org.fcitx.Fcitx.Controller1"),
                            QDBusConnection::sessionBus());
  if (!controller.isValid()) {
    return QStringLiteral("未检测到运行中的 fcitx5（配置已保存，重启输入法后生效）。");
  }
  // fcitx5 的配置 DBus 里所有标量都是**字符串**：GetConfig 吐出来就是
  // <'PreeditInApplication': <'True'>>。塞 bool/int 会被 variantToRawConfig
  // 忽略 —— 表现为「SetConfig 返回成功，但值没变」（实测踩过：设置程序里
  // 改开关后配置文件被 fcitx5 用旧值回写，看着像"没保存"）。
  QVariantMap payload;
  for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
    const QVariant& v = it.value();
    const QString text = (v.type() == QVariant::Bool)
                             ? (v.toBool() ? QStringLiteral("True")
                                           : QStringLiteral("False"))
                             : v.toString();
    payload.insert(it.key(), QVariant::fromValue(QDBusVariant(text)));
  }
  const QString uri = QStringLiteral("fcitx://config/addon/") + addon;
  const QDBusMessage reply = controller.call(
      QStringLiteral("SetConfig"), uri, QVariant::fromValue(QDBusVariant(payload)));
  if (reply.type() == QDBusMessage::ErrorMessage) {
    return QStringLiteral("配置已保存，但推送 %1 失败（%2）—— 重启输入法后生效。")
        .arg(addon, reply.errorMessage());
  }
  return QString();
}

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

QString RimeDeploy::addonConfigDump(const QString& addon, const QStringList& keys) {
  QDBusInterface controller(QStringLiteral("org.fcitx.Fcitx5"),
                            QStringLiteral("/controller"),
                            QStringLiteral("org.fcitx.Fcitx.Controller1"),
                            QDBusConnection::sessionBus());
  if (!controller.isValid()) return QStringLiteral("<没有 fcitx5>");
  const QDBusMessage reply = controller.call(
      QStringLiteral("GetConfig"),
      QStringLiteral("fcitx://config/addon/") + addon);
  if (reply.type() == QDBusMessage::ErrorMessage)
    return QStringLiteral("<GetConfig 失败: %1>").arg(reply.errorMessage());
  // GetConfig 的返回签名是 v（里面才是 a{sv}），必须先拆一层 QDBusVariant，
  // 否则 qvariant_cast 直接失败、拿到空 map —— 之前日志里"五个键全空"就是这个。
  QVariant inner = reply.arguments().value(0);
  if (inner.canConvert<QDBusVariant>()) inner = inner.value<QDBusVariant>().variant();
  const QVariantMap got = inner.value<QVariantMap>();
  QStringList out;
  for (const QString& k : keys) {
    const QString kk = k == QStringLiteral("Vertical Candidate List")
                           ? QStringLiteral("Vertical Candidate List") : k;
    out << QStringLiteral("%1=[%2]").arg(kk, got.value(kk).toString());
  }
  return out.join(QStringLiteral("  "));
}
