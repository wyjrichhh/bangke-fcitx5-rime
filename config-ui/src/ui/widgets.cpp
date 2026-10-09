#include "widgets.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyledItemDelegate>
#include <QStyle>
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
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, kRowSpacing, 0, kRowSpacing);
  layout->setSpacing(6);
  auto* labelWidget = makeLabel(label, false, this);
  labelWidget->setAttribute(Qt::WA_TransparentForMouseEvents);
  layout->addWidget(labelWidget);
  if (!hint.isEmpty()) {
    auto* hintIcon = new HintIcon(hint, this);
    // 问号要自己收鼠标（要弹 tooltip），所以不透明
    layout->addWidget(hintIcon);
  }
  layout->addStretch(1);
  if (control) layout->addWidget(control);
}

void RowShell::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setPen(QPen(palette::separator(this), 1));
  painter.drawLine(0, height() - 1, width(), height() - 1);
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
  slider_->setFixedWidth(220);
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

ComboRow::ComboRow(const QString& label, const QString& hint,
                   const QStringList& items, const QStringList& values,
                   const QString& current, QWidget* parent)
    : RowShell(label, hint, nullptr, parent) {
  combo_ = new QComboBox(this);
  for (int i = 0; i < items.size(); ++i) {
    combo_->addItem(items.at(i), i < values.size() ? values.at(i) : items.at(i));
  }
  combo_->setMinimumWidth(160);
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
  edit_->setMinimumWidth(280);
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
  headerLayout->setContentsMargins(20, 20, 20, 6);
  headerLayout->setSpacing(2);
  auto* titleLabel = makeLabel(title, false, header);
  QFont titleFont = titleLabel->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() + 5);
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  headerLayout->addWidget(titleLabel);
  if (!subtitle.isEmpty()) {
    auto* subtitleLabel = makeLabel(subtitle, true, header);
    QFont subtitleFont = subtitleLabel->font();
    subtitleFont.setPointSizeF(subtitleFont.pointSizeF() - 1);
    subtitleLabel->setFont(subtitleFont);
    headerLayout->addWidget(subtitleLabel);
  }
  outer->addWidget(header);

  auto* scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  content_ = new QWidget(scroll);
  layout_ = new QVBoxLayout(content_);
  layout_->setContentsMargins(20, 4, 20, 20);
  layout_->setSpacing(0);
  layout_->addStretch(1);
  scroll->setWidget(content_);
  outer->addWidget(scroll, 1);
}

void PageScaffold::addSection(const QString& title) {
  if (layout_->count() > 1) addSpacing(18);
  auto* label = makeLabel(title, true, content_);
  QFont font = label->font();
  font.setBold(true);
  label->setFont(font);
  layout_->insertWidget(layout_->count() - 1, label);
}

void PageScaffold::addRow(QWidget* row) {
  layout_->insertWidget(layout_->count() - 1, row);
}

void PageScaffold::addWidget(QWidget* widget) {
  layout_->insertWidget(layout_->count() - 1, widget);
}

void PageScaffold::addSpacing(int height) {
  layout_->insertSpacing(layout_->count() - 1, height);
}

Sidebar::Sidebar(QWidget* parent) : QListWidget(parent) {
  setObjectName(QStringLiteral("sidebar"));
  setFixedWidth(200);
  setFrameShape(QFrame::NoFrame);
  setSelectionMode(QAbstractItemView::SingleSelection);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setSpacing(0);
  // 底色在这里给死：控件级样式表优先级高于应用级 QSS，靠应用级规则会被盖掉。
  setStyleSheet(QStringLiteral("QListWidget { background: %1; outline: none; }"
                               "QListWidget::item { border: none; }"
                               "QListWidget::item:selected { background: transparent; }")
                    .arg(palette::sidebar(this).name()));

  // 选中态是自绘的圆角条，QSS 画不出来。
  class Delegate : public QStyledItemDelegate {
   public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
      painter->save();
      painter->setRenderHint(QPainter::Antialiasing, true);
      const QRectF row = QRectF(option.rect).adjusted(8, 2, -8, -2);
      if (option.state & QStyle::State_Selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(palette::sidebarSelection(option.widget));
        painter->drawRoundedRect(row, 6, 6);
      }
      QColor textColor = palette::text(option.widget);
      if (!(option.state & QStyle::State_Selected))
        textColor.setAlphaF(0.75);  // 未选中也要读得清（mac 侧栏用正文色）
      painter->setPen(textColor);
      QFont font = option.font;
      if (option.state & QStyle::State_Selected) font.setBold(true);
      painter->setFont(font);
      painter->drawText(row.adjusted(8, 0, -8, 0),
                        Qt::AlignLeft | Qt::AlignVCenter,
                        index.data(Qt::DisplayRole).toString());
      painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override {
      Q_UNUSED(index);
      return QSize(option.rect.width(), 30);
    }
  };
  setItemDelegate(new Delegate(this));
}

}  // namespace bangke
