#include "models.h"

#include <QDateTime>
#include <QLocale>

int ActivityModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QString ActivityModel::sportLabel(const QString &key)
{
    if (key == QLatin1String("cycling")) return tr("Cycling");
    if (key == QLatin1String("indoor_cycling")) return tr("Indoor cycling");
    if (key == QLatin1String("mountain_biking")) return tr("Mountain biking");
    if (key == QLatin1String("gravel_cycling")) return tr("Gravel cycling");
    if (key == QLatin1String("cyclocross")) return tr("Cyclocross");
    if (key == QLatin1String("track_cycling")) return tr("Track cycling");
    if (key == QLatin1String("hand_cycling")) return tr("Hand cycling");
    if (key == QLatin1String("e_biking")) return tr("E-biking");
    if (key == QLatin1String("running")) return tr("Running");
    if (key == QLatin1String("treadmill_running")) return tr("Treadmill");
    if (key == QLatin1String("trail_running")) return tr("Trail running");
    if (key == QLatin1String("walking")) return tr("Walking");
    if (key == QLatin1String("hiking")) return tr("Hiking");
    if (key == QLatin1String("swimming")) return tr("Swimming");
    if (key == QLatin1String("training")) return tr("Training");
    if (key == QLatin1String("fitness_equipment")) return tr("Fitness equipment");
    if (key == QLatin1String("rowing")) return tr("Rowing");
    if (key == QLatin1String("cross_country_skiing")) return tr("Cross-country skiing");
    if (key == QLatin1String("alpine_skiing")) return tr("Alpine skiing");
    return tr("Activity");
}

QString ActivityModel::titleFor(const ActivityStore::Row &row)
{
    if (!row.name.isEmpty())
        return row.name;
    return sportLabel(row.sport);
}

QVariant ActivityModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const ActivityStore::Row &r = m_rows.at(index.row());
    switch (role) {
    case IdRole: return r.id;
    case StartRole: return r.startTime * 1000;
    case SportRole: return r.sport;
    case SportLabelRole: return sportLabel(r.sport);
    case NameRole: return r.name;
    case TitleRole: return titleFor(r);
    case MonthRole:
        return QDateTime::fromMSecsSinceEpoch(r.startTime * 1000).toString(QStringLiteral("yyyy-MM"));
    case DistanceRole: return r.distance;
    case TimerRole: return r.timer >= 0 ? r.timer : r.elapsed;
    case ElapsedRole: return r.elapsed;
    case AvgSpeedRole: return r.avgSpeed;
    case AvgHrRole: return r.avgHr;
    case AvgPowerRole: return r.avgPower;
    case AscentRole: return r.ascent;
    case CaloriesRole: return r.calories;
    case ParsedRole: return r.parsed;
    }
    return QVariant();
}

QHash<int, QByteArray> ActivityModel::roleNames() const
{
    return {
        { IdRole, "activityId" },
        { StartRole, "startMs" },
        { SportRole, "sport" },
        { SportLabelRole, "sportLabel" },
        { NameRole, "name" },
        { TitleRole, "title" },
        { MonthRole, "month" },
        { DistanceRole, "distance" },
        { TimerRole, "timer" },
        { ElapsedRole, "elapsed" },
        { AvgSpeedRole, "avgSpeed" },
        { AvgHrRole, "avgHr" },
        { AvgPowerRole, "avgPower" },
        { AscentRole, "ascent" },
        { CaloriesRole, "calories" },
        { ParsedRole, "parsed" },
    };
}

void ActivityModel::setRows(const QList<ActivityStore::Row> &rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
}

int DeviceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

QVariant DeviceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size())
        return QVariant();
    const EdgeScanner::Device &d = m_devices.at(index.row());
    switch (role) {
    case AddressRole: return d.address;
    case NameRole: return d.name;
    case PairedRole: return d.paired;
    case ConnectedRole: return d.connected;
    case RssiRole: return d.rssi;
    }
    return QVariant();
}

QHash<int, QByteArray> DeviceModel::roleNames() const
{
    return {
        { AddressRole, "address" },
        { NameRole, "name" },
        { PairedRole, "paired" },
        { ConnectedRole, "connected" },
        { RssiRole, "rssi" },
    };
}

void DeviceModel::upsert(const EdgeScanner::Device &device)
{
    for (int i = 0; i < m_devices.size(); ++i) {
        if (m_devices.at(i).address == device.address) {
            m_devices[i] = device;
            emit dataChanged(index(i), index(i));
            return;
        }
    }
    beginInsertRows(QModelIndex(), m_devices.size(), m_devices.size());
    m_devices.append(device);
    endInsertRows();
}

void DeviceModel::clear()
{
    beginResetModel();
    m_devices.clear();
    endResetModel();
}
