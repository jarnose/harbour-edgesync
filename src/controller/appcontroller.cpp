#include "appcontroller.h"

#include "export/gpx.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QSaveFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace {

// OpenFreeMap's TileJSON. Not a tile template: the template carries a dated path
// that changes when the planet is rebuilt, so MapTileSource reads the current one
// from this. No account or key; attribution is drawn on the map. See docs/base-map.md.
const QString kDefaultMapTileUrl = QStringLiteral("https://tiles.openfreemap.org/planet");

QString key(const char *k) { return QString::fromLatin1(k); }

QVariant maybe(double v)
{
    return std::isnan(v) || v < 0 ? QVariant() : QVariant(v);
}

QVariant maybeInt(int v)
{
    return v < 0 ? QVariant() : QVariant(v);
}

QString mmss(double seconds)
{
    const int total = static_cast<int>(seconds + 0.5);
    const int h = total / 3600;
    const int m = total % 3600 / 60;
    const int s = total % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

// Averages a series into `buckets` pieces, ignoring missing values, so a chart
// of a three-hour ride shows the shape of it and not one noisy second in 30.
struct Bucketed
{
    QVariantList time;     // seconds since the first record, at the end of each bucket
    QVariantList labels;
    QVariantList distance; // km
    QVariantList speed;    // km/h
    QVariantList heartRate;
    QVariantList power;
    QVariantList cadence;
    QVariantList altitude;
    QVariantList temperature;
    bool hasSpeed = false, hasHr = false, hasPower = false, hasCadence = false, hasAltitude = false,
         hasTemperature = false;
};

Bucketed bucket(const std::vector<Fit::Sample> &s, size_t buckets)
{
    Bucketed out;
    const size_t n = s.size();
    if (n < 2)
        return out;
    const size_t count = std::min(buckets, n);
    const int64_t t0 = s.front().time;

    for (size_t b = 0; b < count; ++b) {
        const size_t from = b * n / count;
        const size_t to = std::max(from + 1, (b + 1) * n / count);

        double sSpeed = 0, sHr = 0, sPower = 0, sCad = 0, sAlt = 0, sTemp = 0;
        int nSpeed = 0, nHr = 0, nPower = 0, nCad = 0, nAlt = 0, nTemp = 0;
        double lastDistance = Fit::kNaN;
        for (size_t i = from; i < to; ++i) {
            const Fit::Sample &x = s[i];
            if (!std::isnan(x.speed)) { sSpeed += x.speed; ++nSpeed; }
            if (x.heartRate > 0) { sHr += x.heartRate; ++nHr; }
            if (x.power >= 0) { sPower += x.power; ++nPower; }
            if (x.cadence >= 0) { sCad += x.cadence; ++nCad; }
            if (!std::isnan(x.altitude)) { sAlt += x.altitude; ++nAlt; }
            if (!std::isnan(x.temperature)) { sTemp += x.temperature; ++nTemp; }
            if (!std::isnan(x.distance)) lastDistance = x.distance;
        }
        const double elapsed = static_cast<double>(s[to - 1].time - t0);
        out.time.append(elapsed);
        out.labels.append(mmss(elapsed));
        out.distance.append(std::isnan(lastDistance) ? QVariant() : QVariant(lastDistance / 1000.0));
        out.speed.append(nSpeed ? QVariant(sSpeed / nSpeed * 3.6) : QVariant());
        out.heartRate.append(nHr ? QVariant(sHr / nHr) : QVariant());
        out.power.append(nPower ? QVariant(sPower / nPower) : QVariant());
        out.cadence.append(nCad ? QVariant(sCad / nCad) : QVariant());
        out.altitude.append(nAlt ? QVariant(sAlt / nAlt) : QVariant());
        out.temperature.append(nTemp ? QVariant(sTemp / nTemp) : QVariant());
        out.hasSpeed |= nSpeed > 0;
        out.hasHr |= nHr > 0;
        out.hasPower |= nPower > 0;
        out.hasCadence |= nCad > 0;
        out.hasAltitude |= nAlt > 0;
        out.hasTemperature |= nTemp > 0;
    }
    return out;
}

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
    m_tick.setInterval(250);
    connect(&m_tick, &QTimer::timeout, this, &AppController::onTick);

    connect(&m_link, &EdgeLink::ready, this, &AppController::onLinkReady);
    connect(&m_link, &EdgeLink::frameReceived, this, &AppController::onFrame);
    connect(&m_link, &EdgeLink::failed, this, &AppController::onLinkFailed);
    connect(&m_link, &EdgeLink::linkLost, this, &AppController::onLinkLost);
    connect(&m_link, &EdgeLink::pairFinished, this, &AppController::onPairFinished);
    connect(&m_link, &EdgeLink::trace, this, [this](const QString &s) { log(s); });

    connect(&m_scanner, &EdgeScanner::deviceUpdated, this,
            [this](const EdgeScanner::Device &d) { m_devices.upsert(d); });
    connect(&m_scanner, &EdgeScanner::errorOccurred, this, [this](const QString &e) { setError(e); });

    QString error;
    m_storeOpen = m_store.open(&error);
    if (!m_storeOpen) {
        setError(tr("Could not open the local database: %1").arg(error));
    } else {
        loadSettings();
        reloadActivities();
    }
    setStatus(QString());
}

