#include "activitystore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {

const char *kConnectionName = "edgesync";

QString twoDigits(int n)
{
    return QStringLiteral("%1").arg(n, 2, 10, QLatin1Char('0'));
}

// The file name keeps what a person looking in the folder wants: the date and
// time of the ride, and the Edge's own index so two rides in one minute differ.
QString fileNameFor(qint64 startTime, int index)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(startTime * 1000, Qt::UTC);
    return QStringLiteral("%1-%2-%3_%4-%5-%6_%7.fit")
            .arg(t.date().year(), 4, 10, QLatin1Char('0'))
            .arg(twoDigits(t.date().month()))
            .arg(twoDigits(t.date().day()))
            .arg(twoDigits(t.time().hour()))
            .arg(twoDigits(t.time().minute()))
            .arg(twoDigits(t.time().second()))
            .arg(index);
}

double num(const double v) { return std::isnan(v) ? -1.0 : v; }

} // namespace

ActivityStore::~ActivityStore()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
    }
}

bool ActivityStore::exec(const QString &sql)
{
    QSqlQuery q(m_db);
    return q.exec(sql);
}

bool ActivityStore::open(QString *error)
{
    m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(m_dataDir + QStringLiteral("/fit"))) {
        if (error)
            *error = QStringLiteral("Cannot create %1").arg(m_dataDir);
        return false;
    }

    m_connection = QLatin1String(kConnectionName);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(m_dataDir + QStringLiteral("/edgesync.db"));
    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }

    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    const bool ok = exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS activities ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " device TEXT NOT NULL,"
            " file_index INTEGER NOT NULL,"
            " file_time INTEGER NOT NULL,"
            " file_size INTEGER NOT NULL,"
            " start_time INTEGER NOT NULL,"
            " sport TEXT NOT NULL,"
            " elapsed REAL, timer REAL, distance REAL, avg_speed REAL, max_speed REAL,"
            " avg_hr INTEGER, max_hr INTEGER, avg_power INTEGER, max_power INTEGER, norm_power INTEGER,"
            " avg_cadence INTEGER, ascent REAL, descent REAL, calories INTEGER, tss REAL,"
            " start_lat REAL, start_lon REAL,"
            " name TEXT NOT NULL DEFAULT '',"
            " fit_path TEXT NOT NULL,"
            " imported_at INTEGER NOT NULL,"
            " parsed INTEGER NOT NULL DEFAULT 1,"
            " UNIQUE(device, file_time, file_size))"))
            && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS activities_start ON activities(start_time DESC)"))
            && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)"));
    if (!ok) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }

    // Columns added after the first release. An older database gets them here, and its
    // rows are given the origin they must have had: a file put in by import was
    // filed under the device "import", everything else came from an Edge.
    bool hasOrigin = false;
    QSqlQuery info(m_db);
    if (info.exec(QStringLiteral("PRAGMA table_info(activities)"))) {
        while (info.next())
            if (info.value(1).toString() == QLatin1String("origin"))
                hasOrigin = true;
    }
    if (!hasOrigin) {
        const bool migrated =
                exec(QStringLiteral("ALTER TABLE activities ADD COLUMN origin TEXT NOT NULL DEFAULT ''"))
                && exec(QStringLiteral("ALTER TABLE activities ADD COLUMN garmin_id INTEGER NOT NULL DEFAULT 0"))
                && exec(QStringLiteral("UPDATE activities SET origin = CASE WHEN device = 'import' THEN 'import' ELSE 'edge' END"));
        if (!migrated) {
            if (error)
                *error = m_db.lastError().text();
            return false;
        }
    }
    return true;
}

bool ActivityStore::has(const QString &device, qint64 fileTime, qint64 fileSize) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM activities WHERE device=? AND file_time=? AND file_size=?"));
    q.addBindValue(device);
    q.addBindValue(fileTime);
    q.addBindValue(fileSize);
    return q.exec() && q.next();
}

bool ActivityStore::hasStart(qint64 startTime) const
{
    if (startTime <= 0)
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT 1 FROM activities WHERE start_time=?"));
    q.addBindValue(startTime);
    return q.exec() && q.next();
}

