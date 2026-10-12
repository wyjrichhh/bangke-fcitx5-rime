// ensureRimeInputMethod 的回归测试：三种起始状态都要正确。
// 用法：QT_QPA_PLATFORM=offscreen HOME=/tmp/pt ./build/profile_test
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cstdio>

#include "fcitxprofile.h"

static QString read(const QString& p) {
  QFile f(p);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QStringLiteral("<无>");
  return QString::fromUtf8(f.readAll());
}
static void write(const QString& p, const QString& t) {
  QDir().mkpath(QFileInfo(p).absolutePath());
  QFile f(p);
  f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
  f.write(t.toUtf8());
}
static int fails = 0;
static void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++fails;
}
static QString profile() {
  return QDir::homePath() + "/.config/fcitx5/profile";
}
// rime 必须出现在 [GroupOrder] 之前（否则会被当成那节的键）
static bool beforeGroupOrder(const QString& text) {
  const int rime = text.indexOf("Name=rime-ai");
  const int order = text.indexOf("[GroupOrder]");
  return rime >= 0 && order >= 0 && rime < order;
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QFile::remove(profile());

  // 1) 全新用户：建模板
  ensureRimeInputMethod();
  QString t = read(profile());
  check(t.contains("Name=rime-ai") && t.contains("DefaultIM=rime-ai"), "全新用户建模板");
  check(beforeGroupOrder(t), "rime 在 [GroupOrder] 之前");

  // 2) 幂等
  const QString once = t;
  ensureRimeInputMethod();
  check(read(profile()) == once, "重复调用不改文件");

  // 3) 已有用户（有 [GroupOrder]，没有 rime）：插在 GroupOrder 之前，且不动别的
  write(profile(),
        QStringLiteral("[Groups/0]\nName=Default\nDefault Layout=us\n"
                       "DefaultIM=keyboard-us\n\n[Groups/0/Items/0]\n"
                       "Name=keyboard-us\nLayout=\n\n[GroupOrder]\n0=Default\n"));
  ensureRimeInputMethod();
  t = read(profile());
  check(beforeGroupOrder(t), "已有用户：插在 [GroupOrder] 之前");
  check(t.contains("Name=keyboard-us"), "不动用户已有的输入法");
  check(t.contains("[Groups/0/Items/1]"), "索引接在已有项后面");

  // 4) 已有 rime：一个字都不改
  const QString withRime =
      QStringLiteral("[Groups/0]\nName=Default\n\n[Groups/0/Items/0]\n"
                     "Name=rime-ai\nLayout=\n\n[GroupOrder]\n0=Default\n");
  write(profile(), withRime);
  ensureRimeInputMethod();
  check(read(profile()) == withRime, "已有 rime 时完全不动");

  std::printf("\n%s\n", fails == 0 ? "全部通过" : "有失败");
  return fails == 0 ? 0 : 1;
}
