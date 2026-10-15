#include <QApplication>
#include <QMessageBox>
#include "theme/platformtheme.h"
#include "ui/themepalette.h"
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusConnection>
#include <QDBusMessage>
#include "ui/widgets.h"
#include <cstdio>
#include "mainwindow.h"
#include "rimedeploy.h"
#include "theme/paths.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  if (app.arguments().contains("--dialogs")) {
    for (bool dark : {false, true}) {
      auto theme = bangke::bakedPlatformTheme(dark);
      theme.window = QColor(dark ? "#202124" : "#fafafa");
      theme.text = QColor(dark ? "#f5f5f5" : "#171717");
      theme.dark = dark;
      bangke::setAppPlatformTheme(theme);
      app.setStyleSheet(bangke::palette::styleSheet(nullptr));
      QMessageBox box(QMessageBox::Information, QStringLiteral("保存结果"),
          QStringLiteral("设置已保存并应用。 Settings saved and applied."), QMessageBox::Ok);
      box.show(); app.processEvents();
      const QImage image = box.grab().toImage();
      if (image.pixelColor(10, 10).rgb() != theme.window.rgb()) return 15;
      int readablePixels = 0;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
          if (qAbs(image.pixelColor(x, y).red() - theme.text.red()) < 40 &&
              qAbs(image.pixelColor(x, y).green() - theme.text.green()) < 40 &&
              qAbs(image.pixelColor(x, y).blue() - theme.text.blue()) < 40) ++readablePixels;
      std::printf("Dialog %s: readable pixels=%d\n", dark ? "dark" : "light", readablePixels);
      if (readablePixels < 50) return 16;
      image.save(app.arguments().last() + (dark ? "/dialog-dark.png" : "/dialog-light.png"));
    }
    puts("PASS: save dialogs use matching backgrounds and readable text in light/dark themes");
    return 0;
  }
  if (!RimeDeploy::usesCustomPanel()) return 1;
  if (app.arguments().contains("--inactive")) {
    MainWindow window;
    bool corner = false;
    for (auto* row : window.findChildren<bangke::SliderRow*>())
      for (auto* label : row->findChildren<QLabel*>())
        if (label->text() == QStringLiteral("圆角半径")) {
          row->setValue(30); corner = row->value() == 30;
        }
    if (!corner || window.findChildren<bangke::SliderRow*>().size() < 9) return 12;
    const QString payload = QStringLiteral("{\"version\":1,\"style\":{\"corner_radius\":30}}");
    if (!RimeDeploy::setCustomPanelAppearance(payload).contains(QStringLiteral("当前桌面"))) return 13;
    QFile file(bangke::paths::configHome() + "/bangke-panel/appearance.json");
    if (!file.open(QIODevice::ReadOnly) || file.readAll() != payload.toUtf8()) return 14;
    puts("PASS: installed inactive GNOME extension exposes full editor and persists deferred appearance");
    return 0;
  }
  QDir().mkpath(QFileInfo(bangke::paths::classicUiConf()).absolutePath());
  const QByteArray sentinel("Theme=SomeoneElse\nVertical Candidate List=False\n");
  { QFile f(bangke::paths::classicUiConf()); if (!f.open(QIODevice::WriteOnly)) return 2; f.write(sentinel); }
  for (const QString orientation : {QStringLiteral("vertical"), QStringLiteral("horizontal")}) {
    MainWindow window;
    bool found = false;
    for (auto* combo : window.findChildren<QComboBox*>()) {
      const int index = combo->findData(orientation == "vertical" ? "stacked" : "linear");
      if (index >= 0) { combo->setCurrentIndex(index); found = true; break; }
    }
    if (!found) return 3;
    const QMap<QString, double> values{{QStringLiteral("圆角半径"), 24}, {QStringLiteral("高亮圆角"), 12},
      {QStringLiteral("字号"), 19}, {QStringLiteral("背景透明度"), .35}, {QStringLiteral("阴影大小"), 8},
      {QStringLiteral("边框宽度"), 3}, {QStringLiteral("边框高度"), 2}, {QStringLiteral("行间距"), 6},
      {QStringLiteral("预编辑间距"), 7}};
    for (auto* row : window.findChildren<bangke::SliderRow*>()) {
      for (auto* label : row->findChildren<QLabel*>()) {
        if (values.contains(label->text())) {
          row->setValue(values.value(label->text()));
          if (qAbs(row->value() - values.value(label->text())) > .001) return 7;
        }
      }
    }
    for (auto* combo : window.findChildren<QComboBox*>()) {
      const int index = combo->findData(orientation);
      if (index >= 0) combo->setCurrentIndex(index);
    }
    const QMap<QString, QString> toggles{{QStringLiteral("毛玻璃背景"), "translucency"},
      {QStringLiteral("避免透明颜色叠加"), "mutual_exclusive"}, {QStringLiteral("记忆本次输入的面板宽度"), "memorize_size"},
      {QStringLiteral("在输入框中显示预编辑"), "inline_preedit"}, {QStringLiteral("在输入框中显示选中候选"), "inline_candidate"},
      {QStringLiteral("显示翻页按钮"), "show_paging"}};
    for (auto* row : window.findChildren<bangke::ToggleRow*>())
      for (auto* label : row->findChildren<QLabel*>())
        if (toggles.contains(label->text())) row->setChecked(orientation == "vertical");
    for (auto* row : window.findChildren<bangke::TextRow*>())
      for (auto* label : row->findChildren<QLabel*>())
        if (label->text() == QStringLiteral("候选格式")) row->setText("[label]. [candidate] ([comment])");
    // A modal error would run its own event loop; terminate rather than hang.
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [] { std::exit(4); });
    timer.start(8000);
    if (!QMetaObject::invokeMethod(&window, "saveAndDeploy", Qt::DirectConnection)) return 5;
    bool applied = false;
    for (auto* label : window.findChildren<QLabel*>())
      applied |= label->text() == QStringLiteral("已保存并应用。");
    if (!applied) return 17;
    QFile f(bangke::paths::classicUiConf());
    if (!f.open(QIODevice::ReadOnly) || f.readAll() != sentinel) return 6;
    auto call = QDBusMessage::createMethodCall("org.kde.impanel", "/org/kde/impanel", "org.kde.impanel2", "GetAppearance");
    auto reply = QDBusConnection::sessionBus().call(call);
    auto style = QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8()).object()["style"].toObject();
    const QMap<QString, double> expected{{"corner_radius",24},{"hilited_corner_radius",12},{"font_point",19},
      {"alpha",.65},{"shadow_size",8},{"border_width",3},{"border_height",2},{"line_spacing",6},{"spacing",7}};
    for (auto it = expected.begin(); it != expected.end(); ++it)
      if (qAbs(style[it.key()].toVariant().toDouble() - it.value()) > .001) return 8;
    for (const auto& key : toggles)
      if (style[key].toVariant().toString() != (orientation == "vertical" ? "true" : "false")) return 10;
    if (style["text_orientation"].toString() != orientation ||
        style["candidate_format"].toString() != "[label]. [candidate] ([comment])") return 11;
    MainWindow reopened;
    for (auto* row : reopened.findChildren<bangke::SliderRow*>())
      for (auto* label : row->findChildren<QLabel*>())
        if (values.contains(label->text()) && qAbs(row->value() - values.value(label->text())) > .001) return 9;
    std::printf("PASS: save %s, classicui configuration unchanged\n", qPrintable(orientation));
  }
  return 0;
}
