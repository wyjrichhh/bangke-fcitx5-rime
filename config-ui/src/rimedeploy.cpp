#include "rimedeploy.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
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

// rime addon 通过自己的 DBus 接口 org.fcitx.Fcitx.Bangke1（/rime）暴露 Deploy 方法，
// 直接调 RimeEngine::deploy() 做 fullcheck 重建（rimeservice.cpp）。失败时兜底提示。
QString RimeDeploy::triggerDeploy() {
  QDBusInterface iface(QStringLiteral("org.fcitx.Fcitx5"), QStringLiteral("/bangke"),
                       QStringLiteral("org.fcitx.Fcitx.Bangke1"),
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

namespace {
QDBusMessage panelCall(const QString& method, const QVariantList& args = {}) {
  auto message = QDBusMessage::createMethodCall(
      QStringLiteral("org.kde.impanel"), QStringLiteral("/org/kde/impanel"),
      QStringLiteral("org.kde.impanel2"), method);
  message.setArguments(args);
  return QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
}
}

void RimeDeploy::ensurePanel() {
  const QString desktop = QString::fromLocal8Bit(qgetenv("XDG_CURRENT_DESKTOP"));
  if (!desktop.contains(QStringLiteral("GNOME"), Qt::CaseInsensitive)) return;
  const auto identity = panelCall(QStringLiteral("GetPanelIdentity"));
  // Respect another panel and avoid interrupting a running input session.
  if (identity.type() == QDBusMessage::ReplyMessage ||
      identity.errorName() != QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown")) return;
  auto message = QDBusMessage::createMethodCall(QStringLiteral("org.gnome.Shell.Extensions"),
      QStringLiteral("/org/gnome/Shell/Extensions"), QStringLiteral("org.gnome.Shell.Extensions"),
      QStringLiteral("EnableExtension"));
  message.setArguments({QStringLiteral("bangke-panel@bangke.org")});
  QDBusConnection::sessionBus().call(message, QDBus::Block, 2000);
}

bool RimeDeploy::usesCustomPanel() {
  if (qgetenv("BANGKE_PANEL_BACKEND") == "kimpanel") return true;
  const auto reply = panelCall(QStringLiteral("GetPanelIdentity"));
  if (reply.type() == QDBusMessage::ReplyMessage)
    return reply.arguments().value(0).toString().startsWith(QStringLiteral("bangke-panel:"));
  // System extensions become discoverable only after the next GNOME login.
  // Keep the complete editor available in that interval, without taking over
  // an existing third-party panel.
  const QString desktop = QString::fromLocal8Bit(qgetenv("XDG_CURRENT_DESKTOP"));
  return desktop.contains(QStringLiteral("GNOME"), Qt::CaseInsensitive) &&
         reply.errorName() == QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown") &&
         QFileInfo::exists(QStringLiteral("/usr/share/gnome-shell/extensions/bangke-panel@bangke.org/metadata.json"));
}

QString RimeDeploy::setCustomPanelLayout(const QString& orientation) {
  const auto reply = panelCall(QStringLiteral("SetLayout"), {orientation});
  if (reply.type() == QDBusMessage::ErrorMessage ||
      reply.arguments().value(0).toString() != orientation) {
    return QStringLiteral("配置已保存，但自绘面板未确认布局更新：%1。候选面板暂时不可用。")
        .arg(reply.errorMessage());
  }
  return QString();
}

QString RimeDeploy::setCustomPanelAppearance(const QString& appearance) {
  ensurePanel();
  const auto reply = panelCall(QStringLiteral("SetAppearance"), {appearance});
  if (reply.type() == QDBusMessage::ErrorMessage) {
    if (qgetenv("BANGKE_PANEL_BACKEND") == "gnome" &&
        reply.errorName() == QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown")) {
      const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/bangke-panel");
      QSaveFile file(dir + QStringLiteral("/appearance.json"));
      const QByteArray bytes = appearance.toUtf8();
      if (!QDir().mkpath(dir) || !file.open(QIODevice::WriteOnly) ||
          file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("外观已保存到 YAML，但面板配置文件写入失败。");
      return QStringLiteral("已保存。当前桌面尚未启用完整外观效果，输入可继续使用。");
    }
    return QStringLiteral("外观已保存，但应用到候选面板失败：%1。请稍后重试。").arg(reply.errorMessage());
  }
  if (reply.arguments().value(0).toString() != QStringLiteral("applied:1"))
    return QStringLiteral("候选面板未确认外观设置已应用。");
  return QString();
}
