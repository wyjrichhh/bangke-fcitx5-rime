#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QTabBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QAbstractItemView>
#include <QStyle>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "mainwindow.h"
#include "theme/paths.h"
#include "theme/themewriter.h"
#include "ui/pages.h"
#include "ui/preview.h"
#include "ui/themepalette.h"

namespace {
void require(bool condition, const QString& message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", qPrintable(message));
    std::exit(1);
  }
}
QWidget* setting(QWidget& page, const QString& name) {
  for (auto* child : page.findChildren<QWidget*>())
    if (child->property("settingLabel").toString() == name) return child;
  require(false, "Missing setting: " + name);
  return nullptr;
}
QImage capture(bangke::PreviewStrip* preview) {
  QApplication::processEvents();
  auto* scroll = preview->findChild<QScrollArea*>("candidatePreviewScroll");
  require(scroll, "Missing candidate canvas");
  return scroll->widget()->grab().toImage();
}
}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir temp;
  require(temp.isValid(), "Temporary directory unavailable");
  qputenv("XDG_CONFIG_HOME", (temp.path() + "/config").toUtf8());
  qputenv("XDG_DATA_HOME", (temp.path() + "/data").toUtf8());
  qputenv("BANGKE_PANEL_BACKEND", "kimpanel");
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  QApplication app(argc, argv);
  const auto dir = YamlConfig::rimeUserDir();
  require(QDir().mkpath(dir), "Cannot create isolated Rime directory");
  require(QFile::copy(QStringLiteral(BANGKE_SOURCE_YAML), dir + "/bangke.yaml"),
          "Cannot copy fixture configuration");
  for (const auto* fixture : {"omarchy-tokyo-night", "omarchy-flexoki-light"}) {
    const auto platform = bangke::loadPlatformTheme(
        QStringLiteral(BANGKE_TEST_FIXTURES) + '/' + fixture);
    bangke::setAppPlatformTheme(platform);
    app.setStyleSheet(bangke::palette::styleSheet(nullptr));
    const auto config = bangke::BangkeConfig::load(dir + "/bangke.yaml", {}, platform);
    auto baseline = config.style();
    baseline.colorScheme = baseline.colorSchemeDark = "native";
    baseline.candidateListLayout = "stacked";
    baseline.textOrientation = "horizontal";
    baseline.inlinePreedit = false;
    baseline.showPaging = false;
    baseline.lineSpacing = 5;
    baseline.cornerRadius = 12;
    baseline.borderWidth = baseline.borderHeight = 2;
    baseline.shadowSize = 0;
    bangke::AppearancePage page(config, baseline);
    page.resize(1160, 700);
    page.show();
    app.processEvents();
    auto* preview = page.findChild<bangke::PreviewStrip*>();
    auto* settingsScroll = page.findChild<QScrollArea*>();
    require(preview && settingsScroll && !settingsScroll->isAncestorOf(preview),
            "Preview must remain outside settings scroll area");
    require(preview->mapTo(&page, {}).x() > settingsScroll->mapTo(&page, {}).x(),
            "Settings must be left of preview");
    require(preview->height() <= 400, "Preview must remain a compact scene");
    const auto previewSize = preview->size();
    auto* advanced = page.findChild<QToolButton*>("advancedAppearanceToggle");
    require(advanced && !advanced->isChecked(), "Advanced controls should start collapsed");
    advanced->click();
    app.processEvents();
    require(settingsScroll->verticalScrollBar()->maximum() > 0,
            "Expanded settings must scroll inside the left column");
    require(preview->size() == previewSize, "Expanding settings stretched the preview");
    auto* sampleTabs = preview->findChild<QTabBar*>("previewSampleTabs");
    require(sampleTabs && sampleTabs->isVisible(), "Sample selector must be available");
    const auto ordinarySample = capture(preview);
    sampleTabs->setCurrentIndex(1);
    require(ordinarySample != capture(preview), "Long sample did not update preview");
    require(preview->size() == previewSize, "Long sample stretched the preview");
    sampleTabs->setCurrentIndex(0);
    const auto position = preview->mapTo(&page, {});
    settingsScroll->verticalScrollBar()->setValue(settingsScroll->verticalScrollBar()->maximum());
    app.processEvents();
    require(preview->mapTo(&page, {}) == position, "Settings scrolling moved preview");

    const auto check = [&](const QString& name, const std::function<void()>& action,
                           const std::function<void(bangke::StyleOptions&)>& prepare = {}) {
      auto initial = baseline;
      if (prepare) prepare(initial);
      page.setStyle(initial);
      const auto before = capture(preview);
      action();
      const auto after = capture(preview);
      require(before != after, "No rendered preview change: " + name);
      std::printf("PASS: %s / %s\n", fixture, qPrintable(name));
    };
    const auto slider = [&](const QString& name, bool decrease = false) {
      check(name, [&] {
        auto* control = setting(page, name)->findChild<QSlider*>();
        require(control, "Missing slider");
        control->setValue(decrease ? control->minimum() : control->maximum());
      });
    };
    for (const auto& name : {"背景透明度", "字号", "圆角半径", "阴影大小", "行间距", "预编辑间距"})
      slider(QString::fromUtf8(name));
    slider(QStringLiteral("边框宽度"), true);
    slider(QStringLiteral("边框高度"), true);
    for (auto* child : page.findChildren<QWidget*>())
      require(child->property("settingLabel").toString() != QStringLiteral("高亮圆角"),
              "Redundant highlight radius control must be absent");
    for (const auto& name : {"候选排列", "文字方向"}) {
      check(QString::fromUtf8(name), [&] {
        auto* control = setting(page, QString::fromUtf8(name))->findChild<QComboBox*>();
        control->setCurrentIndex(control->currentIndex() == 0 ? 1 : 0);
      });
    }
    const auto schemeLabel = platform.dark ? "深色配色方案" : "配色方案";
    check(QString::fromUtf8(schemeLabel), [&] {
      auto* control = setting(page, QString::fromUtf8(schemeLabel))->findChild<QComboBox*>();
      const int index = control->findData("omarchy_gruvbox");
      require(index >= 0, "Missing Omarchy palette catalogue");
      control->setCurrentIndex(index);
    });
    check(QStringLiteral("候选格式"), [&] {
      setting(page, QStringLiteral("候选格式"))->findChild<QLineEdit*>()->setText("[candidate] / [label] / [comment]");
    });
    for (const auto& name : {"在输入框中显示预编辑", "显示翻页按钮"}) {
      check(QString::fromUtf8(name), [&] {
        auto* control = setting(page, QString::fromUtf8(name))->findChild<bangke::Switch*>();
        control->setChecked(!control->isChecked());
      });
    }
    check(QStringLiteral("避免透明颜色叠加"), [&] {
      auto* control = setting(page, QStringLiteral("避免透明颜色叠加"))->findChild<bangke::Switch*>();
      control->setChecked(!control->isChecked());
    }, [](auto& style) { style.alpha = .65; });
    page.setStyle(baseline);
    const auto before = capture(preview);
    QTimer::singleShot(0, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      require(dialog, "Font chooser did not open");
      auto* list = dialog->findChild<QListWidget*>();
      const auto original = bangke::panelSpecFor(config, baseline, platform.dark).font;
      const auto originalFamily = QFontInfo(original).family();
      const QString sample = QStringLiteral("蚌壳 bang ke 123");
      const auto originalWidth = QFontMetricsF(original).horizontalAdvance(sample);
      QListWidgetItem* alternate = nullptr;
      for (int i = 0; i < list->count(); ++i) {
        auto* item = list->item(i);
        QFont candidate(item->text());
        candidate.setPointSize(baseline.fontPoint);
        if (QFontInfo(candidate).family() != originalFamily &&
            qAbs(QFontMetricsF(candidate).horizontalAdvance(sample) - originalWidth) > 1) {
          alternate = item;
          break;
        }
      }
      require(alternate, "QA requires two installed fonts with different glyph metrics");
      list->setCurrentItem(alternate);
      dialog->accept();
    });
    setting(page, QStringLiteral("字体"))->findChild<QPushButton*>()->click();
    require(before != capture(preview), "Font chooser did not update preview pixels");
    std::printf("PASS: %s / 字体选择器\n", fixture);
    auto inPlace = baseline;
    inPlace.inlinePreedit = true;
    page.setStyle(inPlace);
    auto* composition = preview->findChild<QLabel*>("previewComposition");
    const auto text = composition->text();
    setting(page, QStringLiteral("在输入框中显示选中候选"))->findChild<bangke::Switch*>()->setChecked(!inPlace.inlineCandidate);
    require(composition->text() != text, "Inline candidate did not update composition scene");

    auto data = YamlConfig::load();
    data.style = baseline;
    data.spellingHints = false;
    bangke::BehaviorPage behavior(data);
    behavior.resize(1160, 700);
    behavior.show();
    app.processEvents();
    auto* behaviorPreview = behavior.findChild<bangke::PreviewStrip*>();
    auto behaviorBefore = capture(behaviorPreview);
    setting(behavior, QStringLiteral("每页候选数"))->findChild<QSlider*>()->setValue(20);
    require(behaviorBefore != capture(behaviorPreview), "Candidate count not reflected");
    behaviorBefore = capture(behaviorPreview);
    setting(behavior, QStringLiteral("候选词后显示 [拼音] 提示"))->findChild<bangke::Switch*>()->setChecked(true);
    require(behaviorBefore != capture(behaviorPreview), "Spelling hints not reflected");
    std::printf("PASS: %s / 每页候选数、拼音提示、输入框选中候选\n", fixture);

    data.ai.enabled = true;
    data.ai.targetIndex = 0;
    bangke::AiPage aiPage(data);
    aiPage.resize(1160, 700);
    aiPage.show();
    app.processEvents();
    auto* aiPreview = aiPage.findChild<bangke::PreviewStrip*>();
    auto aiBefore = capture(aiPreview);
    setting(aiPage, QStringLiteral("候选插入位置"))->findChild<QSlider*>()->setValue(2);
    require(aiBefore != capture(aiPreview), "AI insertion position not reflected");
    aiBefore = capture(aiPreview);
    auto* punctuation = setting(aiPage, QStringLiteral("过滤标点"))->findChild<bangke::Switch*>();
    punctuation->setChecked(!punctuation->isChecked());
    require(aiBefore != capture(aiPreview), "AI punctuation example not reflected");
    aiBefore = capture(aiPreview);
    setting(aiPage, QStringLiteral("启用 AI 预测"))->findChild<bangke::Switch*>()->setChecked(false);
    require(aiBefore != capture(aiPreview), "AI enable example not reflected");
    std::printf("PASS: %s / AI 启用、候选插入位置、标点示例\n", fixture);

    MainWindow window;
    window.show();
    app.processEvents();
    for (auto* combo : window.findChildren<QComboBox*>()) {
      require(combo->size() == QSize(220, 34), QStringLiteral("Dropdown %1 dimensions: %2 x %3; min %4 x %5, max %6 x %7").arg(combo->currentText()).arg(combo->width()).arg(combo->height()).arg(combo->minimumWidth()).arg(combo->minimumHeight()).arg(combo->maximumWidth()).arg(combo->maximumHeight()));
      require(!combo->style()->styleHint(QStyle::SH_ComboBox_Popup, nullptr, combo),
              "Native menu sizing must not override bounded dropdown lists");
      auto* themed = qobject_cast<bangke::SettingsComboBox*>(combo);
      require(themed, "Every settings dropdown must use the shared popup implementation");
      require(themed->popupView()->objectName() == QStringLiteral("settingsComboPopup"),
              "Every dropdown popup must use the shared theme");
      require(themed->popupView()->maximumHeight() <= 320, "Dropdown list height must be bounded");
      require(themed->popupContainer()->objectName() == QStringLiteral("settingsComboContainer"),
              "Popup frames must use the same theme as their viewports");
      const auto frame = combo->grab().toImage();
      const auto background = platform.window;
      const auto foreground = platform.text;
      const QColor expected(qRound(background.red() * .965 + foreground.red() * .035),
                            qRound(background.green() * .965 + foreground.green() * .035),
                            qRound(background.blue() * .965 + foreground.blue() * .035));
      const auto ratio = frame.devicePixelRatio();
      require(frame.pixelColor(qRound(3 * ratio), qRound(17 * ratio)).rgb() == expected.rgb(),
              "Dropdown field background must follow the current settings theme");
    }
    auto* navigation = window.findChild<QTabBar*>("pageNavigation");
    require(navigation && navigation->count() == 5, "Missing top navigation");
    const auto center = navigation->mapTo(&window, navigation->rect().center());
    require(qAbs(center.x() - window.rect().center().x()) <= 2,
            "Navigation not centered independently of brand/status width");
    for (int index = 0; index < navigation->count(); ++index) {
      window.setPage(index);
      require(navigation->currentIndex() == index, "Navigation failed");
    }
    window.setPage(0);
    app.processEvents();
    const QString output = QString::fromLocal8Bit(qgetenv("BANGKE_LAYOUT_SCREENSHOTS"));
    if (!output.isEmpty()) {
      require(QDir().mkpath(output), "Cannot create screenshot directory");
      require(window.grab().save(output + '/' + fixture + ".png"), "Screenshot failed");
    }
  }
  std::puts("PASS: real controls change rendered pixels; unscaled preview, scrolling and centered navigation verified in dark/light themes");
}
