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
#include <cstdio>

#include "theme/paths.h"
#include "theme/scheme.h"
#include "ui/pages.h"
#include "ui/widgets.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);

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
