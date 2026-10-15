// The FIT reader, on files built byte by byte.
//
// The real-file check lives in tools/fitcheck (it needs fitparse and ride files
// that are personal data); what is here is the container's corner cases: byte
// order, compressed timestamps, developer fields, chained and damaged files.

#include "check.h"
#include "fit/activity.h"
#include "gfdi/bytes.h"
#include "gfdi/crc.h"

using namespace Fit;
using Gfdi::Bytes;
using Gfdi::Writer;

namespace {

struct FieldSpec
{
    uint8_t number;
    uint8_t size;
    uint8_t type;
};

class FitBuilder
{
public:
    FitBuilder &define(uint8_t local, uint16_t global, const std::vector<FieldSpec> &fields, bool bigEndian = false,
                       const std::vector<FieldSpec> &dev = {})
    {
        m_data.push_back(static_cast<uint8_t>(0x40 | (dev.empty() ? 0 : 0x20) | local));
        m_data.push_back(0);
        m_data.push_back(bigEndian ? 1 : 0);
        if (bigEndian) {
            m_data.push_back(global >> 8);
            m_data.push_back(global & 0xFF);
        } else {
            m_data.push_back(global & 0xFF);
            m_data.push_back(global >> 8);
        }
        m_data.push_back(static_cast<uint8_t>(fields.size()));
        for (const FieldSpec &f : fields) {
            m_data.push_back(f.number);
            m_data.push_back(f.size);
            m_data.push_back(f.type);
        }
        if (!dev.empty()) {
            m_data.push_back(static_cast<uint8_t>(dev.size()));
            for (const FieldSpec &f : dev) {
                m_data.push_back(f.number);
                m_data.push_back(f.size);
                m_data.push_back(0); // developer data index
            }
        }
        return *this;
    }
    FitBuilder &data(uint8_t local, const Bytes &body)
    {
        m_data.push_back(local);
        m_data.insert(m_data.end(), body.begin(), body.end());
        return *this;
    }
    // A data record with a compressed-timestamp header.
    FitBuilder &compressed(uint8_t local, uint8_t timeOffset, const Bytes &body)
    {
        m_data.push_back(static_cast<uint8_t>(0x80 | (local << 5) | timeOffset));
        m_data.insert(m_data.end(), body.begin(), body.end());
        return *this;
    }
    Bytes file(bool header14 = true) const
    {
        Writer w;
        w.u8(header14 ? 14 : 12).u8(0x20).u16(2100).u32(static_cast<uint32_t>(m_data.size())).raw(
                reinterpret_cast<const uint8_t *>(".FIT"), 4);
        if (header14)
            w.u16(0);
        w.raw(m_data);
        Bytes b = w.take();
        const uint16_t crc = Gfdi::crc16(b.data(), b.size());
        b.push_back(crc & 0xFF);
        b.push_back(crc >> 8);
        return b;
    }
    const Bytes &raw() const { return m_data; }

private:
    Bytes m_data;
};

constexpr uint32_t kT = 1139678400u; // FIT seconds

// A record message layout used throughout: timestamp, lat, lon, altitude, hr, power, speed.
const std::vector<FieldSpec> kRecordFields = {
    {253, 4, Uint32}, {0, 4, Sint32}, {1, 4, Sint32}, {2, 2, Uint16}, {3, 1, Uint8}, {7, 2, Uint16}, {6, 2, Uint16},
};

Bytes record(uint32_t t, int32_t lat, int32_t lon, uint16_t alt, uint8_t hr, uint16_t power, uint16_t speed)
{
    Writer w;
    w.u32(t).u32(static_cast<uint32_t>(lat)).u32(static_cast<uint32_t>(lon)).u16(alt).u8(hr).u16(power).u16(speed);
    return w.take();
}

int32_t semi(double deg) { return static_cast<int32_t>(deg * (2147483648.0 / 180.0)); }

} // namespace

