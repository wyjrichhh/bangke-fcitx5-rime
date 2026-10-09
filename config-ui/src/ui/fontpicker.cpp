#include "ui/fontpicker.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/themepalette.h"

namespace bangke {
namespace {

// 列表里每项用该字体自己渲染，并附一小段样例，方便一眼看出字形。
const QString kSample = QStringLiteral("永 蚌壳 Aa 123");

}  // namespace

FontPicker::FontPicker(const QString& current, QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(QStringLiteral("选择字体"));
  resize(460, 520);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(14, 14, 14, 14);
  layout->setSpacing(10);

  filter_ = new QLineEdit(this);
  filter_->setPlaceholderText(QStringLiteral("输入字体名筛选…"));
  filter_->setClearButtonEnabled(true);
  layout->addWidget(filter_);

  list_ = new QListWidget(this);
  list_->setUniformItemSizes(false);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  for (const QString& family : QFontDatabase().families()) {
    auto* item = new QListWidgetItem(family, list_);
    QFont font(family);
    font.setPointSizeF(13);
    item->setFont(font);
    item->setToolTip(QStringLiteral("%1 — %2").arg(family, kSample));
    if (family == current) {
      list_->setCurrentItem(item);
      list_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
  }
  layout->addWidget(list_, 1);

  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  auto* cancel = new QPushButton(QStringLiteral("取消"), this);
  accept_ = new QPushButton(QStringLiteral("确定"), this);
  accept_->setProperty("primary", true);
  buttons->addWidget(cancel);
  buttons->addWidget(accept_);
  layout->addLayout(buttons);

  connect(filter_, &QLineEdit::textChanged, this, &FontPicker::applyFilter);
  connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(accept_, &QPushButton::clicked, this, &QDialog::accept);
  connect(list_, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
  filter_->setFocus();
}

void FontPicker::applyFilter(const QString& text) {
  const QString needle = text.trimmed();
  QListWidgetItem* first = nullptr;
  for (int i = 0; i < list_->count(); ++i) {
    QListWidgetItem* item = list_->item(i);
    const bool match =
        needle.isEmpty() ||
        item->text().contains(needle, Qt::CaseInsensitive);
    item->setHidden(!match);
    if (match && !first) first = item;
  }
  // 筛选后如果当前选中项被藏了，落到第一个可见项上，避免「确定」拿到看不见的项。
  if (first && list_->currentItem() && list_->currentItem()->isHidden())
    list_->setCurrentItem(first);
}

QString FontPicker::pick(const QString& current, QWidget* parent) {
  FontPicker dialog(current, parent);
  if (dialog.exec() != QDialog::Accepted) return QString();
  QListWidgetItem* item = dialog.list_->currentItem();
  return item ? item->text() : QString();
}

}  // namespace bangke
