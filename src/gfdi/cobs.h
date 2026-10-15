#pragma once

#include "gfdi/bytes.h"

namespace Gfdi {

// Garmin's COBS: a leading 0x00, the consistent-overhead-byte-stuffed payload,
// a trailing 0x00. The leading zero is what makes a frame self-delimiting when
// BLE notifications are cut at arbitrary places or one is lost.
Bytes cobsEncode(const Bytes &payload);

// Reassembles frames from a byte stream cut at arbitrary places.
class CobsDecoder
{
public:
    // Appends bytes. Complete frames become available through next().
    void feed(const uint8_t *data, size_t length);
    void feed(const Bytes &data) { feed(data.data(), data.size()); }

    // Pops the next decoded payload. False when no complete frame is waiting.
    bool next(Bytes &payload);

    // Bytes that were dropped because they could not belong to a frame (a lost
    // notification leaves a tail with no start). For diagnostics.
    size_t discarded() const { return m_discarded; }

private:
    bool decodeOne(Bytes &payload);

    Bytes m_buf;
    size_t m_discarded = 0;
};

} // namespace Gfdi
