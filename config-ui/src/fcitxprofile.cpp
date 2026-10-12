#include "fcitxprofile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

namespace {
constexpr const char* kImName = "rime-ai";

QString profilePath() {
  QString cfgHome =
      QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
  if (cfgHome.isEmpty()) cfgHome = QDir::homePath() + "/.config";
  return cfgHome + "/fcitx5/profile";
}

QStringList readLines(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  QTextStream stream(&file);
  stream.setCodec("UTF-8");
  QStringList out;
  while (!stream.atEnd()) out << stream.readLine();
  return out;
}

bool writeLines(const QString& path, const QStringList& lines) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  QTextStream stream(&file);
  stream.setCodec("UTF-8");
  for (const QString& line : lines) stream << line << '\n';
  return true;
}

bool hasRime(const QStringList& lines) {
  for (const QString& line : lines) {
    if (line.trimmed() == QStringLiteral("Name=") + QLatin1String(kImName))
      return true;
  }
  return false;
}

}  // namespace

QString ensureRimeInputMethod() {
  const QString path = profilePath();
  QStringList lines = readLines(path);

  if (lines.isEmpty()) {
    // 全新用户：直接给一份带 rime 的模板。
    lines = QStringList{
        QStringLiteral("[Groups/0]"),
        QStringLiteral("Name=Default"),
        QStringLiteral("Default Layout=us"),
        QStringLiteral("DefaultIM=") + QLatin1String(kImName),
        QString(),
        QStringLiteral("[Groups/0/Items/0]"),
        QStringLiteral("Name=") + QLatin1String(kImName),
        QStringLiteral("Layout="),
        QString(),
        QStringLiteral("[GroupOrder]"),
        QStringLiteral("0=Default"),
    };
    return writeLines(path, lines)
               ? QString()
               : QStringLiteral("无法写入 %1").arg(path);
  }

  if (hasRime(lines)) return QString();  // 已有，别动

  // 找第一组的 Items 索引号：挑一个没用过的。
  int count = 0;
  for (const QString& line : lines) {
    if (line.startsWith(QLatin1String("[Groups/0/Items/"))) ++count;
  }
  const QStringList item{QStringLiteral("[Groups/0/Items/%1]").arg(count),
                         QStringLiteral("Name=") + QLatin1String(kImName),
                         QStringLiteral("Layout="), QString()};

  // **必须插在 [GroupOrder] 之前**：profile 是分节 INI，追加到文件末尾会落进
  // GroupOrder 节里 —— fcitx5 当作那节的键，等于没注册。
  int insertAt = lines.size();
  for (int i = 0; i < lines.size(); ++i) {
    if (lines.at(i).trimmed() == QLatin1String("[GroupOrder]")) {
      insertAt = i;
      break;
    }
  }
  for (int i = item.size() - 1; i >= 0; --i) lines.insert(insertAt, item.at(i));

  return writeLines(path, lines)
             ? QString()
             : QStringLiteral("无法写入 %1").arg(path);
}
