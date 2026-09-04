#include "mainwindow.h"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include "rimedeploy.h"
#include "yamlconfig.h"

namespace {

QSpinBox* makeSpin(int min, int max, int value) {
  auto* s = new QSpinBox;
  s->setRange(min, max);
  s->setValue(value);
  return s;
}

QString schemaDisplayName(const QString& id) {
  if (id == QStringLiteral("luna_pinyin")) return QStringLiteral("朙月拼音");
  if (id == QStringLiteral("rime_ice")) return QStringLiteral("雾凇拼音");
  return id;
}

QComboBox* makeShiftCombo() {
  auto* c = new QComboBox;
  c->addItem(QStringLiteral("上屏编码（默认）"), QStringLiteral("commit_code"));
  c->addItem(QStringLiteral("上屏首选文字"), QStringLiteral("commit_text"));
  c->addItem(QStringLiteral("内联英文模式"), QStringLiteral("inline_ascii"));
  c->addItem(QStringLiteral("清空编码"), QStringLiteral("clear"));
  c->addItem(QStringLiteral("关闭（不用此键切换）"), QStringLiteral("noop"));
  return c;
}

struct ColorPreset {
  const char* name;
  const char* normal;
  const char* hlText;
  const char* bg;
  const char* hlBg;
};
// 内置配色（顺序即下拉顺序）。
const ColorPreset kPresets[] = {
    {"经典白", "#000000", "#ffffff", "#ffffff", "#808080"},
    {"暗夜", "#e8e8e8", "#ffffff", "#2d2d30", "#4a78d4"},
    {"纸墨", "#3a3226", "#ffffff", "#f7f2e7", "#b8860b"},
    {"蓝天", "#22262b", "#ffffff", "#eaf1fa", "#3d7eff"},
    {"护眼绿", "#263238", "#ffffff", "#e6efe2", "#57a773"},
};
constexpr int kPresetCount = sizeof(kPresets) / sizeof(kPresets[0]);

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  buildUi();
  data_ = YamlConfig::load();
  loadToUi();
}

