#include "garmin/auth.h"

#include "garmin/json.h"

namespace Garmin {

namespace {

const char *const kSso = "https://sso.garmin.com";
const char *const kSsoClientId = "GCM_IOS_DARK";
const char *const kServiceUrl = "https://mobile.integration.garmin.com/gcm/ios";
const char *const kLoginUserAgent =
        "Mozilla/5.0 (iPhone; CPU iPhone OS 18_7 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Mobile/15E148";

const char *const kDiTokenUrl = "https://diauth.garmin.com/di-oauth2-service/oauth/token";
const char *const kDiGrantType = "https://connectapi.garmin.com/di-oauth2-service/oauth/grant/service_ticket";
// Tried in this order until one is accepted: Garmin retires them as it ships new
// versions of its app.
const char *const kDiClientIds[] = {
    "GARMIN_CONNECT_MOBILE_ANDROID_DI_2025Q2",
    "GARMIN_CONNECT_MOBILE_ANDROID_DI_2024Q4",
    "GARMIN_CONNECT_MOBILE_ANDROID_DI",
    "GARMIN_CONNECT_MOBILE_IOS_DI",
};
constexpr size_t kDiClientCount = sizeof(kDiClientIds) / sizeof(kDiClientIds[0]);

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

AuthResult fail(AuthStatus status, std::string message)
{
    AuthResult r;
    r.status = status;
    r.message = std::move(message);
    return r;
}

// What a transport-level or protection-level failure means, or Ok if the answer
// is one to read.
bool blockedOrDown(const HttpResponse &r, AuthResult &out)
{
    if (r.status == 0) {
        out = fail(AuthStatus::Network, "No answer from Garmin: " + (r.error.empty() ? std::string("no connection") : r.error));
        return true;
    }
    if (r.status == 429) {
        out = fail(AuthStatus::Blocked,
                   "Garmin answered 429: it is rate-limiting or blocking this client. Wait some time and try again.");
        return true;
    }
    if (r.status == 403) {
        out = fail(AuthStatus::Blocked,
                   "Garmin answered 403, which is its bot protection refusing this client. Try again later, "
                   "or from another network.");
        return true;
    }
    return false;
}

std::string jwtPayloadField(const std::string &jwt, const std::string &field, bool wantNumber, int64_t &num)
{
    const size_t a = jwt.find('.');
    if (a == std::string::npos)
        return std::string();
    const size_t b = jwt.find('.', a + 1);
    if (b == std::string::npos)
        return std::string();
    std::string payload;
    if (!base64Decode(jwt.substr(a + 1, b - a - 1), payload))
        return std::string();
    const auto doc = Json::parse(payload);
    if (!doc || !doc->isObject())
        return std::string();
    const Json *v = doc->get(field);
    if (!v)
        return std::string();
    if (wantNumber) {
        num = static_cast<int64_t>(v->asNumber(0));
        return std::string("n");
    }
    return v->asString();
}

} // namespace

int64_t jwtExpiry(const std::string &jwt)
{
    int64_t n = 0;
    jwtPayloadField(jwt, "exp", true, n);
    return n;
}

std::string jwtClientId(const std::string &jwt)
{
    int64_t unused = 0;
    return jwtPayloadField(jwt, "client_id", false, unused);
}

std::string Tokens::toJson() const
{
    Json o = Json::object();
    o.set("access", Json::string(access));
    o.set("refresh", Json::string(refresh));
    o.set("client_id", Json::string(clientId));
    o.set("expires_at", Json::number(static_cast<double>(expiresAt)));
    return o.dump();
}

bool Tokens::fromJson(const std::string &text, Tokens &out)
{
    const auto doc = Json::parse(text);
    if (!doc || !doc->isObject())
        return false;
    Tokens t;
    t.access = doc->str("access");
    t.refresh = doc->str("refresh");
    t.clientId = doc->str("client_id");
    t.expiresAt = static_cast<int64_t>(doc->num("expires_at", 0));
    if (!t.valid())
        return false;
    out = t;
    return true;
}

Auth::Auth(HttpTransport http, std::function<int64_t()> clock)
    : m_http(std::move(http)), m_clock(std::move(clock))
{
}

void Auth::login(const std::string &email, const std::string &password, const AuthDone &done)
{
    m_mfaPending = false;
    Json body = Json::object();
    body.set("username", Json::string(email));
    body.set("password", Json::string(password));
    body.set("rememberMe", Json::boolean(true));
    body.set("captchaToken", Json::string(""));
    ssoPost("/mobile/api/login", body.dump(), done, false);
}

void Auth::submitMfa(const std::string &code, const AuthDone &done)
{
    if (!m_mfaPending) {
        done(fail(AuthStatus::Protocol, "No sign-in is waiting for a code."));
        return;
    }
    Json body = Json::object();
    body.set("mfaMethod", Json::string(m_mfaMethod));
    body.set("mfaVerificationCode", Json::string(code));
    body.set("rememberMyBrowser", Json::boolean(true));
    body.set("reconsentList", Json::array());
    body.set("mfaSetup", Json::boolean(false));
    ssoPost("/mobile/api/mfa/verifyCode", body.dump(), done, true);
}

void Auth::ssoPost(const std::string &path, const std::string &json, const AuthDone &done, bool mfaStep)
{
    HttpRequest req;
    req.method = "POST";
    req.url = std::string(kSso) + path + "?clientId=" + kSsoClientId + "&locale=en-US&service=" + urlEncode(kServiceUrl);
    req.headers = {
        { "User-Agent", kLoginUserAgent },
        { "Accept", "application/json, text/plain, */*" },
        { "Content-Type", "application/json" },
        { "Origin", kSso },
    };
    req.body = json;

    m_http(req, [this, done, mfaStep](const HttpResponse &r) {
        AuthResult blocked;
        if (blockedOrDown(r, blocked)) {
            done(blocked);
            return;
        }
        const auto doc = Json::parse(r.body);
        if (!doc || !doc->isObject()) {
            done(fail(AuthStatus::Protocol,
                      "Garmin answered with something other than JSON (HTTP " + std::to_string(r.status)
                              + "), most likely its bot protection."));
            return;
        }
        // A 429 can also be inside a JSON body.
        if (const Json *err = doc->get("error")) {
            if (err->str("status-code") == "429") {
                done(fail(AuthStatus::Blocked, "Garmin is rate-limiting this client (429). Try again later."));
                return;
            }
        }
        const Json *rs = doc->get("responseStatus");
        const std::string type = rs ? rs->str("type") : std::string();

        if (type == "SUCCESSFUL") {
            const std::string ticket = doc->str("serviceTicketId");
            if (ticket.empty()) {
                done(fail(AuthStatus::Protocol, "Garmin accepted the sign-in but sent no ticket."));
                return;
            }
            m_mfaPending = false;
            exchangeTicket(ticket, 0, done);
            return;
        }
        if (type == "MFA_REQUIRED" && !mfaStep) {
            std::string method = "email";
            if (const Json *info = doc->get("customerMfaInfo"))
                method = info->str("mfaLastMethodUsed", "email");
            m_mfaMethod = method;
            m_mfaPending = true;
            AuthResult r;
            r.status = AuthStatus::NeedMfa;
            r.mfaMethod = method;
            r.message = "Garmin sent a verification code (" + method + ").";
            done(r);
            return;
        }
        if (type == "INVALID_USERNAME_PASSWORD") {
            done(fail(AuthStatus::BadCredentials, "Garmin does not accept that email and password."));
            return;
        }
        if (type == "CAPTCHA_REQUIRED") {
            done(fail(AuthStatus::Blocked, "Garmin wants a captcha solved, which this app cannot do. Try again later."));
            return;
        }
        if (mfaStep) {
            // The code was wrong or has expired; the pending sign-in stays open for another try.
            done(fail(AuthStatus::BadMfaCode, "Garmin does not accept that code" + (type.empty() ? std::string() : " (" + type + ")") + "."));
            return;
        }
        done(fail(AuthStatus::Protocol, "Garmin answered the sign-in with " + (type.empty() ? std::string("an unknown status") : type) + "."));
    });
}

void Auth::exchangeTicket(const std::string &ticket, size_t clientIndex, const AuthDone &done)
{
    if (clientIndex >= kDiClientCount) {
        done(fail(AuthStatus::Protocol, "Garmin signed you in but would not issue an access token to any known app id."));
        return;
    }
    const std::string client = kDiClientIds[clientIndex];

    HttpRequest req;
    req.method = "POST";
    req.url = kDiTokenUrl;
    req.headers = nativeHeaders();
    req.headers.push_back({ "Authorization", "Basic " + base64Encode(client + ":") });
    req.headers.push_back({ "Accept", "application/json,text/html;q=0.9,*/*;q=0.8" });
    req.headers.push_back({ "Content-Type", "application/x-www-form-urlencoded" });
    req.headers.push_back({ "Cache-Control", "no-cache" });
    req.body = formEncode({ { "client_id", client },
                            { "service_ticket", ticket },
                            { "grant_type", kDiGrantType },
                            { "service_url", kServiceUrl } });

    m_http(req, [this, done, ticket, clientIndex, client](const HttpResponse &r) {
        AuthResult blocked;
        if (r.status == 0 || r.status == 429) {
            blockedOrDown(r, blocked);
            done(blocked);
            return;
        }
        if (!r.ok()) {
            // This app id was not accepted: try the next.
            exchangeTicket(ticket, clientIndex + 1, done);
            return;
        }
        finishTokens(r.body, client, done);
    });
}

void Auth::finishTokens(const std::string &json, const std::string &triedClient, const AuthDone &done)
{
    const auto doc = Json::parse(json);
    const std::string access = doc ? doc->str("access_token") : std::string();
    if (access.empty()) {
        done(fail(AuthStatus::Protocol, "Garmin's token answer had no access token."));
        return;
    }
    AuthResult r;
    r.status = AuthStatus::Ok;
    r.tokens.access = access;
    r.tokens.refresh = doc->str("refresh_token");
    const std::string fromJwt = jwtClientId(access);
    r.tokens.clientId = fromJwt.empty() ? triedClient : fromJwt;
    int64_t exp = jwtExpiry(access);
    if (exp == 0)
        exp = m_clock() + static_cast<int64_t>(doc->num("expires_in", 3600));
    r.tokens.expiresAt = exp;
    done(r);
}

void Auth::refresh(const Tokens &old, const AuthDone &done)
{
    if (!old.canRefresh()) {
        done(fail(AuthStatus::BadCredentials, "There is no refresh token: sign in again."));
        return;
    }
    HttpRequest req;
    req.method = "POST";
    req.url = kDiTokenUrl;
    req.headers = nativeHeaders();
    req.headers.push_back({ "Authorization", "Basic " + base64Encode(old.clientId + ":") });
    req.headers.push_back({ "Accept", "application/json" });
    req.headers.push_back({ "Content-Type", "application/x-www-form-urlencoded" });
    req.headers.push_back({ "Cache-Control", "no-cache" });
    req.body = formEncode({ { "grant_type", "refresh_token" }, { "client_id", old.clientId }, { "refresh_token", old.refresh } });

    m_http(req, [this, done, old](const HttpResponse &r) {
        AuthResult blocked;
        if (r.status == 0 || r.status == 429 || r.status == 403) {
            blockedOrDown(r, blocked);
            done(blocked);
            return;
        }
        if (!r.ok()) {
            // 400 and 401 are the refresh token itself being refused: signed out.
            done(fail(r.status >= 500 ? AuthStatus::Network : AuthStatus::BadCredentials,
                      r.status >= 500 ? "Garmin's token service is not answering (HTTP " + std::to_string(r.status) + ")."
                                      : "Garmin no longer accepts the saved sign-in. Sign in again."));
            return;
        }
        const auto doc = Json::parse(r.body);
        const std::string access = doc ? doc->str("access_token") : std::string();
        if (access.empty()) {
            done(fail(AuthStatus::Protocol, "Garmin's refresh answer had no access token."));
            return;
        }
        AuthResult res;
        res.status = AuthStatus::Ok;
        res.tokens = old;
        res.tokens.access = access;
        const std::string newRefresh = doc->str("refresh_token");
        if (!newRefresh.empty())
            res.tokens.refresh = newRefresh;
        const std::string fromJwt = jwtClientId(access);
        if (!fromJwt.empty())
            res.tokens.clientId = fromJwt;
        int64_t exp = jwtExpiry(access);
        if (exp == 0)
            exp = m_clock() + static_cast<int64_t>(doc->num("expires_in", 3600));
        res.tokens.expiresAt = exp;
        done(res);
    });
}

} // namespace Garmin
