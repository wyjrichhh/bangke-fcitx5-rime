// Switch（自绘开关）的回归测试：点击后状态要翻转、旋钮要跟着动。
//
// 这个测试是为了一个真实 bug 写的：Qt 在 click() 内部会把 blockRefresh 置真，
// 于是 setChecked() 会**跳过 checkStateSet()** —— 只在 checkStateSet 里驱动动画的
// 实现，会表现为「构造时旋钮到位，之后怎么点都不动」。
//
// 用法：QT_QPA_PLATFORM=offscreen ./build/switch_test
#include <QApplication>
#include <QTest>
#include <QTimer>
#include <cstdio>

#include "ui/widgets.h"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  bangke::Switch sw;
  sw.resize(sw.sizeHint());
  std::printf("初始: checked=%d position=%.2f\n", sw.isChecked(), sw.position());

  // 真·鼠标点击（走 QWidget 的 press/release 事件链，与用户手动点一致）
  QTest::mouseClick(&sw, Qt::LeftButton, Qt::NoModifier, QPoint(19, 11));
  std::printf("点击后立刻: checked=%d position=%.2f\n", sw.isChecked(),
              sw.position());

  auto* tick = new QTimer(&app);
  int ticks = 0;
  QObject::connect(tick, &QTimer::timeout, [&]() {
    ++ticks;
    std::printf("  t=%3dms position=%.3f\n", ticks * 60, sw.position());
  });
  tick->start(60);

  QTimer::singleShot(400, [&]() {
    const bool ok = sw.isChecked() && sw.position() > 0.9;
    std::printf("400ms 后: checked=%d position=%.2f → %s\n", sw.isChecked(),
                sw.position(), ok ? "旋钮到位 ✓" : "旋钮没动 ✗");
    app.exit(ok ? 0 : 1);
  });
  return app.exec();
}