void MainWindow::buildUi() {
  setWindowTitle(YamlConfig::brandName() + QStringLiteral(" 设置"));
  resize(420, 480);

  auto* tabs = new QTabWidget;

  // ── AI 预测 ────────────────────────────────────────────────
  auto* aiPage = new QWidget;
  auto* aiLayout = new QVBoxLayout(aiPage);

  aiEnabled_ = new QCheckBox(QStringLiteral("启用 AI 预测"));
  aiEnabled_->setChecked(true);
  aiLayout->addWidget(aiEnabled_);
  aiLayout->addSpacing(8);

  aiDevice_ = new QComboBox;
  aiDevice_->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
  aiDevice_->addItem(QStringLiteral("CUDA (GPU)"), QStringLiteral("cuda"));
  aiMaxTokens_ = makeSpin(1, 1024, 64);
  aiDebounce_ = makeSpin(0, 5000, 200);
  aiMinInput_ = makeSpin(1, 100, 12);
  aiMinHanzi_ = makeSpin(1, 10, 2);

  auto* aiForm = new QFormLayout;
  aiForm->addRow(QStringLiteral("设备"), aiDevice_);
  aiForm->addRow(QStringLiteral("最大 token 数"), aiMaxTokens_);
  aiForm->addRow(QStringLiteral("防抖 (ms)"), aiDebounce_);
  aiForm->addRow(QStringLiteral("最小输入长度"), aiMinInput_);
  aiForm->addRow(QStringLiteral("最少汉字数"), aiMinHanzi_);
  aiLayout->addLayout(aiForm);

  aiQuality_ = new QDoubleSpinBox;
  aiQuality_->setRange(-1.0, 100.0);
  aiQuality_->setSingleStep(0.1);
  aiQuality_->setDecimals(2);
  aiTargetIndex_ = makeSpin(0, 20, 1);
  aiSearchRange_ = makeSpin(1, 100, 10);
  aiContextWindow_ = makeSpin(1, 100, 10);
  aiMinContextPrompt_ = makeSpin(1, 20, 2);
  aiModelPath_ = new QLineEdit;
  aiModelPath_->setReadOnly(true);

  auto* adv = new QGroupBox(QStringLiteral("高级选项"));
  auto* advForm = new QFormLayout(adv);
  // 顶部留白给标题（配合 QSS 的 subcontrol-origin: margin），避免标题与首行贴住。
  advForm->setContentsMargins(10, 8, 10, 10);
  advForm->addRow(QStringLiteral("候选质量 (quality)"), aiQuality_);
  advForm->addRow(QStringLiteral("AI 候选显示位置 (target_index)"), aiTargetIndex_);
  advForm->addRow(QStringLiteral("去重扫描范围 (search_range)"), aiSearchRange_);
  advForm->addRow(QStringLiteral("上下文窗口大小 (context_window_size)"), aiContextWindow_);
  advForm->addRow(QStringLiteral("上下文最小拼音数 (min_context_prompt_length)"), aiMinContextPrompt_);
  advForm->addRow(QStringLiteral("模型路径 (model_path)"), aiModelPath_);
  aiLayout->addSpacing(16);
  aiLayout->addWidget(adv);
  aiLayout->addStretch(1);

  // ── 通用 ──────────────────────────────────────────────────
  auto* genPage = new QWidget;
  auto* genLayout = new QVBoxLayout(genPage);
  auto* genForm = new QFormLayout;
  pageSize_ = makeSpin(1, 20, 5);
  genForm->addRow(QStringLiteral("每页候选词数量"), pageSize_);
  candidatesLayout_ = new QComboBox;
  candidatesLayout_->addItem(QStringLiteral("横向"), QStringLiteral("h"));
  candidatesLayout_->addItem(QStringLiteral("纵向"), QStringLiteral("v"));
  genForm->addRow(QStringLiteral("候选词排列"), candidatesLayout_);
  shiftL_ = makeShiftCombo();
  genForm->addRow(QStringLiteral("左 Shift 切英文时"), shiftL_);
  shiftR_ = makeShiftCombo();
  genForm->addRow(QStringLiteral("右 Shift 切英文时"), shiftR_);
  preeditInApp_ = new QCheckBox(QStringLiteral("在应用内显示拼音预编辑"));
  preeditInApp_->setToolTip(QStringLiteral(
      "开启：拼音显示在应用编辑区；关闭：拼音显示在候选框顶部"));
  genForm->addRow(QString(), preeditInApp_);
  spellingHints_ = new QCheckBox(QStringLiteral("候选词后显示 [拼音] 提示"));
  spellingHints_->setToolTip(QStringLiteral(
      "候选词文字后面的拼音注释（translator/spelling_hints）"));
  genForm->addRow(QString(), spellingHints_);
  genLayout->addLayout(genForm);
  genLayout->addStretch(1);
  tabs->addTab(genPage, QStringLiteral("通用"));

  // ── 外观 ──────────────────────────────────────────────────
  auto* appPage = new QWidget;
  auto* appLayout = new QVBoxLayout(appPage);
  auto* appForm = new QFormLayout;

  colorScheme_ = new QComboBox;
  for (int i = 0; i < kPresetCount; ++i)
    colorScheme_->addItem(QString::fromUtf8(kPresets[i].name), i);
  colorScheme_->addItem(QStringLiteral("自定义"), -1);
  connect(colorScheme_, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int idx) {
            const QVariant d = colorScheme_->itemData(idx);
            bool ok = false;
            const int pi = d.toInt(&ok);
            if (!ok || pi < 0 || pi >= kPresetCount) return;  // 自定义：不动
            updateColorButton(normalColorBtn_, kPresets[pi].normal);
            updateColorButton(hlTextColorBtn_, kPresets[pi].hlText);
            updateColorButton(bgColorBtn_, kPresets[pi].bg);
            updateColorButton(hlBgColorBtn_, kPresets[pi].hlBg);
            updatePreview();
          });
  appForm->addRow(QStringLiteral("配色方案"), colorScheme_);

  fontFamily_ = new QFontComboBox;
  fontSize_ = makeSpin(8, 48, 10);
  auto* fontRow = new QHBoxLayout;
  fontRow->addWidget(fontFamily_, 1);
  fontRow->addWidget(fontSize_);
  appForm->addRow(QStringLiteral("候选词字体"), fontRow);

  normalColorBtn_ = makeColorButton(data_.appearance.normalColor);
  appForm->addRow(QStringLiteral("候选词文字颜色"), normalColorBtn_);
  hlTextColorBtn_ = makeColorButton(data_.appearance.highlightTextColor);
  appForm->addRow(QStringLiteral("高亮文字颜色"), hlTextColorBtn_);
  bgColorBtn_ = makeColorButton(data_.appearance.backgroundColor);
  appForm->addRow(QStringLiteral("背景颜色"), bgColorBtn_);
  hlBgColorBtn_ = makeColorButton(data_.appearance.highlightBackgroundColor);
  appForm->addRow(QStringLiteral("高亮条背景颜色"), hlBgColorBtn_);
  appLayout->addLayout(appForm);

  // 颜色实时预览
  preview_ = new QFrame;
  preview_->setFixedHeight(58);
  auto* previewRow = new QHBoxLayout(preview_);
  previewRow->setContentsMargins(10, 8, 10, 8);
  previewNormal_ = new QLabel(QStringLiteral("1. 例子  2. 示例"));
  previewHighlight_ = new QLabel(QStringLiteral(" 3. 高亮 "));
  previewNormal_->setContentsMargins(8, 4, 8, 4);
  previewHighlight_->setContentsMargins(8, 4, 8, 4);
  previewRow->addWidget(previewNormal_);
  previewRow->addWidget(previewHighlight_);
  previewRow->addStretch(1);
  appLayout->addSpacing(12);
  appLayout->addWidget(preview_);
  appLayout->addStretch(1);
  connect(fontFamily_, &QFontComboBox::currentFontChanged, this,
          [this](const QFont&) { updatePreview(); });
  connect(fontSize_, qOverload<int>(&QSpinBox::valueChanged), this,
          [this](int) { updatePreview(); });
  tabs->addTab(appPage, QStringLiteral("外观"));

  // ── 方案 ──────────────────────────────────────────────────
  auto* schemaPage = new QWidget;
  schemaLayout_ = new QVBoxLayout(schemaPage);
  tabs->addTab(schemaPage, QStringLiteral("输入方案"));

  // AI 页放最右。
  tabs->addTab(aiPage, QStringLiteral("AI 预测"));

  // ── 底部 ──────────────────────────────────────────────────
  auto* footer = new QWidget;
  auto* footerLayout = new QHBoxLayout(footer);
  status_ = new QLabel;
  footerLayout->addWidget(status_, 1);
  auto* saveBtn = new QPushButton(QStringLiteral("保存并部署"));
  footerLayout->addWidget(saveBtn);
  connect(saveBtn, &QPushButton::clicked, this, &MainWindow::saveAndDeploy);

  auto* container = new QWidget;
  auto* containerLayout = new QVBoxLayout(container);
  containerLayout->addWidget(tabs);
  containerLayout->addWidget(footer);
  setCentralWidget(container);
}

