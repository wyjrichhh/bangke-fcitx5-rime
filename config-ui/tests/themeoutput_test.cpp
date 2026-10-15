// 生成产物的回归测试：断言**真面板会画的东西**，而不是预览图。
//
// 跑法（容器里）：
//   cmake --build build --target themeoutput_test && ./build/themeoutput_test
// 需要 BANGKE_TEST_YAML（出厂 bangke.yaml）与 BANGKE_TEST_FIXTURES（Omarchy fixture）。
//
// 为什么断言 theme.conf 与九宫格 PNG：classicui 的绘制路径不分后端，最终上屏的
// 就是这两个东西。预览条是缩放过的两栏比对图，反推它的混色不可靠；而 PNG 是
// 1:1 生成的，能直接读像素。

#include <QColor>
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <cstdio>

#include "theme/native.h"
#include "theme/paths.h"
#include "theme/platformtheme.h"
#include "theme/scheme.h"
#include "theme/themewriter.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

// 8% 这种低 alpha 的颜色经过预乘（QImage::Format_ARGB32_Premultiplied）只剩
// ~1.5 位 RGB 精度，逐通道会有几个数的量化误差；给个容差，别把它当成配色错。
void checkRgbNear(const QColor& actual, const QColor& expected, int tolerance,
                  const char* what) {
  const bool ok = qAbs(actual.red() - expected.red()) <= tolerance &&
                  qAbs(actual.green() - expected.green()) <= tolerance &&
                  qAbs(actual.blue() - expected.blue()) <= tolerance;
  std::printf("%s %s（实际 %s / 期望 %s ±%d）\n", ok ? "PASS" : "FAIL", what,
              qPrintable(actual.name()), qPrintable(expected.name()), tolerance);
  if (!ok) ++failures;
}

void checkRgb(const QColor& actual, const QColor& expected, const char* what) {
  const bool ok = actual.rgb() == expected.rgb();
  std::printf("%s %s（实际 %s / 期望 %s）\n", ok ? "PASS" : "FAIL", what,
              qPrintable(actual.name()), qPrintable(expected.name()));
  if (!ok) ++failures;
}

void checkAlpha(int actual, int expected, const char* what) {
  const bool ok = actual == expected;
  std::printf("%s %s（实际 %d / 期望 %d）\n", ok ? "PASS" : "FAIL", what, actual,
              expected);
  if (!ok) ++failures;
}

QString readText(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
  return QString::fromUtf8(file.readAll());
}

// theme.conf 里某个段下某个键的值。
QString confValue(const QString& text, const QString& section, const QString& key) {
  const QStringList lines = text.split(QLatin1Char('\n'));
  bool inSection = false;
  for (const QString& line : lines) {
    const QString trimmed = line.trimmed();
    if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
      inSection = (trimmed == QLatin1String("[") + section + QLatin1String("]"));
      continue;
    }
    if (inSection && trimmed.startsWith(key + QLatin1Char('=')))
      return trimmed.mid(key.size() + 1);
  }
  return QString();
}

QString fixture(const QString& name) {
  return QString::fromLocal8Bit(BANGKE_TEST_FIXTURES) + QLatin1Char('/') + name;
}

}  // namespace