AppController::~AppController()
{
    m_tick.stop();
    m_scanner.stopDiscovery();
    m_session.reset();
    m_link.close(false);
}

// ---- state ------------------------------------------------------------------

void AppController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

void AppController::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void AppController::setProgress(double progress)
{
    if (qFuzzyCompare(m_progress + 1, progress + 1))
        return;
    m_progress = progress;
    emit progressChanged();
}

void AppController::setError(const QString &error)
{
    if (m_lastError == error)
        return;
    m_lastError = error;
    if (!error.isEmpty())
        log(QStringLiteral("error: ") + error);
    emit lastErrorChanged();
}

void AppController::clearError()
{
    setError(QString());
}

void AppController::log(const QString &line)
{
    const QString stamped = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) + QLatin1Char(' ') + line;
    m_log.append(stamped);
    while (m_log.size() > 3000)
        m_log.removeFirst();
    // Also on disk, so a log survives the app being closed. Two files of at most
    // 512 kB each: the current one and the one before.
    if (!m_store.dataDir().isEmpty()) {
        const QString path = m_store.dataDir() + QStringLiteral("/sync.log");
        QFile f(path);
        if (f.size() > 512 * 1024) {
            QFile::remove(path + QStringLiteral(".1"));
            f.rename(path + QStringLiteral(".1"));
        }
        QFile out(path);
        if (out.open(QIODevice::Append | QIODevice::Text))
            out.write((stamped + QLatin1Char('\n')).toUtf8());
    }
    emit logChanged();
}

QString AppController::logText() const
{
    return m_log.join(QLatin1Char('\n'));
}

void AppController::clearLog()
{
    m_log.clear();
    emit logChanged();
}

QString AppController::exportLog()
{
    QString error;
    const QString dir = exportDirectory(&error);
    if (dir.isEmpty()) {
        setError(error);
        return QString();
    }
    const QString path = dir + QStringLiteral("/edgesync-log-")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral(".txt");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(logText().toUtf8()) < 0 || !f.commit()) {
        setError(tr("Could not write %1").arg(path));
        return QString();
    }
    return path;
}

// ---- settings ---------------------------------------------------------------

void AppController::loadSettings()
{
    m_deviceAddress = m_store.setting(key("device_address"));
    m_deviceName = m_store.setting(key("device_name"));
    m_lastSyncMs = m_store.setting(key("last_sync")).toLongLong();
    m_archive = m_store.setting(key("archive"), QStringLiteral("0")) == QLatin1String("1");
    m_onlyNew = m_store.setting(key("only_new"), QStringLiteral("0")) == QLatin1String("1");
    m_syncClock = m_store.setting(key("sync_clock"), QStringLiteral("0")) == QLatin1String("1");
    m_disconnect = m_store.setting(key("disconnect"), QStringLiteral("1")) == QLatin1String("1");
    m_trace = m_store.setting(key("trace"), QStringLiteral("0")) == QLatin1String("1");
    m_fastWrites = m_store.setting(key("fast_writes"), QStringLiteral("0")) == QLatin1String("1");
    m_minimalCaps = m_store.setting(key("minimal_caps"), QStringLiteral("0")) == QLatin1String("1");
    m_baseMapEnabled = m_store.setting(key("map_enabled"), QStringLiteral("0")) == QLatin1String("1");
    m_mapTileUrl = m_store.setting(key("map_tile_url"), kDefaultMapTileUrl);
}