static void basicActivity()
{
    FitBuilder b;
    b.define(0, 20, kRecordFields);
    // 100 m altitude is raw (100 + 500) * 5 = 3000; 5.5 m/s is raw 5500.
    b.data(0, record(kT, semi(60.1699), semi(24.9384), 3000, 120, 200, 5500));
    b.data(0, record(kT + 1, semi(60.1700), semi(24.9385), 3005, 121, 210, 5600));
    b.data(0, record(kT + 2, semi(60.1701), semi(24.9386), 3010, 122, 220, 5700));
    b.define(1, 18, {{2, 4, Uint32}, {253, 4, Uint32}, {5, 1, Enum}, {6, 1, Enum}, {7, 4, Uint32}, {8, 4, Uint32},
                     {9, 4, Uint32}, {11, 2, Uint16}, {16, 1, Uint8}, {17, 1, Uint8}, {20, 2, Uint16}, {21, 2, Uint16}});
    // Field order in the definition is what the data must follow.
    Writer sd;
    sd.u32(kT).u32(kT + 2).u8(2).u8(7).u32(2000).u32(2000).u32(1500).u16(33).u8(121).u8(122).u16(210).u16(220);
    b.data(1, sd.take());

    Activity a;
    std::string err;
    CHECK(parseActivity(b.file(), a, err));
    CHECK(a.stats.crcOk);
    CHECK(!a.stats.truncated);
    CHECK_EQ(a.samples.size(), 3u);
    CHECK_EQ(a.samples[0].time, toUnix(kT));
    CHECK_NEAR(a.samples[0].lat, 60.1699, 1e-6);
    CHECK_NEAR(a.samples[0].lon, 24.9384, 1e-6);
    CHECK_NEAR(a.samples[0].altitude, 100.0, 1e-9);
    CHECK_NEAR(a.samples[2].altitude, 102.0, 1e-9);
    CHECK_NEAR(a.samples[1].speed, 5.6, 1e-9);
    CHECK_EQ(a.samples[1].heartRate, 121);
    CHECK_EQ(a.samples[1].power, 210);
    CHECK(std::isnan(a.samples[0].distance)); // not recorded: not zero
    CHECK(std::isnan(a.samples[0].temperature));
    CHECK_EQ(a.samples[0].cadence, -1);

    const Summary &m = a.summary;
    CHECK_EQ(m.sport, 2);
    CHECK_EQ(m.subSport, 7);
    CHECK_NEAR(m.elapsed, 2.0, 1e-9);
    CHECK_NEAR(m.distance, 15.0, 1e-9);
    CHECK_EQ(m.calories, 33);
    CHECK_EQ(m.avgHeartRate, 121);
    CHECK_EQ(m.maxPower, 220);
    CHECK_EQ(m.startTime, toUnix(kT));
    CHECK_NEAR(m.maxAltitude, 102.0, 1e-9); // the session has none: taken from the records
    CHECK(m.derived);
    CHECK_EQ(sportKey(m.sport, m.subSport), std::string("cycling"));
}

static void invalidValuesAreMissing()
{
    FitBuilder b;
    b.define(0, 20, kRecordFields);
    // lat/lon 0x7FFFFFFF, altitude 0xFFFF, hr 0xFF, power 0xFFFF, speed 0xFFFF: all "not set".
    b.data(0, record(kT, 0x7FFFFFFF, 0x7FFFFFFF, 0xFFFF, 0xFF, 0xFFFF, 0xFFFF));
    b.data(0, record(kT + 1, semi(1.0), semi(2.0), 3000, 0, 0, 0)); // real zeros stay zeros
    Activity a;
    std::string err;
    CHECK(parseActivity(b.file(), a, err));
    CHECK_EQ(a.samples.size(), 2u);
    CHECK(!a.samples[0].hasPosition());
    CHECK(std::isnan(a.samples[0].altitude));
    CHECK_EQ(a.samples[0].heartRate, -1);
    CHECK_EQ(a.samples[0].power, -1);
    CHECK(std::isnan(a.samples[0].speed));
    CHECK_EQ(a.samples[1].power, 0);
    CHECK_EQ(a.samples[1].heartRate, 0);
    CHECK_NEAR(a.samples[1].speed, 0.0, 1e-12);
}

static void bigEndian()
{
    FitBuilder b;
    b.define(0, 20, {{253, 4, Uint32}, {2, 2, Uint16}, {3, 1, Uint8}}, true);
    Writer w;
    // Big-endian: most significant byte first.
    w.u8(kT >> 24).u8((kT >> 16) & 0xFF).u8((kT >> 8) & 0xFF).u8(kT & 0xFF).u8(0x0B).u8(0xB8).u8(150);
    b.data(0, w.take());
    Activity a;
    std::string err;
    CHECK(parseActivity(b.file(), a, err));
    CHECK_EQ(a.samples.size(), 1u);
    CHECK_EQ(a.samples[0].time, toUnix(kT));
    CHECK_NEAR(a.samples[0].altitude, 0x0BB8 / 5.0 - 500.0, 1e-9); // 0x0BB8 = 3000 -> 100 m
    CHECK_EQ(a.samples[0].heartRate, 150);
}