void ActivityStore::fillFromSummary(Row &r, const Fit::Summary &s) const
{
    if (s.startTime != 0)
        r.startTime = s.startTime;
    r.sport = QString::fromStdString(Fit::sportKey(s.sport, s.subSport));
    r.elapsed = num(s.elapsed);
    r.timer = num(s.timer);
    r.distance = num(s.distance);
    r.avgSpeed = num(s.avgSpeed);
    r.maxSpeed = num(s.maxSpeed);
    r.avgHr = s.avgHeartRate;
    r.maxHr = s.maxHeartRate;
    r.avgPower = s.avgPower;
    r.maxPower = s.maxPower;
    r.normalizedPower = s.normalizedPower;
    r.avgCadence = s.avgCadence;
    r.ascent = num(s.ascent);
    r.descent = num(s.descent);
    r.calories = s.calories;
    r.tss = num(s.trainingStress);
    r.hasStart = !std::isnan(s.startLat) && !std::isnan(s.startLon);
    r.startLat = r.hasStart ? s.startLat : 0;
    r.startLon = r.hasStart ? s.startLon : 0;
}

bool ActivityStore::insert(Row &r)
{
    m_lastError.clear();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
            "INSERT INTO activities (device, file_index, file_time, file_size, start_time, sport,"
            " elapsed, timer, distance, avg_speed, max_speed, avg_hr, max_hr, avg_power, max_power, norm_power,"
            " avg_cadence, ascent, descent, calories, tss, start_lat, start_lon, name, fit_path, imported_at, parsed,"
            " origin, garmin_id)"
            " VALUES (?,?,?,?,?,?, ?,?,?,?,?,?,?,?,?,?, ?,?,?,?,?,?,?,?,?,?,?, ?,?)"));
    q.addBindValue(r.device);
    q.addBindValue(r.fileIndex);
    q.addBindValue(r.fileTime);
    q.addBindValue(r.fileSize);
    q.addBindValue(r.startTime);
    q.addBindValue(r.sport);
    q.addBindValue(r.elapsed);
    q.addBindValue(r.timer);
    q.addBindValue(r.distance);
    q.addBindValue(r.avgSpeed);
    q.addBindValue(r.maxSpeed);
    q.addBindValue(r.avgHr);
    q.addBindValue(r.maxHr);
    q.addBindValue(r.avgPower);
    q.addBindValue(r.maxPower);
    q.addBindValue(r.normalizedPower);
    q.addBindValue(r.avgCadence);
    q.addBindValue(r.ascent);
    q.addBindValue(r.descent);
    q.addBindValue(r.calories);
    q.addBindValue(r.tss);
    q.addBindValue(r.hasStart ? QVariant(r.startLat) : QVariant());
    q.addBindValue(r.hasStart ? QVariant(r.startLon) : QVariant());
    // A default QString is *null*, which QtSql binds as SQL NULL: that breaks the
    // NOT NULL column. An empty name must be bound as an empty string.
    q.addBindValue(r.name.isNull() ? QString(QLatin1String("")) : r.name);
    q.addBindValue(r.fitPath);
    q.addBindValue(r.importedAt);
    q.addBindValue(r.parsed ? 1 : 0);
    q.addBindValue(r.origin);
    q.addBindValue(r.garminId);
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        return false;
    }
    r.id = q.lastInsertId().toLongLong();
    return true;
}

