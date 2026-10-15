#pragma once

#include "garmin/http.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

// The Garmin client's HttpTransport over QNetworkAccessManager. One instance, so its
// cookie jar carries the sign-in's session from one request to the next.
//
// What it logs (through trace()) is the method, the address without its query and the
// status: never a header or a body, because the bodies hold the password, the
// verification code and the tokens.
class QtHttp : public QObject
{
    Q_OBJECT
public:
    explicit QtHttp(QObject *parent = nullptr);

    Garmin::HttpTransport transport();

signals:
    void trace(const QString &line);

private:
    void send(const Garmin::HttpRequest &request, const Garmin::HttpDone &done);

    QNetworkAccessManager m_nam;
};
