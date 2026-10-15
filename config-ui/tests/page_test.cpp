// 在「真实的外观页」里测开关：构造整页、打印开关的实际尺寸与位置、
// 再用真实鼠标事件点它，看状态与旋钮位置。
//
// 单测（switch_test）里开关是孤立构造的，一切正常；这个测试复现的是
// 「开关放在真实页面/布局里」的上下文 —— 用户机器上就是那里不动。
//
// 用法（容器或机器上，需要 Qt5Test）：
//   QT_QPA_PLATFORM=offscreen ./build/page_test
#include <QApplication>
#include <QScrollArea>
#include <QTest>
#include <QTimer>
#include <QMessageBox>
#include <QAbstractButton>
#include <QDesktopServices>
#include "update/updatechecker.h"
#include <cstdio>

#include "theme/paths.h"
#include "theme/scheme.h"
#include "ui/pages.h"
#include "ui/widgets.h"

class DownloadRecorder : public QObject {
  Q_OBJECT
 public:
  QUrl opened;
 public slots:
  void open(const QUrl& url) { opened = url; }
};

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  if (app.arguments().contains("--updates")) {
    bangke::AboutPage about;
    auto* checker = about.findChild<bangke::UpdateChecker*>();
    if (!checker || checker->currentVersion().isEmpty()) return 2;
    DownloadRecorder recorder;
    QDesktopServices::setUrlHandler("https", &recorder, "open");
    bool valid = false;
    QTimer::singleShot(0, [&]() {
      auto* box = qobject_cast<QMessageBox*>(app.activeModalWidget());
      if (!box) return;
      valid = box->text().contains(checker->currentVersion()) && box->text().contains("5.0.20");
      for (auto* button : box->buttons()) {
        if (button->text() == QStringLiteral("下载最新安装包")) {
          button->click();
          return;
        }
      }
      valid = false;
      box->reject();
    });
    const QUrl installer("https://shellim.openbankai.com/get/fcitx5-rime-ai-predict_5.0.20_amd64.deb");
    checker->available("5.0.20", installer, "更新说明", true);
    if (!valid || recorder.opened != installer) return 3;
    for (const QString result : {QStringLiteral("当前已是最新版本，无需下载。"), QStringLiteral("暂时无法检查更新，请稍后重试。")}) {
      valid = false;
      QTimer::singleShot(0, [&]() {
        auto* box = qobject_cast<QMessageBox*>(app.activeModalWidget());
        if (!box) return;
        valid = box->text() == result;
        box->accept();
      });
      checker->finished(result, true);
      if (!valid) return 4;
    }
    QDesktopServices::unsetUrlHandler("https");
    puts("PASS actual About dialogs show versions, latest/no-update/failure results; download button opens exact installer URL");
    return 0;
  }

  const bangke::BangkeConfig config = bangke::BangkeConfig::load(
      bangke::paths::sharedBangkeYaml(), bangke::paths::customBangkeYaml());
  bangke::AppearancePage page(config, config.style());
  page.resize(560, 700);
  page.show();
  QCoreApplication::processEvents();

  const auto switches = page.findChildren<bangke::Switch*>();
  std::printf("外观页里找到 %d 个开关\n", switches.size());
  for (auto* sw : switches) {
    std::printf("  开关 %dx%d  位置 (%d,%d)  可见=%d  checked=%d  position=%.2f\n",
                sw->width(), sw->height(), sw->x(), sw->y(), sw->isVisible(),
                sw->isChecked(), sw->position());
  }
  if (switches.isEmpty()) return 1;

  auto* sw = switches.first();
  // 先等构造时的动画跑完，再看它有没有到位
  QTimer::singleShot(400, [&]() {
    std::printf("停 400ms（构造动画应跑完）: checked=%d position=%.2f → %s\n",
                sw->isChecked(), sw->position(),
                sw->position() > 0.9 ? "到位 ✓" : "卡在左边 ✗");
    const int before = sw->isChecked();
    QTest::mouseClick(sw, Qt::LeftButton, Qt::NoModifier,
                      QPoint(sw->width() / 2, sw->height() / 2));
    std::printf("点击后: checked %d→%d position=%.2f\n", before, sw->isChecked(),
                sw->position());
    QTimer::singleShot(400, [&]() {
      std::printf("再停 400ms: checked=%d position=%.2f → %s\n", sw->isChecked(),
                  sw->position(),
                  (sw->isChecked() && sw->position() > 0.9) ||
                          (!sw->isChecked() && sw->position() < 0.1)
                      ? "旋钮与状态一致 ✓"
                      : "旋钮与状态不一致 ✗");
      app.exit(0);
    });
  });
  return app.exec();
}

#include "page_test.moc"