void AppController::saveSetting(const QString &name, const QString &value)
{
    if (m_storeOpen)
        m_store.setSetting(name, value);
}

#define EDGE_BOOL_SETTING(Setter, member, name)                                            \
    void AppController::Setter(bool on)                                                    \
    {                                                                                      \
        if (member == on)                                                                  \
            return;                                                                        \
        member = on;                                                                       \
        saveSetting(key(name), on ? QStringLiteral("1") : QStringLiteral("0"));            \
        emit settingsChanged();                                                            \
    }
EDGE_BOOL_SETTING(setArchiveOnEdge, m_archive, "archive")
EDGE_BOOL_SETTING(setOnlyNewFiles, m_onlyNew, "only_new")
EDGE_BOOL_SETTING(setSyncClock, m_syncClock, "sync_clock")
EDGE_BOOL_SETTING(setDisconnectAfterSync, m_disconnect, "disconnect")
EDGE_BOOL_SETTING(setFastWrites, m_fastWrites, "fast_writes")
EDGE_BOOL_SETTING(setMinimalCapabilities, m_minimalCaps, "minimal_caps")
#undef EDGE_BOOL_SETTING

void AppController::setTraceFrames(bool on)
{
    if (m_trace == on)
        return;
    m_trace = on;
    saveSetting(key("trace"), on ? QStringLiteral("1") : QStringLiteral("0"));
    emit settingsChanged();
}

void AppController::setBaseMapEnabled(bool on)
{
    if (m_baseMapEnabled == on)
        return;
    m_baseMapEnabled = on;
    saveSetting(key("map_enabled"), on ? QStringLiteral("1") : QStringLiteral("0"));
    emit baseMapChanged();
}

void AppController::setMapTileUrl(const QString &url)
{
    // An empty field means "the default", not "no map".
    const QString value = url.trimmed().isEmpty() ? kDefaultMapTileUrl : url.trimmed();
    if (value == m_mapTileUrl)
        return;
    m_mapTileUrl = value;
    saveSetting(key("map_tile_url"), value);
    emit baseMapChanged();
}

QString AppController::defaultMapTileUrl() const
{
    return kDefaultMapTileUrl;
}

// ---- the Edge ---------------------------------------------------------------

void AppController::startScan()
{
    m_devices.clear();
    m_scanner.startDiscovery();
    m_scanner.refresh();
    emit scanningChanged();
}

void AppController::stopScan()
{
    m_scanner.stopDiscovery();
    emit scanningChanged();
}

void AppController::chooseDevice(const QString &address, const QString &name)
{
    m_deviceAddress = address;
    m_deviceName = name;
    saveSetting(key("device_address"), address);
    saveSetting(key("device_name"), name);
    emit deviceChanged();
}

void AppController::forgetDevice()
{
    chooseDevice(QString(), QString());
    m_lastSyncMs = 0;
    saveSetting(key("last_sync"), QStringLiteral("0"));
    emit deviceChanged();
}

void AppController::pairDevice(const QString &address, const QString &name)
{
    if (m_busy)
        return;
    const QString path = m_scanner.devicePathForAddress(address);
    if (path.isEmpty()) {
        setError(tr("Bluetooth does not know this device any more. Scan again."));
        return;
    }
    setError(QString());
    setBusy(true);
    setProgress(-1);
    setStatus(tr("Pairing…"));
    m_pairing = true;
    m_pairingAddress = address;
    m_pairingName = name;
    log(QStringLiteral("pairing with %1 (%2)").arg(name, address));
    m_link.pair(path);
}

void AppController::onPairFinished(const QString &error)
{
    if (!m_pairing)
        return;
    m_pairing = false;
    setBusy(false);
    setStatus(QString());
    if (!error.isEmpty()) {
        setError(error);
        return;
    }
    chooseDevice(m_pairingAddress, m_pairingName);
    setStatus(tr("Paired with %1").arg(m_pairingName));
}

