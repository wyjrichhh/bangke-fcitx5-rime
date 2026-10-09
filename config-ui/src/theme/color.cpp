#include "color.h"

namespace bangke {

QColor parseRimeColor(const QString& spec) {
  const QString s = spec.trimmed();
  if (s.isEmpty()) return QColor();

  if (s.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
    const QString hex = s.mid(2);
    bool ok = false;
    const uint v = hex.toUInt(&ok, 16);
    if (!ok || (hex.size() != 6 && hex.size() != 8)) return QColor();
    const int r = int(v & 0xFF);
    const int g = int((v >> 8) & 0xFF);
    const int b = int((v >> 16) & 0xFF);
    const int a = hex.size() == 8 ? int((v >> 24) & 0xFF) : 255;
    return QColor(r, g, b, a);
  }
  // 其它形态交给 Qt（#RRGGBB / 颜色名）。
  return QColor(s);
}

QColor blendMac(const QColor& fg, const QColor& bg) {
  auto mix = [](int f, int b) { return (f * 2 + b) / 3; };
  return QColor(mix(fg.red(), bg.red()), mix(fg.green(), bg.green()),
                mix(fg.blue(), bg.blue()), mix(fg.alpha(), bg.alpha()));
}

QString toThemeColor(const QColor& color) {
  // fcitx5 的 Color::setFromString 把 8 位 hex 解析成 #RRGGBBAA（先 RGB 后 alpha），
  // 与 CSS/Qt 的 #AARRGGBB 相反 —— 写错会把「85% 黑」读成不透明的暗红。
  // 不透明时写 6 位，避免歧义。
  if (color.alpha() == 255) {
    return QStringLiteral("#%1%2%3")
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .toUpper();
  }
  return QStringLiteral("#%1%2%3%4")
      .arg(color.red(), 2, 16, QLatin1Char('0'))
      .arg(color.green(), 2, 16, QLatin1Char('0'))
      .arg(color.blue(), 2, 16, QLatin1Char('0'))
      .arg(color.alpha(), 2, 16, QLatin1Char('0'))
      .toUpper();
}

QString toRimeColor(const QColor& color) {
  if (color.alpha() == 255) {
    return QStringLiteral("0x%1%2%3")
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .toLower();
  }
  return QStringLiteral("0x%1%2%3%4")
      .arg(color.alpha(), 2, 16, QLatin1Char('0'))
      .arg(color.blue(), 2, 16, QLatin1Char('0'))
      .arg(color.green(), 2, 16, QLatin1Char('0'))
      .arg(color.red(), 2, 16, QLatin1Char('0'))
      .toLower();
}

}  // namespace bangke
