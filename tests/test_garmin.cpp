// The Garmin Connect client against a scripted Garmin: sign-in (with and without a
// verification code), the ways Garmin refuses a client, token refresh, the
// activity list, the download and the upload. The script is written from
// python-garminconnect, yet-another-garmin-connect-client and garmin-cli, so it
// proves the client does what those do; only Garmin itself can prove Garmin agrees.

#include "check.h"
#include "garmin/api.h"
#include "garmin/auth.h"
#include "garmin/json.h"

#include <zlib.h>

#include <cstring>
#include <memory>
#include <map>

using namespace Garmin;

// ---- a scripted server -------------------------------------------------------

struct Server
{
    std::vector<HttpRequest> log;
    std::function<HttpResponse(const HttpRequest &)> handler;

    HttpTransport transport()
    {
        return [this](const HttpRequest &r, const HttpDone &done) {
            log.push_back(r);
            done(handler(r));
        };
    }
    int count(const std::string &needle) const
    {
        int n = 0;
        for (const HttpRequest &r : log)
            if (r.url.find(needle) != std::string::npos)
                ++n;
        return n;
    }
    const HttpRequest *last(const std::string &needle) const
    {
        for (size_t i = log.size(); i-- > 0;)
            if (log[i].url.find(needle) != std::string::npos)
                return &log[i];
        return nullptr;
    }
};

static HttpResponse reply(int status, const std::string &body)
{
    HttpResponse r;
    r.status = status;
    r.body = body;
    return r;
}

static std::string header(const HttpRequest &r, const std::string &name)
{
    for (const Header &h : r.headers)
        if (h.name == name)
            return h.value;
    return std::string();
}

static bool has(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }

static std::string b64url(const std::string &s)
{
    std::string b = base64Encode(s);
    for (char &c : b) {
        if (c == '+') c = '-';
        if (c == '/') c = '_';
    }
    while (!b.empty() && b.back() == '=')
        b.pop_back();
    return b;
}

static std::string jwt(int64_t exp, const std::string &clientId)
{
    return b64url("{\"alg\":\"none\"}") + "." + b64url("{\"exp\":" + std::to_string(exp) + ",\"client_id\":\"" + clientId + "\"}")
            + ".sig";
}

static const int64_t kNow = 1792070000;
static std::function<int64_t()> testClock() { return []() { return kNow; }; }

static std::string tokenAnswer(int64_t exp, const std::string &clientId, const std::string &refresh = "REFRESH-1")
{
    return "{\"access_token\":\"" + jwt(exp, clientId) + "\",\"refresh_token\":\"" + refresh + "\",\"expires_in\":3600}";
}

// ---- helpers -----------------------------------------------------------------

static void encoding()
{
    CHECK_EQ(base64Encode(""), std::string(""));
    CHECK_EQ(base64Encode("f"), std::string("Zg=="));
    CHECK_EQ(base64Encode("fo"), std::string("Zm8="));
    CHECK_EQ(base64Encode("foo"), std::string("Zm9v"));
    CHECK_EQ(base64Encode("CLIENT_ID:"), std::string("Q0xJRU5UX0lEOg=="));
    std::string out;
    CHECK(base64Decode("Zm9vYg==", out));
    CHECK_EQ(out, std::string("foob"));
    CHECK(base64Decode("Zm9vYg", out)); // no padding
    CHECK_EQ(out, std::string("foob"));
    CHECK(base64Decode("-_8", out)); // the URL-safe alphabet
    CHECK_EQ(out, std::string("\xfb\xff"));
    CHECK(!base64Decode("Zm9*", out));

    CHECK_EQ(urlEncode("a b&c=d/é"), std::string("a%20b%26c%3Dd%2F%C3%A9"));
    CHECK_EQ(formEncode({ { "a", "1 2" }, { "b", "x&y" } }), std::string("a=1%202&b=x%26y"));
    CHECK_EQ(parseGarminTime("2026-10-15 12:21:21"), 1792066881);
    CHECK_EQ(parseGarminTime("1970-01-01 00:00:01"), 1);
    CHECK_EQ(parseGarminTime("nonsense"), 0);

    const std::string t = jwt(1792075000, "GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2");
    CHECK_EQ(jwtExpiry(t), 1792075000);
    CHECK_EQ(jwtClientId(t), std::string("GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2"));
    CHECK_EQ(jwtExpiry("not.a.jwt"), 0);
    CHECK_EQ(jwtExpiry("garbage"), 0);

    Tokens tk;
    tk.access = "A";
    tk.refresh = "R";
    tk.clientId = "C";
    tk.expiresAt = 42;
    Tokens back;
    CHECK(Tokens::fromJson(tk.toJson(), back));
    CHECK_EQ(back.access, std::string("A"));
    CHECK_EQ(back.refresh, std::string("R"));
    CHECK_EQ(back.clientId, std::string("C"));
    CHECK_EQ(back.expiresAt, 42);
    CHECK(!Tokens::fromJson("{}", back));
    CHECK(!Tokens::fromJson("not json", back));
}

