#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// What the Garmin Connect client needs from an HTTP stack, and nothing more.
//
// The client's logic (the sign-in sequence, token refresh, what an answer means)
// is the part worth testing, and it needs no Qt: it talks to this interface, a
// test answers it from a script, and the app answers it with
// QNetworkAccessManager (garmin/qtransport.h). A transport must keep cookies
// across requests: the sign-in is several requests that share a session.
namespace Garmin {

struct Header
{
    std::string name;
    std::string value;
};

struct HttpRequest
{
    std::string method = "GET";
    std::string url;
    std::vector<Header> headers;
    std::string body;
    int timeoutMs = 30000;
};

struct HttpResponse
{
    int status = 0; // 0: no HTTP answer at all, `error` says why
    std::vector<Header> headers;
    std::string body;
    std::string error;

    bool ok() const { return status >= 200 && status < 300; }
    std::string header(const std::string &name) const;
};

using HttpDone = std::function<void(const HttpResponse &)>;
// Starts the request and calls `done` exactly once, possibly before returning.
using HttpTransport = std::function<void(const HttpRequest &, const HttpDone &)>;

std::string urlEncode(const std::string &text);
std::string formEncode(const std::vector<std::pair<std::string, std::string>> &fields);

// A multipart/form-data body with one file part. `boundary` must not occur in the bytes.
std::string multipartFile(const std::string &boundary, const std::string &field, const std::string &filename,
                          const std::string &contentType, const std::string &bytes);

std::string base64Encode(const std::string &bytes);
// Accepts both the standard and the URL-safe alphabet, with or without padding.
bool base64Decode(const std::string &text, std::string &out);

} // namespace Garmin
