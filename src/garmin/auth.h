#pragma once

#include "garmin/http.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Garmin {

// The OAuth2 bearer token Garmin Connect's own mobile apps use, and the refresh
// token that renews it. No password is ever part of this.
struct Tokens
{
    std::string access;
    std::string refresh;
    std::string clientId; // the DI client the tokens were issued to; refreshing needs it
    int64_t expiresAt = 0; // Unix seconds

    bool valid() const { return !access.empty(); }
    bool canRefresh() const { return !refresh.empty() && !clientId.empty(); }

    std::string toJson() const;
    static bool fromJson(const std::string &text, Tokens &out);
};

enum class AuthStatus {
    Ok,
    NeedMfa,         // the password was right; a code was sent, give it to submitMfa()
    BadCredentials,
    BadMfaCode,
    Blocked,         // 429, a bot challenge, a captcha: Garmin, or its CDN, refused this client
    Network,
    Protocol,        // an answer this code does not understand
};

struct AuthResult
{
    AuthStatus status = AuthStatus::Protocol;
    std::string message;   // for a person, English
    Tokens tokens;         // when Ok
    std::string mfaMethod; // when NeedMfa: "email", "sms" ...
};

using AuthDone = std::function<void(const AuthResult &)>;

// Garmin Connect sign-in, the way the iOS app does it and python-garminconnect
// (strategy "mobile + requests") ports it:
//
//   POST sso.garmin.com/mobile/api/login      {username, password}  -> a service ticket, or "MFA required"
//   POST sso.garmin.com/mobile/api/mfa/verifyCode  {code}           -> a service ticket
//   POST diauth.garmin.com/.../oauth/token    ticket -> access + refresh token
//   POST diauth.garmin.com/.../oauth/token    refresh_token -> new access token
//
// Garmin protects the first step with Cloudflare, which sometimes refuses a client
// by its TLS fingerprint or its rate. That is reported as Blocked, with what was
// seen, and is the one failure here that nothing in this code can fix.
class Auth
{
public:
    Auth(HttpTransport http, std::function<int64_t()> clock);

    void login(const std::string &email, const std::string &password, const AuthDone &done);
    // After login() answered NeedMfa.
    void submitMfa(const std::string &code, const AuthDone &done);
    void refresh(const Tokens &old, const AuthDone &done);

    bool mfaPending() const { return m_mfaPending; }

private:
    void ssoPost(const std::string &path, const std::string &json, const AuthDone &done, bool mfaStep);
    void exchangeTicket(const std::string &ticket, size_t clientIndex, const AuthDone &done);
    void finishTokens(const std::string &json, const std::string &triedClient, const AuthDone &done);

    HttpTransport m_http;
    std::function<int64_t()> m_clock;
    bool m_mfaPending = false;
    std::string m_mfaMethod = "email";
};

// The "exp" claim of a JWT's payload, or 0.
int64_t jwtExpiry(const std::string &jwt);
// The "client_id" claim of a JWT's payload, or empty.
std::string jwtClientId(const std::string &jwt);

} // namespace Garmin
