// bangke.custom.yaml 的补丁写盘测试（幂等、只写改动过的键、清掉空块头）。
//
// 跑法（容器里；需要一个含 bangke.yaml 的假 HOME）：
//   cmake --build build --target stylepatch_test
//   HOME=/tmp/t1 ./build/stylepatch_test
// 它需要一个出厂 bangke.yaml 可读，路径由 BANGKE_TEST_YAML 指定。

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstdio>

#include "theme/paths.h"
#include "theme/scheme.h"
#include "theme/stylepatch.h"

namespace {

int failures = 0;

QString read(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return QStringLiteral("<读不到>");
  return QString::fromUtf8(file.readAll());
}

void write(const QString& path, const QString& text) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
  file.write(text.toUtf8());
}

void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);

  const QString source = qEnvironmentVariable("BANGKE_TEST_YAML",
                                              QStringLiteral("/src/data/bangke.yaml"));
  const QString rimeDir = bangke::paths::rimeUserDir();
  QDir(rimeDir).removeRecursively();
  QDir().mkpath(rimeDir);
  if (!QFile::copy(source, bangke::paths::sharedBangkeYaml())) {
    std::printf("FAIL 找不到出厂配置 %s\n", qPrintable(source));
    return 1;
  }

  // 出厂值单取（不带用户补丁）——「恢复默认」与判断键是否该写都用它。
  const bangke::StyleOptions factory =
      bangke::BangkeConfig::load(bangke::paths::sharedBangkeYaml(), QString())
          .style();

  // 1) 改两项 → 只写这两项
  bangke::StyleOptions style = factory;
  style.cornerRadius = 12;
  style.alpha = 0.8;
  bangke::writeStylePatch(style, factory);
  QString patch = read(bangke::paths::customBangkeYaml());
  check(patch.contains(QStringLiteral("style/corner_radius: 12")),
        "写入 corner_radius");
  check(patch.contains(QStringLiteral("style/alpha: 0.80")), "写入 alpha");
  check(!patch.contains(QStringLiteral("style/font_point:")),
        "未改动的键不写进补丁");

  // 2) 幂等
  const QString once = patch;
  bangke::writeStylePatch(style, factory);
  check(read(bangke::paths::customBangkeYaml()) == once, "重复写入幂等");

  // 3) 改回出厂值 → 删键，而不是把出厂值写进补丁
  style.cornerRadius = factory.cornerRadius;
  bangke::writeStylePatch(style, factory);
  patch = read(bangke::paths::customBangkeYaml());
  check(!patch.contains(QStringLiteral("style/corner_radius")),
        "改回出厂值后键被删除");
  check(patch.contains(QStringLiteral("style/alpha: 0.80")), "其它键不受影响");

  // 4) 手写的嵌套写法也要能清干净，且不动别人的键与注释
  write(bangke::paths::customBangkeYaml(),
        QStringLiteral("# 手写补丁\npatch:\n  style:\n    alpha: 0.50\n"
                       "  menu/page_size: 7\n"));
  bangke::resetStylePatch(factory);
  patch = read(bangke::paths::customBangkeYaml());
  check(!patch.contains(QStringLiteral("alpha")), "嵌套写法的键被清理");
  check(!patch.contains(QStringLiteral("  style:")), "空的 style 块头被去掉");
  check(patch.contains(QStringLiteral("menu/page_size: 7")), "别人的键保留");
  check(patch.contains(QStringLiteral("# 手写补丁")), "注释保留");

  std::printf("\n%s\n", failures == 0 ? "全部通过" : "有失败");
  return failures == 0 ? 0 : 1;
}
