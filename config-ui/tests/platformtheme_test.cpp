// 系统主题（Omarchy）读取层的回归测试。
//
// 跑法（容器里）：
//   cmake --build build --target platformtheme_test && ./build/platformtheme_test
//
// 两件事必须钉住：
//   1) **中立性** —— 没有 Omarchy 时产出的每一个值都要与 2026-10 之前逐字节一致，
//      否则 Ubuntu / 非 Omarchy 的 Arch 就悄悄变样了。
//   2) 映射 —— Omarchy 的 token 到我们方案键的对应。fixture 是用
//      omarchy/default/themed/shell.toml.tpl 真渲染出来的（见 tests/fixtures/）。

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QHash>
#include <cstdio>

#include "theme/native.h"
#include <QDir>
#include <QTemporaryDir>
#include <QFile>
#include "theme/platformtheme.h"
#include "theme/scheme.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

// 比 **8 位** ARGB，不比 QColor::operator== —— 后者连 16 位内部表示一起比，
// 而 setAlphaF(0.08) 与 QColor(...,20) 的 16 位值并不相等（虽然落到 8 位是同一个数）。
// 真正写进主题文件的是 8 位那一个。
void checkColor(const QColor& actual, const QColor& expected, const char* what) {
  const bool ok = actual.rgba() == expected.rgba();
  std::printf("%s %s（实际 %s / 期望 %s）\n", ok ? "PASS" : "FAIL", what,
              qPrintable(actual.name(QColor::HexArgb)),
              qPrintable(expected.name(QColor::HexArgb)));
  if (!ok) ++failures;
}

void checkInt(int actual, int expected, const char* what) {
  const bool ok = actual == expected;
  std::printf("%s %s（实际 %d / 期望 %d）\n", ok ? "PASS" : "FAIL", what, actual,
              expected);
  if (!ok) ++failures;
}

