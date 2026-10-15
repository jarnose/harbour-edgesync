// Message layouts, and the multi-link transport that carries them.

#include "check.h"
#include "gfdi/cobs.h"
#include "gfdi/messages.h"
#include "gfdi/multilink.h"

using namespace Gfdi;

static void deviceInformation()
{
    Writer w;
    w.u16(150).u16(4062).u32(3951234567u).u16(1530).u16(375).str("Edge 1040").str("Garmin").str("Edge 1040");
    w.u8(1);
    w.raw(Bytes{1, 2, 3, 4, 5, 6});                         // the BLE address
    w.raw(Bytes{0x66, 0x55, 0x44, 0x33, 0x22, 0x11}).u8(1); // the Classic address, little-endian, and a flag
    DeviceInfo d;
    CHECK(parseDeviceInformation(w.bytes(), d));
    CHECK_EQ(d.protocolVersion, 150);
    CHECK_EQ(d.productNumber, 4062);
    CHECK_EQ(d.unitNumber, 3951234567u);
    CHECK_EQ(d.softwareVersion, 1530);
    CHECK_EQ(d.maxPacketSize, 375);
    CHECK_EQ(d.deviceName, std::string("Garmin"));
    CHECK_EQ(d.classicAddress, std::string("11:22:33:44:55:66"));

    // The same without the address block.
    Writer bare;
    bare.u16(150).u16(4062).u32(1).u16(1530).u16(375).str("a").str("b").str("c");
    DeviceInfo plain;
    CHECK(parseDeviceInformation(bare.bytes(), plain));
    CHECK(plain.classicAddress.empty());

    // The answer: a Response to 5024, ACK, then the 1xx protocol flag set.
    const Bytes reply = buildDeviceInformationReply(d, HostInfo());
    Frame f;
    std::string err;
    CHECK(parseFrame(reply, f, err));
    ResponseView rv;
    CHECK(parseResponse(f.payload, rv));
    CHECK_EQ(rv.originalType, MsgDeviceInformation);
    CHECK_EQ(rv.status, StatusAck);
    CHECK_EQ(rv.extra.back(), 1); // protocol 150 / 100 == 1

    DeviceInfo scratch;
    CHECK(!parseDeviceInformation(Bytes{1, 2, 3}, scratch));
}

static void capabilities()
{
    const Bytes ours = ourCapabilities();
    CHECK_EQ(ours.size(), 15u);
    CHECK(hasCapability(ours, CapSync));
    CHECK(hasCapability(ours, CapDeviceInitiatesSync));
    CHECK(hasCapability(ours, CapHostInitiatedSyncRequests));
    CHECK(hasCapability(ours, CapExplicitArchive));
    CHECK(hasCapability(ours, CapMultiLinkService));
    // The ones Gadgetbridge leaves out.
    CHECK(!hasCapability(ours, 28)); // GPS_EPHEMERIS_DOWNLOAD
    CHECK(!hasCapability(ours, 22)); // LIVETRACK
    CHECK(!hasCapability(ours, 23)); // LIVETRACK_AUTO_START
    CHECK(!hasCapability(ours, 104));
    CHECK(!hasCapability(ours, 111));
    CHECK(hasCapability(ours, 112));
    CHECK(hasCapability(ours, 113));
    CHECK(!hasCapability(ours, 114));
    CHECK(!hasCapability(ours, 119));
    CHECK(!hasCapability(ours, 5000));

    // The short list: every bit of it is one of ours, and the ones a sync hangs on are there.
    const Bytes small = minimalCapabilities();
    CHECK_EQ(small.size(), ours.size());
    for (unsigned bit = 0; bit < small.size() * 8; ++bit)
        if (hasCapability(small, bit))
            CHECK(hasCapability(ours, bit));
    CHECK(hasCapability(small, CapSync));
    CHECK(hasCapability(small, CapDeviceInitiatesSync));
    CHECK(hasCapability(small, CapHostInitiatedSyncRequests));
    CHECK(hasCapability(small, CapExplicitArchive));
    CHECK(hasCapability(small, CapMultiLinkService));
    CHECK(hasCapability(small, 44)); // SPORT_CYCLING
    CHECK(!hasCapability(small, 8)); // FIND_MY_PHONE
    CHECK(!hasCapability(small, 26)); // WEATHER_CONDITIONS
    CHECK(!hasCapability(small, 6)); // GNCS, notifications
    int count = 0;
    for (unsigned bit = 0; bit < small.size() * 8; ++bit)
        count += hasCapability(small, bit) ? 1 : 0;
    CHECK_EQ(count, 16);

    const Bytes frame = buildConfiguration(ours);
    Frame f;
    std::string err;
    CHECK(parseFrame(frame, f, err));
    CHECK_EQ(f.type, MsgConfiguration);
    Bytes back;
    CHECK(parseConfiguration(f.payload, back));
    CHECK(back == ours);
    CHECK(!parseConfiguration(Bytes{9, 1, 2}, back)); // claims 9 bytes, has 2
}

