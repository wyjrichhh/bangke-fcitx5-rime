// 验证「把配置推给正在运行的 fcitx5 插件」这条路真的通。
// 需要会话总线（在机器上跑要带上 XDG_RUNTIME_DIR / DBUS_SESSION_BUS_ADDRESS）。
// 用法：./build/addonconfig_test  （会把 PreeditInApplication 写成 false 再写回 true）
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <cstdio>

#include "rimedeploy.h"

static void show(const char* path) {
  QFile f(QString::fromLocal8Bit(path));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    std::printf("  (读不到 %s)\n", path);
    return;
  }
  QTextStream ts(&f);
  ts.setCodec("UTF-8");
  while (!ts.atEnd()) {
    const QString line = ts.readLine();
    if (line.contains(QLatin1String("PreeditInApplication")))
      std::printf("  文件里现在是: %s\n", qPrintable(line));
  }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const char* conf = "/tmp/addonconfig-test-rime.conf";
  Q_UNUSED(conf);

  std::printf("写 PreeditInApplication=false ...\n");
  const QString e1 = RimeDeploy::setAddonConfig(
      QStringLiteral("rime"), {{QStringLiteral("PreeditInApplication"), false}});
  std::printf("  → %s\n", e1.isEmpty() ? "成功" : qPrintable(e1));

  std::printf("写 PreeditInApplication=true ...\n");
  const QString e2 = RimeDeploy::setAddonConfig(
      QStringLiteral("rime"), {{QStringLiteral("PreeditInApplication"), true}});
  std::printf("  → %s\n", e2.isEmpty() ? "成功" : qPrintable(e2));
  return e1.isEmpty() && e2.isEmpty() ? 0 : 1;
}
