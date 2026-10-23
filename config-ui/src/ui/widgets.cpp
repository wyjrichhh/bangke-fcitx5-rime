#include "widgets.h"

#include <QComboBox>
#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QStyle>
#include <QStyleFactory>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
#include <QKeyEvent>
#include <QApplication>
#include <QVBoxLayout>

#include "ui/themepalette.h"

namespace bangke {
namespace {

constexpr int kRowSpacing = 8;

QLabel* makeLabel(const QString& text, bool secondary = false,
                  QWidget* parent = nullptr) {
  auto* label = new QLabel(text, parent);
  QFont font = label->font();
  font.setPointSizeF(font.pointSizeF());
  label->setFont(font);
  label->setProperty("secondary", secondary);
  if (secondary) {
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, palette::secondaryText(label));
    label->setPalette(pal);
  }
  return label;
}

}  // namespace

Switch::Switch(QWidget* parent) : QAbstractButton(parent) {
  setCheckable(true);
  setCursor(Qt::PointingHandCursor);
  // 不给键盘焦点：Fusion 会在获得焦点时画一圈强调色焦点框，容易被误读成
  // 「开关已打开」（实测用户就是这么误会的），而行的整行可点已经够用。
  setFocusPolicy(Qt::NoFocus);
}

QSize Switch::sizeHint() const { return QSize(38, 22); }

void Switch::setPosition(qreal value) {
  position_ = qBound(0.0, value, 1.0);
  update();
}

void Switch::animateTo(bool on) {
  auto* animation = new QPropertyAnimation(this, "position", this);
  animation->setDuration(120);
  animation->setEasingCurve(QEasingCurve::InOutQuad);
  animation->setStartValue(position_);
  animation->setEndValue(on ? 1.0 : 0.0);
  animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void Switch::checkStateSet() { animateTo(isChecked()); }

void Switch::nextCheckState() {
  setChecked(!isChecked());
  animateTo(isChecked());  // blockRefresh 下 checkStateSet 不会跑，这里自己来
}

void Switch::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRectF track = rect().adjusted(0, (rect().height() - 22) / 2.0, 0,
                                       -(rect().height() - 22) / 2.0);
  const qreal radius = track.height() / 2.0;
  QColor trackColor = palette::well(this);
  if (isChecked()) trackColor = palette::accent(this);
  else if (!isEnabled()) trackColor.setAlpha(80);
  painter.setPen(Qt::NoPen);
  painter.setBrush(trackColor);
  painter.drawRoundedRect(track, radius, radius);

  const qreal knobRadius = radius - 2;
  const qreal left = track.left() + radius;
  const qreal right = track.right() - radius;
  const QPointF center(left + (right - left) * position_, track.center().y());
  painter.setBrush(QColor(255, 255, 255));
  painter.drawEllipse(center, knobRadius, knobRadius);
}

HintIcon::HintIcon(const QString& hint, QWidget* parent) : QLabel(parent) {
  setText(QStringLiteral("?"));
  setFixedSize(14, 14);
  setAlignment(Qt::AlignCenter);
  setToolTip(hint);
  setCursor(Qt::WhatsThisCursor);
  const QColor color = palette::secondaryText(this);
  setStyleSheet(QStringLiteral("QLabel { border: 1px solid %1; border-radius: "
                               "7px; color: %1; font-size: 9px; }")
                    .arg(color.name(QColor::HexArgb)));
}

RowShell::RowShell(const QString& label, const QString& hint, QWidget* control,
                   QWidget* parent)
    : QWidget(parent) {
  setProperty("settingLabel", label);
  setAccessibleName(label);
  setMinimumHeight(64);
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(18, 12, 18, 12);
  layout->setSpacing(18);
  auto* labels = new QVBoxLayout;
  labels->setSpacing(5);
  auto* labelWidget = makeLabel(label, false, this);
  labelWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
  labels->addWidget(labelWidget);
  if (!hint.isEmpty()) {
    auto* description = makeLabel(hint, true, this);
    description->setWordWrap(true);
    description->setAttribute(Qt::WA_TransparentForMouseEvents);
    QFont small = description->font();
    small.setPointSizeF(qMax(9.0, small.pointSizeF() - 1));
    description->setFont(small);
    labels->addWidget(description);
  }
  layout->addLayout(labels, 1);
  if (control) layout->addWidget(control);

}

void RowShell::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  QColor line = palette::text(this);
  line.setAlpha(32);
  painter.setPen(QPen(line, 1));
  painter.drawLine(18, height() - 1, width() - 18, height() - 1);
}

