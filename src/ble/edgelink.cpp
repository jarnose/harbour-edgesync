#include "edgelink.h"

#include "bluezcall.h"
#include "bluezmanagedobjects.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>

namespace {

const QString kBluezService = QStringLiteral("org.bluez");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kDeviceInterface = QStringLiteral("org.bluez.Device1");
const QString kServiceInterface = QStringLiteral("org.bluez.GattService1");
const QString kCharInterface = QStringLiteral("org.bluez.GattCharacteristic1");
const QString kObjectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");

const QString kErrInProgress = QStringLiteral("org.bluez.Error.InProgress");
const QString kErrAlreadyConnected = QStringLiteral("org.bluez.Error.AlreadyConnected");
const QString kErrAlreadyExists = QStringLiteral("org.bluez.Error.AlreadyExists");

constexpr int kConnectTimeoutMs = 40000;
constexpr int kServicesTimeoutMs = 25000;
constexpr int kSubscribeTimeoutMs = 15000;

// 6A4E<hhhh>-667B-11E3-949A-0800200C9A66
QString garminUuid(unsigned short part)
{
    return QStringLiteral("6a4e%1-667b-11e3-949a-0800200c9a66").arg(part, 4, 16, QLatin1Char('0'));
}

// "ff 00 1a", for the trace. (QByteArray::toHex() takes no separator before Qt 5.9.)
QString hex(const QByteArray &b)
{
    const QByteArray plain = b.toHex();
    QByteArray spaced;
    spaced.reserve(plain.size() * 3 / 2);
    for (int i = 0; i < plain.size(); i += 2) {
        if (i)
            spaced.append(' ');
        spaced.append(plain.mid(i, 2));
    }
    return QString::fromLatin1(spaced);
}

} // namespace

EdgeLink::EdgeLink(QObject *parent)
    : QObject(parent)
{
    registerBluezManagedObjectTypes();
    m_stepTimer.setSingleShot(true);
    connect(&m_stepTimer, &QTimer::timeout, this, &EdgeLink::onStepTimeout);
}

EdgeLink::~EdgeLink()
{
    close(false);
}

QVariant EdgeLink::deviceProperty(const QString &name) const
{
    if (m_devicePath.isEmpty())
        return QVariant();
    QDBusInterface props(kBluezService, m_devicePath, kPropertiesInterface, QDBusConnection::systemBus());
    QDBusReply<QDBusVariant> reply = props.call(QStringLiteral("Get"), kDeviceInterface, name);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

void EdgeLink::enterStep(Step step, int timeoutMs)
{
    m_step = step;
    if (timeoutMs > 0)
        m_stepTimer.start(timeoutMs);
    else
        m_stepTimer.stop();
}

void EdgeLink::onStepTimeout()
{
    switch (m_step) {
    case Step::Connecting:
        fail(tr("Could not reach the Edge. Is it on, nearby, and not connected to another phone?"));
        break;
    case Step::WaitServices:
        fail(tr("The Edge connected but did not finish Bluetooth service discovery."));
        break;
    case Step::Subscribing:
        fail(tr("The Edge did not open its sync channel."));
        break;
    default:
        break;
    }
}

void EdgeLink::pair(const QString &devicePath, int timeoutMs)
{
    close(false);
    m_devicePath = devicePath;

    // Trusted first: BlueZ then reconnects the Edge on its own and does not ask
    // again before accepting its connections.
    QDBusInterface props(kBluezService, m_devicePath, kPropertiesInterface, QDBusConnection::systemBus());
    props.call(QStringLiteral("Set"), kDeviceInterface, QStringLiteral("Trusted"),
               QVariant::fromValue(QDBusVariant(true)));

    if (deviceProperty(QStringLiteral("Paired")).toBool()) {
        emit pairFinished(QString());
        return;
    }

    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_devicePath, kDeviceInterface, QStringLiteral("Pair"), {}, timeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        if (!reply.isError() || reply.error().name() == kErrAlreadyExists) {
            emit pairFinished(QString());
            return;
        }
        const QString name = reply.error().name();
        // BlueZ sometimes sends an error with no text; the name still says what happened.
        const QString message = reply.error().message().isEmpty() ? name : reply.error().message();
        if (name.endsWith(QLatin1String("AuthenticationFailed")) || name.endsWith(QLatin1String("AuthenticationRejected"))
            || name.endsWith(QLatin1String("AuthenticationCanceled")))
            emit pairFinished(tr("The Edge refused to pair (%1). Open the pairing screen on the Edge "
                                 "(Settings > Connectivity > Phone > Pair Smartphone) and try again.").arg(message));
        else if (name.endsWith(QLatin1String("Timeout")) || name.endsWith(QLatin1String("NoReply")))
            emit pairFinished(tr("Pairing timed out. Put the Edge in pairing mode and keep it next to the phone."));
        else
            emit pairFinished(tr("Pairing failed: %1").arg(message));
    });
}

