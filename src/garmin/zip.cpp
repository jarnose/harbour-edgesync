#include "zip.h"

#include <zlib.h>

#include <cstring>

namespace Garmin {

namespace {

uint16_t u16(const std::string &s, size_t i)
{
    return static_cast<uint16_t>(static_cast<unsigned char>(s[i]) | (static_cast<unsigned char>(s[i + 1]) << 8));
}

uint32_t u32(const std::string &s, size_t i)
{
    return static_cast<uint32_t>(u16(s, i)) | (static_cast<uint32_t>(u16(s, i + 2)) << 16);
}

} // namespace

bool listZip(const std::string &a, std::vector<ZipEntry> &entries, std::string &error)
{
    entries.clear();
    if (a.size() < 22) {
        error = "not a ZIP archive";
        return false;
    }
    // End of central directory: scan back over a possible comment.
    size_t eocd = std::string::npos;
    const size_t lowest = a.size() > 22 + 65535 ? a.size() - 22 - 65535 : 0;
    for (size_t i = a.size() - 22 + 1; i-- > lowest;) {
        if (u32(a, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        error = "no ZIP directory found";
        return false;
    }
    const uint16_t count = u16(a, eocd + 10);
    const uint32_t dirOffset = u32(a, eocd + 16);
    if (count == 0xffff || dirOffset == 0xffffffffu) {
        error = "ZIP64 archives are not supported";
        return false;
    }

    size_t p = dirOffset;
    for (uint16_t n = 0; n < count; ++n) {
        if (p + 46 > a.size() || u32(a, p) != 0x02014b50) {
            error = "damaged ZIP directory";
            return false;
        }
        ZipEntry e;
        const uint16_t flags = u16(a, p + 8);
        e.method = u16(a, p + 10);
        e.compressedSize = u32(a, p + 20);
        e.size = u32(a, p + 24);
        const uint16_t nameLen = u16(a, p + 28);
        const uint16_t extraLen = u16(a, p + 30);
        const uint16_t commentLen = u16(a, p + 32);
        e.localOffset = u32(a, p + 42);
        if (p + 46 + nameLen > a.size()) {
            error = "damaged ZIP directory";
            return false;
        }
        e.name = a.substr(p + 46, nameLen);
        p += 46u + nameLen + extraLen + commentLen;
        if (flags & 1) {
            error = "encrypted ZIP entries are not supported";
            return false;
        }
        entries.push_back(std::move(e));
    }
    return true;
}

bool extractZipEntry(const std::string &a, const ZipEntry &e, std::string &out, std::string &error)
{
    out.clear();
    const size_t h = e.localOffset;
    if (h + 30 > a.size() || u32(a, h) != 0x04034b50) {
        error = "damaged ZIP entry header for " + e.name;
        return false;
    }
    const size_t dataStart = h + 30 + u16(a, h + 26) + u16(a, h + 28);
    if (dataStart + e.compressedSize > a.size()) {
        error = "truncated ZIP entry " + e.name;
        return false;
    }
    if (e.method == 0) {
        out.assign(a, dataStart, e.compressedSize);
        return true;
    }
    if (e.method != 8) {
        error = "unsupported ZIP compression for " + e.name;
        return false;
    }

    z_stream z;
    std::memset(&z, 0, sizeof z);
    if (inflateInit2(&z, -15) != Z_OK) {
        error = "zlib could not start";
        return false;
    }
    z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(a.data() + dataStart));
    z.avail_in = e.compressedSize;
    out.reserve(e.size);
    char buf[64 * 1024];
    int rc;
    do {
        z.next_out = reinterpret_cast<Bytef *>(buf);
        z.avail_out = sizeof buf;
        rc = inflate(&z, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&z);
            error = "damaged compressed data in " + e.name;
            return false;
        }
        out.append(buf, sizeof buf - z.avail_out);
    } while (rc != Z_STREAM_END && (z.avail_in > 0 || z.avail_out == 0));
    inflateEnd(&z);
    if (rc != Z_STREAM_END) {
        error = "incomplete compressed data in " + e.name;
        return false;
    }
    return true;
}

} // namespace Garmin
