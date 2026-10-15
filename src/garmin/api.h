#pragma once

#include "garmin/auth.h"
#include "garmin/http.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Garmin {

enum class ApiStatus {
    Ok,
    SignedOut, // the tokens are refused and cannot be renewed: sign in again
    NotFound,
    Blocked,   // 429 or 403
    Network,
    Server,    // 5xx
    Protocol,  // an answer this code does not understand
};

struct ApiError
{
    ApiStatus status = ApiStatus::Ok;
    int http = 0;
    std::string message;
    bool ok() const { return status == ApiStatus::Ok; }
};

// One row of the activity list.
struct RemoteActivity
{
    int64_t id = 0;
    int64_t start = 0; // Unix seconds, from startTimeGMT
    std::string name;
    std::string typeKey; // "cycling", "running", ...
    double distance = -1; // metres
    double duration = -1; // seconds
};

struct UploadResult
{
    bool uploaded = false;  // Garmin took it as a new activity
    bool duplicate = false; // Garmin already has it
    int64_t activityId = 0; // when known
    std::string message;
};

// Garmin Connect's data API (connectapi.garmin.com) with the bearer token from
// Auth. Keeps the token fresh: renewed when it is about to expire, and once more
// if an answer says it was refused.
class Api
{
public:
    Api(HttpTransport http, Auth &auth, std::function<int64_t()> clock);

    void setTokens(const Tokens &tokens) { m_tokens = tokens; }
    const Tokens &tokens() const { return m_tokens; }
    // Called whenever a refresh produced new tokens, so they can be saved.
    void setTokensChanged(std::function<void(const Tokens &)> cb) { m_onTokens = std::move(cb); }

    // Most recent first. `limit` at most 100 per call.
    void listActivities(int start, int limit,
                        const std::function<void(const ApiError &, const std::vector<RemoteActivity> &)> &done);

    // The original FIT file of an activity (Garmin serves it zipped).
    void downloadFit(int64_t activityId, const std::function<void(const ApiError &, const std::string &)> &done);

    // Uploads a FIT file. `asImport` uses the endpoint and headers of Garmin
    // Connect Mobile's "import", which marks the activity as imported and keeps it
    // from being passed on to services linked to the account (Strava and the like);
    // the plain upload is what a device sync does.
    void upload(const std::string &fit, const std::string &filename, bool asImport,
                const std::function<void(const ApiError &, const UploadResult &)> &done);

private:
    using Raw = std::function<void(const ApiError &, const HttpResponse &)>;

    // Sends with a fresh token. Errors that are about the token or the network
    // come back as an ApiError; any other HTTP answer is handed over as it is.
    void call(HttpRequest request, const Raw &done);
    void send(const HttpRequest &request, bool mayRetry, const Raw &done);
    void ensureFresh(bool force, const std::function<void(const ApiError &)> &then);
    static ApiError errorFor(const HttpResponse &r);

    HttpTransport m_http;
    Auth &m_auth;
    std::function<int64_t()> m_clock;
    Tokens m_tokens;
    std::function<void(const Tokens &)> m_onTokens;
};

// "2026-10-15 12:21:21" (GMT) to Unix seconds; 0 if it is not that.
int64_t parseGarminTime(const std::string &text);

} // namespace Garmin