static void directory()
{
    // Two entries by hand: an activity, and a file with no date.
    Writer w;
    w.u16(11).u8(128).u8(4).u16(2).u8(0).u8(0x00).u32(52437).u32(1139678400u); // 2026-ish
    w.u16(12).u8(8).u8(255).u16(0).u8(0).u8(0x10).u32(88).u32(0);
    std::vector<DirectoryEntry> e;
    CHECK(parseDirectory(w.bytes(), e));
    CHECK_EQ(e.size(), 2u);
    CHECK_EQ(e[0].index, 11);
    CHECK(e[0].isActivity());
    CHECK(e[0].isFit());
    CHECK_EQ(e[0].size, 52437u);
    CHECK_EQ(e[0].timestamp, 1139678400LL + kGarminEpoch);
    CHECK(!e[1].isFit());
    CHECK_EQ(e[1].timestamp, 0);
    CHECK_EQ(e[1].fileFlags, 0x10);

    Bytes odd = w.bytes();
    odd.pop_back();
    CHECK(!parseDirectory(odd, e));
    CHECK(parseDirectory(Bytes(), e) && e.empty());

    DirectoryEntry a;
    Writer fa;
    fa.u16(5).u8(128).u8(4).u16(1).u8(0).u8(0).u32(10).u32(1000).u8(1);
    CHECK(parseFileAvailable(fa.bytes(), a));
    CHECK_EQ(a.index, 5);
    CHECK(!parseFileAvailable(Bytes{1, 2}, a));
}

static void downloadAndTransfer()
{
    // DOWNLOAD_REQUEST for index 0x0102, offset 0, size 0, new, crc seed 0.
    const Bytes req = buildDownloadRequest(0x0102, 0, 0, 0, RequestNew);
    Frame f;
    std::string err;
    CHECK(parseFrame(req, f, err));
    CHECK_EQ(f.type, MsgDownloadRequest);
    const Bytes expect = {0x02, 0x01, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0};
    CHECK(f.payload == expect);

    // Response: ACK, OK, 1234 bytes.
    ResponseView rv;
    rv.status = StatusAck;
    Writer ok;
    ok.u8(DownloadOk).u32(1234);
    rv.extra = ok.take();
    DownloadResponse d;
    CHECK(parseDownloadResponse(rv, d));
    CHECK_EQ(d.fileSize, 1234u);
    rv.status = StatusNak;
    CHECK(!parseDownloadResponse(rv, d));

    Writer c;
    c.u8(0).u16(0xBEEF).u32(700).raw(Bytes{1, 2, 3});
    FileChunk chunk;
    CHECK(parseFileTransferData(c.bytes(), chunk));
    CHECK_EQ(chunk.crc, 0xBEEF);
    CHECK_EQ(chunk.offset, 700u);
    CHECK_EQ(chunk.data.size(), 3u);

    // The acknowledgement carries status, transfer status and the next offset.
    const Bytes ack = buildFileTransferReply(StatusAck, TransferOk, 703);
    CHECK(parseFrame(ack, f, err));
    CHECK(parseResponse(f.payload, rv));
    CHECK_EQ(rv.originalType, MsgFileTransferData);
    CHECK(rv.extra == (Bytes{0, 0xBF, 0x02, 0, 0}));

    const Bytes flags = buildSetFileFlags(0x0305, kFlagArchive);
    CHECK(parseFrame(flags, f, err));
    CHECK(f.payload == (Bytes{0x05, 0x03, 0x10}));
}