void EdgeLink::open(const QString &devicePath)
{
    // Every open is a fresh session: close() stops notifications and drops the
    // hooks, so a repeated sync never inherits a doubled subscription.
    close(false);
    m_devicePath = devicePath;

    QDBusConnection::systemBus().connect(
            kBluezService, m_devicePath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
            SLOT(onDevicePropertiesChanged(QString, QVariantMap, QStringList)));
    m_deviceHooked = true;

    m_connected = deviceProperty(QStringLiteral("Connected")).toBool();
    m_servicesResolved = deviceProperty(QStringLiteral("ServicesResolved")).toBool();

    if (m_connected) {
        enterStep(Step::WaitServices, kServicesTimeoutMs);
        proceed();
        return;
    }

    enterStep(Step::Connecting, kConnectTimeoutMs);
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_devicePath, kDeviceInterface, QStringLiteral("Connect"), {}, kConnectTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        // InProgress and AlreadyConnected mean "it is coming" and "it is there".
        if (reply.isError() && reply.error().name() != kErrInProgress
            && reply.error().name() != kErrAlreadyConnected) {
            fail(tr("Could not connect to the Edge: %1").arg(reply.error().message()));
            return;
        }
        m_connected = deviceProperty(QStringLiteral("Connected")).toBool();
        m_servicesResolved = deviceProperty(QStringLiteral("ServicesResolved")).toBool();
        enterStep(Step::WaitServices, kServicesTimeoutMs);
        proceed();
    });
}

void EdgeLink::proceed()
{
    if (m_step != Step::WaitServices && m_step != Step::Connecting)
        return;
    if (!m_connected)
        return;
    if (m_step == Step::Connecting) {
        // The Connect call has not returned yet but the property says so.
        enterStep(Step::WaitServices, kServicesTimeoutMs);
    }
    if (!m_servicesResolved)
        return;
    subscribe();
}

void EdgeLink::onDevicePropertiesChanged(const QString &interface, const QVariantMap &changed,
                                         const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != kDeviceInterface)
        return;
    if (changed.contains(QStringLiteral("Connected"))) {
        m_connected = changed.value(QStringLiteral("Connected")).toBool();
        if (!m_connected) {
            m_servicesResolved = false;
            if (m_step == Step::Running || m_step == Step::Subscribing) {
                const bool wasRunning = m_step == Step::Running;
                close(false);
                if (wasRunning)
                    emit linkLost();
                else
                    emit failed(tr("The Edge dropped the connection."));
            }
            return;
        }
    }
    if (changed.contains(QStringLiteral("ServicesResolved")))
        m_servicesResolved = changed.value(QStringLiteral("ServicesResolved")).toBool();
    proceed();
}

