// CRC, COBS and the GFDI frame. The vectors are the ones garmin-ble pinned
// against Gadgetbridge's reference implementation (tests/protocol/test_crc.py,
// test_cobs.py) and the Wireshark notes in mjsir911/GarminBLE (crc.py).

#include "check.h"
#include "gfdi/cobs.h"
#include "gfdi/crc.h"
#include "gfdi/message.h"

#include <random>

using namespace Gfdi;

static Bytes bytes(const std::string &s) { return Bytes(s.begin(), s.end()); }

static void crcVectors()
{
    CHECK_EQ(crc16(nullptr, 0), 0);
    const Bytes a = bytes("A");
    CHECK_EQ(crc16(a.data(), a.size()), 12480);
    const Bytes hello = bytes("Hello");
    CHECK_EQ(crc16(hello.data(), hello.size()), 62291);
    Bytes all(256);
    for (int i = 0; i < 256; ++i)
        all[i] = static_cast<uint8_t>(i);
    CHECK_EQ(crc16(all.data(), all.size()), 0xBAD3);

    // From the vivofit 3 notes: two short frames and their CRCs.
    const Bytes v1 = {0x07, 0x00, 0x8f, 0x13, 0x03};
    CHECK_EQ(crc16(v1.data(), v1.size()), 0x1ac9);
    const Bytes v2 = {0x06, 0x00, 0xa7, 0x13};
    CHECK_EQ(crc16(v2.data(), v2.size()), 0x753b);

    // Chaining: the CRC of a file is the fold of its chunks, which is how the
    // Edge checks each FILE_TRANSFER_DATA.
    const uint16_t whole = crc16(all.data(), all.size());
    uint16_t folded = 0;
    for (size_t pos = 0; pos < all.size(); pos += 37)
        folded = crc16(all.data() + pos, std::min<size_t>(37, all.size() - pos), folded);
    CHECK_EQ(folded, whole);
}

static void cobsVectors()
{
    CHECK(cobsEncode(bytes("A")) == (Bytes{0x00, 0x02, 0x41, 0x00}));
    CHECK(cobsEncode(Bytes()) == (Bytes{0x00, 0x00}));
    CHECK(cobsEncode(Bytes{0x00}) == (Bytes{0x00, 0x01, 0x01, 0x00}));

    const Bytes full(254, 'A');
    const Bytes e = cobsEncode(full);
    CHECK_EQ(e.size(), 1u + 1 + 254 + 1 + 1);
    CHECK_EQ(e[1], 0xFF);
    CHECK_EQ(e[e.size() - 2], 0x01);

    const Bytes over(255, 'A');
    const Bytes e2 = cobsEncode(over);
    CHECK_EQ(e2[1], 0xFF);
    CHECK_EQ(e2[1 + 1 + 254], 0x02);
}

static bool roundTrip(const Bytes &data)
{
    CobsDecoder d;
    d.feed(cobsEncode(data));
    Bytes out;
    return d.next(out) && out == data;
}

static void cobsRoundTrips()
{
    CHECK(roundTrip(bytes("Hello")));
    CHECK(roundTrip(Bytes{0x00}));
    CHECK(roundTrip(Bytes{0x00, 0x00, 0x00}));
    CHECK(roundTrip(bytes(std::string("Hello\0World", 11))));
    Bytes range(256);
    for (int i = 0; i < 256; ++i)
        range[i] = static_cast<uint8_t>(i);
    CHECK(roundTrip(range));
    CHECK(roundTrip(Bytes(512, 'A')));
    Bytes edge(254, 'A');
    edge.insert(edge.begin(), 0x00);
    edge.push_back(0x00);
    CHECK(roundTrip(edge));
    for (size_t n : {253u, 254u, 255u, 507u, 508u, 509u, 762u})
        CHECK(roundTrip(Bytes(n, 0x7E)));

    std::mt19937 rng(1234);
    for (int i = 0; i < 400; ++i) {
        Bytes b(1 + rng() % 700);
        const int zeroOdds = 1 + rng() % 40; // from mostly-zero to mostly-not
        for (auto &x : b)
            x = (rng() % zeroOdds == 0) ? 0x00 : static_cast<uint8_t>(1 + rng() % 255);
        CHECK(roundTrip(b));
    }
}