static void synchronization()
{
    Synchronization s;
    Writer w4;
    w4.u8(1).u8(4).u32(0x20);
    CHECK(parseSynchronization(w4.bytes(), s));
    CHECK_EQ(s.type, 1);
    CHECK_EQ(s.fileTypeMask, 0x20u);
    Writer w8;
    w8.u8(0).u8(8).u64(1ull << 40);
    CHECK(parseSynchronization(w8.bytes(), s));
    CHECK_EQ(s.fileTypeMask, 1ull << 40);
    Writer w3;
    w3.u8(0).u8(3).u16(1).u8(0);
    CHECK(!parseSynchronization(w3.bytes(), s));
}

static void systemMessages()
{
    Frame f;
    std::string err;
    CHECK(parseFrame(buildSystemEvent(EventSyncReady), f, err));
    CHECK(f.payload == (Bytes{8, 0}));
    CHECK(parseFrame(buildFilter(FilterOnlyNew), f, err));
    CHECK(f.payload == (Bytes{3}));
    CHECK(parseFrame(buildSupportedFileTypesRequest(), f, err));
    CHECK_EQ(f.type, MsgSupportedFileTypes);
    CHECK(f.payload.empty());
    CHECK(parseFrame(buildDeviceSettings(true, false, false), f, err));
    CHECK(f.payload == (Bytes{3, 6, 1, 1, 7, 1, 0, 8, 1, 0}));

    std::vector<SupportedFileType> types;
    Writer w;
    w.u8(2).u8(128).u8(4).str("ACTIVITY").u8(128).u8(32).str("MONITOR");
    CHECK(parseSupportedFileTypes(w.bytes(), types));
    CHECK_EQ(types.size(), 2u);
    CHECK_EQ(types[1].name, std::string("MONITOR"));

    // The time reply is five 32-bit words.
    CHECK(parseFrame(buildCurrentTimeReply(5, 1000, -3600), f, err));
    ResponseView rv;
    CHECK(parseResponse(f.payload, rv));
    CHECK_EQ(rv.extra.size(), 20u);
    Reader r(rv.extra);
    CHECK_EQ(r.u32(), 5);
    CHECK_EQ(r.u32(), 1000);
    CHECK_EQ(static_cast<int32_t>(r.u32()), -3600);

    CHECK_EQ(garminToUnix(0), 631065600LL);
    CHECK_EQ(unixToGarmin(631065600 + 77), 77u);
}

// ---- the multi-link transport ---------------------------------------------

struct Wire
{
    std::vector<Bytes> written;
    std::vector<Bytes> frames;
    int readyCount = 0;
    Transport::Callbacks callbacks()
    {
        Transport::Callbacks cb;
        cb.write = [this](const Bytes &b) { written.push_back(b); };
        cb.frame = [this](const Bytes &b) { frames.push_back(b); };
        cb.ready = [this]() { ++readyCount; };
        return cb;
    }
};

static Bytes control(uint8_t type, uint16_t service, uint8_t status, uint8_t handle)
{
    Writer w;
    w.u8(0).u8(type).u64(2).u16(service).u8(status).u8(handle).u8(0);
    return w.take();
}

