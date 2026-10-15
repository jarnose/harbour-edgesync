#pragma once

#include "ble/edgescanner.h"
#include "store/activitystore.h"

#include <QAbstractListModel>

// The list of rides, newest first.
class ActivityModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        StartRole,      // milliseconds since the epoch
        SportRole,      // Fit::sportKey
        SportLabelRole, // translated
        NameRole,
        TitleRole,      // the name, or the sport and time of day
        MonthRole,      // "2026-08", for section headings
        DistanceRole,   // metres, -1 if unknown
        TimerRole,      // seconds
        ElapsedRole,
        AvgSpeedRole,   // m/s
        AvgHrRole,
        AvgPowerRole,
        AscentRole,
        CaloriesRole,
        ParsedRole,
    };

    explicit ActivityModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QList<ActivityStore::Row> &rows);
    const QList<ActivityStore::Row> &rows() const { return m_rows; }

    static QString sportLabel(const QString &key);
    static QString titleFor(const ActivityStore::Row &row);

private:
    QList<ActivityStore::Row> m_rows;
};

// The Garmin devices Bluetooth has seen, as the scan page lists them.
class DeviceModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { AddressRole = Qt::UserRole + 1, NameRole, PairedRole, ConnectedRole, RssiRole };

    explicit DeviceModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void upsert(const EdgeScanner::Device &device);
    void clear();

private:
    QList<EdgeScanner::Device> m_devices;
};
