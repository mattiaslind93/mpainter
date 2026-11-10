// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Compares the running version with the newest published release and reports an
// update only when one is genuinely newer. One public request, no identifiers
// sent, and every failure is silent: a notice that cannot be confirmed is not
// shown rather than guessed at.
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <array>
#include <functional>
namespace bts {
// "v0.9.1", "0.9.1" and "0.9.1-alpha" all compare on their numeric part, so a
// pre-release suffix never makes a version look newer than the same number.
inline std::array<int, 3> release_number(QString text)
{
  if (text.startsWith('v') || text.startsWith('V'))
    text.remove(0, 1);
  const auto parts = text.split('-').front().split('.');
  std::array<int, 3> value{0, 0, 0};
  for (int i = 0; i < 3 && i < parts.size(); ++i)
    value[i] = parts.at(i).toInt();
  return value;
}
class UpdateNotice final : public QObject {
 public:
  explicit UpdateNotice(QObject *parent = nullptr) : QObject(parent) {}
  // Called once, with the release name and the page to send the user to.
  std::function<void(const QString &, const QUrl &)> available;
  void check(const QString &current, const QUrl &feed)
  {
    QNetworkRequest request(feed);
    request.setHeader(QNetworkRequest::UserAgentHeader, QString("mPainter/%1").arg(current));
    request.setRawHeader("Accept", "application/vnd.github+json");
    auto *reply = network_.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, current] {
      reply->deleteLater();
      if (reply->error() != QNetworkReply::NoError)
        return;
      const auto object = QJsonDocument::fromJson(reply->readAll()).object();
      if (object.value("draft").toBool() || object.value("prerelease").toBool())
        return;
      const auto tag = object.value("tag_name").toString();
      const auto page = object.value("html_url").toString();
      if (tag.isEmpty() || page.isEmpty())
        return;
      if (release_number(tag) <= release_number(current))
        return;
      if (available)
        available(tag, QUrl(page));
    });
  }

 private:
  QNetworkAccessManager network_;
};
}  // namespace bts
