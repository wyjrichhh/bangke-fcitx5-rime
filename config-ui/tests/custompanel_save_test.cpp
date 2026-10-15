#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <cstdio>
#include "mainwindow.h"
#include "rimedeploy.h"
#include "theme/paths.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  if (!RimeDeploy::usesCustomPanel()) return 1;
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
    // A modal error would run its own event loop; terminate rather than hang.
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, [] { std::exit(4); });
    timer.start(8000);
    if (!QMetaObject::invokeMethod(&window, "saveAndDeploy", Qt::DirectConnection)) return 5;
    QFile f(bangke::paths::classicUiConf());
    if (!f.open(QIODevice::ReadOnly) || f.readAll() != sentinel) return 6;
    std::printf("PASS: save %s, classicui configuration unchanged\n", qPrintable(orientation));
  }
  return 0;
}