SliderRow::SliderRow(const QString& label, const QString& hint, double minimum,
                     double maximum, double step, double initial,
                     const QString& suffix, QWidget* parent)
    : RowShell(label, hint, nullptr, parent),
      minimum_(minimum),
      step_(step > 0 ? step : 1.0),
      suffix_(suffix) {
  auto* container = new QWidget(this);
  auto* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(8);

  slider_ = new QSlider(Qt::Horizontal, container);
  slider_->setSingleStep(1);
  slider_->setPageStep(1);
  slider_->setRange(0, qMax(1, int(qRound((maximum - minimum) / step_))));
  slider_->setMinimumWidth(96);
  slider_->setMaximumWidth(140);
  layout->addWidget(slider_);

  value_ = makeLabel(QString(), true, container);
  value_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  value_->setMinimumWidth(46);
  QFont valueFont = value_->font();
  valueFont.setStyleHint(QFont::Monospace);
  value_->setFont(valueFont);
  layout->addWidget(value_);

  // 把控件塞进 RowShell 的布局末尾（stretch 之前）。
  if (auto* shell = qobject_cast<QHBoxLayout*>(this->layout())) {
    shell->addWidget(container);
    Q_UNUSED(shell);
  }

  connect(slider_, &QSlider::valueChanged, this, [this](int) {
    updateText();
    emit valueChanged(value());
  });
  setValue(initial);
}

double SliderRow::value() const {
  return minimum_ + slider_->value() * step_;
}

void SliderRow::setValue(double value) {
  const int steps = int(qRound((value - minimum_) / step_));
  QSignalBlocker blocker(slider_);
  slider_->setValue(steps);
  updateText();
}

void SliderRow::updateText() {
  const double v = value();
  const QString text = (step_ >= 1.0) ? QString::number(int(qRound(v)))
                                      : QString::number(v, 'f', 2);
  value_->setText(text + suffix_);
}

namespace {
class ComboItemDelegate : public QStyledItemDelegate {
 public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    auto size = QStyledItemDelegate::sizeHint(option, index);
    size.setHeight(32);
    return size;
  }
};
}  // namespace

SettingsComboBox::SettingsComboBox(QWidget* parent) : QComboBox(parent) {
  // Keep GTK menu animation and sizing out of the settings controls.
  auto* fieldStyle = QStyleFactory::create(QStringLiteral("Fusion"));
  fieldStyle->setParent(this);
  setStyle(fieldStyle);
  setFixedSize(220, 34);
  setMaxVisibleItems(10);
  setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  setMinimumContentsLength(12);

  popup_ = new QFrame(this, Qt::Popup | Qt::FramelessWindowHint);
  popup_->setObjectName(QStringLiteral("settingsComboContainer"));
  popup_->setAttribute(Qt::WA_StyledBackground);
  popup_->setAttribute(Qt::WA_TranslucentBackground);
  popup_->setAttribute(Qt::WA_NoMouseReplay);
  auto* popupStyle = QStyleFactory::create(QStringLiteral("Fusion"));
  popupStyle->setParent(popup_);
  popup_->setStyle(popupStyle);
  popup_->installEventFilter(this);
  auto* layout = new QVBoxLayout(popup_);
  layout->setContentsMargins(1, 1, 1, 1);
  layout->setSpacing(0);
  popupView_ = new QListView(popup_);
  popupView_->setObjectName(QStringLiteral("settingsComboPopup"));
  popupView_->viewport()->setObjectName(QStringLiteral("settingsComboViewport"));
  popupView_->setFrameShape(QFrame::NoFrame);
  popupView_->setFocusPolicy(Qt::StrongFocus);
  popup_->setFocusPolicy(Qt::StrongFocus);
  popup_->setFocusProxy(popupView_);
  popupView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  popupView_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  popupView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  // A read-only option list must not start an input-method composition.
  popupView_->setAttribute(Qt::WA_InputMethodEnabled, false);
  popupView_->viewport()->setAttribute(Qt::WA_InputMethodEnabled, false);
  popupView_->setUniformItemSizes(true);
  popupView_->setMouseTracking(true);
  popupView_->setItemDelegate(new ComboItemDelegate(popupView_));
  popupView_->setMaximumHeight(320);
  popupView_->installEventFilter(this);
  popupView_->viewport()->installEventFilter(this);
  layout->addWidget(popupView_);
  connect(popupView_, &QListView::clicked, this, &SettingsComboBox::commitIndex);
  connect(popupView_, &QListView::activated, this, &SettingsComboBox::commitIndex);
  connect(popupView_, &QListView::entered, this, [this](const QModelIndex& index) {
    if (!(index.flags() & Qt::ItemIsEnabled)) return;
    popupView_->setCurrentIndex(index);
    emit highlighted(index.row());
  });
}

