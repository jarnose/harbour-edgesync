#pragma once

#include <cstddef>
#include <cstdint>

namespace Gfdi {

// The CRC-16 of GFDI frames and of file-transfer chunks: the nibble-table form
// of CRC-16/ARC (polynomial 0xA001, reflected). `seed` is the running value, so
// a file's checksum is the fold of its chunks; a frame starts from 0.
uint16_t crc16(const uint8_t *data, size_t length, uint16_t seed = 0);

} // namespace Gfdi