QString EdgeLink::discoverCharacteristics()
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid())
        return tr("Could not list Bluetooth services: %1").arg(reply.error().message());

    const QString devicePrefix = m_devicePath + QLatin1Char('/');
    const BluezObjectMap objects = reply.value();

    // uuid -> path, for every characteristic of this device.
    QMap<QString, QString> characteristics;
    QStringList services;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString path = it.key().path();
        if (!path.startsWith(devicePrefix))
            continue;
        if (it.value().contains(kServiceInterface))
            services << it.value().value(kServiceInterface).value(QStringLiteral("UUID")).toString().toLower();
        if (it.value().contains(kCharInterface))
            characteristics.insert(it.value().value(kCharInterface).value(QStringLiteral("UUID")).toString().toLower(),
                                   path);
    }

    m_writePath.clear();
    m_notifyPath.clear();

    // The multi-link service: the receive characteristic is 2810..2814 and the
    // send one is the same plus 0x10.
    if (services.contains(garminUuid(0x2800))) {
        for (unsigned short part = 0x2810; part <= 0x2814; ++part) {
            const QString recv = characteristics.value(garminUuid(part));
            const QString send = characteristics.value(garminUuid(part + 0x10));
            if (!recv.isEmpty() && !send.isEmpty()) {
                m_notifyPath = recv;
                m_writePath = send;
                m_kind = Gfdi::Transport::Kind::MultiLink;
                m_transportName = QStringLiteral("multi-link %1/%2").arg(part, 4, 16).arg(part + 0x10, 4, 16);
                return QString();
            }
        }
    }
    // Older firmware: GFDI directly on a send and a receive characteristic.
    struct V { const char *send; const char *recv; const char *name; };
    static const V direct[] = {
        { "6a4e4c80-667b-11e3-949a-0800200c9a66", "6a4ecd28-667b-11e3-949a-0800200c9a66", "direct v1" },
        { "df334c80-e6a7-d082-274d-78fc66f85e16", "4acbcd28-7425-868e-f447-915c8f00d0cb", "direct v0" },
    };
    for (const V &v : direct) {
        const QString send = characteristics.value(QLatin1String(v.send));
        const QString recv = characteristics.value(QLatin1String(v.recv));
        if (!send.isEmpty() && !recv.isEmpty()) {
            m_writePath = send;
            m_notifyPath = recv;
            m_kind = Gfdi::Transport::Kind::Direct;
            m_transportName = QLatin1String(v.name);
            return QString();
        }
    }
    return tr("This device does not offer Garmin's sync service. Is it a Garmin Edge, and is it paired?");
}

void EdgeLink::subscribe()
{
    enterStep(Step::Subscribing, kSubscribeTimeoutMs);

    const QString problem = discoverCharacteristics();
    if (!problem.isEmpty()) {
        fail(problem);
        return;
    }

    // The hook must exist exactly once: connect() adds another hook instead of
    // recognising the one already there, and a doubled hook delivers every
    // notification twice.
    if (!m_notifyHooked) {
        QDBusConnection::systemBus().connect(
                kBluezService, m_notifyPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                SLOT(onNotifyPropertiesChanged(QString, QVariantMap, QStringList)));
        m_notifyHooked = true;
    }

    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_notifyPath, kCharInterface, QStringLiteral("StartNotify")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            const QString message = reply.error().message();
            if (reply.error().name() == QLatin1String("org.bluez.Error.NotPermitted")
                || message.contains(QLatin1String("nsufficient"), Qt::CaseInsensitive))
                fail(tr("The Edge refused notifications because the link is not encrypted. Pair the Edge first."));
            else
                fail(tr("Could not subscribe to the Edge: %1").arg(message));
            return;
        }
        m_notifySubscribed = true;
        startTransport();
    });
}

void EdgeLink::startTransport()
{
    // Writes may be as large as the ATT MTU allows less its 3-byte header. The
    // characteristic reports the MTU once the link has negotiated it; older
    // BlueZ has no such property, and 20 bytes is what every link can carry.
    m_maxWrite = 20;
    QDBusInterface props(kBluezService, m_writePath, kPropertiesInterface, QDBusConnection::systemBus());
    QDBusReply<QDBusVariant> mtu = props.call(QStringLiteral("Get"), kCharInterface, QStringLiteral("MTU"));
    if (mtu.isValid()) {
        const int value = mtu.value().variant().toInt();
        if (value > 23)
            m_maxWrite = value - 3;
    }
    emit trace(QStringLiteral("link: %1, max write %2").arg(m_transportName).arg(m_maxWrite));

    Gfdi::Transport::Callbacks cb;
    cb.write = [this](const Gfdi::Bytes &b) {
        m_writeQueue.append(QByteArray(reinterpret_cast<const char *>(b.data()), static_cast<int>(b.size())));
        pumpWrites();
    };
    cb.frame = [this](const Gfdi::Bytes &b) {
        emit frameReceived(QByteArray(reinterpret_cast<const char *>(b.data()), static_cast<int>(b.size())));
    };
    cb.ready = [this]() {
        enterStep(Step::Running, 0);
        emit ready();
    };
    cb.log = [this](const std::string &s) { emit trace(QString::fromStdString(s)); };

    m_transport = std::make_shared<Gfdi::Transport>(m_kind, cb);
    m_transport->setMaxWrite(static_cast<size_t>(m_maxWrite));
    // The Edge answers CLOSE_ALL with the GFDI registration; give that the same
    // patience as the subscription.
    m_transport->start();
}