QAbstractItemView* SettingsComboBox::popupView() const { return popupView_; }
QWidget* SettingsComboBox::popupContainer() const { return popup_; }

void SettingsComboBox::showPopup() {
  if (!isEnabled() || count() == 0) return;
  if (popup_->isVisible()) { hidePopup(); return; }
  popupView_->setModel(model());
  popupView_->setModelColumn(modelColumn());
  popupView_->setRootIndex(rootModelIndex());
  const auto current = model()->index(currentIndex(), modelColumn(), rootModelIndex());
  popupView_->setCurrentIndex(current);
  auto* screen = window()->windowHandle() ? window()->windowHandle()->screen()
                                          : QGuiApplication::primaryScreen();
  const QRect available = screen->availableGeometry();
  const QPoint below = mapToGlobal(QPoint(0, height() + 4));
  const QPoint above = mapToGlobal(QPoint(0, -4));
  int popupHeight = qMin(count(), maxVisibleItems()) * 32 + 2;
  const int roomBelow = available.bottom() - below.y() + 1;
  const int roomAbove = above.y() - available.top();
  const bool useAbove = roomBelow < popupHeight && roomAbove > roomBelow;
  popupHeight = qMax(1, qMin(popupHeight, useAbove ? roomAbove : roomBelow));
  const int popupWidth = qMin(width(), available.width());
  popup_->setFixedSize(popupWidth, popupHeight);
  const int x = qBound(available.left(), below.x(), available.right() - popupWidth + 1);
  popup_->move(x, useAbove ? above.y() - popupHeight : below.y());
  popup_->ensurePolished();
  popupView_->ensurePolished();
  qApp->installEventFilter(this);
  popupView_->setFocus(Qt::PopupFocusReason);
  popup_->show();
  popupView_->setFocus(Qt::PopupFocusReason);
  popupView_->scrollTo(current, QAbstractItemView::PositionAtCenter);
  update();
}

void SettingsComboBox::hidePopup() {
  if (popup_->isVisible()) popup_->hide();
  else QComboBox::hidePopup();
  update();
}

void SettingsComboBox::commitIndex(const QModelIndex& index) {
  if (!index.isValid() || !(index.flags() & Qt::ItemIsEnabled) ||
      !(index.flags() & Qt::ItemIsSelectable)) return;
  const int row = index.row();
  hidePopup();
  setCurrentIndex(row);
  emit activated(row);
}

bool SettingsComboBox::eventFilter(QObject* watched, QEvent* event) {
  if (watched == popup_ && event->type() == QEvent::Hide) {
    qApp->removeEventFilter(this);
    // Outside clicks also need to reset QComboBox's internal pressed state.
    QComboBox::hidePopup();
    update();
  }
  if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride) &&
      popupView_ && (popup_->isVisible() || watched == popup_ ||
                     watched == popupView_ || watched == popupView_->viewport())) {
    auto* key = static_cast<QKeyEvent*>(event);
    const bool navigation = key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return ||
                            key->key() == Qt::Key_Enter || key->key() == Qt::Key_Up ||
                            key->key() == Qt::Key_Down || key->key() == Qt::Key_Home ||
                            key->key() == Qt::Key_End || key->key() == Qt::Key_PageUp ||
                            key->key() == Qt::Key_PageDown;
    if (event->type() == QEvent::ShortcutOverride && navigation) {
      key->accept();
      return true;
    }
    if (event->type() == QEvent::KeyPress) {
      if (key->key() == Qt::Key_Escape) { hidePopup(); return true; }
      if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
        commitIndex(popupView_->currentIndex());
        return true;
      }
      // Wayland can retain keyboard delivery on the parent surface during a popup grab.
      if (navigation && popup_->isVisible() && watched != popupView_ &&
          watched != popupView_->viewport()) {
        QApplication::sendEvent(popupView_, event);
        return true;
      }
    }
  }
  return QComboBox::eventFilter(watched, event);
}

