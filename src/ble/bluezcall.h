#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QString>
#include <QVariantList>

// Method calls to BlueZ as plain D-Bus messages.
//
// QDBusInterface looks the object up with Introspect first, and when that fails
// (the object has just gone away, or the sandbox's bus proxy refuses it) every
// call on it fails with "Method X with signature "" on interface Y doesn't
// exist", which says nothing of the real cause. A message needs no
// introspection, and any failure comes back as BlueZ's own error.
namespace BluezCall {

inline QDBusMessage message(const QString &path, const QString &interface, const QString &method,
                            const QVariantList &arguments = QVariantList())
{
    QDBusMessage m = QDBusMessage::createMethodCall(QStringLiteral("org.bluez"), path, interface, method);
    if (!arguments.isEmpty())
        m.setArguments(arguments);
    return m;
}

inline QDBusMessage call(const QString &path, const QString &interface, const QString &method,
                         const QVariantList &arguments = QVariantList(), int timeoutMs = 25000)
{
    return QDBusConnection::systemBus().call(message(path, interface, method, arguments), QDBus::Block, timeoutMs);
}

inline QDBusPendingCall async(const QString &path, const QString &interface, const QString &method,
                              const QVariantList &arguments = QVariantList(), int timeoutMs = 25000)
{
    return QDBusConnection::systemBus().asyncCall(message(path, interface, method, arguments), timeoutMs);
}

} // namespace BluezCall
