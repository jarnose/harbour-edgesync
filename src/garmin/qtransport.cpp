#include "garmin/qtransport.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <memory>

QtHttp::QtHttp(QObject *parent)
    : QObject(parent)
{
}

Garmin::HttpTransport QtHttp::transport()
{
    return [this](const Garmin::HttpRequest &request, const Garmin::HttpDone &done) { send(request, done); };
}

void QtHttp::send(const Garmin::HttpRequest &r, const Garmin::HttpDone &done)
{
    const QUrl url(QString::fromStdString(r.url));
    QNetworkRequest req(url);
    for (const Garmin::Header &h : r.headers)
        req.setRawHeader(QByteArray::fromStdString(h.name), QByteArray::fromStdString(h.value));
    req.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    // A copy: the request is gone when this function returns, and the upload is not done by then.
    const QByteArray body(r.body.data(), static_cast<int>(r.body.size()));
    QNetworkReply *reply = nullptr;
    if (r.method == "GET")
        reply = m_nam.get(req);
    else if (r.method == "POST")
        reply = m_nam.post(req, body);
    else {
        Garmin::HttpResponse bad;
        bad.error = "unsupported method " + r.method;
        done(bad);
        return;
    }

    // The path without the query, which can carry a ticket.
    const QString shown = url.toString(QUrl::RemoveQuery);
    const QString method = QString::fromStdString(r.method);
    emit trace(QStringLiteral("%1 %2").arg(method, shown));

    // A reply is finished once: by the network, or by this timer.
    auto timedOut = std::make_shared<bool>(false);
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply, timedOut]() {
        *timedOut = true;
        reply->abort();
    });
    timer->start(r.timeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply, done, timedOut, method, shown]() {
        Garmin::HttpResponse out;
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        out.body.assign(data.constData(), static_cast<size_t>(data.size()));
        for (const QNetworkReply::RawHeaderPair &p : reply->rawHeaderPairs())
            out.headers.push_back({ p.first.toStdString(), p.second.toStdString() });
        if (out.status == 0) {
            out.error = *timedOut ? std::string("the request timed out") : reply->errorString().toStdString();
        }
        emit trace(QStringLiteral("  -> %1 (%2 bytes)%3")
                           .arg(out.status)
                           .arg(data.size())
                           .arg(out.status == 0 ? QStringLiteral(" ") + QString::fromStdString(out.error) : QString()));
        reply->deleteLater();
        done(out);
    });
}
