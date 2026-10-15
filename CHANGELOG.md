# Changelog

## 1.0.0

First release.

- Downloads the activities of a Garmin Edge over Bluetooth LE (BlueZ; GFDI multi-link, the
  directory sync Gadgetbridge found), checked on an Edge Explore 2. Each ride is kept as the
  original FIT file, with its summary in a database; the Edge's own flags are left alone unless
  asked.
- Shows a ride: distance, moving time, speed, climb, heart rate, power, cadence, temperature,
  laps, the route and a chart for each recorded series. Optional base map under the route
  (OpenFreeMap vector tiles, off by default).
- Imports FIT files from any device; exports a ride as GPX or as the FIT file.
- Garmin Connect: signs in the way Garmin's mobile app does (with a verification code if the
  account has one; the password is not saved, the tokens go in the Sailfish Secrets vault),
  downloads the rides that are on the account and not on the phone, and sends the rides that
  are on the phone and not on the account. Checked against a real account: sign-in, a 30-day
  download, an Edge ride and a Suunto Race ride sent. Garmin has no public API for this and may
  stop it working or block it.
- Symbols in the list: where a ride came from (Edge, Garmin Connect, imported file) and whether
  Garmin Connect has it.
- A log of the conversations with the Edge and Garmin, kept in a file, for reporting problems.
- Finnish translation.

Known limits: a ride already synced by another app cannot be fetched from the Edge (the Edge
hides it; Garmin Connect has it); Edge models that serve files only over Classic Bluetooth are
not supported; a download from the Edge runs at about 1 kB/s, which is the Edge's own pace.
