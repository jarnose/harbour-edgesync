# Garmin Connect

Edge Sync can download the rides that are on a Garmin Connect account and not on the
phone, and send the rides that are on the phone and not on the account. This is what
that rests on, what it costs, and what has not been proven.

## There is no public API for this

Garmin's official Connect developer programme is for approved businesses. Everything
here uses the private interface Garmin's own mobile apps use, which three open-source
clients have worked out:

| Project | What it was used for |
|---|---|
| [python-garminconnect](https://github.com/cyberjunky/python-garminconnect) | the sign-in sequence, the DI token exchange and refresh, the endpoints, the upload and import calls |
| [yet-another-garmin-connect-client](https://github.com/lswiderski/yet-another-garmin-connect-client) | the upload request and how a duplicate is reported (failure code 202, HTTP 409) |
| [garmin-cli](https://github.com/vicentereig/garmin-cli) | confirmation that the mobile JSON sign-in works from a plain TLS client (Rust, rustls, no browser impersonation) |

Garmin's terms do not allow this for third-party apps. It can stop working whenever
Garmin changes something (all three projects have been through several such changes),
and Garmin can rate-limit or block a client. The sign-in page says so before it asks
for anything.

## The sequence (`src/garmin/auth.cpp`, `api.cpp`)

```
POST sso.garmin.com/mobile/api/login?clientId=GCM_IOS_DARK&locale=en-US&service=<ios url>
     {username, password, rememberMe, captchaToken:""}   -> serviceTicketId | MFA_REQUIRED | INVALID_USERNAME_PASSWORD | CAPTCHA_REQUIRED
POST sso.garmin.com/mobile/api/mfa/verifyCode            {mfaMethod, mfaVerificationCode, ...} -> serviceTicketId
POST diauth.garmin.com/di-oauth2-service/oauth/token     Basic <client id>:  ticket -> access_token (JWT) + refresh_token
POST ... same                                             grant_type=refresh_token
GET  connectapi.garmin.com/activitylist-service/activities/search/activities?start=&limit=
GET  connectapi.garmin.com/download-service/files/activity/{id}      -> a zip with the original FIT
POST connectapi.garmin.com/upload-service/upload                     multipart "file"   (what a device sync does)
POST connectapi.garmin.com/upload-service/upload/fit                 the same, as Garmin Connect Mobile's "import"
```

The DI client ids are tried in order until one is accepted (Garmin retires them as it
ships new app versions). Calls carry `Authorization: Bearer` and the native app's
headers. A token with under 15 minutes left is renewed first; a 401 renews it once and
repeats the call.

A ride *from another device* is sent as an **import**, which Garmin Connect Mobile's own
import uses and which keeps the activity from being passed on to services linked to the
account (Strava and the like): the device that recorded it has done that already. A
ride *from an Edge* goes up as a plain upload, which is what a device sync is.

## Where it can fail, and what the app says

* **Blocked.** Garmin protects the sign-in with Cloudflare, which can refuse a client by
  its TLS fingerprint or its request rate (HTTP 403, 429, an HTML challenge page, or a
  captcha). The other projects keep a chain of strategies, some impersonating a browser's
  TLS handshake with curl_cffi. This app has the one plain strategy, because Qt's
  network stack cannot impersonate a browser. If it is blocked it says so and stops; it
  does not retry in a loop, and it never hits the token endpoint more than once on a 429.
* **No original file.** A manually entered activity has no FIT; it is skipped, and the
  others go on.
* **Wrong password, wrong code.** Reported as such; a wrong code leaves the sign-in open
  for another try.
* **Signed out.** A refresh token Garmin no longer accepts clears the saved sign-in.

## What is kept, and where

The password is used once, to sign in, and is neither saved nor logged. The access and
refresh tokens are kept in the Sailfish Secrets vault (device-lock protected, encrypted;
Sailjail permission *Secrets*). Signing out deletes them from the phone; there is no
call known that revokes them at Garmin. The request log (Settings > Open the log) shows
the method, address without its query, and status of each request, never a header or a
body.

## How rides are matched

A ride here and a ride on Garmin Connect are the same ride if they started within two
seconds of each other. Listing the range (30 days, 90 days, a year, everything; at most
50 per request and 40 requests) links them, so a ride downloaded from the Edge and
already on the account is marked as being there and is not sent again. A ride the
account has and the phone has not is downloaded, oldest first, with a pause between
requests. Rides sent that Garmin calls duplicates (failure code 202, or HTTP 409) are
marked as being there too; the id is stored when Garmin gives it.

Each ride keeps where it came from: `edge`, `garmin` or `import`, and the Garmin id (0
not on Garmin, -1 on Garmin with its id unknown).

## Not verified

**Nothing in `src/garmin` has run against Garmin's servers.** It is tested against a
script written from the three projects above (`tests/test_garmin.cpp`, 200+ checks:
the sign-in with and without a code, every refusal, refresh, 401 retry, list, download
from a zip, upload and duplicates), which proves it does what they do. Only a real
sign-in proves Garmin accepts what this client sends, in particular:

1. that the plain TLS handshake of Qt's network stack is not refused;
2. that `GCM_IOS_DARK` with the iOS service URL still gives a ticket, and that one of
   the four DI client ids still turns it into a token;
3. the exact JSON of the activity list and of the upload report in practice;
4. which upload endpoint Garmin prefers for a device ride (`/upload-service/upload` is
   used; python-garminconnect's `import_activity` endpoint is the alternative).
