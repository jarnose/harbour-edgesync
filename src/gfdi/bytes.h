#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Gfdi {

using Bytes = std::vector<uint8_t>;

// Little-endian appender. Every GFDI integer is little-endian.
class Writer
{
public:
    Writer &u8(uint32_t v)
    {
        m_out.push_back(static_cast<uint8_t>(v));
        return *this;
    }
    Writer &u16(uint32_t v) { return u8(v).u8(v >> 8); }
    Writer &u32(uint32_t v) { return u16(v).u16(v >> 16); }
    Writer &u64(uint64_t v) { return u32(static_cast<uint32_t>(v)).u32(static_cast<uint32_t>(v >> 32)); }
    Writer &raw(const Bytes &b)
    {
        m_out.insert(m_out.end(), b.begin(), b.end());
        return *this;
    }
    Writer &raw(const uint8_t *p, size_t n)
    {
        m_out.insert(m_out.end(), p, p + n);
        return *this;
    }
    // One length byte, then the UTF-8 bytes; longer than 255 is cut.
    Writer &str(const std::string &s)
    {
        const size_t n = s.size() > 255 ? 255 : s.size();
        u8(static_cast<uint32_t>(n));
        return raw(reinterpret_cast<const uint8_t *>(s.data()), n);
    }
    const Bytes &bytes() const { return m_out; }
    Bytes take() { return std::move(m_out); }
    size_t size() const { return m_out.size(); }

private:
    Bytes m_out;
};

// Little-endian cursor. Reading past the end does not throw: it returns 0 and
// clears ok(), so a parser reads its whole layout and checks once at the end.
class Reader
{
public:
    Reader(const uint8_t *p, size_t n) : m_p(p), m_n(n) {}
    explicit Reader(const Bytes &b) : m_p(b.data()), m_n(b.size()) {}

    bool ok() const { return m_ok; }
    size_t remaining() const { return m_n - m_pos; }
    size_t position() const { return m_pos; }

    uint32_t u8()
    {
        if (m_pos + 1 > m_n) {
            m_ok = false;
            return 0;
        }
        return m_p[m_pos++];
    }
    uint32_t u16()
    {
        const uint32_t lo = u8();
        return lo | (u8() << 8);
    }
    uint32_t u32()
    {
        const uint32_t lo = u16();
        return lo | (u16() << 16);
    }
    uint64_t u64()
    {
        const uint64_t lo = u32();
        return lo | (static_cast<uint64_t>(u32()) << 32);
    }
    Bytes raw(size_t n)
    {
        if (m_pos + n > m_n) {
            m_ok = false;
            m_pos = m_n;
            return Bytes();
        }
        Bytes b(m_p + m_pos, m_p + m_pos + n);
        m_pos += n;
        return b;
    }
    Bytes rest() { return raw(remaining()); }
    std::string str()
    {
        const size_t n = u8();
        const Bytes b = raw(n);
        return std::string(b.begin(), b.end());
    }
    void skip(size_t n)
    {
        if (m_pos + n > m_n) {
            m_ok = false;
            m_pos = m_n;
        } else {
            m_pos += n;
        }
    }

private:
    const uint8_t *m_p;
    size_t m_n;
    size_t m_pos = 0;
    bool m_ok = true;
};

} // namespace Gfdi