// ---- sign-in -----------------------------------------------------------------

static const char *kTicketOk = "{\"responseStatus\":{\"type\":\"SUCCESSFUL\"},\"serviceTicketId\":\"ST-123\"}";

static AuthResult doLogin(Server &s, const std::string &pw = "secret")
{
    Auth auth(s.transport(), testClock());
    AuthResult out;
    bool called = false;
    auth.login("me@example.com", pw, [&](const AuthResult &r) { out = r; called = true; });
    CHECK(called);
    return out;
}

static void loginSuccess()
{
    Server s;
    s.handler = [](const HttpRequest &r) {
        if (has(r.url, "/mobile/api/login"))
            return reply(200, kTicketOk);
        if (has(r.url, "diauth.garmin.com")) {
            // The first app id is refused, the second accepted.
            if (has(r.body, "client_id=GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2"))
                return reply(400, "{\"error\":\"invalid_client\"}");
            return reply(200, tokenAnswer(kNow + 3600, "GARMIN_CONNECT_MOBILE_ANDROID_DI_2024Q4"));
        }
        return reply(404, "");
    };
    const AuthResult r = doLogin(s);
    CHECK(r.status == AuthStatus::Ok);
    CHECK_EQ(r.tokens.refresh, std::string("REFRESH-1"));
    CHECK_EQ(r.tokens.clientId, std::string("GARMIN_CONNECT_MOBILE_ANDROID_DI_2024Q4"));
    CHECK_EQ(r.tokens.expiresAt, kNow + 3600);
    CHECK(r.tokens.valid());
    CHECK(r.tokens.canRefresh());

    CHECK_EQ(s.count("/mobile/api/login"), 1);
    const HttpRequest *login = s.last("/mobile/api/login");
    CHECK(login && login->method == "POST");
    CHECK(has(login->url, "clientId=GCM_IOS_DARK"));
    CHECK(has(login->url, "service=https%3A%2F%2Fmobile.integration.garmin.com%2Fgcm%2Fios"));
    CHECK_EQ(header(*login, "Content-Type"), std::string("application/json"));
    CHECK_EQ(header(*login, "Origin"), std::string("https://sso.garmin.com"));
    const auto body = Json::parse(login->body);
    CHECK(body.has_value());
    if (body) {
        CHECK_EQ(body->str("username"), std::string("me@example.com"));
        CHECK_EQ(body->str("password"), std::string("secret"));
        CHECK_EQ(body->str("captchaToken", "x"), std::string(""));
    }

    // The exchange: two attempts, in order, with that client's own Basic auth.
    CHECK_EQ(s.count("diauth.garmin.com"), 2);
    const HttpRequest *ex = s.last("diauth.garmin.com");
    CHECK(ex != nullptr);
    if (ex) {
        CHECK_EQ(header(*ex, "Authorization"), "Basic " + base64Encode("GARMIN_CONNECT_MOBILE_ANDROID_DI_2024Q4:"));
        CHECK(has(ex->body, "service_ticket=ST-123"));
        CHECK(has(ex->body, "grant_type=https%3A%2F%2Fconnectapi.garmin.com%2Fdi-oauth2-service%2Foauth%2Fgrant%2Fservice_ticket"));
        CHECK(has(ex->body, "service_url=https%3A%2F%2Fmobile.integration.garmin.com%2Fgcm%2Fios"));
        CHECK_EQ(header(*ex, "Content-Type"), std::string("application/x-www-form-urlencoded"));
    }
}