void EdgeLink::send(const QByteArray &gfdiFrame)
{
    if (!m_transport)
        return;
    if (m_tracing)
        emit trace(QStringLiteral("TX frame ") + hex(gfdiFrame));
    const std::shared_ptr<Gfdi::Transport> transport = m_transport;
    transport->send(Gfdi::Bytes(gfdiFrame.begin(), gfdiFrame.end()));
}

void EdgeLink::pumpWrites()
{
    if (m_writing || m_writeQueue.isEmpty() || m_writePath.isEmpty())
        return;
    m_writing = true;
    const QByteArray data = m_writeQueue.takeFirst();
    if (m_tracing)
        emit trace(QStringLiteral("TX raw   ") + hex(data));

    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_writePath, kCharInterface, QStringLiteral("WriteValue"), { data, QVariantMap() }), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        m_writing = false;
        if (reply.isError()) {
            fail(tr("Could not write to the Edge: %1").arg(reply.error().message()));
            return;
        }
        pumpWrites();
    });
}

void EdgeLink::onNotifyPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                         const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != kCharInterface || !changed.contains(QStringLiteral("Value")) || !m_transport)
        return;
    const QByteArray value = changed.value(QStringLiteral("Value")).toByteArray();
    if (m_tracing)
        emit trace(QStringLiteral("RX raw   ") + hex(value));
    // Frames come out of the transport as callbacks, and a callback may end the
    // session and so this link: keep the transport alive until it returns.
    const std::shared_ptr<Gfdi::Transport> transport = m_transport;
    transport->onNotification(Gfdi::Bytes(value.begin(), value.end()));
}

void EdgeLink::fail(const QString &message)
{
    if (m_step == Step::Idle)
        return;
    close(false);
    emit failed(message);
}

void EdgeLink::close(bool disconnectDevice)
{
    m_stepTimer.stop();
    ++m_generation;

    // Stopping notifications is not optional. Dropping our signal hook only
    // stops us listening; BlueZ still counts this connection as a notifying
    // client of the characteristic and emits PropertiesChanged once per such
    // client. A session that let go without StopNotify made the next one
    // receive every notification twice, which corrupts any reply that spans
    // several of them. Errors are ignored: "not notifying" is what we want.
    if (m_notifyHooked) {
        QDBusConnection::systemBus().disconnect(
                kBluezService, m_notifyPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                SLOT(onNotifyPropertiesChanged(QString, QVariantMap, QStringList)));
        m_notifyHooked = false;
    }
    if (m_notifySubscribed && !m_notifyPath.isEmpty())
        BluezCall::call(m_notifyPath, kCharInterface, QStringLiteral("StopNotify"));
    m_notifySubscribed = false;

    if (m_deviceHooked) {
        QDBusConnection::systemBus().disconnect(
                kBluezService, m_devicePath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                SLOT(onDevicePropertiesChanged(QString, QVariantMap, QStringList)));
        m_deviceHooked = false;
    }
    if (disconnectDevice && !m_devicePath.isEmpty())
        BluezCall::async(m_devicePath, kDeviceInterface, QStringLiteral("Disconnect"));

    m_transport.reset();
    m_writeQueue.clear();
    m_writing = false;
    m_writePath.clear();
    m_notifyPath.clear();
    m_devicePath.clear();
    m_connected = false;
    m_servicesResolved = false;
    m_step = Step::Idle;
}
