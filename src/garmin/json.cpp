#include "json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <sstream>

namespace Garmin {

namespace {

// Numbers are read and written here with the classic locale on purpose. strtod
// and printf follow the process locale, and a Qt application takes that from the
// environment: on a Finnish phone the decimal separator is a comma, so
// strtod("25919999,\"id_token\"") consumes the comma as a decimal point and a
// JSON object ends up unparseable. Found that way on a real device.
size_t numberLength(const char *p, size_t n)
{
    size_t i = 0;
    if (i < n && (p[i] == '-' || p[i] == '+'))
        ++i;
    size_t digits = 0;
    while (i < n && p[i] >= '0' && p[i] <= '9') {
        ++i;
        ++digits;
    }
    if (i < n && p[i] == '.') {
        ++i;
        while (i < n && p[i] >= '0' && p[i] <= '9') {
            ++i;
            ++digits;
        }
    }
    if (digits == 0)
        return 0;
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        size_t j = i + 1;
        if (j < n && (p[j] == '+' || p[j] == '-'))
            ++j;
        size_t exponent = 0;
        while (j < n && p[j] >= '0' && p[j] <= '9') {
            ++j;
            ++exponent;
        }
        if (exponent > 0)
            i = j;
    }
    return i;
}

std::string formatDouble(double v)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(15);
    out << v;
    return out.str();
}

double toDouble(const char *p, size_t length)
{
    std::istringstream in(std::string(p, length));
    in.imbue(std::locale::classic());
    double v = 0;
    in >> v;
    return in.fail() ? 0.0 : v;
}

} // namespace

Json Json::boolean(bool v)
{
    Json j;
    j.m_type = Type::Bool;
    j.m_bool = v;
    return j;
}

Json Json::number(double v)
{
    Json j;
    j.m_type = Type::Number;
    j.m_number = v;
    return j;
}

Json Json::string(std::string v)
{
    Json j;
    j.m_type = Type::String;
    j.m_string = std::move(v);
    return j;
}

Json Json::array()
{
    Json j;
    j.m_type = Type::Array;
    return j;
}

Json Json::object()
{
    Json j;
    j.m_type = Type::Object;
    return j;
}

const Json *Json::get(const std::string &key) const
{
    if (m_type != Type::Object)
        return nullptr;
    for (const auto &m : m_object)
        if (m.first == key)
            return &m.second;
    return nullptr;
}

void Json::set(const std::string &key, Json value)
{
    for (auto &m : m_object) {
        if (m.first == key) {
            m.second = std::move(value);
            return;
        }
    }
    m_object.emplace_back(key, std::move(value));
}

void Json::push(Json value)
{
    m_array.push_back(std::move(value));
}

bool Json::hasNumber() const
{
    if (m_type == Type::Number)
        return true;
    if (m_type != Type::String || m_string.empty())
        return false;
    return numberLength(m_string.c_str(), m_string.size()) == m_string.size();
}

double Json::asNumber(double fallback) const
{
    if (m_type == Type::Number)
        return m_number;
    if (m_type == Type::Bool)
        return m_bool ? 1 : 0;
    if (m_type == Type::String && hasNumber())
        return toDouble(m_string.c_str(), m_string.size());
    return fallback;
}

std::string Json::asString(const std::string &fallback) const
{
    if (m_type == Type::String)
        return m_string;
    if (m_type == Type::Number) {
        char buf[48];
        if (m_number == std::floor(m_number) && std::fabs(m_number) < 1e15)
            std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(m_number));
        else
            return formatDouble(m_number);
        return buf;
    }
    return fallback;
}

double Json::num(const std::string &key, double fallback) const
{
    const Json *v = get(key);
    return v ? v->asNumber(fallback) : fallback;
}

std::optional<double> Json::optNum(const std::string &key) const
{
    const Json *v = get(key);
    if (v && v->hasNumber())
        return v->asNumber();
    return std::nullopt;
}

std::string Json::str(const std::string &key, const std::string &fallback) const
{
    const Json *v = get(key);
    return v ? v->asString(fallback) : fallback;
}

// ---- writer -----------------------------------------------------------------

