// The sync state machine against a scripted Edge.

#include "scripted_edge.h"

#include <memory>

using namespace Test;

static const int64_t kT0 = 1760000000;

static void addTypicalFiles(ScriptedEdge &edge)
{
    edge.files[11] = makeFile(11, 128, 4, kT0 + 200, 950, 1);   // newer activity
    edge.files[7] = makeFile(7, 128, 4, kT0 + 100, 333, 2);     // older activity
    edge.files[3] = makeFile(3, 128, 32, kT0 + 150, 400, 3);    // monitoring, not wanted
    edge.files[5] = makeFile(5, 8, 255, kT0 + 160, 60, 4);      // device xml, not FIT
}

static void selfStartedSync()
{
    Harness h;
    addTypicalFiles(h.edge);
    h.begin();
    h.pump();

    // The handshake went out: two answers to DEVICE_INFORMATION, two to
    // CONFIGURATION, then the initialisation messages, in this order.
    std::vector<uint16_t> order;
    for (const Frame &f : h.edge.hostSent)
        order.push_back(f.type);
    const std::vector<uint16_t> expect = {MsgResponse,        MsgResponse,   MsgResponse, MsgConfiguration,
                                          MsgSupportedFileTypes, MsgDeviceSettings, MsgSystemEvent};
    CHECK(order == expect);
    CHECK(!h.finished);
    CHECK_EQ(h.edge.countSent(MsgDownloadRequest), 0); // it waits for the Edge first

    h.advanceMs(1000);
    CHECK_EQ(h.edge.countSent(MsgDownloadRequest), 0);
    h.advanceMs(2000); // the self-start delay passes
    CHECK(h.finished);
    CHECK(h.ok);
    CHECK_EQ(h.edge.badFramesFromHost, 0);

    CHECK_EQ(h.listing.size(), 4u);
    // Only the activities, oldest first.
    CHECK_EQ(h.received.size(), 2u);
    if (h.received.size() == 2) {
        CHECK_EQ(h.received[0].first.index, 7);
        CHECK(h.received[0].second == h.edge.files[7].data);
        CHECK_EQ(h.received[1].first.index, 11);
        CHECK(h.received[1].second == h.edge.files[11].data);
        CHECK_EQ(h.received[1].first.timestamp, kT0 + 200);
    }
    CHECK_EQ(h.edge.countSent(MsgFilter), 0);      // the self-started path does not filter
    CHECK_EQ(h.edge.countSent(MsgSetFileFlag), 0); // nothing is archived unless asked
    // The last word is SYNC_COMPLETE.
    const Frame &last = h.edge.hostSent.back();
    CHECK_EQ(last.type, MsgSystemEvent);
    CHECK_EQ(last.payload[0], EventSyncComplete);

    // DEVICE_INFORMATION was answered with who we are.
    Frame reply = h.edge.hostSent[1];
    ResponseView rv;
    CHECK(parseResponse(reply.payload, rv));
    CHECK_EQ(rv.originalType, MsgDeviceInformation);
    Reader r(rv.extra);
    CHECK_EQ(r.u16(), 150);
    // The device-settings message is the auto-upload one.
    CHECK_EQ(h.edge.hostSent[5].payload[0], 3);
}

static void edgeInitiatedSync()
{
    Harness h;
    h.edge.initiatesSync = true;
    addTypicalFiles(h.edge);
    h.begin();
    h.pump();
    h.advanceMs(500); // well inside the self-start delay
    CHECK(h.finished);
    CHECK(h.ok);
    CHECK_EQ(h.edge.countSent(MsgFilter), 1);
    CHECK_EQ(h.received.size(), 2u);
    // FILTER came before the first DOWNLOAD_REQUEST.
    int filterAt = -1, downloadAt = -1;
    for (size_t i = 0; i < h.edge.hostSent.size(); ++i) {
        if (h.edge.hostSent[i].type == MsgFilter && filterAt < 0)
            filterAt = static_cast<int>(i);
        if (h.edge.hostSent[i].type == MsgDownloadRequest && downloadAt < 0)
            downloadAt = static_cast<int>(i);
    }
    CHECK(filterAt >= 0 && downloadAt > filterAt);
    // And only one sync ran, though the self-start timer also expired later.
    h.advanceMs(5000);
    CHECK_EQ(h.edge.countSent(MsgDownloadRequest), 3); // the directory, and two files
}

static void minimalCapabilitiesWhenAsked()
{
    for (bool minimal : {false, true}) {
        Harness h;
        h.options.minimalCapabilities = minimal;
        addTypicalFiles(h.edge);
        h.begin();
        h.pump();
        h.advanceMs(4000);
        CHECK(h.ok);
        CHECK_EQ(h.received.size(), 2u);
        for (const Frame &f : h.edge.hostSent) {
            if (f.type != MsgConfiguration)
                continue;
            Bytes caps;
            CHECK(parseConfiguration(f.payload, caps));
            CHECK(caps == (minimal ? minimalCapabilities() : ourCapabilities()));
        }
    }
}

