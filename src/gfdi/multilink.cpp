#include "gfdi/multilink.h"

#include <algorithm>

namespace Gfdi {

namespace {

// Control-channel message types (the byte after the zero handle).
enum : uint8_t {
    RegisterReq = 0,
    RegisterResp = 1,
    CloseHandleReq = 2,
    CloseHandleResp = 3,
    CloseAllReq = 5,
    CloseAllResp = 6,
};

constexpr uint64_t kClientId = 2; // any value; the Edge echoes it back so it can tell clients apart
constexpr uint16_t kServiceGfdi = 1;

} // namespace

Transport::Transport(Kind kind, Callbacks callbacks)
    : m_kind(kind), m_cb(std::move(callbacks))
{
}

Bytes Transport::closeAllRequest()
{
    Writer w;
    w.u8(0).u8(CloseAllReq).u64(kClientId).u16(0);
    return w.take();
}

Bytes Transport::registerGfdiRequest()
{
    Writer w;
    w.u8(0).u8(RegisterReq).u64(kClientId).u16(kServiceGfdi).u8(0 /* not reliable */);
    return w.take();
}

void Transport::start()
{
    m_ready = false;
    m_handle = -1;
    m_cobs = CobsDecoder();
    if (m_kind == Kind::Direct) {
        m_ready = true;
        if (m_cb.ready)
            m_cb.ready();
        return;
    }
    // Start from a clean slate: a previous session (ours, or another app's that
    // died) may have left channels open.
    m_cb.write(closeAllRequest());
}

void Transport::onNotification(const Bytes &value)
{
    if (value.empty())
        return;

    if (m_kind == Kind::Direct) {
        m_cobs.feed(value);
    } else {
        const int handle = value[0];
        if (handle == 0) {
            handleControl(value);
            return;
        }
        if (m_handle < 0 || handle != m_handle) {
            log("notification for unknown channel " + std::to_string(handle));
            return;
        }
        m_cobs.feed(value.data() + 1, value.size() - 1);
    }

    Bytes frame;
    while (m_cobs.next(frame)) {
        if (m_cb.frame)
            m_cb.frame(frame);
    }
}

void Transport::handleControl(const Bytes &value)
{
    Reader r(value);
    r.u8(); // the zero handle
    const uint32_t type = r.u8();
    const uint64_t client = r.u64();
    if (!r.ok())
        return;
    if (client != kClientId) {
        log("control message for another client ignored");
        return;
    }

    switch (type) {
    case CloseAllResp:
        m_handle = -1;
        m_ready = false;
        m_cb.write(registerGfdiRequest());
        break;
    case RegisterResp: {
        const uint32_t service = r.u16();
        const uint32_t status = r.u8();
        const uint32_t handle = r.u8();
        if (!r.ok())
            return;
        if (service != kServiceGfdi)
            return;
        if (status != 0) {
            log("the Edge refused to open the GFDI channel, status " + std::to_string(status));
            return;
        }
        m_handle = static_cast<int>(handle);
        m_ready = true;
        if (m_cb.ready)
            m_cb.ready();
        break;
    }
    case CloseHandleResp: {
        r.u16(); // service
        const uint32_t handle = r.u8();
        if (r.ok() && static_cast<int>(handle) == m_handle) {
            // The Edge closed our channel under us. Open it again.
            log("the Edge closed the GFDI channel, reopening it");
            m_handle = -1;
            m_ready = false;
            m_cb.write(registerGfdiRequest());
        }
        break;
    }
    default:
        log("unhandled control message type " + std::to_string(type));
        break;
    }
}

void Transport::send(const Bytes &gfdiFrame)
{
    if (!m_ready)
        return;
    const Bytes encoded = cobsEncode(gfdiFrame);

    if (m_kind == Kind::Direct) {
        for (size_t pos = 0; pos < encoded.size(); pos += m_maxWrite) {
            const size_t n = std::min(m_maxWrite, encoded.size() - pos);
            m_cb.write(Bytes(encoded.begin() + pos, encoded.begin() + pos + n));
        }
        return;
    }

    // Every piece carries the channel handle in front, so it holds one byte less.
    const size_t room = m_maxWrite - 1;
    for (size_t pos = 0; pos < encoded.size(); pos += room) {
        const size_t n = std::min(room, encoded.size() - pos);
        Bytes piece;
        piece.reserve(n + 1);
        piece.push_back(static_cast<uint8_t>(m_handle));
        piece.insert(piece.end(), encoded.begin() + pos, encoded.begin() + pos + n);
        m_cb.write(piece);
    }
}

} // namespace Gfdi