namespace {

void writeString(std::string &out, const std::string &s)
{
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

} // namespace

std::string Json::dump() const
{
    std::string out;
    dumpTo(out);
    return out;
}

void Json::dumpTo(std::string &out) const
{
    switch (m_type) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += m_bool ? "true" : "false"; break;
    case Type::Number: out += asString(); break;
    case Type::String: writeString(out, m_string); break;
    case Type::Array: {
        out += '[';
        for (size_t i = 0; i < m_array.size(); ++i) {
            if (i)
                out += ',';
            m_array[i].dumpTo(out);
        }
        out += ']';
        break;
    }
    case Type::Object: {
        out += '{';
        bool first = true;
        for (const auto &m : m_object) {
            if (!first)
                out += ',';
            first = false;
            writeString(out, m.first);
            out += ':';
            m.second.dumpTo(out);
        }
        out += '}';
        break;
    }
    }
}

// ---- parser -----------------------------------------------------------------

namespace {

struct Parser
{
    const std::string &s;
    size_t i = 0;
    int depth = 0;

    explicit Parser(const std::string &text) : s(text) {}

    void ws()
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }

    static void utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xc0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xe0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (cp & 0x3f));
        }
    }

    bool hex4(unsigned &v)
    {
        if (i + 4 > s.size())
            return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool parseString(std::string &out)
    {
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i >= s.size())
                return false;
            const char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned cp;
                if (!hex4(cp))
                    return false;
                if (cp >= 0xd800 && cp < 0xdc00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    i += 2;
                    unsigned low;
                    if (!hex4(low))
                        return false;
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                }
                utf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool literal(const char *word)
    {
        size_t k = 0;
        while (word[k]) {
            if (i + k >= s.size() || s[i + k] != word[k])
                return false;
            ++k;
        }
        i += k;
        return true;
    }

    bool value(Json &out)
    {
        if (++depth > 64)
            return false;
        ws();
        if (i >= s.size())
            return false;
        bool ok = false;
        const char c = s[i];
        if (c == '{') {
            ++i;
            out = Json::object();
            ws();
            if (i < s.size() && s[i] == '}') {
                ++i;
                ok = true;
            } else {
                for (;;) {
                    ws();
                    std::string key;
                    if (!parseString(key))
                        break;
                    ws();
                    if (i >= s.size() || s[i++] != ':')
                        break;
                    Json v;
                    if (!value(v))
                        break;
                    out.set(key, std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == '}') {
                        ++i;
                        ok = true;
                    }
                    break;
                }
            }
        } else if (c == '[') {
            ++i;
            out = Json::array();
            ws();
            if (i < s.size() && s[i] == ']') {
                ++i;
                ok = true;
            } else {
                for (;;) {
                    Json v;
                    if (!value(v))
                        break;
                    out.push(std::move(v));
                    ws();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == ']') {
                        ++i;
                        ok = true;
                    }
                    break;
                }
            }
        } else if (c == '"') {
            std::string str;
            ok = parseString(str);
            if (ok)
                out = Json::string(std::move(str));
        } else if (c == 't') {
            ok = literal("true");
            if (ok)
                out = Json::boolean(true);
        } else if (c == 'f') {
            ok = literal("false");
            if (ok)
                out = Json::boolean(false);
        } else if (c == 'n') {
            ok = literal("null");
            if (ok)
                out = Json();
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            const size_t used = numberLength(s.c_str() + i, s.size() - i);
            ok = used > 0;
            if (ok) {
                out = Json::number(toDouble(s.c_str() + i, used));
                i += used;
            }
        } else if (c == 'N' && literal("NaN")) {
            // Some exports write NaN for a missing number.
            out = Json();
            ok = true;
        }
        --depth;
        return ok;
    }
};

} // namespace

std::optional<Json> Json::parse(const std::string &text, size_t *errorAt)
{
    Parser p(text);
    Json out;
    if (!p.value(out)) {
        if (errorAt)
            *errorAt = p.i;
        return std::nullopt;
    }
    p.ws();
    if (p.i != text.size()) {
        if (errorAt)
            *errorAt = p.i;
        return std::nullopt;
    }
    return out;
}

} // namespace Garmin
