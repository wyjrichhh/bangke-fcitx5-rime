#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include "fcitxprofile.h"
#include "mainwindow.h"
#include "theme/migrate.h"
#include "theme/paths.h"
#include "ui/themepalette.h"
#include "theme/scheme.h"
#include "theme/themewriter.h"
#include "ui/preview.h"

namespace {
constexpr const char* kInstanceKey = "fcitx5-rime-config-instance";

QTextStream& out() {
  static QTextStream stream(stdout);
  return stream;
}

// 命令行入口：装包的 postinst 与验收脚本用它生成主题，不需要开窗口。
int runGenerateTheme(const QString& outRoot) {
  const QString error = bangke::ensureSharedConfig();
  if (!error.isEmpty()) {
    out() << error << Qt::endl;
    return 1;
  }
  bangke::migrateLegacyAppearance(bangke::BangkeConfig::load(
      bangke::paths::sharedBangkeYaml(), QString()));
  // 兜底：确保「蚌壳拼音」在用户的输入法列表里（postinst 装包时也做过一次）。
  ensureRimeInputMethod();
  const bangke::BangkeConfig config = bangke::BangkeConfig::load(
      bangke::paths::sharedBangkeYaml(), bangke::paths::customBangkeYaml());
  const bangke::ThemeResult result =
      bangke::applyTheme(config, config.style(), outRoot.isEmpty(), outRoot);
  if (!result.ok) {
    out() << result.message << Qt::endl;
    return 1;
  }
  for (const QString& file : result.files) out() << file << Qt::endl;
  return 0;
}

// 把整窗渲染成 PNG（离屏，不进真实合成器）。用于与 mac 的设置窗口并排比对。
int runDumpUi(const QString& spec) {
  const QStringList parts = spec.split(QLatin1Char(','));
  const QString path = parts.value(0);
  bangke::ensureSharedConfig();
  MainWindow window;
  window.resize(760, 680);
  if (parts.size() > 1) window.setPage(parts.at(1).toInt());
  window.show();
  QCoreApplication::processEvents();
  if (!window.grab().save(path, "PNG")) {
    out() << QStringLiteral("窗口渲染失败：%1").arg(path) << Qt::endl;
    return 1;
  }
  out() << path << Qt::endl;
  return 0;
}

// 把预览条渲染成 PNG。视觉验收（与 mac 截图、与真机截图并排比对）靠它，
// 不需要真的开窗口。
int runDumpPreview(const QString& path) {
  const QString error = bangke::ensureSharedConfig();
  if (!error.isEmpty()) {
    out() << error << Qt::endl;
    return 1;
  }
  const bangke::BangkeConfig config = bangke::BangkeConfig::load(
      bangke::paths::sharedBangkeYaml(), bangke::paths::customBangkeYaml());
  bangke::PreviewStrip strip;
  strip.setSpecs(bangke::panelSpecFor(config, false),
                 bangke::panelSpecFor(config, true));
  strip.resize(720, 280);
  if (!strip.grab().save(path, "PNG")) {
    out() << QStringLiteral("预览渲染失败：%1").arg(path) << Qt::endl;
    return 1;
  }
  out() << path << Qt::endl;
  return 0;
}
}  // namespace

int main(int argc, char* argv[]) {
  // 无显示环境下也能生成主题（验收脚本、容器里都用得上）。
  bool generateTheme = false;
  QString outRoot;
  QString previewPath;
  QString uiPath;
  for (int i = 1; i < argc; ++i) {
    const QString arg = QString::fromLocal8Bit(argv[i]);
    if (arg == QLatin1String("--generate-theme")) {
      generateTheme = true;
    } else if (arg.startsWith(QLatin1String("--generate-theme="))) {
      generateTheme = true;
      outRoot = arg.mid(int(strlen("--generate-theme=")));
    } else if (arg.startsWith(QLatin1String("--dump-preview="))) {
      previewPath = arg.mid(int(strlen("--dump-preview=")));
    } else if (arg.startsWith(QLatin1String("--dump-ui="))) {
      uiPath = arg.mid(int(strlen("--dump-ui=")));
    }
  }
  const bool headless = generateTheme || !previewPath.isEmpty() || !uiPath.isEmpty();
  if (headless) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  } else if (!qEnvironmentVariableIsEmpty("BK_QT_PLATFORM")) {
    // 留个显式逃生口：想强制走某条渲染路径时设它。
    qputenv("QT_QPA_PLATFORM", qgetenv("BK_QT_PLATFORM"));
  } else if (!qEnvironmentVariableIsEmpty("DISPLAY")) {
    // Qt5 的 wayland 后端在这套界面上不可靠。实测（Hyprland / Omarchy，
    // QT_QPA_PLATFORM=wayland;xcb）候选面板的预览块整块不绘制、下拉框点不开；
    // 而同一份程序用 --dump-ui 离屏渲染完全正常 —— 说明数据没问题，是渲染后端。
    // 有 DISPLAY 就说明有 XWayland（Wayland 桌面上基本是标配），走 xcb 一切正常。
    // 纯 Wayland（没有 XWayland）时 DISPLAY 为空，保持原样、仍然用 wayland。
    qputenv("QT_QPA_PLATFORM", "xcb");
  }

  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("fcitx5-rime-config"));
  bangke::palette::init();  // 深浅色只判一次，QSS 与自绘控件共用
  app.setStyleSheet(bangke::palette::styleSheet(nullptr));

  if (!uiPath.isEmpty()) return runDumpUi(uiPath);
  if (!previewPath.isEmpty()) return runDumpPreview(previewPath);
  if (generateTheme) return runGenerateTheme(outRoot);

  // 单实例：已在运行则唤醒已有窗口并退出。
  {
    QLocalSocket probe;
    probe.connectToServer(QString::fromLatin1(kInstanceKey));
    if (probe.waitForConnected(200)) {
      probe.write("raise\n");
      probe.flush();
      probe.waitForBytesWritten(200);
      return 0;
    }
  }
  bangke::migrateLegacyAppearance(bangke::BangkeConfig::load(
      bangke::paths::sharedBangkeYaml(), QString()));
  // 兜底：确保「蚌壳拼音」在用户的输入法列表里（postinst 装包时也做过一次）。
  ensureRimeInputMethod();

  QLocalServer server;
  QLocalServer::removeServer(QString::fromLatin1(kInstanceKey));
  server.listen(QString::fromLatin1(kInstanceKey));

  MainWindow w;
  w.show();
  QObject::connect(&server, &QLocalServer::newConnection, &server, [&]() {
    while (server.hasPendingConnections()) {
      if (auto* c = server.nextPendingConnection()) {
        // 等客户端消息落地再销毁连接。
        QObject::connect(c, &QLocalSocket::disconnected, c,
                         &QLocalSocket::deleteLater);
        c->readAll();
        c->disconnectFromServer();
      }
    }
    w.show();
    w.raise();
    w.activateWindow();
  });

  return app.exec();
}