qint64 ActivityStore::add(const QString &device, const Gfdi::DirectoryEntry &entry, const QByteArray &fitBytes,
                          QString *error, const QString &origin, qint64 garminId)
{
    Row r;
    r.device = device;
    r.origin = !origin.isEmpty() ? origin : (device == QLatin1String("import") ? QStringLiteral("import")
                                                                               : QStringLiteral("edge"));
    r.garminId = garminId;
    r.fileIndex = entry.index;
    r.fileTime = entry.timestamp;
    r.fileSize = entry.size;
    r.importedAt = QDateTime::currentMSecsSinceEpoch() / 1000;
    r.startTime = entry.timestamp; // until the file says better
    r.sport = QStringLiteral("other");

    Fit::Activity activity;
    std::string parseError;
    const std::vector<uint8_t> bytes(fitBytes.begin(), fitBytes.end());
    if (Fit::parseSummary(bytes, activity.summary, parseError)) {
        fillFromSummary(r, activity.summary);
    } else {
        r.parsed = false;
    }

    const QString year = QString::number(QDateTime::fromMSecsSinceEpoch(r.startTime * 1000, Qt::UTC).date().year());
    const QString relative = QStringLiteral("fit/%1/%2").arg(year, fileNameFor(r.startTime, r.fileIndex));
    const QString absolute = m_dataDir + QLatin1Char('/') + relative;
    QDir().mkpath(QFileInfo(absolute).absolutePath());

    // QSaveFile writes to a temporary and renames: a power cut halfway leaves
    // no half-written file that would later pass for the ride.
    QSaveFile file(absolute);
    if (!file.open(QIODevice::WriteOnly) || file.write(fitBytes) != fitBytes.size() || !file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(absolute, file.errorString());
        return 0;
    }
    r.fitPath = relative;

    if (!insert(r)) {
        if (error)
            *error = m_lastError.isEmpty() ? QStringLiteral("database insert failed") : m_lastError;
        QFile::remove(absolute);
        return 0;
    }
    return r.id;
}

ActivityStore::Row ActivityStore::rowFromQuery(QSqlQuery &q) const
{
    Row r;
    int i = 0;
    r.id = q.value(i++).toLongLong();
    r.device = q.value(i++).toString();
    r.fileIndex = q.value(i++).toInt();
    r.fileTime = q.value(i++).toLongLong();
    r.fileSize = q.value(i++).toLongLong();
    r.startTime = q.value(i++).toLongLong();
    r.sport = q.value(i++).toString();
    r.elapsed = q.value(i++).toDouble();
    r.timer = q.value(i++).toDouble();
    r.distance = q.value(i++).toDouble();
    r.avgSpeed = q.value(i++).toDouble();
    r.maxSpeed = q.value(i++).toDouble();
    r.avgHr = q.value(i++).toInt();
    r.maxHr = q.value(i++).toInt();
    r.avgPower = q.value(i++).toInt();
    r.maxPower = q.value(i++).toInt();
    r.normalizedPower = q.value(i++).toInt();
    r.avgCadence = q.value(i++).toInt();
    r.ascent = q.value(i++).toDouble();
    r.descent = q.value(i++).toDouble();
    r.calories = q.value(i++).toInt();
    r.tss = q.value(i++).toDouble();
    const QVariant lat = q.value(i++);
    const QVariant lon = q.value(i++);
    r.hasStart = !lat.isNull() && !lon.isNull();
    r.startLat = lat.toDouble();
    r.startLon = lon.toDouble();
    r.name = q.value(i++).toString();
    r.fitPath = q.value(i++).toString();
    r.importedAt = q.value(i++).toLongLong();
    r.parsed = q.value(i++).toInt() != 0;
    r.origin = q.value(i++).toString();
    r.garminId = q.value(i++).toLongLong();
    return r;
}

static const char *kColumns =
        "id, device, file_index, file_time, file_size, start_time, sport, elapsed, timer, distance, avg_speed,"
        " max_speed, avg_hr, max_hr, avg_power, max_power, norm_power, avg_cadence, ascent, descent, calories,"
        " tss, start_lat, start_lon, name, fit_path, imported_at, parsed, origin, garmin_id";

QList<ActivityStore::Row> ActivityStore::rows() const
{
    QList<Row> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM activities ORDER BY start_time DESC, id DESC")
                        .arg(QLatin1String(kColumns))))
        return out;
    while (q.next())
        out.append(rowFromQuery(q));
    return out;
}

bool ActivityStore::row(qint64 id, Row &out) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT %1 FROM activities WHERE id=?").arg(QLatin1String(kColumns)));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return false;
    out = rowFromQuery(q);
    return true;
}

QString ActivityStore::absolutePath(const Row &row) const
{
    return m_dataDir + QLatin1Char('/') + row.fitPath;
}