static void loginFailures()
{
    {
        Server s;
        s.handler = [](const HttpRequest &) { return reply(200, "{\"responseStatus\":{\"type\":\"INVALID_USERNAME_PASSWORD\"}}"); };
        CHECK(doLogin(s, "wrong").status == AuthStatus::BadCredentials);
        CHECK_EQ(s.count("diauth"), 0);
    }
    {
        Server s;
        s.handler = [](const HttpRequest &) { return reply(429, ""); };
        const AuthResult r = doLogin(s);
        CHECK(r.status == AuthStatus::Blocked);
        CHECK(has(r.message, "429"));
    }
    {
        Server s;
        s.handler = [](const HttpRequest &) { return reply(403, "<html>Just a moment...</html>"); };
        CHECK(doLogin(s).status == AuthStatus::Blocked);
    }
    {
        Server s; // an HTML page with a 200: the bot challenge
        s.handler = [](const HttpRequest &) { return reply(200, "<html><title>Attention Required</title></html>"); };
        const AuthResult r = doLogin(s);
        CHECK(r.status == AuthStatus::Protocol);
        CHECK(has(r.message, "bot protection"));
    }
    {
        Server s;
        s.handler = [](const HttpRequest &) { return reply(200, "{\"responseStatus\":{\"type\":\"CAPTCHA_REQUIRED\"}}"); };
        CHECK(doLogin(s).status == AuthStatus::Blocked);
    }
    {
        Server s; // a 429 inside a JSON body
        s.handler = [](const HttpRequest &) { return reply(200, "{\"error\":{\"status-code\":\"429\"}}"); };
        CHECK(doLogin(s).status == AuthStatus::Blocked);
    }
    {
        Server s;
        s.handler = [](const HttpRequest &) {
            HttpResponse r;
            r.error = "Host not found";
            return r;
        };
        const AuthResult r = doLogin(s);
        CHECK(r.status == AuthStatus::Network);
        CHECK(has(r.message, "Host not found"));
    }
    {
        Server s; // signed in, but no app id gets a token
        s.handler = [](const HttpRequest &r) {
            if (has(r.url, "/mobile/api/login"))
                return reply(200, kTicketOk);
            return reply(401, "");
        };
        const AuthResult r = doLogin(s);
        CHECK(r.status == AuthStatus::Protocol);
        CHECK_EQ(s.count("diauth.garmin.com"), 4); // every app id was tried
    }
    {
        Server s; // the token service rate-limits
        s.handler = [](const HttpRequest &r) {
            if (has(r.url, "/mobile/api/login"))
                return reply(200, kTicketOk);
            return reply(429, "");
        };
        CHECK(doLogin(s).status == AuthStatus::Blocked);
        CHECK_EQ(s.count("diauth.garmin.com"), 1); // and is not hammered
    }
    {
        Server s; // a token answer with no token in it
        s.handler = [](const HttpRequest &r) {
            if (has(r.url, "/mobile/api/login"))
                return reply(200, kTicketOk);
            return reply(200, "{\"nothing\":true}");
        };
        CHECK(doLogin(s).status == AuthStatus::Protocol);
    }
    {
        Server s; // a ticket-less success
        s.handler = [](const HttpRequest &) { return reply(200, "{\"responseStatus\":{\"type\":\"SUCCESSFUL\"}}"); };
        CHECK(doLogin(s).status == AuthStatus::Protocol);
    }
}

static void loginWithMfa()
{
    Server s;
    int codeTries = 0;
    s.handler = [&](const HttpRequest &r) {
        if (has(r.url, "/mobile/api/login"))
            return reply(200, "{\"responseStatus\":{\"type\":\"MFA_REQUIRED\"},\"customerMfaInfo\":{\"mfaLastMethodUsed\":\"sms\"}}");
        if (has(r.url, "/mobile/api/mfa/verifyCode")) {
            ++codeTries;
            if (has(r.body, "\"654321\""))
                return reply(200, kTicketOk);
            return reply(200, "{\"responseStatus\":{\"type\":\"MFA_CODE_INVALID\"}}");
        }
        return reply(200, tokenAnswer(kNow + 3600, "GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2"));
    };
    Auth auth(s.transport(), testClock());

    // A code with no sign-in waiting is refused without a request.
    AuthResult early;
    auth.submitMfa("123456", [&](const AuthResult &r) { early = r; });
    CHECK(early.status == AuthStatus::Protocol);
    CHECK_EQ(s.log.size(), 0u);

    AuthResult r1;
    auth.login("me@example.com", "secret", [&](const AuthResult &r) { r1 = r; });
    CHECK(r1.status == AuthStatus::NeedMfa);
    CHECK_EQ(r1.mfaMethod, std::string("sms"));
    CHECK(auth.mfaPending());
    CHECK_EQ(s.count("diauth"), 0);

    AuthResult r2;
    auth.submitMfa("111111", [&](const AuthResult &r) { r2 = r; });
    CHECK(r2.status == AuthStatus::BadMfaCode);
    CHECK(auth.mfaPending()); // another try is allowed

    AuthResult r3;
    auth.submitMfa("654321", [&](const AuthResult &r) { r3 = r; });
    CHECK(r3.status == AuthStatus::Ok);
    CHECK(!auth.mfaPending());
    CHECK_EQ(codeTries, 2);

    const HttpRequest *v = s.last("/mfa/verifyCode");
    CHECK(v != nullptr);
    if (v) {
        const auto body = Json::parse(v->body);
        CHECK(body.has_value());
        if (body) {
            CHECK_EQ(body->str("mfaMethod"), std::string("sms"));
            CHECK_EQ(body->str("mfaVerificationCode"), std::string("654321"));
            CHECK(body->get("reconsentList") && body->get("reconsentList")->isArray());
        }
    }
}

