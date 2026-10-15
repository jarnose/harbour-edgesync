#pragma once

#include "garmin/api.h"
#include "garmin/auth.h"
#include "garmin/qtransport.h"
#include "secrets/keyvault.h"
#include "store/activitystore.h"

#include <QList>
#include <QObject>
#include <QString>

#include <memory>
#include <vector>

// The app's side of Garmin Connect: whether it is signed in, and the three things it
// does with it: download the rides that are on Garmin Connect and not here, send the
// rides that are here and not on Garmin Connect, and send one ride.
//
// The protocol is in src/garmin (Qt-free, tested against a scripted Garmin); this is
// the glue: tokens kept in the Sailfish Secrets vault, the rides in the store, status
// text, and the pacing (a pause between requests, and a stop at the first sign that
// Garmin is refusing the client).
//
// One operation runs at a time. Each is a chain of asynchronous steps, and each step
// first checks that the chain it belongs to is still the current one: signing out or
// cancelling ends a chain by making it stale.
class GarminConnect : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY stateChanged)
    Q_PROPERTY(QString email READ email NOTIFY stateChanged)
    Q_PROPERTY(bool needsMfa READ needsMfa NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY stateChanged) // 0..1, or -1
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(qint64 lastSyncMs READ lastSyncMs NOTIFY stateChanged)
    // How far back to look on Garmin Connect, in days; 0 is everything.
    Q_PROPERTY(int rangeDays READ rangeDays WRITE setRangeDays NOTIFY stateChanged)
    // How many rides here are not known to be on Garmin Connect.
    Q_PROPERTY(int unsentCount READ unsentCount NOTIFY stateChanged)

public:
    GarminConnect(ActivityStore *store, QObject *parent = nullptr);

    bool signedIn() const { return m_tokens.valid(); }
    QString email() const { return m_email; }
    bool needsMfa() const { return m_needsMfa; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QString lastError() const { return m_lastError; }
    qint64 lastSyncMs() const { return m_lastSyncMs; }
    int rangeDays() const { return m_rangeDays; }
    void setRangeDays(int days);
    int unsentCount() const;

    // Reads the saved sign-in from the vault and the settings from the store. Call once.
    void start();

    Q_INVOKABLE void signIn(const QString &email, const QString &password);
    Q_INVOKABLE void submitMfa(const QString &code);
    Q_INVOKABLE void signOut();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearError();

    Q_INVOKABLE void downloadNew();
    Q_INVOKABLE void uploadUnsent();
    Q_INVOKABLE void uploadActivity(qint64 id);

signals:
    void stateChanged();
    // Rides were added or changed in the store.
    void activitiesChanged();
    void logLine(const QString &line);

private:
    struct Run; // the state of one operation

    bool begin(const QString &status);
    void end(const QString &status);
    void fail(const QString &message);
    void handleError(const Garmin::ApiError &error);
    void setStatus(const QString &status, double progress = -1);
    void setError(const QString &error);
    void log(const QString &line) { emit logLine(QStringLiteral("garmin: ") + line); }
    bool current(quint64 generation) const { return generation == m_generation; }
    void later(int ms, quint64 generation, const std::function<void()> &step);

    void saveTokens(const Garmin::Tokens &tokens);
    void onAuthResult(const Garmin::AuthResult &result);

    // The list of recent activities on Garmin Connect, matched against what is here.
    void listAndMatch(quint64 generation, const std::function<void(std::vector<Garmin::RemoteActivity>)> &then);
    void listPage(quint64 generation, int start, std::shared_ptr<std::vector<Garmin::RemoteActivity>> all,
                  const std::function<void(std::vector<Garmin::RemoteActivity>)> &then);
    void downloadStep(quint64 generation, std::shared_ptr<std::vector<Garmin::RemoteActivity>> todo, size_t index,
                      int stored);
    void uploadStep(quint64 generation, std::shared_ptr<QList<ActivityStore::Row>> todo, size_t index, int sent,
                    int already, int refused);
    qint64 cutoff() const;

    // Garmin answers an upload of a file from another device "accepted" (HTTP 202): it is
    // processing it, and whether it keeps it shows only in the list a few seconds later.
    // These look for the rides in the list, by their start time, and link the ones found.
    struct Confirm;
    void confirmAccepted(quint64 generation, std::shared_ptr<Confirm> state,
                         const std::function<void(int confirmed)> &then);
    void confirmNext(quint64 generation, std::shared_ptr<Confirm> state, int index,
                     const std::function<void(int confirmed)> &then);
    void finishUploads(quint64 generation, int sent, int already, int refused);

    ActivityStore *m_store;
    QtHttp m_http;
    KeyVault m_vault;
    Garmin::Auth m_auth;
    Garmin::Api m_api;
    Garmin::Tokens m_tokens;

    QString m_email;
    bool m_needsMfa = false;
    bool m_busy = false;
    QString m_status;
    double m_progress = -1;
    QString m_lastError;
    qint64 m_lastSyncMs = 0;
    int m_rangeDays = 30;
    quint64 m_generation = 1;
    bool m_vaultReady = false;
    QList<ActivityStore::Row> m_acceptedRows; // uploads Garmin accepted and has not yet listed
};
