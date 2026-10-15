#include "updatechecker.h"
#include <utility>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace bangke {
namespace {
const char* kOrigin = "https://shellim.openbankai.com";
QList<qulonglong> components(QString s, bool* ok) {
  *ok = QRegularExpression(QStringLiteral("^[0-9]+(?:\\.[0-9]+)*(?:-[0-9]+)?$")).match(s).hasMatch();
  QList<qulonglong> result;
  if (!*ok || s.size() > 100) { *ok = false; return result; }
  s.replace('-', '.');
  for (const auto& part : s.split('.')) {
    bool converted = false;
    auto value = part.toULongLong(&converted);
    if (!converted) { *ok = false; return {}; }
    result.append(value);
  }
  return result;
}
}  // namespace
UpdateChecker::UpdateChecker(QString target, QString currentVersion, QObject* parent, QNetworkAccessManager* transport)
    : QObject(parent), target_(std::move(target)), current_(std::move(currentVersion)),
      network_(this), transport_(transport ? transport : &network_), settings_(QSettings::IniFormat, QSettings::UserScope,
                               QStringLiteral("Bangke"), QStringLiteral("Updates")) {}
QString UpdateChecker::feedUrl() { return QString::fromLatin1(kOrigin) + QStringLiteral("/updates/v1/stable"); }
bool UpdateChecker::automatic() const { settings_.sync(); return settings_.value("automatic", true).toBool(); }
void UpdateChecker::setAutomatic(bool enabled) {
  settings_.setValue("automatic", enabled); settings_.sync(); emit automaticChanged();
}
void UpdateChecker::skipVersion(const QString& version) {
  settings_.setValue(target_ + "/skipped", version); settings_.sync();
}
int UpdateChecker::compareVersions(const QString& a, const QString& b, bool* valid) {
  bool oka = false, okb = false;
  const auto aa = components(a, &oka), bb = components(b, &okb);
  if (valid) *valid = oka && okb;
  if (!oka || !okb) return 0;
  for (int i = 0; i < qMax(aa.size(), bb.size()); ++i) {
    if (aa.value(i) != bb.value(i)) return aa.value(i) > bb.value(i) ? 1 : -1;
  }
  return 0;
}
QJsonObject UpdateChecker::selectRelease(const QByteArray& bytes, const QString& target, QString* error) {
  auto fail = [&](const QString& message) { if (error) *error = message; return QJsonObject(); };
  if (bytes.size() > 256 * 1024) return fail("更新信息过大");
  QJsonParseError parse;
  const auto doc = QJsonDocument::fromJson(bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !doc.isObject()) return fail("更新信息格式错误");
  const auto root = doc.object();
  if (root.value("schemaVersion").toInt() != 1 || root.value("channel").toString() != "stable")
    return fail("暂不支持此更新信息版本");
  const auto release = root.value("releases").toObject().value(target).toObject();
  if (release.isEmpty()) return fail("当前平台暂无发布版本");
  bool valid = false;
  compareVersions(release.value("version").toString(), "0", &valid);
  const QUrl url(release.value("downloadUrl").toString());
  const QUrl origin(QString::fromLatin1(kOrigin));
  // Allow only our HTTPS download handoff; never shell commands or external URLs.
  if (!valid || !url.isValid() || url.scheme() != "https" || url.host() != origin.host() ||
      url.port(-1) != -1 || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
      !QRegularExpression("^/get/[^/\\\\]+\\.(?:exe|msi|pkg|deb|pkg\\.tar\\.zst)$").match(url.path()).hasMatch() ||
      !QRegularExpression("^[a-fA-F0-9]{64}$").match(release.value("sha256").toString()).hasMatch())
    return fail("更新信息校验失败");
  const QHash<QString, QString> patterns = {
    {"windows", "^/get/BangkeSetup-[0-9][A-Za-z0-9._-]*\\.(?:exe|msi)$"},
    {"macos", "^/get/(?:BangkePinyin|Squirrel)-[0-9][A-Za-z0-9._-]*\\.pkg$"},
    {"ubuntu24-amd64", "^/get/fcitx5-rime-ai-predict_[0-9.]+_amd64\\.deb$"},
    {"omarchy-x86_64", "^/get/bangke-pinyin-[0-9.]+-[0-9]+-x86_64\\.pkg\\.tar\\.zst$"},
    {"omarchy-aarch64", "^/get/bangke-pinyin-[0-9.]+-[0-9]+-aarch64\\.pkg\\.tar\\.zst$"},
  };
  if (!patterns.contains(target) || !QRegularExpression(patterns.value(target)).match(url.path()).hasMatch() ||
      release.value("target").toString() != target)
    return fail("安装包与当前平台不匹配");
  if (error) error->clear();
  return release;
}
void UpdateChecker::check(bool manual) {
  if (checking_) return;
  settings_.sync();
  bool valid = false;
  compareVersions(current_, "0", &valid);
  if (target_.isEmpty() || !valid) { emit finished("当前安装无法确定更新通道或版本，请从官网下载完整安装包。", manual); return; }
  const auto now = QDateTime::currentSecsSinceEpoch();
  const auto key = target_ + "/";
  if (!manual && (!automatic() || now - settings_.value(key + "lastSuccess", 0).toLongLong() < 3600 ||
                  now - settings_.value(key + "lastAttempt", 0).toLongLong() < 900)) return;
  settings_.setValue(key + "lastAttempt", now); settings_.sync();
  checking_ = true; emit checkingChanged();
  QNetworkRequest request{QUrl(feedUrl() + "?t=" + QString::number(now / 300))};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
  request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
  request.setRawHeader("Accept", "application/json");
  request.setRawHeader("User-Agent", "BangkeUpdate/1");
  auto* reply = transport_->get(request);
  auto* timer = new QTimer(reply); timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort); timer->start(15000);
  connect(reply, &QNetworkReply::readyRead, reply, [reply]() { if (reply->bytesAvailable() > 256 * 1024) reply->abort(); });
  connect(reply, &QNetworkReply::finished, this, [this, reply, timer, key, now, manual]() {
    timer->stop(); checking_ = false; emit checkingChanged();
    QString error;
    QJsonObject release;
    if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
      error = "暂时无法检查更新，请稍后重试。";
    else release = selectRelease(reply->readAll(), target_, &error);
    reply->deleteLater();
    if (!error.isEmpty()) { emit finished(error, manual); return; }
    settings_.setValue(key + "lastSuccess", now); settings_.sync();
    const QString version = release.value("version").toString();
    if (compareVersions(version, current_) <= 0) { emit finished("当前已是最新版本。", manual); return; }
    if (!manual && version == settings_.value(key + "skipped").toString()) return;
    emit available(version, QUrl(release.value("downloadUrl").toString()), release.value("notes").toString().left(4000), manual);
  });
}
}  // namespace bangke
