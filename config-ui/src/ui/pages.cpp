#include "ui/pages.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "theme/paths.h"
#include "theme/themewriter.h"
#include "ui/preview.h"
#include "ui/themepalette.h"

namespace bangke {
namespace {


// 说明性文字：换行，否则单行标签会把内容撑宽、逼出横向滚动条。
QLabel* noteLabel(const QString& text, QWidget* parent) {
  auto* label = new QLabel(text, parent);
  label->setWordWrap(true);
  QPalette pal = label->palette();
  pal.setColor(QPalette::WindowText, palette::secondaryText(label));
  label->setPalette(pal);
  return label;
}

QString schemaDisplayName(const QString& id) {
  if (id == QLatin1String("luna_pinyin")) return QStringLiteral("朙月拼音");
  if (id == QLatin1String("rime_ice")) return QStringLiteral("雾凇拼音");
  return id;
}

QStringList shiftLabels() {
  return {QStringLiteral("上屏编码（默认）"), QStringLiteral("上屏首选文字"),
          QStringLiteral("内联英文模式"), QStringLiteral("清空编码"),
          QStringLiteral("关闭（不用此键切换）")};
}

QStringList shiftValues() {
  return {QStringLiteral("commit_code"), QStringLiteral("commit_text"),
          QStringLiteral("inline_ascii"), QStringLiteral("clear"),
          QStringLiteral("noop")};
}

QString addonVersion() {
  for (const QString& path :
       {QStringLiteral("/usr/share/fcitx5/addon/rime.conf"),
        QStringLiteral("/usr/local/share/fcitx5/addon/rime.conf")}) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
    while (!file.atEnd()) {
      const QString line = QString::fromUtf8(file.readLine()).trimmed();
      if (line.startsWith(QLatin1String("Version=")))
        return line.mid(int(strlen("Version=")));
    }
  }
  return QString();
}

}  // namespace

QString fcitxVersionString() {
  static const QString version = []() {
    QProcess process;
    process.start(QStringLiteral("fcitx5"), {QStringLiteral("--version")});
    if (!process.waitForFinished(2000)) return QString();
    const QString output =
        QString::fromLocal8Bit(process.readAllStandardOutput()) +
        QString::fromLocal8Bit(process.readAllStandardError());
    const QRegularExpression re(QStringLiteral("(\\d+\\.\\d+\\.\\d+)"));
    const auto match = re.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
  }();
  return version;
}

bool fcitxSupportsDarkTheme() {
  const QString version = fcitxVersionString();
  if (version.isEmpty()) return true;  // 探不到就按新版处理，避免误藏设置
  const QStringList parts = version.split(QLatin1Char('.'));
  if (parts.size() < 2) return true;
  const int major = parts.at(0).toInt();
  const int minor = parts.at(1).toInt();
  // DarkTheme / UseDarkTheme 从 5.1.5 起有（5.0.x 的 classicui 没有这两个键）。
  return major > 5 || (major == 5 && minor >= 1);
}

FontButton::FontButton(const QString& family, QWidget* parent)
    : RowShell(QStringLiteral("字体"), QString(), nullptr, parent), fallback_(family) {
  button_ = new QPushButton(family, this);
  button_->setMinimumWidth(220);
  if (auto* shell = qobject_cast<QHBoxLayout*>(this->layout()))
    shell->addWidget(button_);
}

QString FontButton::family() const {
  const QString shown = button_->text().trimmed();
  if (!shown.isEmpty()) return shown;
  // 按钮文字可能被清空（比如字体选择器返回空）。空家族名会被原样写进
  // classicui.conf 的 `Font=`，fcitx5 解析出 0 号字 —— 候选面板高度跟着文字走，
  // 于是归零、整个看不见。这是真机上「一保存候选面板就消失」的直接原因，
  // 所以这里必须兜底，不能把空串交出去。
  const QString fallback = fallback_.trimmed();
  return fallback.isEmpty() ? QStringLiteral("Noto Sans CJK SC") : fallback;
}
void FontButton::setFamily(const QString& family) {
  fallback_ = family;
  button_->setText(family);
}

// ── 外观页 ───────────────────────────────────────────────────────