QByteArray ActivityStore::readFit(const Row &row) const
{
    QFile f(absolutePath(row));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

qint64 ActivityStore::idForStart(qint64 startTime, int toleranceSeconds) const
{
    if (startTime <= 0)
        return 0;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM activities WHERE start_time BETWEEN ? AND ? "
                             "ORDER BY ABS(start_time - ?) LIMIT 1"));
    q.addBindValue(startTime - toleranceSeconds);
    q.addBindValue(startTime + toleranceSeconds);
    q.addBindValue(startTime);
    return q.exec() && q.next() ? q.value(0).toLongLong() : 0;
}

bool ActivityStore::setGarminId(qint64 id, qint64 garminId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE activities SET garmin_id=? WHERE id=?"));
    q.addBindValue(garminId);
    q.addBindValue(id);
    return q.exec();
}

QList<ActivityStore::Row> ActivityStore::notOnGarmin() const
{
    QList<Row> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT %1 FROM activities WHERE garmin_id=0 AND parsed=1 ORDER BY start_time ASC")
                        .arg(QLatin1String(kColumns))))
        return out;
    while (q.next())
        out.append(rowFromQuery(q));
    return out;
}

bool ActivityStore::setName(qint64 id, const QString &name)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE activities SET name=? WHERE id=?"));
    const QString trimmed = name.trimmed();
    q.addBindValue(trimmed.isNull() ? QString(QLatin1String("")) : trimmed);
    q.addBindValue(id);
    return q.exec();
}

bool ActivityStore::remove(qint64 id, QString *error)
{
    Row r;
    if (!row(id, r))
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM activities WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }
    QFile::remove(absolutePath(r));
    return true;
}

int ActivityStore::count() const
{
    QSqlQuery q(m_db);
    return q.exec(QStringLiteral("SELECT COUNT(*) FROM activities")) && q.next() ? q.value(0).toInt() : 0;
}

int ActivityStore::reindex()
{
    int changed = 0;
    for (const Row &old : rows()) {
        const QByteArray bytes = readFit(old);
        if (bytes.isEmpty())
            continue;
        Fit::Summary summary;
        std::string err;
        const std::vector<uint8_t> raw(bytes.begin(), bytes.end());
        Row r = old;
        if (Fit::parseSummary(raw, summary, err)) {
            fillFromSummary(r, summary);
            r.parsed = true;
        } else {
            r.parsed = false;
        }
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral(
                "UPDATE activities SET start_time=?, sport=?, elapsed=?, timer=?, distance=?, avg_speed=?, max_speed=?,"
                " avg_hr=?, max_hr=?, avg_power=?, max_power=?, norm_power=?, avg_cadence=?, ascent=?, descent=?,"
                " calories=?, tss=?, start_lat=?, start_lon=?, parsed=? WHERE id=?"));
        q.addBindValue(r.startTime);
        q.addBindValue(r.sport);
        q.addBindValue(r.elapsed);
        q.addBindValue(r.timer);
        q.addBindValue(r.distance);
        q.addBindValue(r.avgSpeed);
        q.addBindValue(r.maxSpeed);
        q.addBindValue(r.avgHr);
        q.addBindValue(r.maxHr);
        q.addBindValue(r.avgPower);
        q.addBindValue(r.maxPower);
        q.addBindValue(r.normalizedPower);
        q.addBindValue(r.avgCadence);
        q.addBindValue(r.ascent);
        q.addBindValue(r.descent);
        q.addBindValue(r.calories);
        q.addBindValue(r.tss);
        q.addBindValue(r.hasStart ? QVariant(r.startLat) : QVariant());
        q.addBindValue(r.hasStart ? QVariant(r.startLon) : QVariant());
        q.addBindValue(r.parsed ? 1 : 0);
        q.addBindValue(r.id);
        if (q.exec())
            ++changed;
    }
    return changed;
}

QString ActivityStore::setting(const QString &key, const QString &fallback) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key=?"));
    q.addBindValue(key);
    return q.exec() && q.next() ? q.value(0).toString() : fallback;
}

bool ActivityStore::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    return q.exec();
}
