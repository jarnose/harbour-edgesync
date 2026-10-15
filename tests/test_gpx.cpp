// The GPX export.

#include "check.h"
#include "export/gpx.h"

using namespace Export;

static Fit::Sample point(int64_t t, double lat, double lon)
{
    Fit::Sample s;
    s.time = t;
    s.lat = lat;
    s.lon = lon;
    return s;
}

static bool contains(const std::string &hay, const std::string &needle)
{
    return hay.find(needle) != std::string::npos;
}

static size_t count(const std::string &hay, const std::string &needle)
{
    size_t n = 0, at = 0;
    while ((at = hay.find(needle, at)) != std::string::npos) {
        ++n;
        at += needle.size();
    }
    return n;
}

static void dates()
{
    CHECK_EQ(isoUtc(0), std::string("1970-01-01T00:00:00Z"));
    CHECK_EQ(isoUtc(1785740505), std::string("2026-08-03T07:01:45Z"));
    CHECK_EQ(isoUtc(951782400), std::string("2000-02-29T00:00:00Z"));   // a leap day
    CHECK_EQ(isoUtc(4107542399), std::string("2100-02-28T23:59:59Z"));  // 2100 is not a leap year
    CHECK_EQ(isoUtc(-1), std::string("1969-12-31T23:59:59Z"));
    CHECK_EQ(isoUtc(631065600), std::string("1989-12-31T00:00:00Z"));   // the FIT epoch
}

static void escaping()
{
    CHECK_EQ(xmlEscape("A & B <c> \"d\" 'e'"), std::string("A &amp; B &lt;c&gt; &quot;d&quot; &apos;e&apos;"));
    CHECK_EQ(xmlEscape(std::string("a\x01" "b")), std::string("ab"));
    CHECK_EQ(xmlEscape("Ääkköset"), std::string("Ääkköset"));
}

static void track()
{
    Fit::Activity a;
    a.summary.startTime = 1785740505;
    Fit::Sample p = point(1785740505, 60.1699, 24.9384);
    p.altitude = 12.34;
    p.heartRate = 120;
    p.cadence = 85;
    p.power = 210;
    p.temperature = 21.4;
    a.samples.push_back(p);
    a.samples.push_back(point(1785740506, 60.1700, 24.9385));
    a.samples.push_back(point(1785740507, Fit::kNaN, Fit::kNaN)); // lost the fix
    a.samples.push_back(point(1785740508, 60.1702, 24.9387));
    a.samples.push_back(point(1785740600, 60.1710, 24.9390));      // 92 s later: a new segment

    const std::string gpx = toGpx(a, "Morning <ride>");
    CHECK(contains(gpx, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
    CHECK(contains(gpx, "<name>Morning &lt;ride&gt;</name>"));
    CHECK(contains(gpx, "<time>2026-08-03T07:01:45Z</time>"));
    CHECK(contains(gpx, "<trkpt lat=\"60.1699000\" lon=\"24.9384000\">"));
    CHECK(contains(gpx, "<ele>12.3</ele>"));
    CHECK(contains(gpx, "<gpxtpx:hr>120</gpxtpx:hr>"));
    CHECK(contains(gpx, "<gpxtpx:cad>85</gpxtpx:cad>"));
    CHECK(contains(gpx, "<gpxtpx:atemp>21</gpxtpx:atemp>"));
    CHECK(contains(gpx, "<power>210</power>"));
    CHECK_EQ(count(gpx, "<trkpt "), 4u);        // the point without a fix is not written
    CHECK_EQ(count(gpx, "<trkseg>"), 3u);       // before the gap, after it, and after the long pause
    CHECK_EQ(count(gpx, "</trkseg>"), 3u);
    CHECK_EQ(count(gpx, "<trk>"), count(gpx, "</trk>"));
    CHECK(contains(gpx, "</gpx>\n"));
    // A second point has no extras and so no <extensions>.
    CHECK_EQ(count(gpx, "<extensions>"), 1u);
}

static void indoor()
{
    Fit::Activity a;
    Fit::Sample s;
    s.time = 5;
    s.heartRate = 100;
    a.samples.push_back(s);
    CHECK(toGpx(a, "Trainer").empty());
    CHECK(toGpx(Fit::Activity(), "Empty").empty());
}

int main()
{
    dates();
    escaping();
    track();
    indoor();
    return checkSummary("gpx");
}