// ---- sync -------------------------------------------------------------------

void AppController::syncNow()
{
    if (m_busy) {
        setError(tr("Another operation is still running."));
        return;
    }
    if (!m_storeOpen)
        return;
    if (!hasDevice()) {
        setError(tr("Choose an Edge first."));
        return;
    }
    const QString path = m_scanner.devicePathForAddress(m_deviceAddress);
    if (path.isEmpty()) {
        setError(tr("Bluetooth does not know this Edge any more. Choose it again and pair it."));
        return;
    }

    setError(QString());
    setBusy(true);
    m_newActivities = 0;
    m_noFitFiles = false;
    setProgress(-1);
    setStatus(tr("Connecting to the Edge…"));
    log(QStringLiteral("--- sync with %1 (%2)").arg(m_deviceName, m_deviceAddress));
    m_link.setTracing(m_trace);
    m_link.setWriteWithoutResponse(m_fastWrites);
    m_link.open(path);
}

void AppController::cancelSync()
{
    if (!m_busy)
        return;
    if (m_pairing) {
        m_pairing = false;
        setBusy(false);
        setStatus(QString());
        return;
    }
    finishSync(false, tr("Cancelled."));
}

void AppController::onLinkReady()
{
    if (!m_busy)
        return;
    if (m_session) {
        // The Edge closed the GFDI channel and the transport opened it again.
        // The conversation goes on over the new channel; it does not restart.
        log(QStringLiteral("channel reopened"));
        return;
    }
    log(QStringLiteral("link ready (%1)").arg(m_link.transportName()));
    setStatus(tr("Talking to the Edge…"));

    Gfdi::SessionOptions options;
    options.unixNow = QDateTime::currentMSecsSinceEpoch() / 1000;
    options.utcOffsetSeconds = QDateTime::currentDateTime().offsetFromUtc();
    options.filterOnlyNew = m_onlyNew;
    options.archiveAfterDownload = m_archive;
    options.syncClock = m_syncClock;
    options.minimalCapabilities = m_minimalCaps;
    log(QStringLiteral("options: fastWrites=%1 minimalCapabilities=%2 archive=%3 onlyNew=%4")
                .arg(m_fastWrites).arg(m_minimalCaps).arg(m_archive).arg(m_onlyNew));

    Gfdi::SessionEvents events;
    events.send = [this](const Gfdi::Bytes &b) {
        m_link.send(QByteArray(reinterpret_cast<const char *>(b.data()), static_cast<int>(b.size())));
    };
    events.wantFile = [this](const Gfdi::DirectoryEntry &e) {
        // Not already here, whether it came from this Edge or from an imported file.
        return e.isActivity() && e.size > 0 && !m_store.has(m_deviceAddress, e.timestamp, e.size)
                && !m_store.hasStart(e.timestamp);
    };
    events.fileReceived = [this](const Gfdi::DirectoryEntry &e, const Gfdi::Bytes &data) {
        QString error;
        const qint64 id = m_store.add(m_deviceAddress, e,
                                      QByteArray(reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size())),
                                      &error);
        if (id <= 0) {
            log(QStringLiteral("could not store file %1: %2").arg(e.index).arg(error));
            setError(tr("Could not save an activity: %1").arg(error));
            return false;
        }
        ++m_newActivities;
        log(QStringLiteral("stored file %1 as activity %2 (%3 bytes)").arg(e.index).arg(id).arg(data.size()));
        reloadActivities();
        return true;
    };
    events.deviceInfo = [this](const Gfdi::DeviceInfo &d) {
        log(QStringLiteral("Edge: product %1, software %2, max packet %3, name \"%4\" / \"%5\" / \"%6\"")
                    .arg(d.productNumber).arg(d.softwareVersion).arg(d.maxPacketSize)
                    .arg(QString::fromStdString(d.bluetoothName), QString::fromStdString(d.deviceName),
                         QString::fromStdString(d.deviceModel)));
        if (!d.classicAddress.empty())
            log(QStringLiteral("Edge classic address: ") + QString::fromStdString(d.classicAddress));
    };
    events.directory = [this](const std::vector<Gfdi::DirectoryEntry> &entries) {
        log(QStringLiteral("directory: %1 entries").arg(entries.size()));
        m_noFitFiles = true;
        for (const Gfdi::DirectoryEntry &e : entries)
            if (e.isFit())
                m_noFitFiles = false;
        int shown = 0;
        for (const Gfdi::DirectoryEntry &e : entries) {
            if (++shown > 80) {
                log(QStringLiteral("  ..."));
                break;
            }
            log(QStringLiteral("  #%1 type %2/%3 flags %4/%5 size %6 time %7")
                        .arg(e.index).arg(e.dataType).arg(e.subType).arg(e.specificFlags).arg(e.fileFlags).arg(e.size)
                        .arg(e.timestamp ? QDateTime::fromMSecsSinceEpoch(e.timestamp * 1000).toString(Qt::ISODate)
                                         : QStringLiteral("-")));
        }
    };
    events.progress = [this](const Gfdi::Progress &p) { onProgress(p); };
    events.log = [this](const std::string &s) { log(QStringLiteral("session: ") + QString::fromStdString(s)); };
    events.finished = [this](bool ok, const std::string &message) {
        // The session is still running the call that got us here; end it after
        // that call has returned.
        const QString text = QString::fromStdString(message);
        QTimer::singleShot(0, this, [this, ok, text]() { finishSync(ok, text); });
    };

    m_session = std::make_shared<Gfdi::Session>(options, events);
    m_session->start(nowMs());
    m_tick.start();
}

