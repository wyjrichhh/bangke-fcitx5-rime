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

  // 三种形态都算这个键的位置：`Key=…`、`# Key=…`、`#Key=…`。
  //
  // fcitx5 自己序列化 classicui.conf 时，把「等于默认值」的键写成 `# Key=默认值`
  // 的**注释**形式。只认第一种的话，这些键匹配不到，就会走到函数末尾**再追加一行**
  // —— 同一个键在文件里出现两次。功能上靠"后面的覆盖前面的"侥幸成立，但那是在赌
  // 解析顺序，真机上已经出现过由此引发的怪异行为。
  // 就地改写注释行正是 fcitx5 自己的做法（非默认值就取消注释）。
  const QStringList keySlots{key + QLatin1Char('='),
                          QStringLiteral("# ") + key + QLatin1Char('='),
                          QStringLiteral("#") + key + QLatin1Char('=')};
  for (int i = 0; i < lines.size(); ++i) {
    for (const QString& slot : keySlots) {
      if (!lines[i].startsWith(slot)) continue;
      if (lines[i] == newLine) return true;  // 已经是这个值，不重写文件
      lines[i] = newLine;
      return writeLines(path, lines);
    }
  }
  lines << newLine;
  return writeLines(path, lines);
}

}  // namespace bangke::ini