QPushButton* MainWindow::makeColorButton(const QString& initial) {
  auto* btn = new QPushButton;
  btn->setFixedHeight(26);
  updateColorButton(btn, initial);
  connect(btn, &QPushButton::clicked, this, [this, btn]() {
    const QColor cur(btn->property("color").toString());
    const QColor c =
        QColorDialog::getColor(cur, this, QStringLiteral("选择颜色"));
    if (!c.isValid()) return;
    updateColorButton(btn, c.name());
    updatePreview();
    // 手动改色后不再是预设。
    if (colorScheme_) {
      QSignalBlocker blocker(colorScheme_);
      colorScheme_->setCurrentIndex(colorScheme_->findData(-1));
    }
  });
  return btn;
}

void MainWindow::updateColorButton(QPushButton* btn, const QString& color) {
  btn->setProperty("color", color);
  const QColor c(color);
  const QString textColor = c.lightness() > 128 ? "#000000" : "#ffffff";
  btn->setText(color);
  btn->setStyleSheet(QStringLiteral("background-color: %1; color: %2; "
                                    "border: 1px solid #888888;")
                         .arg(color, textColor));
}

void MainWindow::updatePreview() {
  if (!preview_) return;
  const QString bg = bgColorBtn_->property("color").toString();
  const QString hl = hlBgColorBtn_->property("color").toString();
  const QString normal = normalColorBtn_->property("color").toString();
  const QString hlText = hlTextColorBtn_->property("color").toString();
  preview_->setStyleSheet(QStringLiteral("QFrame { background-color: %1; "
                                         "border: 1px solid #c0c0c0; }")
                              .arg(bg));
  previewNormal_->setStyleSheet(QStringLiteral("color: %1;").arg(normal));
  previewHighlight_->setStyleSheet(
      QStringLiteral("background-color: %1; color: %2; border-radius: 3px;")
          .arg(hl, hlText));
  const QFont f(fontFamily_->currentFont().family(), fontSize_->value());
  previewNormal_->setFont(f);
  previewHighlight_->setFont(f);
}

