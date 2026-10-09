#include "stylepatch.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include "paths.h"
#include "scheme.h"

namespace bangke {
namespace {

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
  for (int i = 0; i < lines.size(); ++i) {
    stream << lines.at(i);
    if (i + 1 < lines.size()) stream << '\n';
  }
  return true;
}

int indentOf(const QString& line) {
  int n = 0;
  while (n < line.size() && line.at(n) == QLatin1Char(' ')) ++n;
  return n;
}

// 删掉平铺（style/corner_radius:）与嵌套（style: 下面缩进的 corner_radius:）
// 两种写法里的同一个键；嵌套块因此空了就把块头也删掉。
void removePatchKey(QStringList& lines, const QString& keyPath) {
  const int slash = keyPath.indexOf(QLatin1Char('/'));
  if (slash < 0) return;
  const QString parentHeader = keyPath.left(slash) + QLatin1Char(':');
  const QString childKey = keyPath.mid(slash + 1) + QLatin1Char(':');
  const QString flat = keyPath + QLatin1Char(':');

  int parentIndent = -1;
  int parentLine = -1;
  bool parentHasChild = false;
  const auto closeParent = [&]() {
    if (parentIndent >= 0 && !parentHasChild && parentLine >= 0)
      lines.removeAt(parentLine);
    parentIndent = -1;
    parentLine = -1;
    parentHasChild = false;
  };

  for (int i = 0; i < lines.size();) {
    const QString trimmed = lines.at(i).trimmed();
    const int indent = indentOf(lines.at(i));
    if (parentIndent >= 0 && indent > parentIndent) {
      if (trimmed.startsWith(childKey)) {
        lines.removeAt(i);
        continue;
      }
      parentHasChild = true;
      ++i;
      continue;
    }
    if (parentIndent >= 0) {
      closeParent();
      if (parentLine >= 0 && parentLine < i) --i;
      continue;
    }
    if (trimmed == parentHeader) {
      parentIndent = indent;
      parentLine = i;
      ++i;
      continue;
    }
    if (trimmed.startsWith(flat)) {
      lines.removeAt(i);
      continue;
    }
    ++i;
  }
  closeParent();
}

// 把键写到 patch: 块末尾（块不存在就先补一行块头）。
void setPatchKey(QStringList& lines, const QString& keyPath,
                 const QString& value) {
  removePatchKey(lines, keyPath);
  int patchLine = -1;
  for (int i = 0; i < lines.size(); ++i) {
    if (indentOf(lines.at(i)) == 0 &&
        lines.at(i).trimmed() == QLatin1String("patch:")) {
      patchLine = i;
      break;
    }
  }
  if (patchLine < 0) {
    lines.prepend(QStringLiteral("patch:"));
    patchLine = 0;
  }
  int insertAt = lines.size();
  for (int i = patchLine + 1; i < lines.size(); ++i) {
    if (lines.at(i).trimmed().isEmpty()) continue;
    if (indentOf(lines.at(i)) == 0) {
      insertAt = i;
      break;
    }
  }
  lines.insert(insertAt,
               QStringLiteral("  ") + keyPath + QStringLiteral(": ") + value);
}

}  // namespace

void writeStylePatch(const StyleOptions& style, const StyleOptions& factory) {
  const QString path = paths::customBangkeYaml();
  QStringList lines = readLines(path);
  if (lines.isEmpty()) lines << QStringLiteral("patch:");

  const QList<QPair<QString, QString>> factoryEntries = styleToPatch(factory);
  const QList<QPair<QString, QString>> entries = styleToPatch(style);
  for (int i = 0; i < entries.size() && i < factoryEntries.size(); ++i) {
    const QString keyPath = QStringLiteral("style/") + entries.at(i).first;
    if (entries.at(i).second == factoryEntries.at(i).second)
      removePatchKey(lines, keyPath);
    else
      setPatchKey(lines, keyPath, entries.at(i).second);
  }
  writeLines(path, lines);
}

void resetStylePatch(const StyleOptions& factory) {
  writeStylePatch(factory, factory);
}

void writeCustomScheme(const QString& id, const QString& displayName,
                       const QList<QPair<QString, QString>>& colors) {
  const QString path = paths::customBangkeYaml();
  QStringList lines = readLines(path);
  if (lines.isEmpty()) lines << QStringLiteral("patch:");

  const QString prefix = QStringLiteral("preset_color_schemes/") + id + QLatin1Char('/');
  if (!displayName.isEmpty())
    setPatchKey(lines, prefix + QStringLiteral("name"), displayName);
  for (const auto& entry : colors) {
    if (entry.second.isEmpty()) continue;
    setPatchKey(lines, prefix + entry.first, entry.second);
  }
  writeLines(path, lines);
}

}  // namespace bangke