int main(int argc, char** argv) {
  // resolveFont 要用 QFontDatabase，必须有 QGuiApplication；离屏即可。
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);

  const QString source = qEnvironmentVariable("BANGKE_TEST_YAML",
                                              QStringLiteral("/src/data/bangke.yaml"));
  QDir(bangke::paths::rimeUserDir()).removeRecursively();
  QDir().mkpath(bangke::paths::rimeUserDir());
  if (!QFile::copy(source, bangke::paths::sharedBangkeYaml())) {
    std::printf("FAIL 找不到出厂配置 %s\n", qPrintable(source));
    return 1;
  }

  QTemporaryDir out;
  if (!out.isValid()) return 1;

  // ------------------------------------------------------------ 有 Omarchy
  {
    const bangke::PlatformTheme theme =
        bangke::loadPlatformTheme(fixture(QStringLiteral("omarchy-tokyo-night")));
    const bangke::BangkeConfig config = bangke::BangkeConfig::load(
        bangke::paths::sharedBangkeYaml(), bangke::paths::customBangkeYaml(), theme);
    const bangke::ThemeResult result =
        bangke::applyTheme(config, config.style(), false, out.filePath(QStringLiteral("omarchy")));
    check(result.ok, "Omarchy：主题生成成功");

    const QString dir = out.filePath(QStringLiteral("omarchy/BangKe"));
    const QString conf = readText(dir + QStringLiteral("/theme.conf"));
    check(confValue(conf, QStringLiteral("InputPanel/Background"), QStringLiteral("Color")) ==
              QStringLiteral("#1A1B26"),
          "面板底色 = Tokyo Night 的 background");
    check(confValue(conf, QStringLiteral("InputPanel/Background"),
                    QStringLiteral("BorderColor")) == QStringLiteral("#7AA2F7"),
          "面板边框 = accent（烘焙路径下这里是 #00000000）");
    check(confValue(conf, QStringLiteral("InputPanel"), QStringLiteral("NormalColor")) ==
              QStringLiteral("#A9B1D6"),
          "候选文字 = [popups] text");
    // BorderWidth 必须显式写出来，classicui 才描边（光有 BorderColor 不画）。
    check(confValue(conf, QStringLiteral("InputPanel/Background"),
                    QStringLiteral("BorderWidth")) == QStringLiteral("2"),
          "面板边框宽写进 theme.conf（否则描边不发生）");
    check(confValue(conf, QStringLiteral("InputPanel/Background/Margin"),
                    QStringLiteral("Left")) == QStringLiteral("2"),
          "纯色路径的 Margin 取边框宽（九宫格切片宽只给 Image= 用）");

    // 九宫格底图：中心是底色，四角透明（圆角），上边中点是描边色。
    const QImage background(dir + QStringLiteral("/background.png"));
    check(!background.isNull(), "底图生成成功");
    if (!background.isNull()) {
      const QImage rgba = background.convertToFormat(QImage::Format_ARGB32);
      checkRgb(rgba.pixelColor(rgba.width() / 2, rgba.height() / 2),
               QColor(0x1A, 0x1B, 0x26), "底图中心 = 面板底色");
      checkAlpha(rgba.pixelColor(0, 0).alpha(), 0, "底图左上角透明（圆角）");
      checkRgb(rgba.pixelColor(rgba.width() / 2, 0), QColor(0x7A, 0xA2, 0xF7),
               "底图上边中点 = 边框色");
    }

    // 高亮块：Omarchy 的 menu.selected-background = 前景 @8%，且**不带边框**。
    const QImage highlight(dir + QStringLiteral("/highlight.png"));
    check(!highlight.isNull(), "高亮图生成成功");
    if (!highlight.isNull()) {
      const QImage rgba = highlight.convertToFormat(QImage::Format_ARGB32);
      const QColor center = rgba.pixelColor(rgba.width() / 2, rgba.height() / 2);
      checkRgbNear(center, QColor(0xA9, 0xB1, 0xD6), 12, "高亮块中心 = 前景色");
      checkAlpha(center.alpha(), 20, "高亮块 alpha = 8%");
    }
  }

  // ------------------------------------------------------------ 没有 Omarchy
  // 中立性：产出必须与 2026-10 之前一致 —— 没有边框、用烘焙的 native 色。
  for (const bool dark : {false, true}) {
    const bangke::PlatformTheme theme = bangke::bakedPlatformTheme(dark);
    const bangke::BangkeConfig config = bangke::BangkeConfig::load(
        bangke::paths::sharedBangkeYaml(), QString(), theme);
    const QString root = out.filePath(dark ? QStringLiteral("baked-dark")
                                          : QStringLiteral("baked-light"));
    const bangke::ThemeResult result =
        bangke::applyTheme(config, config.style(), false, root);
    check(result.ok, dark ? "烘焙 深色：生成成功" : "烘焙 浅色：生成成功");

    // applyTheme 两个目录都写；深色那份在 BangKe-Dark 里。
    const QString dir = root + (dark ? QStringLiteral("/BangKe-Dark")
                                     : QStringLiteral("/BangKe"));
    const QString conf = readText(dir + QStringLiteral("/theme.conf"));
    check(confValue(conf, QStringLiteral("InputPanel/Background"),
                    QStringLiteral("BorderColor")) == QStringLiteral("#00000000"),
          dark ? "烘焙 深色：没有面板边框" : "烘焙 浅色：没有面板边框");
    check(confValue(conf, QStringLiteral("InputPanel/Background"),
                    QStringLiteral("BorderWidth")).isEmpty(),
          dark ? "烘焙 深色：不写 BorderWidth（不描边）"
               : "烘焙 浅色：不写 BorderWidth（不描边）");
    const QColor expectedBack =
        bangke::argb(dark ? bangke::kNativeDark.back : bangke::kNativeLight.back);
    const QImage background(dir + QStringLiteral("/background.png"));
    checkRgb(background.convertToFormat(QImage::Format_ARGB32)
                 .pixelColor(background.width() / 2, background.height() / 2),
             expectedBack, dark ? "烘焙 深色：底图中心 = native 底色"
                                : "烘焙 浅色：底图中心 = native 底色");
  }

  std::printf("\n%s 共 %d 处失败\n", failures ? "失败" : "全部通过", failures);
  return failures == 0 ? 0 : 1;
}