void MainWindow::loadToUi() {
  const AISettings& ai = data_.ai;
  aiEnabled_->setChecked(ai.enabled);
  int di = aiDevice_->findData(ai.device);
  aiDevice_->setCurrentIndex(di >= 0 ? di : 0);
  aiMaxTokens_->setValue(ai.maxTokens);
  aiDebounce_->setValue(ai.debounceMs);
  aiMinInput_->setValue(ai.minInputLength);
  aiMinHanzi_->setValue(ai.minHanzi);
  aiQuality_->setValue(ai.quality);
  aiTargetIndex_->setValue(ai.targetIndex);
  aiSearchRange_->setValue(ai.searchRange);
  aiContextWindow_->setValue(ai.contextWindowSize);
  aiMinContextPrompt_->setValue(ai.minContextPromptLength);
  aiModelPath_->setText(ai.modelPath);

  // 方案页动态重建：行数随 schema_list 变化。
  while (auto* item = schemaLayout_->takeAt(0)) {
    if (auto* w = item->widget()) delete w;
    delete item;
  }
  schemaRows_.clear();
  schemaLayout_->addWidget(new QLabel(QStringLiteral(
      "AI 与通用设置对所有方案生效；默认字形与自学习按方案独立。\n"
      "打字中 Ctrl+~ 的临时简繁切换优先，切换方案或部署后回到各方案默认值。")));
  const QStringList managed = {QStringLiteral("luna_pinyin"),
                               QStringLiteral("rime_ice")};
  for (const auto& s : data_.schemas) {
    auto* row = new QHBoxLayout;
    auto* cb = new QCheckBox(schemaDisplayName(s.name));
    cb->setToolTip(s.name);
    cb->setChecked(s.enabled);
    auto* combo = new QComboBox;
    combo->addItem(QStringLiteral("简体"));
    combo->addItem(QStringLiteral("繁体"));
    combo->setCurrentIndex(s.simplified ? 0 : 1);
    QCheckBox* userDict = nullptr;
    if (managed.contains(s.name)) {
      userDict = new QCheckBox(QStringLiteral("自学习"));
      userDict->setToolTip(QStringLiteral("用户词典与自动调频（translator/enable_user_dict）"));
      userDict->setChecked(s.userDict);
    }
    row->addWidget(cb);
    row->addStretch(1);
    if (userDict) row->addWidget(userDict);
    row->addWidget(combo);
    schemaLayout_->addLayout(row);
    schemaRows_.append({cb, combo, userDict});
  }
  schemaLayout_->addStretch(1);
  pageSize_->setValue(data_.pageSize);
  candidatesLayout_->setCurrentIndex(data_.verticalCandidates ? 1 : 0);
  int si = shiftL_->findData(data_.shiftLAction);
  shiftL_->setCurrentIndex(si >= 0 ? si : 0);
  si = shiftR_->findData(data_.shiftRAction);
  shiftR_->setCurrentIndex(si >= 0 ? si : 0);
  preeditInApp_->setChecked(data_.preeditInApp);
  spellingHints_->setChecked(data_.spellingHints);

  fontFamily_->setCurrentFont(QFont(data_.appearance.fontFamily));
  fontSize_->setValue(data_.appearance.fontSize);
  updateColorButton(normalColorBtn_, data_.appearance.normalColor);
  updateColorButton(hlTextColorBtn_, data_.appearance.highlightTextColor);
  updateColorButton(bgColorBtn_, data_.appearance.backgroundColor);
  updateColorButton(hlBgColorBtn_, data_.appearance.highlightBackgroundColor);
  {
    // 当前颜色匹配某预设则选中它，否则“自定义”。
    int match = -1;
    for (int i = 0; i < kPresetCount; ++i) {
      if (data_.appearance.normalColor == kPresets[i].normal &&
          data_.appearance.highlightTextColor == kPresets[i].hlText &&
          data_.appearance.backgroundColor == kPresets[i].bg &&
          data_.appearance.highlightBackgroundColor == kPresets[i].hlBg) {
        match = i;
        break;
      }
    }
    QSignalBlocker blocker(colorScheme_);
    colorScheme_->setCurrentIndex(colorScheme_->findData(match));
  }
  updatePreview();
}

