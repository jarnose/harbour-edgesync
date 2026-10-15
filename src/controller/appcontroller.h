#pragma once

#include "ble/edgelink.h"
#include "ble/edgescanner.h"
#include "controller/models.h"
#include "gfdi/session.h"
#include "store/activitystore.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <memory>

// Everything the QML pages know about: the Edge that is set up, a sync in
// progress, the rides on the phone, and the settings. One operation runs at a
// time; a second request while `busy` is refused.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged) // 0..1, or -1 when not measurable
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    Q_PROPERTY(bool hasDevice READ hasDevice NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceChanged)
    Q_PROPERTY(QString deviceAddress READ deviceAddress NOTIFY deviceChanged)
    Q_PROPERTY(qint64 lastSyncMs READ lastSyncMs NOTIFY deviceChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)

    Q_PROPERTY(QObject *activities READ activities CONSTANT)
    Q_PROPERTY(QObject *devices READ devices CONSTANT)
    Q_PROPERTY(int activityCount READ activityCount NOTIFY activitiesChanged)

    Q_PROPERTY(bool archiveOnEdge READ archiveOnEdge WRITE setArchiveOnEdge NOTIFY settingsChanged)
    Q_PROPERTY(bool onlyNewFiles READ onlyNewFiles WRITE setOnlyNewFiles NOTIFY settingsChanged)
    Q_PROPERTY(bool syncClock READ syncClock WRITE setSyncClock NOTIFY settingsChanged)
    Q_PROPERTY(bool disconnectAfterSync READ disconnectAfterSync WRITE setDisconnectAfterSync NOTIFY settingsChanged)
    Q_PROPERTY(bool baseMapEnabled READ baseMapEnabled WRITE setBaseMapEnabled NOTIFY baseMapChanged)
    Q_PROPERTY(QString mapTileUrl READ mapTileUrl WRITE setMapTileUrl NOTIFY baseMapChanged)
    Q_PROPERTY(QString defaultMapTileUrl READ defaultMapTileUrl CONSTANT)
    Q_PROPERTY(bool fastWrites READ fastWrites WRITE setFastWrites NOTIFY settingsChanged)
    Q_PROPERTY(bool minimalCapabilities READ minimalCapabilities WRITE setMinimalCapabilities NOTIFY settingsChanged)
    Q_PROPERTY(bool traceFrames READ traceFrames WRITE setTraceFrames NOTIFY settingsChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QString lastError() const { return m_lastError; }

    bool hasDevice() const { return !m_deviceAddress.isEmpty(); }
    QString deviceName() const { return m_deviceName; }
    QString deviceAddress() const { return m_deviceAddress; }
    qint64 lastSyncMs() const { return m_lastSyncMs; }
    bool scanning() const { return m_scanner.discovering(); }

    QObject *activities() { return &m_activities; }
    QObject *devices() { return &m_devices; }
    int activityCount() const { return m_activities.rowCount(); }

    bool archiveOnEdge() const { return m_archive; }
    bool onlyNewFiles() const { return m_onlyNew; }
    bool syncClock() const { return m_syncClock; }
    bool disconnectAfterSync() const { return m_disconnect; }
    bool traceFrames() const { return m_trace; }
    void setArchiveOnEdge(bool on);
    void setOnlyNewFiles(bool on);
    void setSyncClock(bool on);
    void setDisconnectAfterSync(bool on);
    void setTraceFrames(bool on);
    // Experiments with the speed of a download; see docs/protocol.md.
    bool fastWrites() const { return m_fastWrites; }
    bool minimalCapabilities() const { return m_minimalCaps; }
    void setFastWrites(bool on);
    void setMinimalCapabilities(bool on);

    // The base map under the route. Off by default: its tiles come from someone
    // else's servers, which learn the area of every ride that is drawn on it.
    bool baseMapEnabled() const { return m_baseMapEnabled; }
    void setBaseMapEnabled(bool on);
    // Either a TileJSON address or a {z}/{x}/{y} template; see MapTileSource.
    QString mapTileUrl() const { return m_mapTileUrl; }
    void setMapTileUrl(const QString &url);
    QString defaultMapTileUrl() const;

    // ---- the Edge ----
    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    Q_INVOKABLE void chooseDevice(const QString &address, const QString &name);
    Q_INVOKABLE void pairDevice(const QString &address, const QString &name);
    Q_INVOKABLE void forgetDevice();

    // ---- sync ----
    Q_INVOKABLE void syncNow();
    Q_INVOKABLE void cancelSync();
    Q_INVOKABLE void clearError();

    // ---- rides ----
    Q_INVOKABLE QVariantMap activityDetail(qint64 id);
    Q_INVOKABLE QVariantMap latestActivity();
    Q_INVOKABLE bool renameActivity(qint64 id, const QString &name);
    Q_INVOKABLE bool deleteActivity(qint64 id);
    // Write a copy into ~/Documents/EdgeSync and return its path, or "" (lastError says why).
    Q_INVOKABLE QString exportGpx(qint64 id);
    Q_INVOKABLE QString exportFit(qint64 id);

    // Reads a FIT file (a path, or the file:// URL a file picker hands back),
    // keeps it as a ride, and says what happened in `status` or `lastError`.
    // From any device: nothing in FIT is Edge-specific.
    Q_INVOKABLE void importFitFile(const QString &path);

    // ---- diagnostics ----
    Q_INVOKABLE QString logText() const;
    Q_INVOKABLE void clearLog();
    Q_INVOKABLE QString exportLog();
    Q_INVOKABLE QString dataDirectory() const { return m_store.dataDir(); }

signals:
    void busyChanged();
    void statusChanged();
    void progressChanged();
    void lastErrorChanged();
    void deviceChanged();
    void scanningChanged();
    void activitiesChanged();
    void settingsChanged();
    void baseMapChanged();
    void logChanged();
    void syncFinished(int newActivities, bool ok);

private slots:
    void onLinkReady();
    void onFrame(const QByteArray &frame);
    void onLinkFailed(const QString &message);
    void onLinkLost();
    void onPairFinished(const QString &error);
    void onTick();

private:
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void setProgress(double progress);
    void setError(const QString &error);
    void log(const QString &line);
    void reloadActivities();
    void loadSettings();
    void saveSetting(const QString &key, const QString &value);
    void finishSync(bool ok, const QString &message);
    void onProgress(const Gfdi::Progress &p);
    qint64 nowMs() const { return m_clock.elapsed(); }
    QString exportDirectory(QString *error);
    QString baseNameFor(const ActivityStore::Row &row) const;

    ActivityStore m_store;
    EdgeScanner m_scanner;
    EdgeLink m_link;
    ActivityModel m_activities;
    DeviceModel m_devices;

    std::shared_ptr<Gfdi::Session> m_session;
    QTimer m_tick;
    QElapsedTimer m_clock;
    int m_newActivities = 0;
    bool m_noFitFiles = false; // the last directory listed no FIT file at all
    bool m_pairing = false;
    QString m_pairingAddress;
    QString m_pairingName;

    bool m_busy = false;
    QString m_status;
    double m_progress = -1;
    QString m_lastError;
    QStringList m_log;

    QString m_deviceAddress;
    QString m_deviceName;
    qint64 m_lastSyncMs = 0;
    bool m_archive = false;
    bool m_onlyNew = false;
    bool m_syncClock = false;
    bool m_disconnect = true;
    bool m_trace = false;
    bool m_fastWrites = false;
    bool m_minimalCaps = false;
    bool m_baseMapEnabled = false;
    QString m_mapTileUrl;
    bool m_storeOpen = false;
};
