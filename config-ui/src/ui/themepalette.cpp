#include "themepalette.h"

#include <QWidget>

#include <algorithm>

namespace bangke::palette {
namespace {

// 把一个颜色朝另一个颜色混，t = 0 全是 a、1 全是 b。只用于 hover 这类
// 需要"比底色亮一点"的地方。
QColor mix(const QColor& a, const QColor& b, double t) {
  const double keep = 1.0 - std::clamp(t, 0.0, 1.0);
  return QColor(qRound(a.red() * keep + b.red() * t),
                qRound(a.green() * keep + b.green() * t),
                qRound(a.blue() * keep + b.blue() * t));
}

// 落在强调色上的文字色：亮底配深字，否则配白字。Omarchy 的 accent 什么色都有
// （黄的、浅蓝的都有），写死白色会出现白底白字。
QColor onAccent(const QColor& accent) {
  const double luminance = (0.299 * accent.red() + 0.587 * accent.green() +
                            0.114 * accent.blue()) /
                           255.0;
  return luminance > 0.6 ? QColor(0x1A, 0x1A, 0x1A) : QColor(Qt::white);
}

}  // namespace

bool dark() { return appPlatformTheme().dark; }

bool isDark(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().dark;
}

QColor window(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().window;
}

QColor sidebar(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().sidebar;
}

QColor card(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().card;
}

QColor separator(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().separator;
}

QColor text(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().text;
}

QColor secondaryText(const QWidget* widget) {
  Q_UNUSED(widget);
  return mix(appPlatformTheme().text, appPlatformTheme().window, 0.35);
}

QColor accent(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().accent;
}

QColor sidebarSelection(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().sidebarSelection;
}

QColor well(const QWidget* widget) {
  Q_UNUSED(widget);
  return appPlatformTheme().well;
}

QString styleSheet(const QWidget* widget) {
  Q_UNUSED(widget);
  const QString fg = text(nullptr).name();
  const QString bg = window(nullptr).name();
  const QString cardBg = card(nullptr).name();
  const QString line = mix(card(nullptr), text(nullptr), 0.16).name();
  const QString acc = accent(nullptr).name();
  const QString wellColor = well(nullptr).name();
  const QString onAcc = onAccent(accent(nullptr)).name();
  // hover 底色原来写死成 #525252 / #F0F0F0（Adwaita 的两档）。有桌面主题时
  // 由底色朝文字方向混出来，才跟着主题走。
  const QString hover = appPlatformTheme().valid
                            ? mix(card(nullptr), text(nullptr), 0.10).name()
                            : (appPlatformTheme().dark ? QStringLiteral("#525252")
                                            : QStringLiteral("#F0F0F0"));

  // 没有平台主题时 Qt 用 Fusion（浅色），深色系统下这些原生控件会亮得刺眼，
  // 所以把用到的都自己画一遍。
  return QStringLiteral(R"(
QWidget { color: %1; font-size: 13px; }
QMainWindow, QStackedWidget { background: %2; }
QWidget#brandNavigation { background: %10; }
QLabel[secondary="true"] { color: %9; }
QLabel#pageTitle { font-size: 26px; font-weight: 600; }
QLabel#brandMark { color: %7; font-size: 76px; font-weight: 300; background: transparent; }
QLabel#brandTitle { font-size: 23px; font-weight: 600; }
QLabel#brandCaption { color: %9; font-size: 12px; line-height: 1.6; }
QFrame#settingsGroup { background: %4; border: 1px solid %5; border-radius: 8px; }
QLabel#sectionHeading { color: %7; border-bottom: 1px solid %5; font-size: 12px; }
QWidget#settingsFooter { background: %2; border-top: 1px solid %5; }
QScrollArea { border: none; background: transparent; }
QScrollArea > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: %3; border-radius: 4px; min-height: 24px; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QComboBox {
  background: %4; border: 1px solid %5; border-radius: 6px;
  padding: 7px 12px; min-height: 24px;
}
QComboBox:hover { background: %6; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
  background: %4; border: 1px solid %5; border-radius: 6px;
  selection-background-color: %7; selection-color: %8; outline: none;
}
QLineEdit {
  background: %4; border: 1px solid %5; border-radius: 6px; padding: 8px 12px;
}
QPushButton {
  background: %4; border: 1px solid %5; border-radius: 6px;
  padding: 8px 18px; min-height: 24px;
}
QPushButton:hover { background: %6; }
QPushButton[primary="true"] { background: %7; border-color: %7; color: %8; }
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
QListView::item:selected, QListWidget::item:selected { background: %7; color: %8; }
QToolTip { background: %4; color: %1; border: 1px solid %5; padding: 4px 6px; }
)")
      .arg(fg, bg, wellColor, cardBg, line, hover, acc, onAcc, secondaryText(nullptr).name())
      .arg(sidebar(nullptr).name());
}

}  // namespace bangke::palette