static void refreshing()
{
    Tokens old;
    old.access = "OLD";
    old.refresh = "R-OLD";
    old.clientId = "GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2";
    old.expiresAt = kNow - 10;

    {
        Server s;
        s.handler = [](const HttpRequest &) { return reply(200, tokenAnswer(kNow + 3600, "GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2", "R-NEW")); };
        Auth auth(s.transport(), testClock());
        AuthResult r;
        auth.refresh(old, [&](const AuthResult &x) { r = x; });
        CHECK(r.status == AuthStatus::Ok);
        CHECK_EQ(r.tokens.refresh, std::string("R-NEW")); // the refresh token rotates
        CHECK(r.tokens.access != "OLD");
        CHECK_EQ(r.tokens.expiresAt, kNow + 3600);
        const HttpRequest *q = s.last("diauth");
        CHECK(q && has(q->body, "grant_type=refresh_token") && has(q->body, "refresh_token=R-OLD"));
        CHECK_EQ(header(*q, "Authorization"), "Basic " + base64Encode(old.clientId + ":"));
    }
    {
        Server s; // an answer with no new refresh token keeps the old one
        s.handler = [](const HttpRequest &) { return reply(200, "{\"access_token\":\"" + jwt(kNow + 7200, "X") + "\"}"); };
        Auth auth(s.transport(), testClock());
        AuthResult r;
        auth.refresh(old, [&](const AuthResult &x) { r = x; });
        CHECK(r.status == AuthStatus::Ok);
        CHECK_EQ(r.tokens.refresh, std::string("R-OLD"));
        CHECK_EQ(r.tokens.clientId, std::string("X"));
    }
    {
        Server s;
        s.handler = [](const HttpRequest &) { return reply(400, "{\"error\":\"invalid_grant\"}"); };
        Auth auth(s.transport(), testClock());
        AuthResult r;
        auth.refresh(old, [&](const AuthResult &x) { r = x; });
        CHECK(r.status == AuthStatus::BadCredentials);
    }
    {
        Server s;
        s.handler = [](const HttpRequest &) { return reply(503, ""); };
        Auth auth(s.transport(), testClock());
        AuthResult r;
        auth.refresh(old, [&](const AuthResult &x) { r = x; });
        CHECK(r.status == AuthStatus::Network);
    }
    {
        Server s;
        s.handler = [](const HttpRequest &) { return reply(200, ""); };
        Auth auth(s.transport(), testClock());
        Tokens none;
        none.access = "A";
        AuthResult r;
        auth.refresh(none, [&](const AuthResult &x) { r = x; });
        CHECK(r.status == AuthStatus::BadCredentials);
        CHECK_EQ(s.log.size(), 0u);
    }
}

// ---- the API -----------------------------------------------------------------

static Tokens goodTokens(int64_t exp = kNow + 3600)
{
    Tokens t;
    t.access = "ACCESS";
    t.refresh = "REFRESH";
    t.clientId = "GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2";
    t.expiresAt = exp;
    return t;
}

struct ApiRig
{
    Server s;
    std::unique_ptr<Auth> auth;
    std::unique_ptr<Api> api;
    int tokensSaved = 0;
    explicit ApiRig(Tokens t = goodTokens())
    {
        auth.reset(new Auth(s.transport(), testClock()));
        api.reset(new Api(s.transport(), *auth, testClock()));
        api->setTokens(t);
        api->setTokensChanged([this](const Tokens &) { ++tokensSaved; });
    }
};

static const char *kList =
        "[{\"activityId\":22000000001,\"activityName\":\"Helsinki Cycling\",\"startTimeGMT\":\"2026-10-15 12:21:21\","
        "\"activityType\":{\"typeKey\":\"cycling\"},\"distance\":51.81,\"duration\":30.0},"
        "{\"activityId\":22000000002,\"activityName\":\"Morning Run\",\"startTimeGMT\":\"2026-10-14 06:00:00\","
        "\"activityType\":{\"typeKey\":\"running\"},\"distance\":5000.5,\"duration\":1800},"
        "{\"activityId\":22000000003,\"startTimeGMT\":\"garbage\"}]";