static void archiveWhenAsked()
{
    Harness h;
    h.options.archiveAfterDownload = true;
    addTypicalFiles(h.edge);
    h.begin();
    h.pump();
    h.advanceMs(4000);
    CHECK(h.ok);
    CHECK_EQ(h.edge.countSent(MsgSetFileFlag), 2);
    for (const Frame &f : h.edge.hostSent) {
        if (f.type != MsgSetFileFlag)
            continue;
        Reader r(f.payload);
        const uint32_t idx = r.u16();
        CHECK(idx == 7 || idx == 11);
        CHECK_EQ(r.u8(), kFlagArchive);
    }
}

static void notStoredIsNotArchived()
{
    Harness h;
    h.options.archiveAfterDownload = true;
    addTypicalFiles(h.edge);
    h.events.fileReceived = [](const DirectoryEntry &, const Bytes &) { return false; }; // disk full, say
    h.begin();
    h.pump();
    h.advanceMs(4000);
    CHECK(h.ok);
    CHECK_EQ(h.edge.countSent(MsgSetFileFlag), 0);
}

static void notReadyIsRetried()
{
    Harness h;
    addTypicalFiles(h.edge);
    h.edge.files[7].notReadyFirst = 2;
    h.begin();
    h.pump();
    h.advanceMs(8000);
    CHECK(h.finished);
    CHECK(h.ok);
    CHECK_EQ(h.received.size(), 2u);
    CHECK_EQ(h.edge.countSent(MsgDownloadRequest), 1 + 3 + 1);
}

static void refusedFileIsSkipped()
{
    Harness h;
    addTypicalFiles(h.edge);
    h.edge.files[7].refuse = DownloadIndexNotReadable;
    h.begin();
    h.pump();
    h.advanceMs(4000);
    CHECK(h.ok);
    CHECK_EQ(h.received.size(), 1u);
    if (!h.received.empty())
        CHECK_EQ(h.received[0].first.index, 11);
}

static void emptyFile()
{
    Harness h;
    h.edge.files[2] = makeFile(2, 128, 4, kT0, 0, 0);
    h.edge.files[4] = makeFile(4, 128, 4, kT0 + 5, 10, 9);
    h.begin();
    h.pump();
    h.advanceMs(4000);
    CHECK(h.ok);
    CHECK_EQ(h.received.size(), 2u);
    if (h.received.size() == 2)
        CHECK(h.received[0].second.empty());
}

static void emptyDirectory()
{
    Harness h;
    h.begin();
    h.pump();
    h.advanceMs(4000);
    CHECK(h.finished);
    CHECK(h.ok);
    CHECK(h.received.empty());
}

static void chunkSizes()
{
    for (size_t chunk : {1u, 7u, 100u, 361u, 5000u}) {
        Harness h;
        h.edge.chunkSize = chunk;
        h.edge.files[1] = makeFile(1, 128, 4, kT0, chunk == 1 ? 40 : 1500, 5);
        h.begin();
        h.pump();
        h.advanceMs(4000);
        CHECK(h.ok);
        CHECK_EQ(h.received.size(), 1u);
        if (h.received.size() == 1)
            CHECK(h.received[0].second == h.edge.files[1].data);
    }
}

static void corruptChunkFails()
{
    Harness h;
    addTypicalFiles(h.edge);
    h.edge.corruptCrcOnce = true; // the directory's first chunk
    h.begin();
    h.pump();
    h.advanceMs(4000);
    CHECK(h.finished);
    CHECK(!h.ok);
    CHECK(h.message.find("checksum") != std::string::npos);
    CHECK(h.received.empty());
}

static void handshakeTimeout()
{
    Harness h;
    h.edge.skipHandshake = true;
    h.begin();
    h.advanceMs(20000);
    CHECK(!h.finished);
    h.advanceMs(8000);
    CHECK(h.finished);
    CHECK(!h.ok);
    CHECK(h.message.find("did not start") != std::string::npos);
}

static void stallDuringTransferFails()
{
    Harness h;
    h.options.stepTimeoutMs = 5000;
    addTypicalFiles(h.edge);
    h.begin();
    h.pump();
    h.advanceMs(1000);
    // The Edge goes silent before it answers the first DOWNLOAD_REQUEST.
    h.edge.mute = true;
    h.advanceMs(4000);
    CHECK_EQ(h.edge.countSent(MsgDownloadRequest), 1);
    CHECK(!h.finished);
    h.advanceMs(6000);
    CHECK(h.finished);
    CHECK(!h.ok);
    CHECK(h.message.find("stopped answering") != std::string::npos);
}

