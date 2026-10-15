#include "paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace bangke::paths {
namespace {

QString standardLocation(QStandardPaths::StandardLocation location,
                         const QString& fallback) {
  const QString path = QStandardPaths::writableLocation(location);
  return path.isEmpty() ? QDir::homePath() + fallback : path;
}

// Qt5 的 QStandardPaths 没有 state 位置（Qt6 才有 GenericStateLocation），
// 按 XDG 规范自己拼：XDG_STATE_HOME，否则 ~/.local/state。
QString stateHome() {
  const QByteArray env = qgetenv("XDG_STATE_HOME");
  if (!env.isEmpty()) return QString::fromLocal8Bit(env);
  return QDir::homePath() + QStringLiteral("/.local/state");
}

}  // namespace

QString dataHome() {
  return standardLocation(QStandardPaths::GenericDataLocation,
                          QStringLiteral("/.local/share"));
}

QString configHome() {
  return standardLocation(QStandardPaths::GenericConfigLocation,
                          QStringLiteral("/.config"));
}

QString themeRoot() { return dataHome() + QStringLiteral("/fcitx5/themes"); }

QString themeDir(const QString& name) {
  return themeRoot() + QLatin1Char('/') + name;
}

QString classicUiConf() {
  return configHome() + QStringLiteral("/fcitx5/conf/classicui.conf");
}

QString stateDir() {
  return stateHome() + QStringLiteral("/bangke-pinyin");
}

QString omarchyThemeDir() {
  const QByteArray override = qgetenv("BK_OMARCHY_THEME_DIR");
  if (!override.isEmpty()) return QString::fromLocal8Bit(override);
  const QString state = stateHome() + QStringLiteral("/omarchy/current/theme");
  if (QFileInfo::exists(state + QStringLiteral("/colors.toml"))) return state;
  const QString legacy = configHome() + QStringLiteral("/omarchy/current/theme");
  return QFileInfo::exists(legacy + QStringLiteral("/colors.toml")) ? legacy : state;
}

QString rimeUserDir() { return dataHome() + QStringLiteral("/fcitx5/rime"); }

QString sharedBangkeYaml() {
  return rimeUserDir() + QStringLiteral("/bangke.yaml");
}

QString customBangkeYaml() {
  return rimeUserDir() + QStringLiteral("/bangke.custom.yaml");
}

QString seedBangkeYaml() {
  const QStringList candidates = {
      QStringLiteral("/usr/share/fcitx5-rime/seed/bangke.yaml"),
      QStringLiteral("/usr/local/share/fcitx5-rime/seed/bangke.yaml"),
  };
  for (const QString& path : candidates) {
    if (QFileInfo::exists(path)) return path;
  }
  return QString();
}

}  // namespace bangke::paths