AppearancePage::AppearancePage(const BangkeConfig& config,
                               const StyleOptions& style, QWidget* parent)
    : PageScaffold(QStringLiteral("外观"), QStringLiteral("配色、字体与候选面板"),
                   parent),
      config_(config) {
  preview_ = new PreviewStrip(this);
  addWidget(preview_);
  addSpacing(14);

  QStringList schemeNames;
  QStringList schemeIds;
  for (const QString& id : config_.schemeIds()) {
    schemeIds << id;
    schemeNames << config_.displayName(id);
  }

  addSection(QStringLiteral("颜色"));
  scheme_ = new ComboRow(QStringLiteral("配色方案"), QString(), schemeNames,
                         schemeIds, style.colorScheme, this);
  addRow(scheme_);
  if (fcitxSupportsDarkTheme()) {
    QStringList darkNames{QStringLiteral("跟随浅色方案")};
    QStringList darkIds{QString()};
    darkNames += schemeNames;
    darkIds += schemeIds;
    schemeDark_ = new ComboRow(
        QStringLiteral("深色配色方案"),
        QStringLiteral("系统切到深色时使用。"), darkNames,
        darkIds, style.colorSchemeDark, this);
    addRow(schemeDark_);
  }
  transparency_ = new SliderRow(
      QStringLiteral("背景透明度"),
      QStringLiteral("0 为不透明。"),
      0.0, 0.95, 0.05, 1.0 - style.alpha, QString(), this);
  addRow(transparency_);

  addSection(QStringLiteral("字体"));
  font_ = new FontButton(style.fontFace, this);
  addRow(font_);
  fontSize_ = new SliderRow(QStringLiteral("字号"), QString(), 8, 48, 1,
                            style.fontPoint, QStringLiteral(" pt"), this);
  addRow(fontSize_);

  addSection(QStringLiteral("布局"));
  orientation_ = new ComboRow(
      QStringLiteral("候选排列"),
      QStringLiteral("竖排一列显示；关闭则横排一行。"),
      {QStringLiteral("横排"), QStringLiteral("竖排")},
      {QStringLiteral("horizontal"), QStringLiteral("vertical")},
      style.textOrientation, this);
  addRow(orientation_);
  corner_ = new SliderRow(
      QStringLiteral("圆角半径"),
      QStringLiteral("候选面板的圆角半径。"),
      0, bangke::maxSafeCornerRadiusPt(), 1, style.cornerRadius, QStringLiteral(" pt"),
      this);
  addRow(corner_);
  shadow_ = new SliderRow(
      QStringLiteral("阴影大小"),
      QStringLiteral("高亮候选投在面板上的柔影。"),
      0, 30, 1, style.shadowSize, QStringLiteral(" pt"), this);
  addRow(shadow_);
  border_ = new SliderRow(
      QStringLiteral("边框宽度"),
      QStringLiteral("0 为不画边框，颜色取自配色方案。"),
      0, 20, 1, qMax(0, style.borderWidth), QStringLiteral(" pt"), this);
  addRow(border_);

  addSection(QStringLiteral("显示"));
  showPaging_ = new ToggleRow(
      QStringLiteral("显示翻页按钮"),
      QStringLiteral("候选超过一页时显示左右翻页箭头。"), style.showPaging,
      this);
  addRow(showPaging_);


  const auto notify = [this]() {
    updatePreview();
    emit changed();
  };
  for (QWidget* control :
       {static_cast<QWidget*>(scheme_), static_cast<QWidget*>(schemeDark_),
        static_cast<QWidget*>(orientation_)}) {
    if (auto* row = qobject_cast<ComboRow*>(control))
      connect(row->combo(), qOverload<int>(&QComboBox::currentIndexChanged), this,
              [notify](int) { notify(); });
  }
  for (SliderRow* row : {transparency_, fontSize_, corner_, shadow_, border_})
    connect(row, &SliderRow::valueChanged, this, [notify](double) { notify(); });
  connect(font_->button(), &QPushButton::clicked, this, [this, notify]() {
    const QString chosen = FontPicker::pick(font_->family(), this);
    if (chosen.isEmpty() || chosen == font_->family()) return;
    font_->setFamily(chosen);
    notify();
  });
  for (ToggleRow* row : {showPaging_}) {
    connect(row->toggle(), &QAbstractButton::toggled, this,
            [notify](bool) { notify(); });
  }

  updatePreview();
}