static void answersWhatItShould()
{
    Harness h;
    h.begin();
    h.pump();
    h.edge.hostSent.clear();

    // CURRENT_TIME_REQUEST gets the time, in Garmin seconds, and our UTC offset.
    Writer t;
    t.u32(77);
    h.session->onFrame(buildFrame(MsgCurrentTimeRequest, t.bytes()), h.now);
    CHECK_EQ(h.edge.hostSent.size(), 1u);
    ResponseView rv;
    CHECK(parseResponse(h.edge.hostSent[0].payload, rv));
    CHECK_EQ(rv.originalType, MsgCurrentTimeRequest);
    Reader r(rv.extra);
    CHECK_EQ(r.u32(), 77);
    CHECK_EQ(garminToUnix(r.u32()), h.options.unixNow + 0);
    CHECK_EQ(static_cast<int32_t>(r.u32()), 7200);

    // Anything we do not implement is answered "unsupported", not ignored.
    h.edge.hostSent.clear();
    h.session->onFrame(buildFrame(MsgWeatherRequest, Bytes{1, 2, 3}), h.now);
    h.session->onFrame(buildFrame(MsgProtobufRequest, Bytes{1, 2, 3}), h.now);
    CHECK_EQ(h.edge.hostSent.size(), 2u);
    for (const Frame &f : h.edge.hostSent) {
        CHECK(parseResponse(f.payload, rv));
        CHECK_EQ(rv.status, StatusUnsupported);
    }

    // AUTH_NEGOTIATION is echoed with "ok".
    h.edge.hostSent.clear();
    Writer a;
    a.u8(3).u32(0x5);
    h.session->onFrame(buildFrame(MsgAuthNegotiation, a.bytes()), h.now);
    CHECK(parseResponse(h.edge.hostSent[0].payload, rv));
    CHECK_EQ(rv.originalType, MsgAuthNegotiation);
    CHECK_EQ(rv.status, StatusAck);

    // A damaged frame is dropped without an answer.
    h.edge.hostSent.clear();
    Bytes bad = buildFrame(MsgWeatherRequest, Bytes{1});
    bad[4] ^= 0xFF;
    h.session->onFrame(bad, h.now);
    CHECK(h.edge.hostSent.empty());
}

static void chunkOutOfOrderAsksForResend()
{
    Harness h;
    addTypicalFiles(h.edge);
    h.begin();
    h.pump();
    // Run until the directory request is out, then discard the real directory
    // chunks and inject one from the wrong place.
    h.now += 3000;
    h.session->tick(h.now);
    h.edge.toHost.clear();
    CHECK_EQ(h.edge.countSent(MsgDownloadRequest), 1);

    Writer ok;
    ok.u8(DownloadOk).u32(64);
    h.session->onFrame(buildResponse(MsgDownloadRequest, StatusAck, ok.bytes()), h.now);
    h.edge.toHost.clear();
    h.edge.hostSent.clear();

    Writer c;
    c.u8(0).u16(0).u32(32).raw(Bytes(8, 1)); // offset 32 when 0 is expected
    h.session->onFrame(buildFrame(MsgFileTransferData, c.bytes()), h.now);
    CHECK_EQ(h.edge.hostSent.size(), 1u);
    ResponseView rv;
    CHECK(parseResponse(h.edge.hostSent[0].payload, rv));
    CHECK_EQ(rv.originalType, MsgFileTransferData);
    Reader r(rv.extra);
    CHECK_EQ(r.u8(), TransferResend);
    CHECK_EQ(r.u32(), 0);
}

static void progressIsReported()
{
    Harness h;
    addTypicalFiles(h.edge);
    h.begin();
    h.pump();
    h.advanceMs(4000);
    CHECK(h.ok);
    bool sawFiles = false;
    uint32_t maxBytes = 0;
    for (const Progress &p : h.progress) {
        if (p.phase == Progress::Files && p.fileCount == 2)
            sawFiles = true;
        if (p.phase == Progress::Files && p.fileSize == 950)
            maxBytes = std::max(maxBytes, p.fileBytes);
    }
    CHECK(sawFiles);
    CHECK_EQ(maxBytes, 950u);
}

static void abortFinishesOnce()
{
    Harness h;
    h.begin();
    h.session->abort("the link dropped");
    h.session->abort("again");
    CHECK(h.finished);
    CHECK(!h.ok);
    CHECK_EQ(h.message, std::string("the link dropped"));
}

int main()
{
    selfStartedSync();
    edgeInitiatedSync();
    minimalCapabilitiesWhenAsked();
    archiveWhenAsked();
    notStoredIsNotArchived();
    notReadyIsRetried();
    refusedFileIsSkipped();
    emptyFile();
    emptyDirectory();
    chunkSizes();
    corruptChunkFails();
    handshakeTimeout();
    stallDuringTransferFails();
    answersWhatItShould();
    chunkOutOfOrderAsksForResend();
    progressIsReported();
    abortFinishesOnce();
    return checkSummary("session");
}