static void listing()
{
    ApiRig rig;
    rig.s.handler = [](const HttpRequest &) { return reply(200, kList); };
    ApiError err;
    std::vector<RemoteActivity> list;
    rig.api->listActivities(20, 500, [&](const ApiError &e, const std::vector<RemoteActivity> &l) { err = e; list = l; });
    CHECK(err.ok());
    CHECK_EQ(list.size(), 2u); // the one with an unreadable date is dropped
    if (list.size() == 2) {
        CHECK_EQ(list[0].id, 22000000001LL);
        CHECK_EQ(list[0].start, 1792066881);
        CHECK_EQ(list[0].typeKey, std::string("cycling"));
        CHECK_NEAR(list[0].distance, 51.81, 1e-9);
        CHECK_EQ(list[1].name, std::string("Morning Run"));
    }
    const HttpRequest &q = rig.s.log[0];
    CHECK(has(q.url, "https://connectapi.garmin.com/activitylist-service/activities/search/activities?start=20&limit=100")); // limit capped
    CHECK_EQ(header(q, "Authorization"), std::string("Bearer ACCESS"));
    CHECK_EQ(header(q, "User-Agent"), std::string("GCM-Android-5.23"));
    CHECK_EQ(header(q, "Accept"), std::string("application/json"));
    CHECK_EQ(rig.s.count("diauth"), 0); // a token with time left is not renewed

    ApiRig bad;
    bad.s.handler = [](const HttpRequest &) { return reply(200, "{\"not\":\"a list\"}"); };
    bad.api->listActivities(0, 10, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; });
    CHECK(err.status == ApiStatus::Protocol);
}

static void listingBetween()
{
    ApiRig rig;
    rig.s.handler = [](const HttpRequest &) { return reply(200, kList); };
    ApiError err;
    rig.api->listActivities(0, 20, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; }, "2026-10-14", "2026-10-16");
    CHECK(err.ok());
    CHECK(has(rig.s.log[0].url, "&startDate=2026-10-14&endDate=2026-10-16"));
    CHECK_EQ(garminDate(1792066881), std::string("2026-10-15"));
    CHECK_EQ(garminDate(0), std::string("1970-01-01"));
    CHECK_EQ(garminDate(951782400), std::string("2000-02-29"));
    CHECK_EQ(garminDate(-1), std::string("1969-12-31"));
}

static void tokenHandling()
{
    // About to expire: renewed first, and the new tokens are handed on to be saved.
    {
        ApiRig rig(goodTokens(kNow + 100));
        rig.s.handler = [](const HttpRequest &r) {
            if (has(r.url, "diauth"))
                return reply(200, tokenAnswer(kNow + 3600, "GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2", "R2"));
            return reply(200, "[]");
        };
        ApiError err;
        rig.api->listActivities(0, 10, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; });
        CHECK(err.ok());
        CHECK_EQ(rig.s.count("diauth"), 1);
        CHECK_EQ(rig.tokensSaved, 1);
        CHECK_EQ(rig.api->tokens().refresh, std::string("R2"));
        CHECK(has(header(*rig.s.last("activitylist"), "Authorization"), "Bearer ey")); // a JWT, not "ACCESS"
    }
    // A 401: renewed once, and the call repeated.
    {
        ApiRig rig;
        int calls = 0;
        rig.s.handler = [&](const HttpRequest &r) {
            if (has(r.url, "diauth"))
                return reply(200, tokenAnswer(kNow + 3600, "X"));
            return ++calls == 1 ? reply(401, "") : reply(200, "[]");
        };
        ApiError err;
        rig.api->listActivities(0, 10, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; });
        CHECK(err.ok());
        CHECK_EQ(calls, 2);
        CHECK_EQ(rig.s.count("diauth"), 1);
    }
    // A 401 that renewing does not cure: signed out, and no loop.
    {
        ApiRig rig;
        rig.s.handler = [](const HttpRequest &r) {
            if (has(r.url, "diauth"))
                return reply(200, tokenAnswer(kNow + 3600, "X"));
            return reply(401, "");
        };
        ApiError err;
        rig.api->listActivities(0, 10, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; });
        CHECK(err.status == ApiStatus::SignedOut);
        CHECK_EQ(rig.s.count("activitylist"), 2);
    }
    // The refresh token is refused: signed out.
    {
        ApiRig rig(goodTokens(kNow - 5));
        rig.s.handler = [](const HttpRequest &) { return reply(400, ""); };
        ApiError err;
        rig.api->listActivities(0, 10, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; });
        CHECK(err.status == ApiStatus::SignedOut);
        CHECK_EQ(rig.s.count("activitylist"), 0);
    }
    // Expired and unrenewable.
    {
        Tokens t = goodTokens(kNow - 5);
        t.refresh.clear();
        ApiRig rig(t);
        rig.s.handler = [](const HttpRequest &) { return reply(200, "[]"); };
        ApiError err;
        rig.api->listActivities(0, 10, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; });
        CHECK(err.status == ApiStatus::SignedOut);
    }
    // The other ways a call fails.
    const std::pair<int, ApiStatus> statuses[] = {
        { 429, ApiStatus::Blocked }, { 403, ApiStatus::Blocked }, { 404, ApiStatus::NotFound },
        { 500, ApiStatus::Server }, { 502, ApiStatus::Server }, { 400, ApiStatus::Protocol },
    };
    for (const auto &st : statuses) {
        ApiRig rig;
        rig.s.handler = [&](const HttpRequest &) { return reply(st.first, "{\"message\":\"nope\"}"); };
        ApiError err;
        rig.api->listActivities(0, 10, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; });
        CHECK(err.status == st.second);
        CHECK_EQ(err.http, st.first);
    }
    {
        ApiRig rig;
        rig.s.handler = [](const HttpRequest &) {
            HttpResponse r;
            r.error = "timed out";
            return r;
        };
        ApiError err;
        rig.api->listActivities(0, 10, [&](const ApiError &e, const std::vector<RemoteActivity> &) { err = e; });
        CHECK(err.status == ApiStatus::Network);
        CHECK(has(err.message, "timed out"));
    }
}

