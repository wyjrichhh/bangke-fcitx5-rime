#include "themepalette.h"

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QPalette>
#include <QProcess>
#include <QWidget>

namespace bangke::palette {
namespace {

bool g_dark = false;

// 系统深浅：先问 GNOME（gsettings），问不到再退回 Qt 调色板。
bool detectDark() {
  QProcess process;
  process.start(QStringLiteral("gsettings"),
                {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                 QStringLiteral("color-scheme")});
  if (process.waitForFinished(1500)) {
    const QString out = QString::fromLocal8Bit(process.readAllStandardOutput());
    if (out.contains(QLatin1String("dark"))) return true;
    if (out.contains(QLatin1String("light"))) return false;
  }
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

bool onUbuntu() {
  static const bool ubuntu = []() {
    QFile file(QStringLiteral("/etc/os-release"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    return QString::fromUtf8(file.readAll()).contains(QLatin1String("ID=ubuntu"));
  }();
  return ubuntu;
}

// 系统强调色：GNOME 46 起有 accent-color，取不到（这台 24.04 就没有）时在
// Ubuntu 上退回 Yaru 的橙 —— 比 Qt 默认那个蓝更像 Ubuntu 原生。
QColor systemAccent() {
  QProcess process;
  process.start(QStringLiteral("gsettings"),
                {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.interface"),
                 QStringLiteral("accent-color")});
  if (process.waitForFinished(1500)) {
    QString name = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    name.remove(QLatin1Char('\''));
    static const QHash<QString, QColor> kAccents = {
        {QStringLiteral("blue"), QColor(0x35, 0x84, 0xE4)},
        {QStringLiteral("teal"), QColor(0x21, 0x9A, 0x8B)},
        {QStringLiteral("green"), QColor(0x3A, 0x94, 0x4A)},
        {QStringLiteral("yellow"), QColor(0xF5, 0xC2, 0x11)},
        {QStringLiteral("orange"), QColor(0xE9, 0x54, 0x20)},
        {QStringLiteral("red"), QColor(0xE0, 0x1B, 0x24)},
        {QStringLiteral("pink"), QColor(0xD5, 0x61, 0x99)},
        {QStringLiteral("purple"), QColor(0x91, 0x41, 0xAC)},
        {QStringLiteral("slate"), QColor(0x6F, 0x83, 0x96)},
    };
    if (kAccents.contains(name)) return kAccents.value(name);
  }
  if (onUbuntu()) return QColor(0xE9, 0x54, 0x20);  // Yaru 的 Ubuntu 橙
  const QColor pal = QApplication::palette().color(QPalette::Highlight);
  return pal.isValid() ? pal : QColor(0x35, 0x84, 0xE4);
}

}  // namespace

void init() { g_dark = detectDark(); }

bool dark() { return g_dark; }

bool isDark(const QWidget* widget) {
  Q_UNUSED(widget);
  return g_dark;
}

// 中性色取 Adwaita / Yaru 的常用值，而不是自己编的灰 —— 跟系统其它应用
// 摆在一起才不违和。
QColor window(const QWidget* widget) {
  Q_UNUSED(widget);
  return g_dark ? QColor(0x33, 0x33, 0x33) : QColor(0xFC, 0xFC, 0xFC);
}

QColor sidebar(const QWidget* widget) {
  Q_UNUSED(widget);
  return g_dark ? QColor(0x3D, 0x3D, 0x3D) : QColor(0xF0, 0xF0, 0xF0);
}

QColor card(const QWidget* widget) {
  Q_UNUSED(widget);
  return g_dark ? QColor(0x46, 0x46, 0x46) : QColor(0xFF, 0xFF, 0xFF);
}

QColor separator(const QWidget* widget) {
  Q_UNUSED(widget);
  return g_dark ? QColor(0xFF, 0xFF, 0xFF, 0x2B) : QColor(0x00, 0x00, 0x00, 0x1F);
}

QColor text(const QWidget* widget) {
  Q_UNUSED(widget);
  return g_dark ? QColor(0xFF, 0xFF, 0xFF) : QColor(0x2E, 0x34, 0x36);
}

QColor secondaryText(const QWidget* widget) {
  Q_UNUSED(widget);
  return g_dark ? QColor(0xFF, 0xFF, 0xFF, 0xC4) : QColor(0x2E, 0x34, 0x36, 0x9E);
}

QColor accent(const QWidget* widget) {
  Q_UNUSED(widget);
  static const QColor cached = systemAccent();
  return cached;
}

QColor sidebarSelection(const QWidget* widget) {
  QColor color = accent(widget);
  color.setAlphaF(0.22);
  return color;
}

QColor well(const QWidget* widget) {
  Q_UNUSED(widget);
  return g_dark ? QColor(0x62, 0x62, 0x62) : QColor(0xDc, 0xDc, 0xDc);
}

QString styleSheet(const QWidget* widget) {
  const QString fg = text(widget).name();
  const QString bg = window(widget).name();
  const QString cardBg = card(widget).name();
  const QString line = separator(widget).name(QColor::HexArgb);
  const QString acc = accent(widget).name();
  const QString wellColor = well(widget).name();
  const QString hover = g_dark ? QStringLiteral("#525252") : QStringLiteral("#F0F0F0");

  // 没有平台主题时 Qt 用 Fusion（浅色），深色系统下这些原生控件会亮得刺眼，
  // 所以把用到的都自己画一遍。
  return QStringLiteral(R"(
QWidget { color: %1; }
QMainWindow, QStackedWidget { background: %2; }
QScrollArea { border: none; background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QComboBox {
  background: %4; border: 1px solid %5; border-radius: 6px;
  padding: 3px 8px; min-height: 24px;
}
QComboBox:hover { background: %6; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
  background: %4; border: 1px solid %5; border-radius: 6px;
  selection-background-color: %7; selection-color: white; outline: none;
}
QLineEdit {
  background: %4; border: 1px solid %5; border-radius: 6px; padding: 4px 8px;
}
QPushButton {
  background: %4; border: 1px solid %5; border-radius: 6px;
  padding: 5px 16px; min-height: 22px;
}
QPushButton:hover { background: %6; }
QPushButton[primary="true"] { background: %7; border-color: %7; color: white; }
QPushButton[primary="true"]:hover { background: %7; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator {
  width: 16px; height: 16px; border-radius: 4px;
  border: 1px solid %5; background: %4;
}
QCheckBox::indicator:checked { background: %7; border-color: %7; }
QSlider::groove:horizontal { height: 4px; border-radius: 2px; background: %3; }
QSlider::sub-page:horizontal { background: %7; border-radius: 2px; }
QSlider::handle:horizontal {
  width: 14px; margin: -6px 0; border-radius: 7px;
  background: white; border: 1px solid %5;
}
QListView, QListWidget {
  background: %4; color: %1; border: 1px solid %5; border-radius: 6px; outline: none;
}
QListView::item, QListWidget::item { padding: 4px 8px; border-radius: 4px; }
QListView::item:hover, QListWidget::item:hover { background: %6; }
QListView::item:selected, QListWidget::item:selected { background: %7; color: white; }
QToolTip { background: %4; color: %1; border: 1px solid %5; padding: 4px 6px; }
)")
      .arg(fg, bg, wellColor, cardBg, line, hover, acc);
}

}  // namespace bangke::palette
