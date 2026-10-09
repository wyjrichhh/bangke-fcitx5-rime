#include "inifile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

namespace bangke::ini {
namespace {

QStringList readLines(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  QTextStream ts(&f);
  ts.setCodec("UTF-8");
  QStringList out;
  while (!ts.atEnd()) out << ts.readLine();
  return out;
}

bool writeLines(const QString& path, const QStringList& lines) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  QTextStream ts(&f);
  ts.setCodec("UTF-8");
  for (const QString& line : lines) ts << line << '\n';
  return true;
}

}  // namespace

QString readValue(const QString& path, const QString& key,
                  const QString& fallback) {
  for (const QString& line : readLines(path)) {
    if (line.startsWith(key + QLatin1Char('='))) {
      return line.mid(key.size() + 1).trimmed();
    }
  }
  return fallback;
}

bool setValue(const QString& path, const QString& key, const QString& value) {
  QStringList lines = readLines(path);
  const QString newLine = key + QLatin1Char('=') + value;
  for (int i = 0; i < lines.size(); ++i) {
    if (lines[i].startsWith(key + QLatin1Char('=')) || lines[i] == key) {
      if (lines[i] == newLine) return true;  // 已经是这个值，不重写文件
      lines[i] = newLine;
      return writeLines(path, lines);
    }
  }
  lines << newLine;
  return writeLines(path, lines);
}

}  // namespace bangke::ini