void MainWindow::readFromUi() {
  AISettings& ai = data_.ai;
  ai.enabled = aiEnabled_->isChecked();
  ai.device = aiDevice_->currentData().toString();
  ai.maxTokens = aiMaxTokens_->value();
  ai.debounceMs = aiDebounce_->value();
  ai.minInputLength = aiMinInput_->value();
  ai.minHanzi = aiMinHanzi_->value();
  ai.quality = aiQuality_->value();
  ai.targetIndex = aiTargetIndex_->value();
  ai.searchRange = aiSearchRange_->value();
  ai.contextWindowSize = aiContextWindow_->value();
  ai.minContextPromptLength = aiMinContextPrompt_->value();

  for (int i = 0; i < data_.schemas.size() && i < schemaRows_.size(); ++i) {
    data_.schemas[i].enabled = schemaRows_[i].enabled->isChecked();
    data_.schemas[i].simplified = schemaRows_[i].simplified->currentIndex() == 0;
    if (schemaRows_[i].userDict)
      data_.schemas[i].userDict = schemaRows_[i].userDict->isChecked();
  }
  data_.pageSize = pageSize_->value();
  data_.verticalCandidates = candidatesLayout_->currentIndex() == 1;
  data_.shiftLAction = shiftL_->currentData().toString();
  data_.shiftRAction = shiftR_->currentData().toString();
  data_.preeditInApp = preeditInApp_->isChecked();
  data_.spellingHints = spellingHints_->isChecked();
  data_.appearance.fontFamily = fontFamily_->currentFont().family();
  data_.appearance.fontSize = fontSize_->value();
  data_.appearance.normalColor = normalColorBtn_->property("color").toString();
  data_.appearance.highlightTextColor =
      hlTextColorBtn_->property("color").toString();
  data_.appearance.backgroundColor = bgColorBtn_->property("color").toString();
  data_.appearance.highlightBackgroundColor =
      hlBgColorBtn_->property("color").toString();
}

void MainWindow::saveAndDeploy() {
  readFromUi();
  if (!YamlConfig::save(data_)) {
    QMessageBox::warning(this, QStringLiteral("保存失败"),
                         QStringLiteral("无法写入 %1").arg(YamlConfig::rimeUserDir()));
    return;
  }
  QString hint = RimeDeploy::triggerDeploy();
  if (hint.isEmpty()) {
    status_->setText(QStringLiteral("已保存并部署。"));
  } else {
    status_->setText(hint);
    QMessageBox::information(this, QStringLiteral("已保存"), hint);
  }
}
