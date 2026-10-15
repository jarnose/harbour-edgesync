#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <functional>

namespace Sailfish {
namespace Secrets {
class SecretManager;
}
}

// Thin async wrapper around the Sailfish Secrets API. It holds the Garmin
// Connect sign-in tokens.
//
// The tokens are what lets anyone who holds them act on the person's Garmin
// account, so they are kept in a device-lock-protected, block-encrypted
// collection (sqlcipher), never in plain text on disk and never in the ride
// database. The password is not kept at all: it is used once, to sign in.
// Structure and platform workarounds are those of harbour-otpcove's SecretVault
// and harbour-suuntosync's TokenVault: the SystemInteraction mode is required,
// and a sandboxed app needs the "Secrets" Sailjail permission just to reach the
// daemon.
//
// Callbacks run once, on the calling thread's event loop, when the underlying
// Sailfish::Secrets::Request finishes. KeyVault lives for the whole run of the
// app, so callbacks are not guarded against the vault being destroyed first.
class KeyVault : public QObject
{
    Q_OBJECT
public:
    using ResultCallback = std::function<void(bool ok, const QString &error)>;
    using DataCallback = std::function<void(bool ok, const QByteArray &data, const QString &error)>;

    explicit KeyVault(QObject *parent = nullptr);
    ~KeyVault() override;

    // Creates the vault collection if it doesn't already exist. Safe to call
    // every time the app starts; "already exists" counts as success.
    void ensureCollection(ResultCallback callback);

    void storeSecret(const QString &name, const QByteArray &data, ResultCallback callback);
    void loadSecret(const QString &name, DataCallback callback);
    void deleteSecret(const QString &name, ResultCallback callback);

private:
    Sailfish::Secrets::SecretManager *m_manager;
    static const QString CollectionName;
};
