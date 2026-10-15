#include "gfdi/cobs.h"

namespace Gfdi {

Bytes cobsEncode(const Bytes &data)
{
    Bytes out;
    out.reserve(data.size() + data.size() / 254 + 4);
    out.push_back(0x00);

    const size_t n = data.size();
    size_t i = 0;
    while (i < n) {
        size_t start = i;
        while (i < n && data[i] != 0x00)
            ++i;
        const bool endedOnZero = i < n;
        size_t runLength = i - start;

        // A run of 254 or more non-zero bytes is cut into 0xFF blocks, each of
        // which carries 254 bytes and implies no zero after it.
        while (runLength >= 0xFE) {
            out.push_back(0xFF);
            out.insert(out.end(), data.begin() + start, data.begin() + start + 0xFE);
            runLength -= 0xFE;
            start += 0xFE;
        }
        out.push_back(static_cast<uint8_t>(runLength + 1));
        out.insert(out.end(), data.begin() + start, data.begin() + start + runLength);

        if (endedOnZero)
            ++i; // the zero is implied by the block boundary
    }

    // A payload that ends in 0x00 needs one more, empty, block to say so.
    if (!data.empty() && data.back() == 0x00)
        out.push_back(0x01);

    out.push_back(0x00);
    return out;
}

void CobsDecoder::feed(const uint8_t *data, size_t length)
{
    m_buf.insert(m_buf.end(), data, data + length);
}

bool CobsDecoder::next(Bytes &payload)
{
    return decodeOne(payload);
}

bool CobsDecoder::decodeOne(Bytes &payload)
{
    for (;;) {
        // Anything before the first 0x00 is the tail of a frame whose start was lost.
        size_t first = 0;
        while (first < m_buf.size() && m_buf[first] != 0x00)
            ++first;
        if (first > 0) {
            m_discarded += first;
            m_buf.erase(m_buf.begin(), m_buf.begin() + first);
        }
        // A trailing zero followed straight away by the next frame's leading
        // zero shows up as 00 00: the first one closes nothing, drop it.
        while (m_buf.size() >= 2 && m_buf[1] == 0x00) {
            ++m_discarded;
            m_buf.erase(m_buf.begin());
        }
        if (m_buf.size() < 2)
            return false;

        size_t end = 1;
        while (end < m_buf.size() && m_buf[end] != 0x00)
            ++end;
        if (end >= m_buf.size())
            return false; // frame not complete yet

        // m_buf[0] is the leading zero, m_buf[end] the trailing one.
        Bytes out;
        out.reserve(end);
        size_t i = 1;
        bool truncated = false;
        while (i < end) {
            const uint32_t code = m_buf[i++];
            const size_t run = code - 1;
            if (i + run > end) {
                truncated = true;
                break;
            }
            out.insert(out.end(), m_buf.begin() + i, m_buf.begin() + i + run);
            i += run;
            // Every block but a 0xFF one, and but the last, stands for a zero.
            if (code != 0xFF && i < end)
                out.push_back(0x00);
        }

        // The trailing zero is left in place: it doubles as the next frame's
        // leading zero if that frame was sent without its own.
        m_buf.erase(m_buf.begin(), m_buf.begin() + end);

        if (truncated) {
            m_discarded += end;
            continue;
        }
        payload = std::move(out);
        return true;
    }
}

} // namespace Gfdi