QString fixture(const QString& name) {
  return QString::fromLocal8Bit(BANGKE_TEST_FIXTURES) + QLatin1Char('/') + name;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  // ---------------------------------------------------------------- 中立性
  // 读不到 Omarchy（目录不存在）时必须无效。
  check(!bangke::loadPlatformTheme(fixture(QStringLiteral("没有这个目录"))).valid,
        "没有 Omarchy 时 loadPlatformTheme 返回无效");

  for (const bool dark : {false, true}) {
    const bangke::PlatformTheme baked = bangke::bakedPlatformTheme(dark);
    check(!baked.valid, "烘焙主题 valid=false（panelRoles 为空 = 走老路）");
    check(baked.panelRoles.isEmpty(), "烘焙主题不提供 panelRoles");
    check(!baked.metrics.valid, "烘焙主题不带 px 度量");
    checkInt(baked.controlHeight, 28, "烘焙主题控件高");
    checkInt(baked.uiFontPx, 12, "烘焙主题界面字号");

    // 这几个常量就是 2026-10 之前 ui/themepalette.cpp 里的值，一个都不能动。
    if (dark) {
      checkColor(baked.window, QColor(0x33, 0x33, 0x33), "烘焙 深色 window");
      checkColor(baked.sidebar, QColor(0x3D, 0x3D, 0x3D), "烘焙 深色 sidebar");
      checkColor(baked.card, QColor(0x46, 0x46, 0x46), "烘焙 深色 card");
      checkColor(baked.text, QColor(0xFF, 0xFF, 0xFF), "烘焙 深色 text");
      checkColor(baked.well, QColor(0x62, 0x62, 0x62), "烘焙 深色 well");
      checkColor(baked.separator, QColor(0xFF, 0xFF, 0xFF, 0x2B),
                 "烘焙 深色 separator");
      checkColor(baked.secondaryText, QColor(0xFF, 0xFF, 0xFF, 0xC4),
                 "烘焙 深色 secondaryText");
    } else {
      checkColor(baked.window, QColor(0xFC, 0xFC, 0xFC), "烘焙 浅色 window");
      checkColor(baked.sidebar, QColor(0xF0, 0xF0, 0xF0), "烘焙 浅色 sidebar");
      checkColor(baked.card, QColor(0xFF, 0xFF, 0xFF), "烘焙 浅色 card");
      checkColor(baked.text, QColor(0x2E, 0x34, 0x36), "烘焙 浅色 text");
      checkColor(baked.well, QColor(0xDC, 0xDC, 0xDC), "烘焙 浅色 well");
      checkColor(baked.separator, QColor(0x00, 0x00, 0x00, 0x1F),
                 "烘焙 浅色 separator");
      checkColor(baked.secondaryText, QColor(0x2E, 0x34, 0x36, 0x9E),
                 "烘焙 浅色 secondaryText");
    }

    // native 方案不吃桌面时，解析结果必须还是 native.h 那套。
    const bangke::SchemeColors colors =
        bangke::BangkeConfig::load(QString(), QString(), baked).resolve(
            QStringLiteral("native"), dark);
    checkColor(colors.back, bangke::argb(dark ? bangke::kNativeDark.back
                                              : bangke::kNativeLight.back),
               "烘焙 native 底色");
    checkColor(colors.candidateText,
               bangke::argb(dark ? bangke::kNativeDark.text : bangke::kNativeLight.text),
               "烘焙 native 候选文字");
    // 烘焙表里没有 border_color —— 所以没有边框（这是今天的行为，别顺手加上）。
    check(!colors.border.isValid(), "烘焙 native 没有边框色");
  }

  // ---------------------------------------------------------------- 深色映射
  const bangke::PlatformTheme tokyo =
      bangke::loadPlatformTheme(fixture(QStringLiteral("omarchy-tokyo-night")));
  check(tokyo.valid, "Tokyo Night：有效");
  check(tokyo.source == bangke::ThemeSource::Omarchy, "Tokyo Night：来源是 Omarchy");
  check(tokyo.dark, "Tokyo Night：深色");
  check(tokyo.name == QStringLiteral("tokyo-night"), "Tokyo Night：主题名");
  checkInt(tokyo.metrics.borderWidthPx, 2, "边框宽 = Omarchy 缺省 2px");
  checkInt(tokyo.metrics.textPadPx, 6, "文字内边距 = control-padding-y");
  checkInt(tokyo.uiFontPx, 12, "界面字号 = [font] base-size");
  // 没有 Hyprland 会话（容器里）→ 问不到就**不表态**，交回用户设置。
  checkInt(tokyo.metrics.cornerRadiusPx, -1, "问不到 Hyprland 时圆角留 -1");

  const QHash<QString, QColor>& roles = tokyo.panelRoles;
  checkColor(roles.value(QStringLiteral("back_color")), QColor(0x1A, 0x1B, 0x26),
             "面板底色 = [popups] background");
  checkColor(roles.value(QStringLiteral("candidate_text_color")),
             QColor(0xA9, 0xB1, 0xD6), "候选文字 = [popups] text");
  checkColor(roles.value(QStringLiteral("hilited_candidate_back_color")),
             QColor(0xA9, 0xB1, 0xD6, 20), "高亮底 = [menu] selected-background");
  checkInt(roles.value(QStringLiteral("hilited_candidate_back_color")).alpha(), 20,
           "高亮底 alpha = 0.08");
  checkColor(roles.value(QStringLiteral("hilited_candidate_text_color")),
             QColor(0x7A, 0xA2, 0xF7), "高亮文字 = [menu] selected-text (accent)");
  checkColor(roles.value(QStringLiteral("label_color")), QColor(0x41, 0x48, 0x68),
             "序号 = muted");
  checkColor(roles.value(QStringLiteral("comment_text_color")), QColor(0x41, 0x48, 0x68),
             "注释 = muted");
  checkColor(roles.value(QStringLiteral("border_color")), QColor(0x7A, 0xA2, 0xF7),
             "边框 = [popups] border → 活动边框 → accent");
  checkColor(roles.value(QStringLiteral("accent_text_color")), QColor(0x7A, 0xA2, 0xF7),
             "强调色 = accent");

  checkColor(tokyo.window, QColor(0x1A, 0x1B, 0x26), "设置窗底 = background");
  checkColor(tokyo.sidebar, QColor(0x0E, 0x0E, 0x14), "侧栏 = darker_background");
  checkColor(tokyo.card, QColor(0x24, 0x28, 0x3B), "卡片 = lighter_background");
  checkColor(tokyo.text, QColor(0xA9, 0xB1, 0xD6), "正文 = foreground");
  checkColor(tokyo.secondaryText, QColor(0x41, 0x48, 0x68), "次要文字 = muted");
  checkColor(tokyo.accent, QColor(0x7A, 0xA2, 0xF7), "强调色 = accent");

  // 面板配色真的走进了 resolve()。
  const bangke::SchemeColors fromDesktop =
      bangke::BangkeConfig::load(QString(), QString(), tokyo)
          .resolve(QStringLiteral("native"), false);
  checkColor(fromDesktop.back, QColor(0x1A, 0x1B, 0x26), "resolve 吃到面板底色");
  checkColor(fromDesktop.hilitedCandidateBack,
             QColor(0xA9, 0xB1, 0xD6, 20), "resolve 吃到高亮底");
  check(fromDesktop.border.isValid(), "resolve 吃到边框色（烘焙路径没有）");

  // ---------------------------------------------------------------- 浅色映射
  const bangke::PlatformTheme light =
      bangke::loadPlatformTheme(fixture(QStringLiteral("omarchy-flexoki-light")));
  check(light.valid && !light.dark, "Flexoki Light：浅色");
  checkColor(light.window, QColor(0xFF, 0xFC, 0xF0), "浅色 window = background");
  checkColor(light.sidebar, QColor(0xE5, 0xE2, 0xD8), "浅色 sidebar = darker_background");
  checkColor(light.text, QColor(0x10, 0x0F, 0x0F), "浅色 text = foreground");
  checkColor(light.accent, QColor(0x20, 0x5E, 0xA6), "浅色 accent");

  // ---------------------------------------------------------------- 梯度
  // 活动边框是梯度时只取第一站（复刻 Omarchy Color.qml 的 firstColorToken）。
  const bangke::PlatformTheme gradient =
      bangke::loadPlatformTheme(fixture(QStringLiteral("omarchy-gradient")));
  check(gradient.valid, "梯度 fixture：有效");
  checkColor(gradient.panelRoles.value(QStringLiteral("border_color")),
             QColor(0x33, 0xCC, 0xFF), "梯度只取第一站");

  const QString catalogue = QString::fromLocal8Bit(qgetenv("BK_OMARCHY_CATALOG_DIR"));
  if (!catalogue.isEmpty()) {
    const auto config = bangke::BangkeConfig::load(QString(),QString(),tokyo);
    for (const QString& name : QDir(catalogue).entryList(QDir::Dirs|QDir::NoDotAndDotDot)) {
      const QString id = QStringLiteral("omarchy_") + name;
      check(config.hasScheme(id), "每个系统主题都有独立方案");
      const auto theme = bangke::loadPlatformTheme(catalogue+QLatin1Char('/')+name);
      const auto colors = config.resolve(id,false);
      checkColor(colors.back,theme.panelRoles.value(QStringLiteral("back_color")),"主题方案背景一致");
      checkColor(colors.candidateText,theme.panelRoles.value(QStringLiteral("candidate_text_color")),"主题方案文字一致");
      check(colors.back != colors.candidateText,"主题文字可见");
    }
  }
  QTemporaryDir legacy;
  QFile legacyColors(legacy.path()+QStringLiteral("/colors.toml"));
  check(legacyColors.open(QIODevice::WriteOnly), "旧版主题 fixture");
  legacyColors.write("background = \"#ffffff\"\nforeground = \"#111111\"\naccent = \"#2255aa\"\nmuted = \"#666666\"\n");
  legacyColors.close();
  const auto old = bangke::loadPlatformTheme(legacy.path());
  check(old.valid && !old.dark,"没有 mode 的旧版浅色主题仍可跟随");

  std::printf("\n%s 共 %d 处失败\n", failures ? "失败" : "全部通过", failures);
  return failures == 0 ? 0 : 1;
}