void AppController::onFrame(const QByteArray &frame)
{
    if (!m_session)
        return;
    if (m_trace)
        log(QStringLiteral("RX frame ") + QString::fromLatin1(frame.toHex()));
    const std::shared_ptr<Gfdi::Session> session = m_session;
    session->onFrame(Gfdi::Bytes(frame.begin(), frame.end()), nowMs());
}

void AppController::onTick()
{
    if (!m_session)
        return;
    const std::shared_ptr<Gfdi::Session> session = m_session;
    session->tick(nowMs());
}

void AppController::onProgress(const Gfdi::Progress &p)
{
    switch (p.phase) {
    case Gfdi::Progress::Handshake:
        setStatus(tr("Talking to the Edge…"));
        setProgress(-1);
        break;
    case Gfdi::Progress::Directory:
        setStatus(tr("Reading the Edge's file list…"));
        setProgress(-1);
        break;
    case Gfdi::Progress::Files: {
        if (p.fileCount <= 0)
            break;
        const double within = p.fileSize > 0 ? static_cast<double>(p.fileBytes) / p.fileSize : 0.0;
        setStatus(tr("Downloading activity %1 of %2…").arg(p.fileNumber).arg(p.fileCount));
        setProgress((std::max(p.fileNumber, 1) - 1 + within) / p.fileCount);
        break;
    }
    case Gfdi::Progress::Finishing:
        setProgress(1);
        break;
    }
}

void AppController::onLinkFailed(const QString &message)
{
    if (!m_busy)
        return;
    finishSync(false, message);
}

void AppController::onLinkLost()
{
    if (!m_busy)
        return;
    finishSync(false, tr("The Bluetooth link to the Edge was lost."));
}

void AppController::finishSync(bool ok, const QString &message)
{
    if (!m_busy)
        return;
    m_tick.stop();
    // Keep the session alive until the call that may be inside it has returned.
    std::shared_ptr<Gfdi::Session> dying = std::move(m_session);
    if (dying && !dying->isFinished())
        dying->abort(message.toStdString());
    QTimer::singleShot(0, this, [dying]() {});

    m_link.close(m_disconnect);

    if (ok) {
        m_lastSyncMs = QDateTime::currentMSecsSinceEpoch();
        saveSetting(key("last_sync"), QString::number(m_lastSyncMs));
        emit deviceChanged();
        setStatus(m_newActivities > 0 ? tr("%n new activities", "", m_newActivities) : tr("Nothing new"));
        log(QStringLiteral("--- sync done, %1 new").arg(m_newActivities));
        if (m_noFitFiles && m_newActivities == 0) {
            // Gadgetbridge's experience with the Edge 520 Plus: no FIT files in the BLE
            // directory means the files are served over Classic Bluetooth instead.
            setError(tr("The Edge lists no activity files over Bluetooth LE. Some older models "
                        "(the Edge 520 Plus, for one) send their files only over classic Bluetooth, which "
                        "this app does not support yet. The log has the Edge's file list."));
        }
    } else {
        setStatus(QString());
        setError(message.isEmpty() ? tr("The sync failed.") : message);
        log(QStringLiteral("--- sync failed: ") + message);
    }
    setProgress(-1);
    setBusy(false);
    reloadActivities();
    emit syncFinished(m_newActivities, ok);
}

