#include "garmin/api.h"

#include "garmin/json.h"
#include "garmin/zip.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace Garmin {

namespace {

const char *const kApi = "https://connectapi.garmin.com";
constexpr int64_t kRefreshMargin = 900; // renew a token with less than 15 minutes left

std::vector<Header> nativeHeaders()
{
    return {
        { "User-Agent", "GCM-Android-5.23" },
        { "X-Garmin-User-Agent", "com.garmin.android.apps.connectmobile/5.23; ; Google/sdk_gphone64_arm64/google; Android/33; Dalvik/2.1.0" },
        { "X-Garmin-Paired-App-Version", "10861" },
        { "X-Garmin-Client-Platform", "Android" },
        { "X-App-Ver", "10861" },
        { "X-Lang", "en" },
        { "X-GCExperience", "GC5" },
        { "Accept-Language", "en-US,en;q=0.9" },
    };
}

// Days since 1970-01-01 of a civil date (Howard Hinnant's algorithm).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

std::string messageFrom(const std::string &body)
{
    const auto doc = Json::parse(body);
    if (!doc || !doc->isObject())
        return std::string();
    std::string m = doc->str("message");
    if (m.empty())
        m = doc->str("content");
    return m;
}

bool looksLikeFit(const std::string &b)
{
    return b.size() >= 12 && b.compare(8, 4, ".FIT") == 0;
}

} // namespace

