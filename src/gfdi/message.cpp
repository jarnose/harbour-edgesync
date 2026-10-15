#include "gfdi/message.h"

#include "gfdi/crc.h"

namespace Gfdi {

bool parseFrame(const Bytes &raw, Frame &out, std::string &error)
{
    if (raw.size() < 6) {
        error = "frame shorter than the 6-byte minimum";
        return false;
    }
    Reader r(raw);
    const size_t size = r.u16();
    if (size != raw.size()) {
        error = "frame size field " + std::to_string(size) + " does not match the " + std::to_string(raw.size())
                + " bytes received";
        return false;
    }
    const uint16_t expected = crc16(raw.data(), raw.size() - 2);
    const uint16_t actual = static_cast<uint16_t>(raw[raw.size() - 2] | (raw[raw.size() - 1] << 8));
    if (expected != actual) {
        error = "frame CRC mismatch";
        return false;
    }

    uint32_t type = r.u16();
    if (type & 0x8000)
        type = (type & 0xFF) + 5000;
    out.type = static_cast<uint16_t>(type);
    out.payload.assign(raw.begin() + 4, raw.end() - 2);
    return true;
}

Bytes buildFrame(uint16_t type, const Bytes &payload)
{
    Writer w;
    w.u16(static_cast<uint32_t>(2 + 2 + payload.size() + 2)).u16(type).raw(payload);
    const uint16_t crc = crc16(w.bytes().data(), w.size());
    w.u16(crc);
    return w.take();
}

Bytes buildResponse(uint16_t originalType, Status status, const Bytes &extra)
{
    Writer w;
    w.u16(originalType).u8(status).raw(extra);
    return buildFrame(MsgResponse, w.bytes());
}

} // namespace Gfdi