// ---- download ----------------------------------------------------------------

static void put16(std::string &s, unsigned v) { s += char(v & 0xff); s += char(v >> 8); }
static void put32(std::string &s, uint32_t v) { put16(s, v & 0xffff); put16(s, v >> 16); }

static std::string makeZip(const std::vector<std::pair<std::string, std::string>> &files, bool compress)
{
    std::string out, central;
    for (const auto &f : files) {
        std::string payload = f.second;
        unsigned method = 0;
        if (compress) {
            z_stream z;
            std::memset(&z, 0, sizeof z);
            deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            std::string buf(f.second.size() + 64, '\0');
            z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(f.second.data()));
            z.avail_in = static_cast<uInt>(f.second.size());
            z.next_out = reinterpret_cast<Bytef *>(&buf[0]);
            z.avail_out = static_cast<uInt>(buf.size());
            deflate(&z, Z_FINISH);
            buf.resize(buf.size() - z.avail_out);
            deflateEnd(&z);
            payload = buf;
            method = 8;
        }
        const uint32_t offset = static_cast<uint32_t>(out.size());
        const uint32_t crc = crc32(0, reinterpret_cast<const Bytef *>(f.second.data()), static_cast<uInt>(f.second.size()));
        put32(out, 0x04034b50); put16(out, 20); put16(out, 0); put16(out, method); put16(out, 0); put16(out, 0);
        put32(out, crc); put32(out, uint32_t(payload.size())); put32(out, uint32_t(f.second.size()));
        put16(out, unsigned(f.first.size())); put16(out, 0);
        out += f.first; out += payload;
        put32(central, 0x02014b50); put16(central, 20); put16(central, 20); put16(central, 0); put16(central, method);
        put16(central, 0); put16(central, 0); put32(central, crc); put32(central, uint32_t(payload.size()));
        put32(central, uint32_t(f.second.size())); put16(central, unsigned(f.first.size())); put16(central, 0);
        put16(central, 0); put16(central, 0); put16(central, 0); put32(central, 0); put32(central, offset);
        central += f.first;
    }
    const uint32_t dirOffset = static_cast<uint32_t>(out.size());
    out += central;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, unsigned(files.size())); put16(out, unsigned(files.size()));
    put32(out, uint32_t(central.size())); put32(out, dirOffset); put16(out, 0);
    return out;
}

// A stand-in FIT file: 12-byte header, ".FIT", then enough bytes to look like one.
static std::string fakeFit(char fill = 'x')
{
    std::string f(12, '\0');
    f[0] = 12;
    f.replace(8, 4, ".FIT");
    f += std::string(200, fill);
    return f;
}

