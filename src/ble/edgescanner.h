#pragma once

#include "bluezmanagedobjects.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

// Finds Garmin devices through BlueZ's own D-Bus API (org.bluez.*), not Qt
// Bluetooth: Sailfish OS 5.1's SDK target has no pkgconfig(Qt5Bluetooth), so
// QLowEnergyController is not available. Same approach as harbour-ourasync and
// harbour-suuntosync.
//
// A Garmin device is recognised by one of the GATT services its firmware offers
// (the multi-link GFDI service, or the older plain ones) when BlueZ has seen
// them, and otherwise by "edge" or "garmin" in its name. A bike computer in
// pairing mode advertises its model name ("Edge 1040") and little else.
class EdgeScanner : public QObject
{
    Q_OBJECT
public:
    struct Device
    {
        QString objectPath;
        QString address;
        QString name;
        bool paired = false;
        bool connected = false;
        bool trusted = false;
        int rssi = 0; // dBm, 0 when BlueZ has no reading
    };

    explicit EdgeScanner(QObject *parent = nullptr);

    // Re-reads every device BlueZ knows and emits deviceUpdated() for Garmin ones.
    void refresh();

    // Scanning also polls refresh() every couple of seconds: RSSI and the
    // paired/connected flags change without a new object appearing.
    void startDiscovery();
    void stopDiscovery();
    bool discovering() const { return m_discovering; }

    // Object path of the BlueZ device with this MAC address, or an empty string.
    QString devicePathForAddress(const QString &address);

    // Makes BlueZ forget the device and its bond.
    bool removeDevice(const QString &objectPath, QString *error = nullptr);

    static bool looksLikeGarmin(const QVariantMap &deviceProperties);

signals:
    void deviceUpdated(const EdgeScanner::Device &device);
    void errorOccurred(const QString &message);

private slots:
    void onInterfacesAdded(const QDBusObjectPath &path, const BluezInterfaceMap &interfaces);

private:
    QString adapterPath();
    void emitIfGarmin(const QString &objectPath, const BluezInterfaceMap &interfaces);

    QString m_adapterPath;
    QTimer m_poll;
    bool m_discovering = false;
};

Q_DECLARE_METATYPE(EdgeScanner::Device)