// ---- rides ------------------------------------------------------------------

void AppController::reloadActivities()
{
    if (!m_storeOpen)
        return;
    m_activities.setRows(m_store.rows());
    emit activitiesChanged();
}

QVariantMap AppController::latestActivity()
{
    QVariantMap out;
    const QList<ActivityStore::Row> &rows = m_activities.rows();
    if (rows.isEmpty())
        return out;
    const ActivityStore::Row &r = rows.first();
    out.insert(QStringLiteral("id"), r.id);
    out.insert(QStringLiteral("startMs"), r.startTime * 1000);
    out.insert(QStringLiteral("title"), ActivityModel::titleFor(r));
    out.insert(QStringLiteral("distance"), maybe(r.distance));
    out.insert(QStringLiteral("timer"), maybe(r.timer >= 0 ? r.timer : r.elapsed));
    return out;
}

bool AppController::renameActivity(qint64 id, const QString &name)
{
    if (!m_storeOpen || !m_store.setName(id, name))
        return false;
    reloadActivities();
    return true;
}

bool AppController::deleteActivity(qint64 id)
{
    if (!m_storeOpen)
        return false;
    QString error;
    if (!m_store.remove(id, &error)) {
        setError(error.isEmpty() ? tr("Could not delete the activity.") : error);
        return false;
    }
    reloadActivities();
    return true;
}