// A frame arrives cut at arbitrary places, two frames back to back, with
// leading garbage, and one with its start lost.
static void cobsStreaming()
{
    const Bytes a = bytes("first message");
    const Bytes b = bytes(std::string("second\0message", 14));
    Bytes stream = cobsEncode(a);
    const Bytes eb = cobsEncode(b);
    stream.insert(stream.end(), eb.begin(), eb.end());

    for (size_t cut = 1; cut < stream.size(); ++cut) {
        CobsDecoder d;
        d.feed(stream.data(), cut);
        d.feed(stream.data() + cut, stream.size() - cut);
        Bytes out;
        CHECK(d.next(out) && out == a);
        CHECK(d.next(out) && out == b);
        CHECK(!d.next(out));
    }

    // One byte at a time, as a worst-case MTU of 20 would deliver it.
    CobsDecoder slow;
    int got = 0;
    for (uint8_t x : stream) {
        slow.feed(&x, 1);
        Bytes out;
        while (slow.next(out))
            ++got;
    }
    CHECK_EQ(got, 2);

    // The tail of a frame whose start was lost is dropped; the next frame is fine.
    Bytes broken = {0x05, 0x41, 0x42, 0x00};
    broken.insert(broken.end(), eb.begin(), eb.end());
    CobsDecoder d;
    d.feed(broken);
    Bytes out;
    CHECK(d.next(out) && out == b);
    CHECK(d.discarded() > 0);

    // A frame sharing one zero with its neighbour (no second leading zero).
    Bytes shared = cobsEncode(a);
    Bytes second = cobsEncode(b);
    second.erase(second.begin());
    shared.insert(shared.end(), second.begin(), second.end());
    CobsDecoder d2;
    d2.feed(shared);
    CHECK(d2.next(out) && out == a);
    CHECK(d2.next(out) && out == b);
}

static void frames()
{
    const Bytes payload = {1, 2, 3, 4};
    const Bytes raw = buildFrame(MsgSystemEvent, payload);
    CHECK_EQ(raw.size(), 4u + payload.size() + 2);
    CHECK_EQ(raw[0], raw.size());
    CHECK_EQ(raw[2], 5030 & 0xFF);
    CHECK_EQ(raw[3], 5030 >> 8);

    Frame f;
    std::string err;
    CHECK(parseFrame(raw, f, err));
    CHECK_EQ(f.type, MsgSystemEvent);
    CHECK(f.payload == payload);

    Bytes badCrc = raw;
    badCrc[5] ^= 0x01;
    CHECK(!parseFrame(badCrc, f, err));
    Bytes badLen = raw;
    badLen.push_back(0);
    CHECK(!parseFrame(badLen, f, err));
    CHECK(!parseFrame(Bytes{1, 2, 3}, f, err));

    // A type with the top bit set carries a sequence in the high byte and the
    // type minus 5000 in the low one.
    Bytes seq = {6, 0, static_cast<uint8_t>(5030 - 5000), static_cast<uint8_t>(0x80 | 3), 0, 0};
    const uint16_t crc = crc16(seq.data(), 4);
    seq[4] = crc & 0xFF;
    seq[5] = crc >> 8;
    CHECK(parseFrame(seq, f, err));
    CHECK_EQ(f.type, 5030);

    const Bytes resp = buildResponse(MsgDownloadRequest, StatusAck, Bytes{0, 9, 0, 0, 0});
    CHECK(parseFrame(resp, f, err));
    CHECK_EQ(f.type, MsgResponse);
    CHECK_EQ(f.payload[0], 5002 & 0xFF);
    CHECK_EQ(f.payload[1], 5002 >> 8);
    CHECK_EQ(f.payload[2], StatusAck);
}

int main()
{
    crcVectors();
    cobsVectors();
    cobsRoundTrips();
    cobsStreaming();
    frames();
    return checkSummary("framing");
}