StyleOptions AppearancePage::style() const {
  StyleOptions out = config_.style();
  out.colorScheme = scheme_->currentValue();
  if (schemeDark_) out.colorSchemeDark = schemeDark_->currentValue();
  out.alpha = qBound(0.0, 1.0 - transparency_->value(), 1.0);
  out.fontFace = font_->family();
  out.fontPoint = int(fontSize_->value());
  out.textOrientation = orientation_->currentValue();
  out.cornerRadius = int(corner_->value());
  out.shadowSize = int(shadow_->value());
  // classicui 只有单一 BorderWidth；把 mac 的两个方向一起设成这个值。
  out.borderWidth = int(border_->value());
  out.borderHeight = int(border_->value());
  out.showPaging = showPaging_->isChecked();
  return out;
}

void AppearancePage::setStyle(const StyleOptions& style) {
  scheme_->setCurrentValue(style.colorScheme);
  if (schemeDark_) schemeDark_->setCurrentValue(style.colorSchemeDark);
  transparency_->setValue(1.0 - style.alpha);
  font_->setFamily(style.fontFace);
  fontSize_->setValue(style.fontPoint);
  orientation_->setCurrentValue(style.textOrientation);
  corner_->setValue(style.cornerRadius);
  shadow_->setValue(style.shadowSize);
  border_->setValue(qMax(0, style.borderWidth));
  showPaging_->setChecked(style.showPaging);
  updatePreview();
}

void AppearancePage::updatePreview() {
  const StyleOptions current = style();
  preview_->setSpecs(panelSpecFor(config_, current, false),
                     panelSpecFor(config_, current, true));
}

// ── 行为页 ───────────────────────────────────────────────────────

BehaviorPage::BehaviorPage(const RimeData& data, QWidget* parent)
    : PageScaffold(QStringLiteral("行为"), QStringLiteral("候选数量与切换键"), parent) {
  addSection(QStringLiteral("输入"));
  pageSize_ = new SliderRow(QStringLiteral("每页候选数"),
                            QStringLiteral("一页显示几个候选。"), 1, 20,
                            1, data.pageSize, QString(), this);
  addRow(pageSize_);
  shiftL_ = new ComboRow(QStringLiteral("左 Shift 切英文时"), QString(),
                         shiftLabels(), shiftValues(), data.shiftLAction, this);
  addRow(shiftL_);
  shiftR_ = new ComboRow(QStringLiteral("右 Shift 切英文时"), QString(),
                         shiftLabels(), shiftValues(), data.shiftRAction, this);
  addRow(shiftR_);

  addSection(QStringLiteral("候选"));
  spellingHints_ = new ToggleRow(
      QStringLiteral("候选词后显示 [拼音] 提示"),
      QString(), data.spellingHints,
      this);
  addRow(spellingHints_);

}

void BehaviorPage::collect(RimeData* data) const {
  data->pageSize = int(pageSize_->value());
  data->shiftLAction = shiftL_->currentValue();
  data->shiftRAction = shiftR_->currentValue();
  data->spellingHints = spellingHints_->isChecked();
}

// ── 智能联想页 ───────────────────────────────────────────────────

namespace {
// 随包分发的默认模型（与出厂种子 ai-predict-defaults 里的 model_path 一致）。
constexpr const char* kDefaultModelPath =
    "/usr/share/fcitx5-rime/predict_models/zh-base-ct2-int8";
} // namespace

