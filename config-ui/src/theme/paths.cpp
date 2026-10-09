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