QVariantMap AppController::activityDetail(qint64 id)
{
    QVariantMap out;
    ActivityStore::Row row;
    if (!m_storeOpen || !m_store.row(id, row))
        return out;

    out.insert(QStringLiteral("id"), row.id);
    out.insert(QStringLiteral("name"), row.name);
    out.insert(QStringLiteral("title"), ActivityModel::titleFor(row));
    out.insert(QStringLiteral("sport"), row.sport);
    out.insert(QStringLiteral("sportLabel"), ActivityModel::sportLabel(row.sport));
    out.insert(QStringLiteral("startMs"), row.startTime * 1000);
    out.insert(QStringLiteral("parsed"), row.parsed);
    out.insert(QStringLiteral("fitPath"), m_store.absolutePath(row));
    out.insert(QStringLiteral("fileSize"), row.fileSize);

    const QByteArray bytes = m_store.readFit(row);
    Fit::Activity a;
    std::string error;
    if (bytes.isEmpty() || !Fit::parseActivity(std::vector<uint8_t>(bytes.begin(), bytes.end()), a, error)) {
        out.insert(QStringLiteral("error"), bytes.isEmpty() ? tr("The file is missing.")
                                                           : QString::fromStdString(error));
        return out;
    }
    const Fit::Summary &s = a.summary;
    out.insert(QStringLiteral("timer"), maybe(s.timer));
    out.insert(QStringLiteral("elapsed"), maybe(s.elapsed));
    out.insert(QStringLiteral("distance"), maybe(s.distance));
    out.insert(QStringLiteral("avgSpeed"), maybe(s.avgSpeed));
    out.insert(QStringLiteral("maxSpeed"), maybe(s.maxSpeed));
    out.insert(QStringLiteral("avgHr"), maybeInt(s.avgHeartRate));
    out.insert(QStringLiteral("maxHr"), maybeInt(s.maxHeartRate));
    out.insert(QStringLiteral("avgPower"), maybeInt(s.avgPower));
    out.insert(QStringLiteral("maxPower"), maybeInt(s.maxPower));
    out.insert(QStringLiteral("normalizedPower"), maybeInt(s.normalizedPower));
    out.insert(QStringLiteral("avgCadence"), maybeInt(s.avgCadence));
    out.insert(QStringLiteral("ascent"), maybe(s.ascent));
    out.insert(QStringLiteral("descent"), maybe(s.descent));
    out.insert(QStringLiteral("minAltitude"), std::isnan(s.minAltitude) ? QVariant() : QVariant(s.minAltitude));
    out.insert(QStringLiteral("maxAltitude"), std::isnan(s.maxAltitude) ? QVariant() : QVariant(s.maxAltitude));
    out.insert(QStringLiteral("avgTemperature"), std::isnan(s.avgTemperature) ? QVariant() : QVariant(s.avgTemperature));
    out.insert(QStringLiteral("calories"), maybeInt(s.calories));
    out.insert(QStringLiteral("trainingStress"), maybe(s.trainingStress));
    out.insert(QStringLiteral("intensityFactor"), maybe(s.intensityFactor));
    out.insert(QStringLiteral("device"), QString::fromStdString(s.productName));
    out.insert(QStringLiteral("crcOk"), a.stats.crcOk);
    out.insert(QStringLiteral("truncated"), a.stats.truncated);
    out.insert(QStringLiteral("records"), static_cast<int>(a.samples.size()));

    // Charts: the series, averaged into at most 300 points.
    const Bucketed b = bucket(a.samples, 300);
    QVariantMap series;
    series.insert(QStringLiteral("time"), b.time);
    series.insert(QStringLiteral("labels"), b.labels);
    series.insert(QStringLiteral("distance"), b.distance);
    series.insert(QStringLiteral("speed"), b.speed);
    series.insert(QStringLiteral("heartRate"), b.heartRate);
    series.insert(QStringLiteral("power"), b.power);
    series.insert(QStringLiteral("cadence"), b.cadence);
    series.insert(QStringLiteral("altitude"), b.altitude);
    series.insert(QStringLiteral("temperature"), b.temperature);
    series.insert(QStringLiteral("hasSpeed"), b.hasSpeed);
    series.insert(QStringLiteral("hasHeartRate"), b.hasHr);
    series.insert(QStringLiteral("hasPower"), b.hasPower);
    series.insert(QStringLiteral("hasCadence"), b.hasCadence);
    series.insert(QStringLiteral("hasAltitude"), b.hasAltitude);
    series.insert(QStringLiteral("hasTemperature"), b.hasTemperature);
    out.insert(QStringLiteral("series"), series);

    // The route, at most ~600 points.
    std::vector<Fit::Sample> positioned;
    for (const Fit::Sample &x : a.samples)
        if (x.hasPosition())
            positioned.push_back(x);
    const std::vector<Fit::Sample> thinned = Fit::thin(positioned, 600);
    QVariantList track;
    for (const Fit::Sample &x : thinned)
        track.append(QVariant(QVariantList{ x.lat, x.lon }));
    out.insert(QStringLiteral("track"), track);

    // For the base map, which projects the route itself: real degrees, as
    // { latitude, longitude }. Only built when the map is on.
    if (m_baseMapEnabled) {
        QVariantList mapTrack;
        for (const Fit::Sample &x : Fit::thin(positioned, 1500)) {
            QVariantMap p;
            p.insert(QStringLiteral("latitude"), x.lat);
            p.insert(QStringLiteral("longitude"), x.lon);
            mapTrack.append(p);
        }
        out.insert(QStringLiteral("mapTrack"), mapTrack);
    }

    QVariantList laps;
    int n = 0;
    for (const Fit::Lap &l : a.laps) {
        QVariantMap m;
        m.insert(QStringLiteral("number"), ++n);
        m.insert(QStringLiteral("timer"), maybe(l.timer));
        m.insert(QStringLiteral("distance"), maybe(l.distance));
        m.insert(QStringLiteral("avgSpeed"), maybe(l.avgSpeed));
        m.insert(QStringLiteral("avgHr"), maybeInt(l.avgHeartRate));
        m.insert(QStringLiteral("avgPower"), maybeInt(l.avgPower));
        m.insert(QStringLiteral("ascent"), maybe(l.ascent));
        laps.append(m);
    }
    out.insert(QStringLiteral("laps"), laps);
    return out;
}

// ---- import -----------------------------------------------------------------

