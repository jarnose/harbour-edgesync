#pragma once

// A scripted Edge: the other end of a Gfdi::Session, written from the same
// reading of the protocol as the session itself. It can therefore not prove
// the protocol is right (only a real Edge can), but it does prove the state
// machine holds up its end: ordering, acknowledgements, chunk arithmetic,
// retries, and what happens when the other side misbehaves.

#include "check.h"
#include "gfdi/crc.h"
#include "gfdi/messages.h"
#include "gfdi/session.h"

#include <deque>
#include <map>
#include <memory>

namespace Test {

using namespace Gfdi;

struct EdgeFile
{
    DirectoryEntry entry;
    Bytes data;
    DownloadStatus refuse = DownloadOk; // answer the request with this instead of sending the file
    int notReadyFirst = 0;              // answer NotReady this many times first
};

class ScriptedEdge
{
public:
    // What the Edge does on its own initiative.
    bool initiatesSync = false;      // sends SYNCHRONIZATION after SYNC_READY
    bool corruptCrcOnce = false;     // damages the CRC of the first file chunk
    bool skipHandshake = false;      // says nothing at all
    bool mute = false;               // hears the host but stops answering
    size_t chunkSize = 100;

    std::map<uint16_t, EdgeFile> files;

    // Everything the host sent, decoded, in order.
    std::vector<Frame> hostSent;
    std::deque<Bytes> toHost;

    void greet()
    {
        if (skipHandshake)
            return;
        Writer di;
        di.u16(150).u16(4062).u32(3951234567u).u16(1530).u16(375).str("Edge 1040").str("Edge 1040").str("Edge 1040")
                .u8(0);
        toHost.push_back(buildFrame(MsgDeviceInformation, di.bytes()));
        Writer cfg;
        const Bytes caps = {0xFF, 0xFF, 0x3F, 0xEF};
        cfg.u8(static_cast<uint32_t>(caps.size())).raw(caps);
        toHost.push_back(buildFrame(MsgConfiguration, cfg.bytes()));
    }

    // Delivers one frame the host sent.
    void receive(const Bytes &raw)
    {
        Frame f;
        std::string err;
        if (!parseFrame(raw, f, err)) {
            ++badFramesFromHost;
            return;
        }
        hostSent.push_back(f);
        if (mute)
            return;

        switch (f.type) {
        case MsgSystemEvent:
            if (!f.payload.empty() && f.payload[0] == EventSyncReady && initiatesSync) {
                Writer w;
                w.u8(1).u8(4).u32(1u << 5);
                toHost.push_back(buildFrame(MsgSynchronization, w.bytes()));
            }
            break;
        case MsgFilter:
            toHost.push_back(buildResponse(MsgFilter, StatusAck, Bytes{0}));
            break;
        case MsgSupportedFileTypes:
            toHost.push_back(buildResponse(MsgSupportedFileTypes, StatusAck, Bytes{1, 128, 4, 8, 'A', 'C', 'T', 'I', 'V', 'I', 'T', 'Y'}));
            break;
        case MsgDownloadRequest:
            onDownloadRequest(f);
            break;
        case MsgResponse: {
            ResponseView r;
            parseResponse(f.payload, r);
            if (r.originalType == MsgFileTransferData)
                onChunkAck(r);
            break;
        }
        default:
            break;
        }
    }

    // The frames of a given type the host sent.
    int countSent(uint16_t type) const
    {
        int n = 0;
        for (const Frame &f : hostSent)
            if (f.type == type)
                ++n;
        return n;
    }

    int badFramesFromHost = 0;
    int resendsAsked = 0;

private:
    struct Out
    {
        const EdgeFile *file = nullptr;
        Bytes data;
        size_t sent = 0;
        uint16_t crc = 0;
    } m_out;
    bool m_sending = false;
    std::map<uint16_t, int> m_requests;
    bool m_corrupted = false;

    Bytes directoryFile() const
    {
        Writer w;
        for (const auto &kv : files) {
            const DirectoryEntry &e = kv.second.entry;
            w.u16(e.index).u8(e.dataType).u8(e.subType).u16(e.number).u8(e.specificFlags).u8(e.fileFlags)
                    .u32(e.size).u32(e.timestamp == 0 ? 0 : unixToGarmin(e.timestamp));
        }
        return w.take();
    }