int64_t parseGarminTime(const std::string &t)
{
    int y, mo, d, h, mi, s;
    if (std::sscanf(t.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6)
        return 0;
    return daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 + h * 3600 + mi * 60 + s;
}

Api::Api(HttpTransport http, Auth &auth, std::function<int64_t()> clock)
    : m_http(std::move(http)), m_auth(auth), m_clock(std::move(clock))
{
}

ApiError Api::errorFor(const HttpResponse &r)
{
    ApiError e;
    e.http = r.status;
    if (r.status == 0) {
        e.status = ApiStatus::Network;
        e.message = "No answer from Garmin: " + (r.error.empty() ? std::string("no connection") : r.error);
    } else if (r.status == 401) {
        e.status = ApiStatus::SignedOut;
        e.message = "Garmin no longer accepts the saved sign-in. Sign in again.";
    } else if (r.status == 404) {
        e.status = ApiStatus::NotFound;
        e.message = "Garmin has no such item (404).";
    } else if (r.status == 429 || r.status == 403) {
        e.status = ApiStatus::Blocked;
        e.message = "Garmin answered " + std::to_string(r.status) + ": it is rate-limiting or blocking this client. Try again later.";
    } else if (r.status >= 500) {
        e.status = ApiStatus::Server;
        e.message = "Garmin's server failed (HTTP " + std::to_string(r.status) + ").";
    } else if (!r.ok()) {
        e.status = ApiStatus::Protocol;
        const std::string detail = messageFrom(r.body);
        e.message = "Garmin answered HTTP " + std::to_string(r.status) + (detail.empty() ? std::string() : ": " + detail);
    }
    return e;
}

void Api::ensureFresh(bool force, const std::function<void(const ApiError &)> &then)
{
    const bool due = force || (m_tokens.expiresAt != 0 && m_tokens.expiresAt - m_clock() < kRefreshMargin);
    if (!due) {
        then(ApiError());
        return;
    }
    if (!m_tokens.canRefresh()) {
        ApiError e;
        // Nothing to renew with. If the token is not yet expired it may still work.
        if (force || m_tokens.expiresAt <= m_clock()) {
            e.status = ApiStatus::SignedOut;
            e.message = "The saved Garmin sign-in has expired. Sign in again.";
        }
        then(e);
        return;
    }
    m_auth.refresh(m_tokens, [this, then](const AuthResult &r) {
        ApiError e;
        if (r.status == AuthStatus::Ok) {
            m_tokens = r.tokens;
            if (m_onTokens)
                m_onTokens(m_tokens);
            then(e);
            return;
        }
        e.message = r.message;
        switch (r.status) {
        case AuthStatus::BadCredentials: e.status = ApiStatus::SignedOut; break;
        case AuthStatus::Blocked: e.status = ApiStatus::Blocked; break;
        case AuthStatus::Network: e.status = ApiStatus::Network; break;
        default: e.status = ApiStatus::Protocol; break;
        }
        then(e);
    });
}

void Api::call(HttpRequest request, const Raw &done)
{
    ensureFresh(false, [this, request, done](const ApiError &e) {
        if (!e.ok()) {
            done(e, HttpResponse());
            return;
        }
        send(request, true, done);
    });
}

void Api::send(const HttpRequest &request, bool mayRetry, const Raw &done)
{
    HttpRequest req = request;
    std::vector<Header> headers = nativeHeaders();
    // The request's own headers win over the defaults.
    for (const Header &h : request.headers) {
        headers.erase(std::remove_if(headers.begin(), headers.end(),
                                     [&](const Header &x) { return x.name == h.name; }),
                      headers.end());
        headers.push_back(h);
    }
    headers.push_back({ "Authorization", "Bearer " + m_tokens.access });
    req.headers = headers;

    m_http(req, [this, request, mayRetry, done](const HttpResponse &r) {
        if (r.status == 401 && mayRetry) {
            // The token was refused: renew it once and send the request again.
            ensureFresh(true, [this, request, done](const ApiError &e) {
                if (!e.ok()) {
                    done(e, HttpResponse());
                    return;
                }
                send(request, false, done);
            });
            return;
        }
        done(ApiError(), r);
    });
}

std::string garminDate(int64_t t)
{
    // Civil date from days since 1970-01-01 (Howard Hinnant's algorithm).
    int64_t z = t / 86400;
    if (t % 86400 < 0)
        --z;
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", static_cast<int>(y + (m <= 2)), m, d);
    return buf;
}

void Api::listActivities(int start, int limit,
                         const std::function<void(const ApiError &, const std::vector<RemoteActivity> &)> &done,
                         const std::string &startDate, const std::string &endDate)
{
    HttpRequest req;
    req.url = std::string(kApi) + "/activitylist-service/activities/search/activities?start=" + std::to_string(start)
            + "&limit=" + std::to_string(std::min(limit, 100));
    if (!startDate.empty())
        req.url += "&startDate=" + startDate;
    if (!endDate.empty())
        req.url += "&endDate=" + endDate;
    req.headers = { { "Accept", "application/json" } };
    call(req, [done](const ApiError &e, const HttpResponse &r) {
        if (!e.ok()) {
            done(e, {});
            return;
        }
        if (!r.ok()) {
            done(errorFor(r), {});
            return;
        }
        const auto doc = Json::parse(r.body);
        if (!doc || !doc->isArray()) {
            ApiError pe;
            pe.status = ApiStatus::Protocol;
            pe.message = "Garmin's activity list was not a list.";
            done(pe, {});
            return;
        }
        std::vector<RemoteActivity> out;
        for (const Json &item : doc->items()) {
            if (!item.isObject())
                continue;
            RemoteActivity a;
            a.id = static_cast<int64_t>(item.num("activityId", 0));
            a.start = parseGarminTime(item.str("startTimeGMT"));
            a.name = item.str("activityName");
            if (const Json *type = item.get("activityType"))
                a.typeKey = type->str("typeKey");
            a.distance = item.num("distance", -1);
            a.duration = item.num("duration", -1);
            if (a.id > 0 && a.start > 0)
                out.push_back(a);
        }
        done(ApiError(), out);
    });
}

void Api::downloadFit(int64_t activityId, const std::function<void(const ApiError &, const std::string &)> &done)
{
    HttpRequest req;
    req.url = std::string(kApi) + "/download-service/files/activity/" + std::to_string(activityId);
    req.headers = { { "Accept", "*/*" } };
    req.timeoutMs = 120000;
    call(req, [done](const ApiError &e, const HttpResponse &r) {
        if (!e.ok()) {
            done(e, std::string());
            return;
        }
        if (!r.ok()) {
            done(errorFor(r), std::string());
            return;
        }
        ApiError pe;
        pe.status = ApiStatus::Protocol;
        if (r.body.empty()) {
            pe.message = "Garmin has no original file for this activity.";
            done(pe, std::string());
            return;
        }
        if (looksLikeFit(r.body)) { // some answers are the bare file
            done(ApiError(), r.body);
            return;
        }
        std::vector<ZipEntry> entries;
        std::string zipError;
        if (!listZip(r.body, entries, zipError)) {
            pe.message = "Garmin's download was not a zip file (" + zipError + ").";
            done(pe, std::string());
            return;
        }
        const ZipEntry *pick = nullptr;
        for (const ZipEntry &z : entries) {
            std::string n = z.name;
            for (char &c : n)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".fit") == 0) {
                pick = &z;
                break;
            }
        }
        if (!pick) {
            pe.message = "The zip Garmin sent holds no FIT file.";
            done(pe, std::string());
            return;
        }
        std::string fit;
        if (!extractZipEntry(r.body, *pick, fit, zipError) || !looksLikeFit(fit)) {
            pe.message = "Could not unpack the FIT file from Garmin's zip" + (zipError.empty() ? std::string() : " (" + zipError + ")") + ".";
            done(pe, std::string());
            return;
        }
        done(ApiError(), fit);
    });
}