AiPage::AiPage(const RimeData& data, QWidget* parent)
    : PageScaffold(QStringLiteral("智能联想"),
                   QStringLiteral("AI 预测候选（对两个内置方案同时生效）"), parent) {
  const AISettings& ai = data.ai;
  modelPath_ = ai.modelPath;

  addSection(QStringLiteral("总开关"));
  enabled_ = new ToggleRow(QStringLiteral("启用 AI 预测"), QString(), ai.enabled,
                           this);
  addRow(enabled_);
  device_ = new ComboRow(QStringLiteral("推理设备"),
                         QStringLiteral("CUDA 需要可用的 GPU。"),
                         {QStringLiteral("CPU"), QStringLiteral("CUDA (GPU)")},
                         {QStringLiteral("cpu"), QStringLiteral("cuda")}, ai.device,
                         this);
  addRow(device_);

  addSection(QStringLiteral("触发"));
  // 插件要求防抖 > 0，所以下限给 20ms，不暴露 0 那个死值。
  debounce_ = new SliderRow(QStringLiteral("防抖"), QString(), 20, 2000, 20,
                            qMax(20, ai.debounceMs), QStringLiteral(" ms"), this);
  addRow(debounce_);
  minInput_ = new SliderRow(QStringLiteral("最小输入长度"), QString(), 1, 40, 1,
                            ai.minInputLength, QString(), this);
  addRow(minInput_);
  minHanzi_ = new SliderRow(QStringLiteral("最少汉字数"), QString(), 1, 10, 1,
                            ai.minHanzi, QString(), this);
  addRow(minHanzi_);
  contextWindow_ = new SliderRow(QStringLiteral("上下文窗口"), QString(), 1, 50, 1,
                                 ai.contextWindowSize, QString(), this);
  addRow(contextWindow_);
  minContextPrompt_ =
      new SliderRow(QStringLiteral("上下文最小拼音数"), QString(), 1, 20, 1,
                    ai.minContextPromptLength, QString(), this);
  addRow(minContextPrompt_);

  addSection(QStringLiteral("展示与去重"));
  filterPunctuation_ = new ToggleRow(
      QStringLiteral("过滤标点"),
      QStringLiteral("剥掉模型输出里的标点。关掉可保留模型自己吐出的句读"
                     "（如「，我们」），代价是上下文复读的标点残留也会进候选。"),
      ai.filterPunctuation, this);
  addRow(filterPunctuation_);
  targetIndex_ = new SliderRow(
      QStringLiteral("候选插入位置"),
      QStringLiteral("AI 候选插在常规候选的第几位（0 = 最前）。"), 0, 20, 1,
      ai.targetIndex, QString(), this);
  addRow(targetIndex_);
  searchRange_ = new SliderRow(QStringLiteral("去重扫描范围"), QString(), 1, 50, 1,
                               ai.searchRange, QString(), this);
  addRow(searchRange_);
  maxTokens_ = new SliderRow(QStringLiteral("最大 token 数"), QString(), 8, 512, 8,
                             ai.maxTokens, QString(), this);
  addRow(maxTokens_);

  addSection(QStringLiteral("模型"));
  auto* modelRow = new QWidget(this);
  auto* modelLay = new QHBoxLayout(modelRow);
  modelLay->setContentsMargins(0, 0, 0, 0);
  modelPathLabel_ = new QLabel(modelRow);
  modelPathLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  modelLay->addWidget(modelPathLabel_, 1);
  auto* pickBtn = new QPushButton(QStringLiteral("选择目录…"), modelRow);
  auto* resetBtn = new QPushButton(QStringLiteral("恢复默认"), modelRow);
  modelLay->addWidget(pickBtn);
  modelLay->addWidget(resetBtn);
  addWidget(modelRow);
  updateModelPathLabel();
  connect(pickBtn, &QPushButton::clicked, this, [this] { pickModelDir(); });
  connect(resetBtn, &QPushButton::clicked, this, [this] {
    modelPath_ = QString::fromLatin1(kDefaultModelPath);
    updateModelPathLabel();
  });

}

void AiPage::updateModelPathLabel() {
  if (!modelPathLabel_) {
    return;
  }
  modelPathLabel_->setText(modelPath_.isEmpty()
                               ? QStringLiteral("（未配置模型路径）")
                               : modelPath_);
}

void AiPage::pickModelDir() {
  const QString start = modelPath_.isEmpty() ? QDir::homePath() : modelPath_;
  const QString dir = QFileDialog::getExistingDirectory(
      this, QStringLiteral("选择模型目录"), start);
  if (dir.isEmpty()) {
    return; // 用户取消
  }
  // 预检：插件要求目录成套，缺件会在启动时直接关掉 AI 预测。先拦住，别让用户
  // 存进一个必然失败的路径（错误信息要好懂，不是让他去看日志）。
  const QStringList need{QStringLiteral("model.bin"),
                         QStringLiteral("tokenizer.json"),
                         QStringLiteral("shared_vocabulary.json")};
  QStringList missing;
  for (const QString& f : need) {
    if (!QFile::exists(dir + QLatin1Char('/') + f)) {
      missing << f;
    }
  }
  if (!missing.isEmpty()) {
    QMessageBox::warning(
        this, QStringLiteral("模型目录不完整"),
        QStringLiteral("这个目录里缺少：%1\n\n模型目录必须成套 —— model.bin、"
                       "tokenizer.json、shared_vocabulary.json 三者要来自同一个模型。")
            .arg(missing.join(QStringLiteral("、"))));
    return;
  }
  modelPath_ = dir;
  updateModelPathLabel();
}

