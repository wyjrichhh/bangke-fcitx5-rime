#ifndef FCITX5_RIME_CONFIG_MAINWINDOW_H_
#define FCITX5_RIME_CONFIG_MAINWINDOW_H_

#include <QList>
#include <QMainWindow>

#include "yamlconfig.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private slots:
  void saveAndDeploy();

 private:
  void buildUi();
  void loadToUi();
  void readFromUi();
  QPushButton* makeColorButton(const QString& initial);
  void updateColorButton(QPushButton* btn, const QString& color);
  void updatePreview();

  RimeData data_;

  // AI 预测页
  QCheckBox* aiEnabled_ = nullptr;
  QComboBox* aiDevice_ = nullptr;
  QSpinBox* aiMaxTokens_ = nullptr;
  QSpinBox* aiDebounce_ = nullptr;
  QSpinBox* aiMinInput_ = nullptr;
  QSpinBox* aiMinHanzi_ = nullptr;
  QDoubleSpinBox* aiQuality_ = nullptr;
  QSpinBox* aiTargetIndex_ = nullptr;
  QSpinBox* aiSearchRange_ = nullptr;
  QSpinBox* aiContextWindow_ = nullptr;
  QSpinBox* aiMinContextPrompt_ = nullptr;
  QLineEdit* aiModelPath_ = nullptr;

  // 方案页：每个方案一个启用勾选 + 简繁下拉（0=简体，1=繁体）+ 自学习开关。
  // 方案列表随 schema_list 动态生成（用户自装方案加入后即可管理）。
  struct SchemaRow {
    QCheckBox* enabled = nullptr;
    QComboBox* simplified = nullptr;
    QCheckBox* userDict = nullptr;
  };
  QList<SchemaRow> schemaRows_;
  QVBoxLayout* schemaLayout_ = nullptr;

  // 通用页
  QSpinBox* pageSize_ = nullptr;
  QComboBox* candidatesLayout_ = nullptr;
  QComboBox* shiftL_ = nullptr;
  QComboBox* shiftR_ = nullptr;
  QCheckBox* preeditInApp_ = nullptr;
  QCheckBox* spellingHints_ = nullptr;

  // 外观页
  QComboBox* colorScheme_ = nullptr;
  QFontComboBox* fontFamily_ = nullptr;
  QSpinBox* fontSize_ = nullptr;
  QPushButton* normalColorBtn_ = nullptr;
  QPushButton* hlTextColorBtn_ = nullptr;
  QPushButton* bgColorBtn_ = nullptr;
  QPushButton* hlBgColorBtn_ = nullptr;
  QFrame* preview_ = nullptr;
  QLabel* previewNormal_ = nullptr;
  QLabel* previewHighlight_ = nullptr;

  QLabel* status_ = nullptr;
};

#endif  // FCITX5_RIME_CONFIG_MAINWINDOW_H_
