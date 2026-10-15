#include "garmin/http.h"

#include <cctype>

namespace Garmin {

std::string HttpResponse::header(const std::string &name) const
{
    auto lower = [](std::string s) {
        for (char &c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string want = lower(name);
    for (const Header &h : headers)
        if (lower(h.name) == want)
            return h.value;
    return std::string();
}

std::string urlEncode(const std::string &text)
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields)
{
    std::string out;
    for (const auto &f : fields) {
        if (!out.empty())
            out += '&';
        out += urlEncode(f.first) + '=' + urlEncode(f.second);
    }
    return out;
}

std::string multipartFile(const std::string &boundary, const std::string &field, const std::string &filename,
                          const std::string &contentType, const std::string &bytes)
{
    std::string out;
    out += "--" + boundary + "\r\n";
    out += "Content-Disposition: form-data; name=\"" + field + "\"; filename=\"" + filename + "\"\r\n";
    out += "Content-Type: " + contentType + "\r\n\r\n";
    out += bytes;
    out += "\r\n--" + boundary + "--\r\n";
    return out;
}

std::string base64Encode(const std::string &bytes)
{
    static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < bytes.size()) {
        const uint32_t v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8)
                | static_cast<unsigned char>(bytes[i + 2]);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += tbl[v & 63];
        i += 3;
    }
    if (i + 1 == bytes.size()) {
        const uint32_t v = static_cast<unsigned char>(bytes[i]) << 16;
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const uint32_t v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
        out += tbl[(v >> 18) & 63];
        out += tbl[(v >> 12) & 63];
        out += tbl[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

bool base64Decode(const std::string &text, std::string &out)
{
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char ch : text) {
        int v;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '+' || ch == '-') v = 62;
        else if (ch == '/' || ch == '_') v = 63;
        else if (ch == '=' || ch == '\n' || ch == '\r' || ch == ' ') continue;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    return true;
}

} // namespace Garmin