void AiPage::collect(RimeData* data) const {
  AISettings& ai = data->ai;
  ai.modelPath = modelPath_;
  ai.enabled = enabled_->isChecked();
  ai.filterPunctuation = filterPunctuation_->isChecked();
  ai.device = device_->currentValue();
  ai.debounceMs = int(debounce_->value());
  ai.minInputLength = int(minInput_->value());
  ai.minHanzi = int(minHanzi_->value());
  ai.contextWindowSize = int(contextWindow_->value());
  ai.minContextPromptLength = int(minContextPrompt_->value());
  ai.targetIndex = int(targetIndex_->value());
  ai.searchRange = int(searchRange_->value());
  ai.maxTokens = int(maxTokens_->value());
}

// ── 方案与词典页 ─────────────────────────────────────────────────

SchemasPage::SchemasPage(const RimeData& data, QWidget* parent)
    : PageScaffold(QStringLiteral("方案与词典"),
                   QStringLiteral("启用的输入方案、默认字形与自学习"), parent),
      schemas_(data.schemas) {
  const QStringList managed = {QStringLiteral("luna_pinyin"),
                               QStringLiteral("rime_ice")};
  addSection(QStringLiteral("输入方案"));
  for (const SchemaSettings& schema : schemas_) {
    auto* row = new QWidget(this);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 8, 0, 8);
    auto* enabled = new QCheckBox(schemaDisplayName(schema.name), row);
    enabled->setToolTip(schema.name);
    enabled->setChecked(schema.enabled);
    layout->addWidget(enabled);
    layout->addStretch(1);

    QCheckBox* userDict = nullptr;
    if (managed.contains(schema.name)) {
      userDict = new QCheckBox(QStringLiteral("自学习"), row);
      userDict->setToolTip(
          QStringLiteral("用户词典与自动调频（translator/enable_user_dict）"));
      userDict->setChecked(schema.userDict);
      layout->addWidget(userDict);
    }
    auto* simplified = new QComboBox(row);
    simplified->addItem(QStringLiteral("简体"));
    simplified->addItem(QStringLiteral("繁体"));
    simplified->setCurrentIndex(schema.simplified ? 0 : 1);
    layout->addWidget(simplified);
    addRow(row);
    rows_.append({enabled, simplified, userDict});
  }

  addSpacing(12);
  addWidget(noteLabel(QStringLiteral(
      "AI 与通用设置对所有方案生效；默认字形与自学习按方案独立。"
      "打字中 Ctrl+~ 的临时简繁切换优先，切换方案或部署后回到各方案默认值。"),
      this));
}

void SchemasPage::collect(RimeData* data) {
  for (int i = 0; i < schemas_.size() && i < rows_.size(); ++i) {
    schemas_[i].enabled = rows_[i].enabled->isChecked();
    schemas_[i].simplified = rows_[i].simplified->currentIndex() == 0;
    if (rows_[i].userDict) schemas_[i].userDict = rows_[i].userDict->isChecked();
  }
  data->schemas = schemas_;
}

// ── 关于页 ───────────────────────────────────────────────────────

AboutPage::AboutPage(QWidget* parent)
    : PageScaffold(QStringLiteral("关于"), QString(), parent) {
  addSpacing(24);
  auto* icon = new QLabel(this);
  const QIcon themeIcon = QIcon::fromTheme(QStringLiteral("fcitx-rime"));
  if (!themeIcon.isNull())
    icon->setPixmap(themeIcon.pixmap(96, 96));
  icon->setAlignment(Qt::AlignCenter);
  addWidget(icon);

  auto* name = new QLabel(QStringLiteral("蚌壳拼音"), this);
  QFont nameFont = name->font();
  nameFont.setPointSizeF(nameFont.pointSizeF() + 4);
  nameFont.setBold(true);
  name->setFont(nameFont);
  name->setAlignment(Qt::AlignCenter);
  addSpacing(10);
  addWidget(name);

  QStringList lines;
  const QString version = addonVersion();
  if (!version.isEmpty())
    lines << QStringLiteral("版本 %1").arg(version);
  const QString fcitx = fcitxVersionString();
  if (!fcitx.isEmpty())
    lines << QStringLiteral("fcitx5 %1").arg(fcitx);
  auto* detail = new QLabel(lines.join(QStringLiteral(" · ")), this);
  detail->setAlignment(Qt::AlignCenter);
  QPalette pal = detail->palette();
  pal.setColor(QPalette::WindowText, palette::secondaryText(detail));
  detail->setPalette(pal);
  addWidget(detail);

}

}  // namespace bangke
