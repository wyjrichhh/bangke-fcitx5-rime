#ifndef FCITX5_RIME_CONFIG_UI_FONTPICKER_H_
#define FCITX5_RIME_CONFIG_UI_FONTPICKER_H_

#include <QDialog>
#include <QString>

class QLineEdit;
class QListWidget;
class QPushButton;

namespace bangke {

// 字体选择：系统里字体族动辄几百个，下拉列表既难滚动、点一下还容易误选，
// 所以改用带过滤框的列表。
class FontPicker : public QDialog {
  Q_OBJECT
 public:
  // 返回选中的字体族；取消返回空。
  static QString pick(const QString& current, QWidget* parent = nullptr);

 private:
  explicit FontPicker(const QString& current, QWidget* parent);

  void applyFilter(const QString& text);

  QLineEdit* filter_ = nullptr;
  QListWidget* list_ = nullptr;
  QPushButton* accept_ = nullptr;
};

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_UI_FONTPICKER_H_