ComboRow::ComboRow(const QString& label, const QString& hint,
                   const QStringList& items, const QStringList& values,
                   const QString& current, QWidget* parent)
    : RowShell(label, hint, nullptr, parent) {
  combo_ = new SettingsComboBox(this);
  for (int i = 0; i < items.size(); ++i) {
    combo_->addItem(items.at(i), i < values.size() ? values.at(i) : items.at(i));
  }
  if (auto* shell = qobject_cast<QHBoxLayout*>(this->layout()))
    shell->addWidget(combo_);
  setCurrentValue(current);
}

QString ComboRow::currentValue() const {
  return combo_->currentData().toString();
}

void ComboRow::setCurrentValue(const QString& value) {
  int index = combo_->findData(value);
  // 配置里的值不在候选里（例如字体已卸载）也要如实显示，不能悄悄落回第一项。
  if (index < 0 && !value.isEmpty()) {
    combo_->insertItem(0, value, value);
    index = 0;
  }
  combo_->setCurrentIndex(index >= 0 ? index : 0);
}

ToggleRow::ToggleRow(const QString& label, const QString& hint, bool checked,
                     QWidget* parent)
    : RowShell(label, hint, nullptr, parent) {
  toggle_ = new Switch(this);
  toggle_->setChecked(checked);
  if (auto* shell = qobject_cast<QHBoxLayout*>(this->layout()))
    shell->addWidget(toggle_);
}

void ToggleRow::mouseReleaseEvent(QMouseEvent* event) {
  // 整行都能点：只让那个小开关响应太难点了（用户实测点行上没反应）
  if (event->button() == Qt::LeftButton && rect().contains(event->pos()) &&
      !toggle_->geometry().contains(event->pos()))
    toggle_->toggle();
  QWidget::mouseReleaseEvent(event);
}

bool ToggleRow::isChecked() const { return toggle_->isChecked(); }

void ToggleRow::setChecked(bool checked) { toggle_->setChecked(checked); }

TextRow::TextRow(const QString& label, const QString& hint,
                 const QString& value, QWidget* parent)
    : RowShell(label, hint, nullptr, parent) {
  edit_ = new QLineEdit(value, this);
  edit_->setMinimumWidth(150);
  edit_->setMaximumWidth(220);
  if (auto* shell = qobject_cast<QHBoxLayout*>(this->layout()))
    shell->addWidget(edit_);
}

QString TextRow::text() const { return edit_->text(); }

void TextRow::setText(const QString& text) { edit_->setText(text); }

PageScaffold::PageScaffold(const QString& title, const QString& subtitle,
                           QWidget* parent)
    : QWidget(parent) {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  auto* header = new QWidget(this);
  auto* headerLayout = new QVBoxLayout(header);
  headerLayout->setContentsMargins(32, 28, 32, 18);
  headerLayout->setSpacing(8);
  auto* titleLabel = makeLabel(title, false, header);
  QFont titleFont = titleLabel->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() + 5);
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  titleLabel->setObjectName(QStringLiteral("pageTitle"));
  headerLayout->addWidget(titleLabel);
  if (!subtitle.isEmpty()) {
    auto* subtitleLabel = makeLabel(subtitle, true, header);
    QFont subtitleFont = subtitleLabel->font();
    subtitleFont.setPointSizeF(subtitleFont.pointSizeF() - 1);
    subtitleLabel->setFont(subtitleFont);
    subtitleLabel->setWordWrap(true);
    headerLayout->addWidget(subtitleLabel);
  }
  outer->addWidget(header);

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setMinimumSize(0, 0);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setFrameShape(QFrame::NoFrame);
  content_ = new QWidget(scroll);
  layout_ = new QVBoxLayout(content_);
  layout_->setContentsMargins(32, 6, 32, 28);
  layout_->setSpacing(0);
  layout_->addStretch(1);
  scroll->setWidget(content_);
  outer->addWidget(scroll, 1);
}

void PageScaffold::addSideWidget(QWidget* widget) {
  auto* outer = static_cast<QVBoxLayout*>(layout());
  auto* scroll = findChild<QScrollArea*>();
  outer->removeWidget(scroll);
  auto* columns = new QHBoxLayout;
  columns->setContentsMargins(32, 0, 32, 28);
  columns->setSpacing(28);
  layout_->setContentsMargins(0, 0, 8, 0);
  columns->addWidget(scroll, 1);
  auto* previewColumn = new QWidget(this);
  auto* previewLayout = new QVBoxLayout(previewColumn);
  previewLayout->setContentsMargins(0, 0, 0, 0);
  previewLayout->setSpacing(12);
  auto* caption = makeLabel(QStringLiteral("实时预览"), true, previewColumn);
  caption->setObjectName(QStringLiteral("previewColumnHeading"));
  previewLayout->addWidget(caption);
  widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  previewLayout->addWidget(widget);
  auto* explanation = makeLabel(QStringLiteral("调整左侧设置，即时查看候选窗的变化。"), true, previewColumn);
  explanation->setWordWrap(true);
  previewLayout->addWidget(explanation);
  previewLayout->addStretch(1);
  columns->addWidget(previewColumn, 1);
  scroll->setObjectName(QStringLiteral("settingsScroll"));
  outer->addLayout(columns, 1);
}

