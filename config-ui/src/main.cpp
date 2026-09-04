#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>
#include <QTimer>

#include "mainwindow.h"

namespace {
const char* kStyleSheet = R"(
QGroupBox { font-weight: bold; margin-top: 20px; }
QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0 4px; }
QPushButton { padding: 6px 16px; }
)";
constexpr const char* kInstanceKey = "fcitx5-rime-config-instance";
}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("fcitx5-rime-config"));
  app.setStyleSheet(QString::fromUtf8(kStyleSheet));

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
