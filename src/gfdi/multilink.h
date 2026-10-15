#pragma once

#include "gfdi/bytes.h"
#include "gfdi/cobs.h"

#include <functional>
#include <vector>

namespace Gfdi {

// How GFDI frames travel over the Edge's BLE characteristics.
//
// Two transports exist in the field. Older devices put COBS frames straight on
// a send and a receive characteristic ("V1"). Newer ones, which is every Edge
// worth buying now, have a *multi-link* service: one send/receive pair carries
// several logical channels, each tagged by a leading handle byte, and handle 0
// is the control channel on which channels are opened and closed. GFDI is the
// channel this app opens. It is opened *unreliable* (no MLR windowing layer): the
// link layer below already retransmits, and the file-sync conversation is
// request/response with per-chunk CRCs.
//
// This class is the byte-level half and has no Bluetooth in it: the owner feeds
// it every notification and sends whatever it asks to have written.
class Transport
{
public:
    enum class Kind { Direct, MultiLink };

    struct Callbacks
    {
        // Write these bytes to the send characteristic (already handle-prefixed).
        std::function<void(const Bytes &)> write;
        // A whole, COBS-decoded GFDI frame arrived.
        std::function<void(const Bytes &)> frame;
        // The GFDI channel is open and frames can be sent.
        std::function<void()> ready;
        std::function<void(const std::string &)> log;
    };

    Transport(Kind kind, Callbacks callbacks);

    // Largest single characteristic write (ATT MTU minus 3).
    void setMaxWrite(size_t bytes) { m_maxWrite = bytes < 20 ? 20 : bytes; }

    // Starts the conversation. Direct transports are ready at once; multi-link
    // ones close every channel and open GFDI first.
    void start();

    // One notification from the receive characteristic.
    void onNotification(const Bytes &value);

    // Sends one GFDI frame: COBS-encodes it, cuts it to the MTU and writes the
    // pieces. Silently dropped before ready().
    void send(const Bytes &gfdiFrame);

    bool isReady() const { return m_ready; }
    int gfdiHandle() const { return m_handle; }

    // The control messages, exposed for tests.
    static Bytes closeAllRequest();
    static Bytes registerGfdiRequest();

private:
    void handleControl(const Bytes &value);
    void log(const std::string &s) const
    {
        if (m_cb.log)
            m_cb.log(s);
    }

    Kind m_kind;
    Callbacks m_cb;
    CobsDecoder m_cobs;
    size_t m_maxWrite = 20;
    bool m_ready = false;
    int m_handle = -1;
};

} // namespace Gfdi
