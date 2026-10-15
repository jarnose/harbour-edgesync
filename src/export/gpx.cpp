#include "export/gpx.h"

#include <cstdio>

namespace Export {

std::string xmlEscape(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:
            // XML 1.0 has no control characters other than tab, newline, return.
            if (static_cast<unsigned char>(c) >= 0x20 || c == '\t' || c == '\n' || c == '\r')
                out += c;
            break;
        }
    }
    return out;
}

// Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm), so the
// text does not depend on the C library's time zone state.
static void civilFromDays(int64_t z, int &year, unsigned &month, unsigned &day)
{
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(y + (month <= 2));
}

std::string isoUtc(int64_t t)
{
    int64_t days = t / 86400;
    int64_t rem = t % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    int year;
    unsigned month, day;
    civilFromDays(days, year, month, day);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02d:%02d:%02dZ", year, month, day, static_cast<int>(rem / 3600),
                  static_cast<int>(rem % 3600 / 60), static_cast<int>(rem % 60));
    return buf;
}

std::string toGpx(const Fit::Activity &a, const std::string &name)
{
    bool any = false;
    for (const Fit::Sample &s : a.samples) {
        if (s.hasPosition()) {
            any = true;
            break;
        }
    }
    if (!any)
        return std::string();

    std::string out;
    out.reserve(a.samples.size() * 220 + 1024);
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<gpx version=\"1.1\" creator=\"Edge Sync\" xmlns=\"http://www.topografix.com/GPX/1/1\" "
           "xmlns:gpxtpx=\"http://www.garmin.com/xmlschemas/TrackPointExtension/v1\" "
           "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
           "xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd\">\n";
    out += " <metadata>\n  <name>" + xmlEscape(name) + "</name>\n";
    if (a.summary.startTime != 0)
        out += "  <time>" + isoUtc(a.summary.startTime) + "</time>\n";
    out += " </metadata>\n <trk>\n  <name>" + xmlEscape(name) + "</name>\n";

    char buf[160];
    bool open = false;
    int64_t lastTime = 0;
    for (const Fit::Sample &s : a.samples) {
        if (!s.hasPosition()) {
            // A gap in the fix: close the segment now, so a later fix starts a new one.
            if (open) {
                out += "  </trkseg>\n";
                open = false;
            }
            continue;
        }
        if (open && s.time - lastTime > 30) {
            out += "  </trkseg>\n";
            open = false;
        }
        if (!open) {
            out += "  <trkseg>\n";
            open = true;
        }
        lastTime = s.time;

        std::snprintf(buf, sizeof buf, "   <trkpt lat=\"%.7f\" lon=\"%.7f\">\n", s.lat, s.lon);
        out += buf;
        if (!std::isnan(s.altitude)) {
            std::snprintf(buf, sizeof buf, "    <ele>%.1f</ele>\n", s.altitude);
            out += buf;
        }
        if (s.time != 0)
            out += "    <time>" + isoUtc(s.time) + "</time>\n";

        const bool hasExt = s.heartRate > 0 || s.cadence >= 0 || !std::isnan(s.temperature) || s.power >= 0;
        if (hasExt) {
            out += "    <extensions>\n";
            if (s.power >= 0) {
                std::snprintf(buf, sizeof buf, "     <power>%d</power>\n", s.power);
                out += buf;
            }
            if (s.heartRate > 0 || s.cadence >= 0 || !std::isnan(s.temperature)) {
                out += "     <gpxtpx:TrackPointExtension>\n";
                if (!std::isnan(s.temperature)) {
                    std::snprintf(buf, sizeof buf, "      <gpxtpx:atemp>%.0f</gpxtpx:atemp>\n", s.temperature);
                    out += buf;
                }
                if (s.heartRate > 0) {
                    std::snprintf(buf, sizeof buf, "      <gpxtpx:hr>%d</gpxtpx:hr>\n", s.heartRate);
                    out += buf;
                }
                if (s.cadence >= 0) {
                    std::snprintf(buf, sizeof buf, "      <gpxtpx:cad>%d</gpxtpx:cad>\n", s.cadence);
                    out += buf;
                }
                out += "     </gpxtpx:TrackPointExtension>\n";
            }
            out += "    </extensions>\n";
        }
        out += "   </trkpt>\n";
    }
    if (open)
        out += "  </trkseg>\n";
    out += " </trk>\n</gpx>\n";
    return out;
}

} // namespace Export
