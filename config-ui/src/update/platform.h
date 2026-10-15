#pragma once
#include <QFile>
#include <QJsonDocument>
#include <QSysInfo>
#include <QProcess>
#include <QString>
namespace bangke {
inline QString updateTarget() {
  auto arch = QSysInfo::currentCpuArchitecture();
  if (QFile::exists("/usr/share/bangke-pinyin/omarchy")) {
    if (arch == "arm64" || arch == "aarch64") return "omarchy-aarch64";
    if (arch == "x86_64") return "omarchy-x86_64";
    return {};
  }
  QFile os("/etc/os-release");
  if (!os.open(QIODevice::ReadOnly)) return {};
  const auto text = QString::fromUtf8(os.readAll());
  QString id, version;
  for (auto line : text.split('\n')) {
    if (line.startsWith("ID=")) id = line.mid(3).remove('"');
    if (line.startsWith("VERSION_ID=")) version = line.mid(11).remove('"');
  }
  return id == "ubuntu" && version == "24.04" && arch == "x86_64" ? QStringLiteral("ubuntu24-amd64") : QString();
}
inline QString installedUpdateVersion() {
  QProcess process;
  const bool arch = QFile::exists("/usr/share/bangke-pinyin/omarchy");
  process.start(arch ? "pacman" : "dpkg-query", arch ? QStringList{"-Q", "bangke-pinyin"} :
                QStringList{"-W", "-f=${Version}", "fcitx5-rime-ai-predict"});
  if (!process.waitForFinished(2000)) { process.kill(); process.waitForFinished(); return {}; }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return {};
  const auto text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
  return arch ? text.section(' ', 1, 1) : text;
}
}  // namespace bangke
