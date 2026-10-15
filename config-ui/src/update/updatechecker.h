#pragma once
#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSettings>
#include <QUrl>

namespace bangke {
// Notification-only protocol. This client never executes downloaded code.
class UpdateChecker : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool automatic READ automatic WRITE setAutomatic NOTIFY automaticChanged)
  Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
 public:
  UpdateChecker(QString target, QString currentVersion, QObject* parent = nullptr,
                QNetworkAccessManager* transport = nullptr);
  bool automatic() const;
  bool checking() const { return checking_; }
  Q_INVOKABLE void setAutomatic(bool enabled);
  Q_INVOKABLE void check(bool manual = true);
  Q_INVOKABLE void skipVersion(const QString& version);
  static int compareVersions(const QString& a, const QString& b, bool* valid = nullptr);
  static QJsonObject selectRelease(const QByteArray& bytes, const QString& target, QString* error);
  static QString feedUrl();
 signals:
  void automaticChanged();
  void checkingChanged();
  void available(const QString& version, const QUrl& url, const QString& notes, bool manual);
  void finished(const QString& message, bool manual);
 private:
  QString target_, current_;
  QNetworkAccessManager network_;
  QNetworkAccessManager* transport_;
  mutable QSettings settings_;
  bool checking_ = false;
};
}  // namespace bangke