static void downloading()
{
    const std::string fit = fakeFit('a');
    for (bool compress : { false, true }) {
        ApiRig rig;
        rig.s.handler = [&](const HttpRequest &) { return reply(200, makeZip({ { "22000000001_ACTIVITY.fit", fit } }, compress)); };
        ApiError err;
        std::string got;
        rig.api->downloadFit(22000000001LL, [&](const ApiError &e, const std::string &b) { err = e; got = b; });
        CHECK(err.ok());
        CHECK(got == fit);
        CHECK(has(rig.s.log[0].url, "/download-service/files/activity/22000000001"));
        CHECK_EQ(header(rig.s.log[0], "Accept"), std::string("*/*"));
    }
    {
        ApiRig rig; // the FIT is not the first entry
        rig.s.handler = [&](const HttpRequest &) { return reply(200, makeZip({ { "readme.txt", "hi" }, { "X.FIT", fit } }, true)); };
        ApiError err;
        std::string got;
        rig.api->downloadFit(1, [&](const ApiError &e, const std::string &b) { err = e; got = b; });
        CHECK(err.ok() && got == fit);
    }
    {
        ApiRig rig; // a bare FIT
        rig.s.handler = [&](const HttpRequest &) { return reply(200, fit); };
        ApiError err;
        std::string got;
        rig.api->downloadFit(1, [&](const ApiError &e, const std::string &b) { err = e; got = b; });
        CHECK(err.ok() && got == fit);
    }
    {
        ApiRig rig; // a zip with no FIT in it
        rig.s.handler = [&](const HttpRequest &) { return reply(200, makeZip({ { "a.gpx", "<gpx/>" } }, false)); };
        ApiError err;
        rig.api->downloadFit(1, [&](const ApiError &e, const std::string &) { err = e; });
        CHECK(err.status == ApiStatus::Protocol);
        CHECK(has(err.message, "no FIT"));
    }
    {
        ApiRig rig; // an empty answer: a manual activity has no original
        rig.s.handler = [&](const HttpRequest &) { return reply(200, ""); };
        ApiError err;
        rig.api->downloadFit(1, [&](const ApiError &e, const std::string &) { err = e; });
        CHECK(err.status == ApiStatus::Protocol);
    }
    {
        ApiRig rig; // something that is neither
        rig.s.handler = [&](const HttpRequest &) { return reply(200, "<html>sign in</html>"); };
        ApiError err;
        rig.api->downloadFit(1, [&](const ApiError &e, const std::string &) { err = e; });
        CHECK(err.status == ApiStatus::Protocol);
    }
    {
        ApiRig rig;
        rig.s.handler = [&](const HttpRequest &) { return reply(404, ""); };
        ApiError err;
        rig.api->downloadFit(1, [&](const ApiError &e, const std::string &) { err = e; });
        CHECK(err.status == ApiStatus::NotFound);
    }
}

// ---- upload ------------------------------------------------------------------