void PageScaffold::addFixedWidget(QWidget* widget) {
  auto* container = new QWidget(this);
  auto* fixed = new QVBoxLayout(container);
  fixed->setContentsMargins(32, 0, 32, 14);
  fixed->addWidget(widget);
  static_cast<QVBoxLayout*>(layout())->insertWidget(1, container);
}

void PageScaffold::addSection(const QString& title, bool collapsible) {
  auto* group = new QFrame(content_);
  group->setObjectName(QStringLiteral("settingsGroup"));
  sectionLayout_ = new QVBoxLayout(group);
  sectionLayout_->setContentsMargins(0, 0, 0, 0);
  sectionLayout_->setSpacing(0);
  if (layout_->count() > 1) layout_->insertSpacing(layout_->count() - 1, 22);
  if (collapsible) {
    auto* toggle = new QToolButton(content_);
    toggle->setObjectName(QStringLiteral("advancedAppearanceToggle"));
    toggle->setText(title);
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggle->setAccessibleName(title);
    group->setVisible(false);
    connect(toggle, &QToolButton::toggled, group, [group, toggle](bool checked) {
      group->setVisible(checked);
      toggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
    });
    layout_->insertWidget(layout_->count() - 1, toggle);
  } else {
    auto* label = makeLabel(title, true, content_);
    label->setObjectName(QStringLiteral("sectionHeading"));
    label->setContentsMargins(0, 0, 0, 10);
    layout_->insertWidget(layout_->count() - 1, label);
  }
  layout_->insertWidget(layout_->count() - 1, group);
}

void PageScaffold::addRow(QWidget* row) {
  if (sectionLayout_) sectionLayout_->addWidget(row);
  else layout_->insertWidget(layout_->count() - 1, row);
}

void PageScaffold::addWidget(QWidget* widget) {
  if (sectionLayout_) sectionLayout_->addWidget(widget);
  else layout_->insertWidget(layout_->count() - 1, widget);
}

void PageScaffold::addSpacing(int height) {
  layout_->insertSpacing(layout_->count() - 1, height);
}

Sidebar::Sidebar(QWidget* parent) : QListWidget(parent) {
  setObjectName(QStringLiteral("sidebar"));
  setMinimumWidth(200);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setFrameShape(QFrame::NoFrame);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setSpacing(0);
  // 底色在这里给死：控件级样式表优先级高于应用级 QSS，靠应用级规则会被盖掉。
  setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; outline: none; }"
                               "QListWidget::item { border: none; }"
                               "QListWidget::item:selected { background: transparent; }"));

  // 选中态是自绘的圆角条，QSS 画不出来。
  class Delegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
      painter->save();
      painter->setRenderHint(QPainter::Antialiasing, true);
      const QRectF row = QRectF(option.rect).adjusted(0, 3, 0, -3);
      if (option.state & QStyle::State_Selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette::sidebarSelection(option.widget));
        painter->drawRoundedRect(row, 6, 6);
        painter->setBrush(palette::accent(option.widget));
        painter->drawRoundedRect(QRectF(row.left(), row.top() + 10, 3, row.height() - 20), 1.5, 1.5);
      }
      QColor textColor = palette::text(option.widget);
      if (!(option.state & QStyle::State_Selected))
        textColor.setAlphaF(0.75);  // 未选中也要读得清（mac 侧栏用正文色）
      painter->setPen(textColor);
      QFont font = option.font;
      if (option.state & QStyle::State_Selected) font.setBold(true);
      painter->setFont(font);
      painter->drawText(row.adjusted(18, 0, -12, 0),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        index.data(Qt::DisplayRole).toString());
      painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
      Q_UNUSED(index);
      return QSize(option.rect.width(), 48);
    }
  };
  setItemDelegate(new Delegate(this));
}

}  // namespace bangke
