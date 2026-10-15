#include "update/updatechecker.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QDir>
#include <cstdio>
#include <QNetworkReply>
#include <QTimer>
#include <QEventLoop>
#include <QDateTime>
#include <cstring>

class FakeReply : public QNetworkReply {
 public:
  FakeReply(const QNetworkRequest& request, QByteArray body, bool fail, QObject* parent)
      : QNetworkReply(parent), body_(std::move(body)) {
    setRequest(request); setUrl(request.url());
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, fail ? 503 : 200);
    if (fail) setError(QNetworkReply::ServiceUnavailableError, "offline");
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    QTimer::singleShot(0, this, [this]() { emit readyRead(); setFinished(true); emit finished(); });
  }
  void abort() override { setError(QNetworkReply::OperationCanceledError, "aborted"); }
  qint64 bytesAvailable() const override { return body_.size() - offset_ + QNetworkReply::bytesAvailable(); }
 protected:
  qint64 readData(char* data, qint64 max) override {
    const auto count = qMin(max, qint64(body_.size() - offset_));
    if (!count) return -1;
    std::memcpy(data, body_.constData() + offset_, size_t(count)); offset_ += count; return count;
  }
 private:
  QByteArray body_;
  qint64 offset_ = 0;
};
class FakeNetwork : public QNetworkAccessManager {
 public:
  QByteArray body;
  bool fail = false;
  int requests = 0;
 protected:
  QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
    ++requests;
    Q_ASSERT(request.url().host() == "shellim.openbankai.com");
    return new FakeReply(request, body, fail, this);
  }
};
#define REQUIRE(condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; } } while (0)
int main(int argc, char** argv) {
  QTemporaryDir config;
  qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("--live")) {
    bangke::UpdateChecker live("windows", "999.0.0");
    QObject::connect(&live, &bangke::UpdateChecker::finished, &app, [&](const QString& message, bool) {
      const bool ok = message.startsWith(QStringLiteral("当前已是最新版本")) && message.contains("999.0.0");
      std::fprintf(stdout, "%s HTTPS update endpoint: %s\n", ok ? "PASS" : "FAIL", message.toUtf8().constData());
      app.exit(ok ? 0 : 1);
    });
    QTimer::singleShot(20000, &app, [&]() { app.exit(2); });
    live.check(true);
    return app.exec();
  }
  bool valid = false;
  REQUIRE(bangke::UpdateChecker::compareVersions("5.0.19-10", "5.0.19-9", &valid) > 0 && valid);
  REQUIRE(bangke::UpdateChecker::compareVersions("0.4.48.1356", "0.4.48.999") > 0);
  REQUIRE(bangke::UpdateChecker::compareVersions("0.1.10", "0.1.9") > 0);
  REQUIRE(bangke::UpdateChecker::compareVersions("1.0", "1.0.0") == 0);
  REQUIRE(bangke::UpdateChecker::compareVersions("1.0beta", "1.0", &valid) == 0 && !valid);
  auto make = [](const QString& target, const QString& version, const QString& url) {
    const QJsonObject release{{"target", target}, {"version", version}, {"downloadUrl", url}, {"sha256", QString(64, 'a')}, {"notes", "test"}};
    return QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"channel", "stable"}, {"releases", QJsonObject{{target, release}}}}).toJson();
  };
  QString error;
  const QString origin = "https://shellim.openbankai.com/get/";
  REQUIRE(!bangke::UpdateChecker::selectRelease(make("omarchy-aarch64", "5.0.20-1", origin + "bangke-pinyin-5.0.20-1-aarch64.pkg.tar.zst"), "omarchy-aarch64", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease(make("omarchy-aarch64", "5.0.20-1", origin + "bangke-pinyin-5.0.20-1-aarch64.pkg.tar.zst"), "omarchy-x86_64", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease(make("windows", "1.0", "http://shellim.openbankai.com/get/setup.exe"), "windows", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease(make("windows", "1.0", "https://evil.example/get/setup.exe"), "windows", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease(make("windows", "1.0", origin + "linux.deb"), "windows", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease("not json", "windows", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease(make("omarchy-aarch64", "5.0.20-1", origin + "bangke-pinyin-5.0.20-1-x86_64.pkg.tar.zst"), "omarchy-aarch64", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease(make("windows", "1.0", origin + "BangkeSetup-1.0.exe?redirect=evil"), "windows", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease(make("windows", "18446744073709551616", origin + "BangkeSetup-1.0.exe"), "windows", &error).isEmpty());
  REQUIRE(bangke::UpdateChecker::selectRelease(QByteArray(256 * 1024 + 1, 'x'), "windows", &error).isEmpty());
  REQUIRE(!bangke::UpdateChecker::selectRelease(make("windows", "0.4.49.1357", origin + "BangkeSetup-0.4.49.1357-win64.exe"), "windows", &error).isEmpty());
  bangke::UpdateChecker checker("windows", "1.0");
  checker.setAutomatic(false);
  bool started = false;
  QObject::connect(&checker, &bangke::UpdateChecker::checkingChanged, [&]() { started = true; });
  checker.check(false);
  REQUIRE(!started && !checker.checking());
  checker.setAutomatic(true);
  checker.skipVersion("2.0");
  bangke::UpdateChecker another("windows", "1.0");
  REQUIRE(another.automatic());
  FakeNetwork network;
  network.body = make("windows", "2.0", origin + "BangkeSetup-2.0-win64.exe");
  QSettings settings(QSettings::IniFormat, QSettings::UserScope, "Bangke", "Updates");
  settings.clear();
  bangke::UpdateChecker simulated("windows", "1.0", nullptr, &network);
  int offers = 0, errors = 0;
  QObject::connect(&simulated, &bangke::UpdateChecker::available, [&](const QString&, const QUrl&, const QString&, bool) { ++offers; });
  QObject::connect(&simulated, &bangke::UpdateChecker::finished, [&](const QString&, bool manual) { if (manual) ++errors; });
  auto settle = [&]() {
    QEventLoop loop;
    QObject::connect(&simulated, &bangke::UpdateChecker::checkingChanged, &loop, [&]() { if (!simulated.checking()) loop.quit(); });
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    if (simulated.checking()) loop.exec();
  };
  simulated.skipVersion("2.0");
  simulated.check(false); settle();
  REQUIRE(network.requests == 1 && offers == 0);
  simulated.check(false);
  REQUIRE(network.requests == 1); // Hourly successful-check throttle.
  settings.setValue("windows/lastSuccess", QDateTime::currentSecsSinceEpoch() - 3601);
  settings.setValue("windows/lastAttempt", QDateTime::currentSecsSinceEpoch() - 3601);
  settings.sync();
  simulated.check(false); settle();
  REQUIRE(network.requests == 2 && offers == 0); // Eligible again after one hour; skip still applies.
  simulated.setAutomatic(false);
  simulated.check(true); settle();
  REQUIRE(network.requests == 3 && offers == 1); // Manual overrides opt-out and skip.
  network.fail = true;
  simulated.check(true); settle();
  REQUIRE(errors == 1 && !simulated.checking());
  network.fail = false;
  network.body = make("windows", "0.9", origin + "BangkeSetup-0.9-win64.exe");
  simulated.check(true); settle();
  REQUIRE(offers == 1 && errors == 2); // No downgrade offer.
  FakeNetwork ubuntu;
  ubuntu.body = make("ubuntu24-amd64", "5.0.20", origin + "fcitx5-rime-ai-predict_5.0.20_amd64.deb");
  for (const QString current : {QString("5.0.19+main1"), QString("5.0.19+gnome4"), QString("5.0.20"), QString("5.0.21")}) {
    bangke::UpdateChecker local("ubuntu24-amd64", current, nullptr, &ubuntu);
    local.setAutomatic(false);
    local.skipVersion("5.0.20");
    int available = 0;
    QString result;
    bool downloadMatches = false;
    QObject::connect(&local, &bangke::UpdateChecker::available,
      [&](const QString& latest, const QUrl& url, const QString&, bool manual) {
        ++available;
        downloadMatches = manual && latest == "5.0.20" &&
            url == QUrl(origin + "fcitx5-rime-ai-predict_5.0.20_amd64.deb");
      });
    QObject::connect(&local, &bangke::UpdateChecker::finished,
      [&](const QString& message, bool manual) { if (manual) result = message; });
    auto wait = [&]() {
      QEventLoop loop;
      QObject::connect(&local, &bangke::UpdateChecker::checkingChanged, &loop,
        [&]() { if (!local.checking()) loop.quit(); });
      QTimer::singleShot(1000, &loop, &QEventLoop::quit);
      if (local.checking()) loop.exec();
    };
    const int before = ubuntu.requests;
    local.check(true); wait();
    REQUIRE(ubuntu.requests == before + 1 && local.currentVersion() == current);
    if (current.startsWith("5.0.19")) REQUIRE(available == 1 && downloadMatches && result.isEmpty());
    else REQUIRE(available == 0 && result.contains("无需下载") && result.contains(current) && result.contains("5.0.20"));
    local.check(true); wait(); // A second click bypasses successful-check throttling too.
    REQUIRE(ubuntu.requests == before + 2);
    ubuntu.fail = true;
    local.check(true); wait();
    REQUIRE(result.contains("暂时无法检查更新") && !local.checking());
    ubuntu.fail = false;
  }
  REQUIRE(bangke::UpdateChecker::selectRelease(make("ubuntu24-amd64", "5.0.20+main1",
      origin + "fcitx5-rime-ai-predict_5.0.20_amd64.deb"), "ubuntu24-amd64", &error).isEmpty());
  std::puts("PASS Ubuntu test versions query immediately, manual repeat/opt-out/skip override, explicit results, latest installer link and network failures");
  std::puts("PASS versions, target isolation, trusted links, opt-out, skip, hourly throttle, manual override, offline recovery and no downgrade");
}