static void uploading()
{
    const std::string fit = fakeFit('u');
    const char *ok = "{\"detailedImportResult\":{\"uploadId\":9,\"successes\":[{\"internalId\":22000000099,\"externalId\":\"e\",\"messages\":[]}],\"failures\":[]}}";
    const char *dup = "{\"detailedImportResult\":{\"uploadId\":9,\"successes\":[],\"failures\":[{\"internalId\":22000000001,\"messages\":[{\"code\":202,\"content\":\"Duplicate Activity.\"}]}]}}";
    const char *bad = "{\"detailedImportResult\":{\"successes\":[],\"failures\":[{\"messages\":[{\"code\":500,\"content\":\"Unsupported file.\"}]}]}}";

    for (bool asImport : { false, true }) {
        ApiRig rig;
        rig.s.handler = [&](const HttpRequest &) { return reply(201, ok); };
        ApiError err;
        UploadResult res;
        rig.api->upload(fit, "ride.fit", asImport, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        CHECK(err.ok());
        CHECK(res.uploaded);
        CHECK(!res.duplicate);
        CHECK_EQ(res.activityId, 22000000099LL);

        const HttpRequest &q = rig.s.log[0];
        CHECK(q.method == "POST");
        CHECK(has(header(q, "Content-Type"), "multipart/form-data; boundary="));
        CHECK(has(q.body, "name=\"file\"; filename=\"ride.fit\""));
        CHECK(has(q.body, fit)); // the bytes go through whole
        CHECK_EQ(header(q, "Authorization"), std::string("Bearer ACCESS"));
        if (asImport) {
            CHECK(q.url == "https://connectapi.garmin.com/upload-service/upload/fit");
            CHECK_EQ(header(q, "NK"), std::string("NT"));
            CHECK_EQ(header(q, "User-Agent"), std::string("GCM-iOS-5.7.2.1"));
            CHECK_EQ(header(q, "origin"), std::string("https://sso.garmin.com"));
        } else {
            CHECK(q.url == "https://connectapi.garmin.com/upload-service/upload");
            CHECK_EQ(header(q, "User-Agent"), std::string("GCM-Android-5.23"));
        }
        // The boundary in the header is the one in the body.
        const std::string ct = header(q, "Content-Type");
        const std::string boundary = ct.substr(ct.find("boundary=") + 9);
        CHECK(has(q.body, "--" + boundary + "\r\n"));
        CHECK(has(q.body, "--" + boundary + "--\r\n"));
    }
    {
        ApiRig rig; // 409 with the report: a duplicate
        rig.s.handler = [&](const HttpRequest &) { return reply(409, dup); };
        ApiError err;
        UploadResult res;
        rig.api->upload(fit, "a.fit", false, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        CHECK(err.ok());
        CHECK(res.duplicate);
        CHECK(!res.uploaded);
        CHECK_EQ(res.activityId, 22000000001LL);
    }
    {
        ApiRig rig; // 409 with nothing readable: still a duplicate
        rig.s.handler = [&](const HttpRequest &) { return reply(409, ""); };
        ApiError err;
        UploadResult res;
        rig.api->upload(fit, "a.fit", false, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        CHECK(err.ok() && res.duplicate);
    }
    {
        ApiRig rig; // 202 in a 201: a duplicate Garmin accepted the request for
        rig.s.handler = [&](const HttpRequest &) { return reply(201, dup); };
        ApiError err;
        UploadResult res;
        rig.api->upload(fit, "a.fit", false, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        CHECK(err.ok() && res.duplicate && !res.uploaded);
    }
    {
        ApiRig rig; // 202: taken, being processed; neither a success nor a refusal
        rig.s.handler = [&](const HttpRequest &) {
            return reply(202, "{\"detailedImportResult\":{\"uploadId\":777,\"fileName\":\"a.fit\",\"report\":null,\"successes\":[],\"failures\":[]}}");
        };
        ApiError err;
        UploadResult res;
        std::string logged;
        rig.api->setDebug([&](const std::string &s) { logged = s; });
        rig.api->upload(fit, "a.fit", true, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        CHECK(err.ok());
        CHECK(res.accepted);
        CHECK(!res.uploaded && !res.duplicate);
        CHECK_EQ(res.uploadId, 777LL);
        CHECK(has(logged, "HTTP 202"));
        CHECK(has(logged, "uploadId"));
    }
    {
        ApiRig rig;
        rig.s.handler = [&](const HttpRequest &) { return reply(201, bad); };
        ApiError err;
        UploadResult res;
        rig.api->upload(fit, "a.fit", false, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        CHECK(err.status == ApiStatus::Protocol);
        CHECK(has(err.message, "Unsupported file."));
    }
    {
        ApiRig rig;
        rig.s.handler = [&](const HttpRequest &) { return reply(200, "{}"); };
        ApiError err;
        UploadResult res;
        rig.api->upload(fit, "a.fit", false, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        CHECK(err.status == ApiStatus::Protocol);
    }
    {
        ApiRig rig;
        rig.s.handler = [&](const HttpRequest &) { return reply(500, ""); };
        ApiError err;
        UploadResult res;
        rig.api->upload(fit, "a.fit", false, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        CHECK(err.status == ApiStatus::Server);
    }
    {
        // A file that happens to contain the boundary string gets another boundary.
        ApiRig rig;
        rig.s.handler = [&](const HttpRequest &) { return reply(201, ok); };
        const std::string tricky = fakeFit('t') + "EdgeSyncBoundary7f3a9c41";
        UploadResult res;
        ApiError err;
        rig.api->upload(tricky, "a.fit", false, [&](const ApiError &e, const UploadResult &r) { err = e; res = r; });
        const std::string ct = header(rig.s.log[0], "Content-Type");
        const std::string boundary = ct.substr(ct.find("boundary=") + 9);
        CHECK(boundary != "EdgeSyncBoundary7f3a9c41");
        CHECK(err.ok() && res.uploaded);
    }
}

int main()
{
    encoding();
    loginSuccess();
    loginFailures();
    loginWithMfa();
    refreshing();
    listing();
    listingBetween();
    tokenHandling();
    downloading();
    uploading();
    return checkSummary("garmin");
}
