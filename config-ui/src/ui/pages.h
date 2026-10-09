#ifndef FCITX5_RIME_CONFIG_UI_PAGES_H_
#define FCITX5_RIME_CONFIG_UI_PAGES_H_

#include <QList>
#include <QString>

#include "theme/scheme.h"
#include "ui/fontpicker.h"
#include "ui/widgets.h"
#include "yamlconfig.h"

class QCheckBox;
class QPushButton;
class QComboBox;
class QLabel;

namespace bangke {

class PreviewStrip;

// 「字体」行：按钮显示当前字体族，点击开 FontPicker（几百个字体族用下拉太难用）。
class FontButton : public RowShell {
 public:
  FontButton(const QString& family, QWidget* parent = nullptr);
  QString family() const;
  void setFamily(const QString& family);
  QPushButton* button() const { return button_; }

 private:
  QPushButton* button_ = nullptr;
};

class TextRow;

// 当前的 fcitx5 版本，以及它认不认 DarkTheme / UseDarkTheme（5.1.5 起有）。
QString fcitxVersionString();
bool fcitxSupportsDarkTheme();

// 外观页：只放 classicui 真能生效的项。classicui 表达不出来的（单行排列、
// 高亮圆角、叠加透明、候选格式模板、毛玻璃、记忆面板宽度）**不放**，
// 免得出现「滑杆拉了半天面板不动」的死控件。
class AppearancePage : public PageScaffold {
  Q_OBJECT
 public:
  AppearancePage(const BangkeConfig& config, const StyleOptions& style,
                 QWidget* parent = nullptr);

  StyleOptions style() const;
  // 把控件刷成给定设置（「恢复默认」用）。
  void setStyle(const StyleOptions& style);

 signals:
  void changed();

 private:
  void updatePreview();

  BangkeConfig config_;
  PreviewStrip* preview_ = nullptr;
  ComboRow* scheme_ = nullptr;
  ComboRow* schemeDark_ = nullptr;
  SliderRow* transparency_ = nullptr;
  FontButton* font_ = nullptr;
  SliderRow* fontSize_ = nullptr;
  ComboRow* orientation_ = nullptr;
  SliderRow* corner_ = nullptr;
  SliderRow* shadow_ = nullptr;
  SliderRow* border_ = nullptr;
  ToggleRow* showPaging_ = nullptr;
};

// 行为页。mac 的菜单栏图标 / 通知 / 和弦时长在 Linux 上要么没有对应物、
// 要么是前端实现（和弦），都不放。
class BehaviorPage : public PageScaffold {
 public:
  BehaviorPage(const RimeData& data, QWidget* parent = nullptr);
  void collect(RimeData* data) const;

 private:
  SliderRow* pageSize_ = nullptr;
  ComboRow* shiftL_ = nullptr;
  ComboRow* shiftR_ = nullptr;
  ToggleRow* spellingHints_ = nullptr;
};

// 智能联想页。
class AiPage : public PageScaffold {
 public:
  AiPage(const RimeData& data, QWidget* parent = nullptr);
  void collect(RimeData* data) const;

 private:
  ToggleRow* enabled_ = nullptr;
  ComboRow* device_ = nullptr;
  SliderRow* targetIndex_ = nullptr;
  SliderRow* maxTokens_ = nullptr;
  SliderRow* debounce_ = nullptr;
  SliderRow* minInput_ = nullptr;
  SliderRow* minHanzi_ = nullptr;
  SliderRow* quality_ = nullptr;
  SliderRow* searchRange_ = nullptr;
  SliderRow* contextWindow_ = nullptr;
  SliderRow* minContextPrompt_ = nullptr;
  QString modelPath_;
};

// 方案与词典页（mac 上没有这一页 —— mac 把它放在菜单栏里；Linux 需要有入口）。
class SchemasPage : public PageScaffold {
 public:
  SchemasPage(const RimeData& data, QWidget* parent = nullptr);
  void collect(RimeData* data);

 private:
  struct SchemaRow {
    QCheckBox* enabled = nullptr;
    QComboBox* simplified = nullptr;
    QCheckBox* userDict = nullptr;
  };
  QList<SchemaRow> rows_;
  QList<SchemaSettings> schemas_;
};

class AboutPage : public PageScaffold {
 public:
  explicit AboutPage(QWidget* parent = nullptr);
};

}  // namespace bangke

#endif  // FCITX5_RIME_CONFIG_UI_PAGES_H_