static void multiLink()
{
    Wire wire;
    Transport t(Transport::Kind::MultiLink, wire.callbacks());
    t.setMaxWrite(20);
    t.start();
    // First thing out: CLOSE_ALL for client 2.
    CHECK_EQ(wire.written.size(), 1u);
    CHECK(wire.written[0] == (Bytes{0, 5, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0}));
    CHECK(wire.written[0] == Transport::closeAllRequest());
    CHECK(!t.isReady());

    // Sending before the channel is open does nothing.
    t.send(buildFrame(MsgSystemEvent, Bytes{8, 0}));
    CHECK_EQ(wire.written.size(), 1u);

    // CLOSE_ALL_RESP -> we register GFDI (service 1, unreliable).
    t.onNotification(control(6, 0, 0, 0));
    CHECK_EQ(wire.written.size(), 2u);
    CHECK(wire.written[1] == (Bytes{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0}));

    // Another client's answer is not ours.
    Bytes foreign = control(1, 1, 0, 9);
    foreign[2] = 7;
    t.onNotification(foreign);
    CHECK(!t.isReady());

    t.onNotification(control(1, 1, 0, 13));
    CHECK(t.isReady());
    CHECK_EQ(t.gfdiHandle(), 13);
    CHECK_EQ(wire.readyCount, 1);

    // A frame out: every write begins with the handle and fits the MTU.
    wire.written.clear();
    const Bytes msg = buildFrame(MsgDownloadRequest, Bytes(40, 0x11));
    t.send(msg);
    CHECK(wire.written.size() > 1);
    Bytes joined;
    for (const Bytes &piece : wire.written) {
        CHECK(piece.size() <= 20);
        CHECK_EQ(piece[0], 13);
        joined.insert(joined.end(), piece.begin() + 1, piece.end());
    }
    CobsDecoder d;
    d.feed(joined);
    Bytes back;
    CHECK(d.next(back) && back == msg);

    // A frame in, cut over several notifications, handle 13.
    const Bytes incoming = buildFrame(MsgFileAvailable, Bytes(30, 0x22));
    const Bytes enc = cobsEncode(incoming);
    for (size_t pos = 0; pos < enc.size(); pos += 19) {
        Bytes n = {13};
        n.insert(n.end(), enc.begin() + pos, enc.begin() + std::min(enc.size(), pos + 19));
        t.onNotification(n);
    }
    CHECK_EQ(wire.frames.size(), 1u);
    if (!wire.frames.empty())
        CHECK(wire.frames[0] == incoming);

    // Data on a channel that is not ours is ignored.
    Bytes stray = {99};
    stray.insert(stray.end(), enc.begin(), enc.end());
    t.onNotification(stray);
    CHECK_EQ(wire.frames.size(), 1u);

    // The Edge closing our channel: we ask again.
    wire.written.clear();
    t.onNotification(control(3, 1, 13, 0)); // close-handle response: service 1, handle 13, status 0
    CHECK(!t.isReady());
    CHECK_EQ(wire.written.size(), 1u);
}

static void direct()
{
    Wire wire;
    Transport t(Transport::Kind::Direct, wire.callbacks());
    t.setMaxWrite(20);
    t.start();
    CHECK(t.isReady());
    CHECK_EQ(wire.readyCount, 1);
    CHECK(wire.written.empty());

    const Bytes msg = buildFrame(MsgDownloadRequest, Bytes(40, 0x11));
    t.send(msg);
    Bytes joined;
    for (const Bytes &piece : wire.written) {
        CHECK(piece.size() <= 20);
        joined.insert(joined.end(), piece.begin(), piece.end());
    }
    CHECK(joined == cobsEncode(msg));

    const Bytes enc = cobsEncode(buildFrame(MsgFileAvailable, Bytes(30, 0x22)));
    t.onNotification(Bytes(enc.begin(), enc.begin() + 10));
    CHECK(wire.frames.empty());
    t.onNotification(Bytes(enc.begin() + 10, enc.end()));
    CHECK_EQ(wire.frames.size(), 1u);
}

int main()
{
    deviceInformation();
    capabilities();
    directory();
    downloadAndTransfer();
    synchronization();
    systemMessages();
    multiLink();
    direct();
    return checkSummary("messages");
}
