#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Just enough of a ZIP reader for Garmin Connect's activity download (a zip with the
// original FIT inside): the central directory, and
// stored or deflated entries. No encryption, no ZIP64 (a ride is far
// below 4 GB), no multi-disk archives. Inflating uses zlib, which is
// already linked into every Sailfish app through Qt.
namespace Garmin {

struct ZipEntry
{
    std::string name;
    uint32_t compressedSize = 0;
    uint32_t size = 0;
    uint16_t method = 0;       // 0 stored, 8 deflate
    uint32_t localOffset = 0;
};

bool listZip(const std::string &archive, std::vector<ZipEntry> &entries, std::string &error);
bool extractZipEntry(const std::string &archive, const ZipEntry &entry, std::string &out, std::string &error);

} // namespace Garmin
