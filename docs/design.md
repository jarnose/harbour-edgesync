# Design

Edge Sync downloads the activities of a Garmin Edge to a Sailfish OS phone and
shows them. It follows harbour-ourasync and harbour-suuntosync: C++ and QML
Silica, BlueZ over D-Bus (Sailfish's Qt has no QtBluetooth), and a Qt-free core
that is tested with plain `g++`.

## Layout

```
src/gfdi/      Qt-free   the protocol: crc, cobs, frames, messages, multi-link transport, sync session
src/fit/       Qt-free   FIT container reader, and what an activity file means
src/export/    Qt-free   GPX writer
src/garmin/    both      Garmin Connect: sign-in, token refresh, activity list, download, upload (Qt-free, tested against a scripted Garmin) and the QNetworkAccessManager transport (Qt)
src/map/       both      base map: Web Mercator and the vector-tile decoder (Qt-free), tile source and MapCanvas (Qt)
src/ble/       Qt        BlueZ: scanner, and EdgeLink (connect, GATT, writes, notifications)
src/store/     Qt        SQLite summary rows + the FIT files themselves
src/controller Qt        AppController and the two list models; everything QML sees
qml/           Silica    pages, components
tests/                   Qt-free suites, scripted Edge
tools/                   check_qml.py, fitcheck/ (fitparse cross-check), make_icon.py
```

## Decisions

**The sync is a state machine, not a function.** harbour-ourasync's ring link
blocks inside nested event loops so a sync reads top to bottom. Here the Edge
drives as much as the phone does (it pushes SYNCHRONIZATION and every file chunk),
so `gfdi::Session` is fed frames and time and answers through callbacks, with no
I/O of its own. That is what lets a scripted Edge test it, including the
failures: a stall, a corrupt chunk, a "not ready", a refused file, a damaged frame.

**The FIT file is the record.** The phone keeps each activity exactly as
received; the database row is a cache of its summary, and can be rebuilt from the
file (`ActivityStore::reindex`). A file the parser cannot read is kept and
flagged, never dropped. Missing readings are NaN or -1, never 0: a rider can have
0 W, and a stretch without a power meter must not look like one.

**De-duplication is by what the Edge says about a file, not by index.** The key
is (device, directory timestamp, size). Indexes are reused by the Edge.

**The Edge's flags are left alone by default.** "Archive" is the flag Garmin
Connect also sets, and what "new" means to the Edge. Setting it from here would
hide rides from another sync. It is a setting, off.

**The network is for two things, both started by the user.** Garmin Connect (after
signing in; every request follows a button) and the optional base map (off by
default), whose tile server sees the area of a ride that is drawn on it. Nothing
else is sent anywhere, and the app says so. Exports go to `~/Documents/EdgeSync` (Sailjail permission *Documents*), and
FIT files to import are read from Documents or Downloads.

**Unverified protocol, so a log.** The protocol is reverse engineered and has
never been run against an Edge by this project, so the app keeps a log of the
conversation (including every byte, if asked) that can be saved to a file and
attached to a report. It is the main debugging tool, and the first place to look.

## Not built (yet)

* MLR, the reliable window layer (see protocol.md).
* The protobuf `FileSyncService` (newer watches; no Edge needs it per Gadgetbridge).
* Raster map tiles, and labels on the map (see `docs/base-map.md`).
* Background or automatic sync. A sync runs when asked.
* Strava and the like. Garmin Connect is built (docs/garmin-connect.md); a ride sent
  there reaches Strava through the user's own account links.
* Sending new Edge rides to Garmin Connect automatically after each sync.
* Laps beyond a table; zones; segments; developer fields.