static void compressedTimestamps()
{
    FitBuilder b;
    b.define(0, 20, kRecordFields);
    b.define(1, 20, {{3, 1, Uint8}}); // a record with no timestamp field of its own
    // kT & 0x1F decides where the offsets land; the first record anchors it.
    b.data(0, record(kT, semi(1), semi(1), 3000, 100, 0, 0));
    const uint32_t low = kT & 0x1F;
    b.compressed(1, (low + 1) & 0x1F, Bytes{101});
    b.compressed(1, (low + 2) & 0x1F, Bytes{102});
    // An offset smaller than the anchor's low bits means the 5-bit counter wrapped.
    b.compressed(1, (low + 31) & 0x1F, Bytes{103});

    Activity a;
    std::string err;
    CHECK(parseActivity(b.file(), a, err));
    CHECK_EQ(a.samples.size(), 4u);
    CHECK_EQ(a.samples[1].time, toUnix(kT + 1));
    CHECK_EQ(a.samples[2].time, toUnix(kT + 2));
    CHECK_EQ(a.samples[3].time, toUnix(kT + 31));
    CHECK_EQ(a.samples[3].heartRate, 103);
}

static void developerFieldsAreSkipped()
{
    FitBuilder b;
    b.define(0, 20, {{253, 4, Uint32}, {3, 1, Uint8}}, false, {{0, 4, 0}, {1, 2, 0}});
    Writer w;
    w.u32(kT).u8(140).u32(0xDEADBEEF).u16(0x1234); // the developer bytes must not shift later fields
    b.data(0, w.take());
    b.define(1, 20, {{253, 4, Uint32}, {3, 1, Uint8}});
    Writer w2;
    w2.u32(kT + 1).u8(141);
    b.data(1, w2.take());
    Activity a;
    std::string err;
    CHECK(parseActivity(b.file(), a, err));
    CHECK_EQ(a.samples.size(), 2u);
    CHECK_EQ(a.samples[0].heartRate, 140);
    CHECK_EQ(a.samples[1].heartRate, 141);
    CHECK_EQ(a.samples[1].time, toUnix(kT + 1));
}

static void headerVariantsAndChaining()
{
    FitBuilder b;
    b.define(0, 20, {{253, 4, Uint32}, {3, 1, Uint8}});
    Writer w;
    w.u32(kT).u8(90);
    b.data(0, w.take());

    // The 12-byte header form.
    Activity a;
    std::string err;
    CHECK(parseActivity(b.file(false), a, err));
    CHECK_EQ(a.samples.size(), 1u);

    // Two files back to back, as some devices write when a ride is split.
    FitBuilder c;
    c.define(0, 20, {{253, 4, Uint32}, {3, 1, Uint8}});
    Writer w2;
    w2.u32(kT + 60).u8(95);
    c.data(0, w2.take());
    Bytes both = b.file();
    const Bytes second = c.file();
    both.insert(both.end(), second.begin(), second.end());
    CHECK(parseActivity(both, a, err));
    CHECK_EQ(a.samples.size(), 2u);
    CHECK_EQ(a.stats.files, 2u);
    CHECK(a.stats.crcOk);

    // Padding after the last file is not a third file.
    both.insert(both.end(), 40, 0);
    CHECK(parseActivity(both, a, err));
    CHECK_EQ(a.stats.files, 2u);
}

static void damagedFiles()
{
    FitBuilder b;
    b.define(0, 20, {{253, 4, Uint32}, {3, 1, Uint8}});
    for (int i = 0; i < 20; ++i) {
        Writer w;
        w.u32(kT + i).u8(static_cast<uint32_t>(100 + i));
        b.data(0, w.take());
    }
    const Bytes good = b.file();
    Activity a;
    std::string err;

    // A flipped bit in the data: still readable, but flagged.
    Bytes flipped = good;
    flipped[30] ^= 0x01;
    CHECK(parseActivity(flipped, a, err));
    CHECK(!a.stats.crcOk);

    // Cut short: what is there is returned and flagged, not thrown away.
    Bytes cut(good.begin(), good.begin() + 60);
    CHECK(parseActivity(cut, a, err));
    CHECK(a.stats.truncated);
    CHECK(a.samples.size() >= 5 && a.samples.size() < 20);

    // Not a FIT file at all.
    CHECK(!parseActivity(Bytes(100, 0x41), a, err));
    CHECK(!err.empty());
    CHECK(!parseActivity(Bytes(), a, err));
    CHECK(!parseActivity(Bytes{14, 0x20, 0, 0}, a, err));

    // A data record before any definition for its local type.
    FitBuilder orphan;
    orphan.data(3, Bytes{1, 2, 3});
    CHECK(!parseActivity(orphan.file(), a, err));
}