    void onDownloadRequest(const Frame &f)
    {
        Reader r(f.payload);
        const uint16_t index = static_cast<uint16_t>(r.u16());
        Bytes content;
        if (index == 0) {
            content = directoryFile();
        } else {
            auto it = files.find(index);
            if (it == files.end()) {
                toHost.push_back(buildResponse(MsgDownloadRequest, StatusAck, Bytes{DownloadIndexUnknown, 0, 0, 0, 0}));
                return;
            }
            const EdgeFile &ef = it->second;
            if (m_requests[index]++ < ef.notReadyFirst) {
                toHost.push_back(buildResponse(MsgDownloadRequest, StatusAck, Bytes{DownloadNotReady, 0, 0, 0, 0}));
                return;
            }
            if (ef.refuse != DownloadOk) {
                toHost.push_back(buildResponse(MsgDownloadRequest, StatusAck, Bytes{ef.refuse, 0, 0, 0, 0}));
                return;
            }
            content = ef.data;
        }
        Writer w;
        w.u8(DownloadOk).u32(static_cast<uint32_t>(content.size()));
        toHost.push_back(buildResponse(MsgDownloadRequest, StatusAck, w.bytes()));

        m_out = Out();
        m_out.data = content;
        m_sending = !content.empty();
        if (m_sending)
            sendChunk();
    }

    void sendChunk()
    {
        const size_t n = std::min(chunkSize, m_out.data.size() - m_out.sent);
        const uint8_t *p = m_out.data.data() + m_out.sent;
        m_out.crc = crc16(p, n, m_out.crc);
        Writer w;
        uint16_t crc = m_out.crc;
        if (corruptCrcOnce && !m_corrupted) {
            crc ^= 0x1234;
            m_corrupted = true;
        }
        w.u8(0).u16(crc).u32(static_cast<uint32_t>(m_out.sent)).raw(p, n);
        toHost.push_back(buildFrame(MsgFileTransferData, w.bytes()));
    }

    void onChunkAck(const ResponseView &r)
    {
        if (!m_sending || r.status != StatusAck)
            return;
        Reader x(r.extra);
        const uint32_t transfer = x.u8();
        const uint32_t next = x.u32();
        if (transfer == TransferResend) {
            ++resendsAsked;
            return;
        }
        m_out.sent = next;
        if (m_out.sent >= m_out.data.size()) {
            m_sending = false;
            return;
        }
        sendChunk();
    }
};

inline Bytes patterned(size_t n, uint8_t seed)
{
    Bytes b(n);
    for (size_t i = 0; i < n; ++i)
        b[i] = static_cast<uint8_t>(seed + i * 7 + (i >> 5));
    return b;
}

inline EdgeFile makeFile(uint16_t index, uint8_t type, uint8_t sub, int64_t unixTime, size_t size, uint8_t seed)
{
    EdgeFile f;
    f.data = patterned(size, seed);
    f.entry.index = index;
    f.entry.dataType = type;
    f.entry.subType = sub;
    f.entry.number = index;
    f.entry.size = static_cast<uint32_t>(size);
    f.entry.timestamp = unixTime;
    return f;
}

// Runs a Session against an Edge until nothing more is said, a deadline passes
// or the session finishes. Time advances only through `advanceMs`.
struct Harness
{
    ScriptedEdge edge;
    SessionOptions options;
    SessionEvents events;
    std::unique_ptr<Session> session;

    int64_t now = 1000;
    bool finished = false;
    bool ok = false;
    std::string message;
    std::vector<std::pair<DirectoryEntry, Bytes>> received;
    std::vector<DirectoryEntry> listing;
    std::vector<std::string> logs;
    std::function<bool(const DirectoryEntry &)> want = [](const DirectoryEntry &e) { return e.isActivity(); };
    std::vector<Progress> progress;

    Harness()
    {
        options.unixNow = 1760000000;
        options.utcOffsetSeconds = 7200;
        events.send = [this](const Bytes &b) { edge.receive(b); };
        events.wantFile = [this](const DirectoryEntry &e) { return want(e); };
        events.fileReceived = [this](const DirectoryEntry &e, const Bytes &d) {
            received.emplace_back(e, d);
            return true;
        };
        events.directory = [this](const std::vector<DirectoryEntry> &d) { listing = d; };
        events.log = [this](const std::string &s) { logs.push_back(s); };
        events.progress = [this](const Progress &p) { progress.push_back(p); };
        events.finished = [this](bool o, const std::string &m) {
            finished = true;
            ok = o;
            message = m;
        };
    }

    void begin()
    {
        session.reset(new Session(options, events));
        session->start(now);
        edge.greet();
    }

    // Delivers frames both ways until quiet.
    void pump()
    {
        for (int guard = 0; guard < 100000 && !edge.toHost.empty(); ++guard) {
            Bytes b = std::move(edge.toHost.front());
            edge.toHost.pop_front();
            session->onFrame(b, now);
        }
    }

    void advanceMs(int64_t ms, int64_t step = 100)
    {
        for (int64_t t = 0; t < ms; t += step) {
            now += step;
            session->tick(now);
            pump();
        }
    }
};

} // namespace Test
