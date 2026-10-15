#pragma once

#include "gfdi/multilink.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>

#include <memory>

// The Edge's GATT link over BlueZ's D-Bus API, as a source and sink of GFDI
// frames.
//
// Unlike harbour-ourasync's RingLink, which blocks inside nested event loops
// so that a sync reads top to bottom, this is plainly asynchronous: a file
// sync is a long conversation the Edge drives as much as we do (it pushes
// SYNCHRONIZATION, FILE_AVAILABLE and every chunk of every file), so it is a
// state machine in gfdi/session.* fed by signals from here.
class EdgeLink : public QObject
{
    Q_OBJECT
public:
    explicit EdgeLink(QObject *parent = nullptr);
    ~EdgeLink() override;

    // Bonds with the device (Device1.Pair). The Edge's GFDI characteristics are
    // only readable on an encrypted link, so this has to happen once. Marks the
    // device trusted so BlueZ reconnects it. Emits pairFinished(); the error is
    // empty on success, and for a device that was already paired.
    void pair(const QString &devicePath, int timeoutMs = 60000);

    // Connects, waits for GATT services, finds the GFDI characteristics,
    // subscribes, and opens the GFDI channel. Emits ready() or failed().
    void open(const QString &devicePath);

    // Stops notifications (which is not optional, see close() in the .cpp) and
    // optionally drops the connection.
    void close(bool disconnectDevice);

    bool isReady() const { return m_transport && m_transport->isReady(); }

    // Queues one GFDI frame for the Edge. Frames are written one at a time, in order.
    void send(const QByteArray &gfdiFrame);

    // Reports every byte written and read through trace().
    void setTracing(bool on) { m_tracing = on; }

    QString transportName() const { return m_transportName; }
    int maxWrite() const { return m_maxWrite; }

signals:
    void pairFinished(const QString &error);
    void ready();
    void frameReceived(const QByteArray &gfdiFrame);
    void failed(const QString &message);
    void linkLost();
    void trace(const QString &line);

private slots:
    void onDevicePropertiesChanged(const QString &interface, const QVariantMap &changed,
                                   const QStringList &invalidated);
    void onNotifyPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                   const QStringList &invalidated);
    void onStepTimeout();

private:
    enum class Step { Idle, Connecting, WaitServices, Subscribing, Running };

    void proceed();
    QString discoverCharacteristics();
    void subscribe();
    void startTransport();
    void fail(const QString &message);
    QVariant deviceProperty(const QString &name) const;
    void pumpWrites();
    void enterStep(Step step, int timeoutMs);

    Step m_step = Step::Idle;
    QTimer m_stepTimer;

    QString m_devicePath;
    QString m_writePath;
    QString m_notifyPath;
    bool m_deviceHooked = false;
    bool m_notifyHooked = false;
    bool m_notifySubscribed = false;
    bool m_connected = false;
    bool m_servicesResolved = false;
    bool m_tracing = false;

    Gfdi::Transport::Kind m_kind = Gfdi::Transport::Kind::MultiLink;
    QString m_transportName;
    int m_maxWrite = 20;
    // Shared, not unique: a frame callback may end the session, and with it this
    // link, while the transport is still executing the call that delivered it.
    std::shared_ptr<Gfdi::Transport> m_transport;

    QList<QByteArray> m_writeQueue;
    bool m_writing = false;
    quint64 m_generation = 0; // bumped by close(): replies from an older session are ignored
};