void AppController::importFitFile(const QString &path)
{
    if (!m_storeOpen)
        return;
    // A file picker hands back a file:// URL as often as a path.
    const QString local = path.startsWith(QStringLiteral("file://")) ? QUrl(path).toLocalFile() : path;
    const QString name = QFileInfo(local).fileName();

    QFile file(local);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(tr("Could not open %1: %2").arg(name, file.errorString()));
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    Fit::Activity activity;
    std::string parseError;
    if (!Fit::parseActivity(std::vector<uint8_t>(bytes.begin(), bytes.end()), activity, parseError)) {
        setError(tr("%1 is not a readable FIT activity: %2").arg(name, QString::fromStdString(parseError)));
        return;
    }
    const qint64 start = activity.summary.startTime;
    if (start == 0) {
        // Without a start time there is nothing to tell this ride from another.
        setError(tr("%1 has no start time, so it cannot be filed.").arg(name));
        return;
    }
    if (m_store.hasStart(start)) {
        setError(tr("%1 is already here (a ride with the same start time).").arg(name));
        return;
    }

    // Filed like a ride fetched from an Edge: the same row, the same folder. The
    // "directory entry" is made up from the file; its device is "import".
    Gfdi::DirectoryEntry entry;
    entry.dataType = 128;
    entry.subType = 4;
    entry.timestamp = start;
    entry.size = static_cast<uint32_t>(bytes.size());
    QString error;
    const qint64 id = m_store.add(QStringLiteral("import"), entry, bytes, &error);
    if (id <= 0) {
        setError(tr("Could not save %1: %2").arg(name, error));
        return;
    }
    log(QStringLiteral("imported %1 as activity %2 (%3 bytes, %4 records)")
                .arg(name).arg(id).arg(bytes.size()).arg(activity.samples.size()));
    setError(QString());
    reloadActivities();
    setStatus(tr("Imported %1").arg(name));
}

// ---- export -----------------------------------------------------------------

QString AppController::exportDirectory(QString *error)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/EdgeSync");
    if (!QDir().mkpath(dir)) {
        if (error)
            *error = tr("Could not create %1").arg(dir);
        return QString();
    }
    return dir;
}

QString AppController::baseNameFor(const ActivityStore::Row &row) const
{
    return QDateTime::fromMSecsSinceEpoch(row.startTime * 1000).toString(QStringLiteral("yyyy-MM-dd_HHmm")) + QLatin1Char('_')
            + row.sport;
}

QString AppController::exportFit(qint64 id)
{
    ActivityStore::Row row;
    if (!m_storeOpen || !m_store.row(id, row))
        return QString();
    QString error;
    const QString dir = exportDirectory(&error);
    if (dir.isEmpty()) {
        setError(error);
        return QString();
    }
    const QString path = dir + QLatin1Char('/') + baseNameFor(row) + QStringLiteral(".fit");
    QFile::remove(path);
    if (!QFile::copy(m_store.absolutePath(row), path)) {
        setError(tr("Could not write %1").arg(path));
        return QString();
    }
    return path;
}

QString AppController::exportGpx(qint64 id)
{
    ActivityStore::Row row;
    if (!m_storeOpen || !m_store.row(id, row))
        return QString();
    const QByteArray bytes = m_store.readFit(row);
    Fit::Activity a;
    std::string parseError;
    if (bytes.isEmpty() || !Fit::parseActivity(std::vector<uint8_t>(bytes.begin(), bytes.end()), a, parseError)) {
        setError(tr("The activity file could not be read."));
        return QString();
    }
    const std::string gpx = Export::toGpx(a, ActivityModel::titleFor(row).toStdString());
    if (gpx.empty()) {
        setError(tr("This activity has no GPS track to export."));
        return QString();
    }
    QString error;
    const QString dir = exportDirectory(&error);
    if (dir.isEmpty()) {
        setError(error);
        return QString();
    }
    const QString path = dir + QLatin1Char('/') + baseNameFor(row) + QStringLiteral(".gpx");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(gpx.data(), static_cast<qint64>(gpx.size())) < 0 || !f.commit()) {
        setError(tr("Could not write %1").arg(path));
        return QString();
    }
    return path;
}
