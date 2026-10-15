#include "edgescanner.h"

#include "bluezcall.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>

namespace {

const QString kBluezService = QStringLiteral("org.bluez");
const QString kObjectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString kAdapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString kDeviceInterface = QStringLiteral("org.bluez.Device1");

// The services Garmin firmware offers for GFDI: the multi-link one, and the two
// older plain ones (see gfdi/multilink.h).
const char *const kGarminServices[] = {
    "6a4e2800-667b-11e3-949a-0800200c9a66",
    "6a4e2401-667b-11e3-949a-0800200c9a66",
    "9b012401-bc30-ce9a-e111-0f67e491abde",
};

} // namespace

EdgeScanner::EdgeScanner(QObject *parent)
    : QObject(parent)
{
    registerBluezManagedObjectTypes();
    qRegisterMetaType<EdgeScanner::Device>();

    QDBusConnection::systemBus().connect(
            kBluezService, QString(), kObjectManagerInterface, QStringLiteral("InterfacesAdded"), this,
            SLOT(onInterfacesAdded(QDBusObjectPath, BluezInterfaceMap)));

    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, &EdgeScanner::refresh);
}

bool EdgeScanner::looksLikeGarmin(const QVariantMap &props)
{
    const QStringList uuids = props.value(QStringLiteral("UUIDs")).toStringList();
    for (const QString &uuid : uuids)
        for (const char *service : kGarminServices)
            if (uuid.compare(QLatin1String(service), Qt::CaseInsensitive) == 0)
                return true;
    const QString name = props.value(QStringLiteral("Alias"), props.value(QStringLiteral("Name"))).toString();
    return name.startsWith(QLatin1String("Edge"), Qt::CaseInsensitive)
            || name.contains(QLatin1String("garmin"), Qt::CaseInsensitive);
}

QString EdgeScanner::adapterPath()
{
    if (!m_adapterPath.isEmpty())
        return m_adapterPath;

    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        emit errorOccurred(tr("Could not reach Bluetooth (org.bluez): %1").arg(reply.error().message()));
        return QString();
    }
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        if (it.value().contains(kAdapterInterface)) {
            m_adapterPath = it.key().path();
            break;
        }
    }
    if (m_adapterPath.isEmpty())
        emit errorOccurred(tr("No Bluetooth adapter found"));
    return m_adapterPath;
}

void EdgeScanner::emitIfGarmin(const QString &objectPath, const BluezInterfaceMap &interfaces)
{
    if (!interfaces.contains(kDeviceInterface))
        return;
    const QVariantMap props = interfaces.value(kDeviceInterface);
    if (!looksLikeGarmin(props))
        return;

    Device d;
    d.objectPath = objectPath;
    d.address = props.value(QStringLiteral("Address")).toString();
    d.name = props.value(QStringLiteral("Alias"), props.value(QStringLiteral("Name"), d.address)).toString();
    d.paired = props.value(QStringLiteral("Paired")).toBool();
    d.connected = props.value(QStringLiteral("Connected")).toBool();
    d.trusted = props.value(QStringLiteral("Trusted")).toBool();
    d.rssi = props.value(QStringLiteral("RSSI")).toInt();
    emit deviceUpdated(d);
}

void EdgeScanner::refresh()
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        emit errorOccurred(tr("Could not list Bluetooth devices: %1").arg(reply.error().message()));
        return;
    }
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
        emitIfGarmin(it.key().path(), it.value());
}

void EdgeScanner::onInterfacesAdded(const QDBusObjectPath &path, const BluezInterfaceMap &interfaces)
{
    emitIfGarmin(path.path(), interfaces);
}

void EdgeScanner::startDiscovery()
{
    const QString path = adapterPath();
    if (path.isEmpty())
        return;
    m_discovering = true;
    m_poll.start();

    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(path, kAdapterInterface, QStringLiteral("StartDiscovery")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        // InProgress means someone else (Settings) is already scanning, which
        // serves us just as well.
        if (reply.isError() && reply.error().name() != QLatin1String("org.bluez.Error.InProgress"))
            emit errorOccurred(tr("Could not start scanning: %1").arg(reply.error().message()));
        w->deleteLater();
    });
}

void EdgeScanner::stopDiscovery()
{
    m_poll.stop();
    if (!m_discovering)
        return;
    m_discovering = false;
    const QString path = adapterPath();
    if (path.isEmpty())
        return;
    BluezCall::async(path, kAdapterInterface, QStringLiteral("StopDiscovery"));
}

QString EdgeScanner::devicePathForAddress(const QString &address)
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid())
        return QString();
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QVariantMap props = it.value().value(kDeviceInterface);
        if (!props.isEmpty()
            && props.value(QStringLiteral("Address")).toString().compare(address, Qt::CaseInsensitive) == 0)
            return it.key().path();
    }
    return QString();
}

bool EdgeScanner::removeDevice(const QString &objectPath, QString *error)
{
    const QString path = adapterPath();
    if (path.isEmpty()) {
        if (error)
            *error = tr("No Bluetooth adapter found");
        return false;
    }
    QDBusReply<void> reply(BluezCall::call(path, kAdapterInterface, QStringLiteral("RemoveDevice"),
                                           { QVariant::fromValue(QDBusObjectPath(objectPath)) }));
    if (!reply.isValid()) {
        if (error)
            *error = reply.error().message();
        return false;
    }
    return true;
}