static void summaryDerivedFromRecords()
{
    FitBuilder b;
    b.define(0, 20, {{253, 4, Uint32}, {5, 4, Uint32}, {3, 1, Uint8}, {7, 2, Uint16}, {4, 1, Uint8}});
    for (int i = 0; i < 10; ++i) {
        Writer w;
        w.u32(kT + i).u32(static_cast<uint32_t>(i * 500)).u8(static_cast<uint32_t>(100 + i)).u16(static_cast<uint32_t>(200 + i * 10))
                .u8(80);
        b.data(0, w.take());
    }
    Activity a;
    std::string err;
    CHECK(parseActivity(b.file(), a, err)); // no session message at all
    CHECK(a.summary.derived);
    CHECK_EQ(a.summary.startTime, toUnix(kT));
    CHECK_NEAR(a.summary.elapsed, 9.0, 1e-9);
    CHECK_NEAR(a.summary.distance, 45.0, 1e-9); // 4500 raw cm
    CHECK_EQ(a.summary.maxHeartRate, 109);
    CHECK_EQ(a.summary.avgHeartRate, 104);
    CHECK_EQ(a.summary.maxPower, 290);
    CHECK_EQ(a.summary.avgCadence, 80);
    CHECK_NEAR(a.summary.avgSpeed, 45.0 / 9.0, 1e-9);
}

static void lapsAndSessions()
{
    FitBuilder b;
    b.define(0, 19, {{2, 4, Uint32}, {7, 4, Uint32}, {8, 4, Uint32}, {9, 4, Uint32}, {13, 2, Uint16}, {15, 1, Uint8}});
    for (int i = 0; i < 3; ++i) {
        Writer w;
        w.u32(kT + i * 600).u32(600000).u32(590000).u32(300000).u16(7500).u8(static_cast<uint32_t>(140 + i));
        b.data(0, w.take());
    }
    b.define(1, 18, {{5, 1, Enum}, {9, 4, Uint32}, {8, 4, Uint32}});
    Writer s1, s2;
    s1.u8(2).u32(100000).u32(1000000);
    s2.u8(2).u32(50000).u32(500000);
    b.data(1, s1.take());
    b.data(1, s2.take());

    Activity a;
    std::string err;
    CHECK(parseActivity(b.file(), a, err));
    CHECK_EQ(a.laps.size(), 3u);
    CHECK_NEAR(a.laps[1].distance, 3000.0, 1e-9);
    CHECK_NEAR(a.laps[1].avgSpeed, 7.5, 1e-9);
    CHECK_EQ(a.laps[2].avgHeartRate, 142);
    CHECK_EQ(a.laps[1].startTime, toUnix(kT + 600));
    // Two sessions add up.
    CHECK_EQ(a.sessions, 2);
    CHECK_NEAR(a.summary.distance, 1500.0, 1e-9);
    CHECK_NEAR(a.summary.timer, 1500.0, 1e-9);
}

static void thinning()
{
    std::vector<Sample> s(1000);
    for (size_t i = 0; i < s.size(); ++i)
        s[i].time = static_cast<int64_t>(i);
    const std::vector<Sample> t = thin(s, 100);
    CHECK(t.size() >= 99 && t.size() <= 101);
    CHECK_EQ(t.front().time, 0);
    CHECK_EQ(t.back().time, 999);
    for (size_t i = 1; i < t.size(); ++i)
        CHECK(t[i].time > t[i - 1].time);
    CHECK_EQ(thin(s, 5000).size(), 1000u);
    CHECK_EQ(thin(s, 1).size(), 1000u);
}

static void sportNames()
{
    CHECK_EQ(sportKey(2, -1), std::string("cycling"));
    CHECK_EQ(sportKey(2, 6), std::string("indoor_cycling"));
    CHECK_EQ(sportKey(2, 8), std::string("mountain_biking"));
    CHECK_EQ(sportKey(2, 46), std::string("gravel_cycling"));
    CHECK_EQ(sportKey(2, 28), std::string("e_biking"));
    CHECK_EQ(sportKey(21, 0), std::string("e_biking"));
    CHECK_EQ(sportKey(11, 0), std::string("walking"));
    CHECK_EQ(sportKey(-1, -1), std::string("other"));
    CHECK_EQ(sportKey(200, 0), std::string("other"));
}

int main()
{
    basicActivity();
    invalidValuesAreMissing();
    bigEndian();
    compressedTimestamps();
    developerFieldsAreSkipped();
    headerVariantsAndChaining();
    damagedFiles();
    summaryDerivedFromRecords();
    lapsAndSessions();
    thinning();
    sportNames();
    return checkSummary("fit");
}
