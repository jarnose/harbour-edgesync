#pragma once

#include "fit/activity.h"
#include "gfdi/messages.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

// What the phone keeps: every activity file exactly as the Edge sent it, and a
// row of summary numbers for each so the list does not parse a hundred FIT
// files to draw itself.
//
// The FIT file is the record; the row is a cache of it. A row can always be
// rebuilt from its file, and a file that this build cannot read is still kept
// (and flagged) rather than thrown away, because a later build may read it.
class ActivityStore
{
public:
    struct Row
    {
        qint64 id = 0;
        QString device;       // the Edge's Bluetooth address
        int fileIndex = 0;
        qint64 fileTime = 0;  // the directory's timestamp for it, Unix seconds
        qint64 fileSize = 0;
        qint64 startTime = 0; // Unix seconds
        QString sport;        // Fit::sportKey
        double elapsed = -1;  // seconds; -1 where the file did not say (and below)
        double timer = -1;
        double distance = -1;
        double avgSpeed = -1;
        double maxSpeed = -1;
        int avgHr = -1;
        int maxHr = -1;
        int avgPower = -1;
        int maxPower = -1;
        int normalizedPower = -1;
        int avgCadence = -1;
        double ascent = -1;
        double descent = -1;
        int calories = -1;
        double tss = -1;
        double startLat = 0;
        double startLon = 0;
        bool hasStart = false;
        QString name;         // the rider's own title, empty if none
        QString fitPath;      // relative to the data directory
        qint64 importedAt = 0;
        bool parsed = true;
    };

    ActivityStore() = default;
    ~ActivityStore();

    // Opens (and migrates) the database under QStandardPaths::AppDataLocation.
    bool open(QString *error);
    QString dataDir() const { return m_dataDir; }

    // Whether this file of this device is already here. Compared by the
    // directory's timestamp and size, not by index: the Edge reuses indexes.
    bool has(const QString &device, qint64 fileTime, qint64 fileSize) const;

    // Writes the file and its row. Returns the new id, or 0 with `error`.
    qint64 add(const QString &device, const Gfdi::DirectoryEntry &entry, const QByteArray &fitBytes, QString *error);

    QList<Row> rows() const;
    bool row(qint64 id, Row &out) const;
    QString absolutePath(const Row &row) const;
    QByteArray readFit(const Row &row) const;

    bool setName(qint64 id, const QString &name);
    bool remove(qint64 id, QString *error = nullptr);
    int count() const;

    // Rebuilds every row from its file; for after the parser improved.
    int reindex();

    QString setting(const QString &key, const QString &fallback = QString()) const;
    bool setSetting(const QString &key, const QString &value);

private:
    bool exec(const QString &sql);
    void fillFromSummary(Row &r, const Fit::Summary &s) const;
    bool insert(Row &r);
    Row rowFromQuery(class QSqlQuery &q) const;

    QString m_dataDir;
    QString m_connection;
    mutable QSqlDatabase m_db;
};
