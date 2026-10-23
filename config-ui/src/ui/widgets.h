#ifndef FCITX5_RIME_CONFIG_UI_WIDGETS_H_
#define FCITX5_RIME_CONFIG_UI_WIDGETS_H_

#include <QAbstractButton>
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QString>
#include <QWidget>

class QAbstractItemView;
class QFrame;
class QListView;
class QModelIndex;
class QMouseEvent;
class QLineEdit;
class QScrollArea;
class QSlider;
class QVBoxLayout;

namespace bangke {

class SettingsComboBox : public QComboBox {
  Q_OBJECT
 public:
  explicit SettingsComboBox(QWidget* parent = nullptr);
  void showPopup() override;
  void hidePopup() override;
  QAbstractItemView* popupView() const;
  QWidget* popupContainer() const;

 protected:
  void paintEvent(QPaintEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void commitIndex(const QModelIndex& index);
  QFrame* popup_ = nullptr;
  QListView* popupView_ = nullptr;
};

// mac 的开关：轨道 + 圆点，切换时滑动。QSS 的 ::indicator 做不出这个观感。
class Switch : public QAbstractButton {
  Q_OBJECT
  Q_PROPERTY(qreal position READ position WRITE setPosition)
 public:
  explicit Switch(QWidget* parent = nullptr);
  QSize sizeHint() const override;
  qreal position() const { return position_; }
  void setPosition(qreal value);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void checkStateSet() override;
  // Qt 在 click() 内部把 blockRefresh 置真，于是 setChecked 会**跳过 checkStateSet()**：
  // 只重写 checkStateSet 的话，旋钮只在构造时到位、之后点了永远不动（实测踩过）。
  // 所以点击那条路自己驱动动画。
  void nextCheckState() override;

 private:
  void animateTo(bool on);

  qreal position_ = 0.0;
};

// 行尾的问号：悬停弹出说明。mac 用自制气泡（系统 tooltip 在 accessory 进程里
// 表现不一致），这里用 Qt 的 tooltip 达到同样的可达性。
class HintIcon : public QLabel {
 public:
  explicit HintIcon(const QString& hint, QWidget* parent = nullptr);
};

// 一行设置：左标签（+可选问号），右侧控件，底部发丝线。
class RowShell : public QWidget {
 public:
  RowShell(const QString& label, const QString& hint, QWidget* control,
           QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;
};

// 滑杆行：右侧实时数值（mac 是等宽数字的定宽数值）。
class SliderRow : public RowShell {
  Q_OBJECT
 public:
  SliderRow(const QString& label, const QString& hint, double minimum,
            double maximum, double step, double initial,
            const QString& suffix = QString(), QWidget* parent = nullptr);

  double value() const;
  void setValue(double value);

 signals:
  void valueChanged(double value);

 private:
  void updateText();
  QSlider* slider_ = nullptr;
  QLabel* value_ = nullptr;
  double minimum_ = 0.0;
  double step_ = 1.0;
  QString suffix_;
};

// 下拉行。
class ComboRow : public RowShell {
  Q_OBJECT
 public:
  ComboRow(const QString& label, const QString& hint,
           const QStringList& items, const QStringList& values,
           const QString& current, QWidget* parent = nullptr);
  QComboBox* combo() const { return combo_; }
  QString currentValue() const;
  void setCurrentValue(const QString& value);

 private:
  QComboBox* combo_ = nullptr;
};

// 开关行。
class ToggleRow : public RowShell {
  Q_OBJECT
 public:
  ToggleRow(const QString& label, const QString& hint, bool checked,
            QWidget* parent = nullptr);
  bool isChecked() const;
  void setChecked(bool checked);
  Switch* toggle() const { return toggle_; }

 protected:
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  Switch* toggle_ = nullptr;
};

// 文本输入行。
class TextRow : public RowShell {
  Q_OBJECT
 public:
  TextRow(const QString& label, const QString& hint, const QString& value,
          QWidget* parent = nullptr);
  QString text() const;
  void setText(const QString& text);

 private:
  QLineEdit* edit_ = nullptr;
};

// 页脚手架：页头（标题 + 可选副标题）+ 可滚动内容 + 分组小标题。
class PageScaffold : public QWidget {
 public:
  PageScaffold(const QString& title, const QString& subtitle,
               QWidget* parent = nullptr);

  void addSection(const QString& title, bool collapsible = false);
  void addRow(QWidget* row);
  void addWidget(QWidget* widget);
  void addFixedWidget(QWidget* widget);
  void addSideWidget(QWidget* widget);
  void addSpacing(int height);

 private:
  QVBoxLayout* layout_ = nullptr;
  QWidget* content_ = nullptr;
  QVBoxLayout* sectionLayout_ = nullptr;
};

// 左侧导航（200px，选中态是 accent 22% 的圆角条）。
class Sidebar : public QListWidget {
  Q_OBJECT
 public:
  explicit Sidebar(QWidget* parent = nullptr);
};

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_UI_WIDGETS_H_