void Api::upload(const std::string &fit, const std::string &filename, bool asImport,
                 const std::function<void(const ApiError &, const UploadResult &)> &done)
{
    std::string boundary = "EdgeSyncBoundary7f3a9c41";
    while (fit.find(boundary) != std::string::npos)
        boundary += 'x';

    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kApi) + (asImport ? "/upload-service/upload/fit" : "/upload-service/upload");
    req.headers = { { "Accept", "application/json" }, { "Content-Type", "multipart/form-data; boundary=" + boundary } };
    if (asImport) {
        req.headers.push_back({ "NK", "NT" });
        req.headers.push_back({ "origin", "https://sso.garmin.com" });
        req.headers.push_back({ "User-Agent", "GCM-iOS-5.7.2.1" });
    }
    req.body = multipartFile(boundary, "file", filename, "application/octet-stream", fit);
    req.timeoutMs = 120000;

    call(req, [this, done](const ApiError &e, const HttpResponse &r) {
        UploadResult out;
        if (!e.ok()) {
            done(e, out);
            return;
        }
        if (m_debug)
            m_debug("upload answered HTTP " + std::to_string(r.status) + ": " + r.body.substr(0, 700));
        // 409 is "already there", and carries the same report as a success does.
        if (!r.ok() && r.status != 409) {
            done(errorFor(r), out);
            return;
        }
        const auto doc = Json::parse(r.body);
        const Json *result = doc && doc->isObject() ? doc->get("detailedImportResult") : nullptr;
        if (!result) {
            if (r.status == 409) {
                out.duplicate = true;
                out.message = "Garmin already has this activity.";
                done(ApiError(), out);
                return;
            }
            ApiError pe;
            pe.status = ApiStatus::Protocol;
            pe.message = "Garmin's upload answer had no import report.";
            done(pe, out);
            return;
        }
        out.uploadId = static_cast<int64_t>(result->num("uploadId", 0));
        std::string problem;
        if (const Json *successes = result->get("successes")) {
            for (const Json &s : successes->items()) {
                out.uploaded = true;
                const double id = s.num("internalId", 0);
                if (id > 0)
                    out.activityId = static_cast<int64_t>(id);
            }
        }
        if (const Json *failures = result->get("failures")) {
            for (const Json &f : failures->items()) {
                const Json *messages = f.get("messages");
                if (!messages)
                    continue;
                for (const Json &m : messages->items()) {
                    if (static_cast<int>(m.num("code", 0)) == 202)
                        out.duplicate = true;
                    else if (problem.empty())
                        problem = m.str("content");
                }
                const double id = f.num("internalId", 0);
                if (out.duplicate && id > 0 && out.activityId == 0)
                    out.activityId = static_cast<int64_t>(id);
            }
        }
        if (out.uploaded) {
            out.duplicate = false;
            out.message = "Uploaded.";
        } else if (out.duplicate) {
            out.message = "Garmin already has this activity.";
        } else if (problem.empty()) {
            // Nothing succeeded and nothing was refused: Garmin has the file and is working on
            // it. (HTTP 202, as seen with a file from another device.)
            out.accepted = true;
            out.message = "Garmin accepted the file and is processing it.";
        } else {
            ApiError pe;
            pe.status = ApiStatus::Protocol;
            pe.message = "Garmin refused the file" + (problem.empty() ? std::string() : ": " + problem) + ".";
            done(pe, out);
            return;
        }
        done(ApiError(), out);
    });
}

} // namespace Garmin
