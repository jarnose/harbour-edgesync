# Tests

Everything under `src/gfdi/`, `src/fit/` and `src/export/` is Qt-free on purpose,
so it compiles and runs with plain `g++` on a development machine. A protocol
decoder is exactly the kind of code where "it compiles" proves nothing, and the
phone is a slow place to find out.

```
tests/run.sh
```

builds and runs each suite twice, in strict `c++17` and in `gnu++17` (the mode the
Sailfish build uses, where `unix` is a macro).

| suite | what it checks |
|---|---|
| `framing` | CRC-16 and COBS against the vectors garmin-ble and GarminBLE pinned; 400 random COBS round trips, every possible cut of a two-frame stream, loss of a frame start, the GFDI frame |
| `messages` | each message's byte layout both ways, the capability bitmask, the 16-byte directory entry, and the multi-link transport (control handshake, MTU cutting, channel reopening) |
| `session` | the sync state machine against a scripted Edge (`scripted_edge.h`): both ways a sync starts, ordering, acknowledgements, archive on and off, "not ready" retries, refused files, empty files, every chunk size, a corrupt chunk, a stall, a handshake that never comes, unsupported messages |
| `fit` | the FIT reader on files built byte by byte: byte order, compressed timestamps, developer fields, invalid values, chained files, damaged and truncated files, summaries derived from records, laps, several sessions |
| `gpx` | the GPX writer: dates (leap days, the FIT epoch), escaping, segments across a lost fix, power and heart rate extensions, an indoor ride |

## What is *not* proven here

The scripted Edge is written from the same reading of the protocol as the session
under test. It proves the state machine holds up its end; it cannot prove the
protocol is right. Only a real Edge can, and until one has been through it,
`docs/protocol.md` lists what is assumed.

## Beyond `run.sh`

* `python3 tools/check_qml.py`: duplicate handlers and ES6 syntax the phone's Qt 5.6 QML engine rejects.
* `tools/fitcheck/`: compares the FIT reader with **fitparse** on real files, field by field and with a digest of every record. This is how a wrong field number (the session's maximum altitude is field 50) was found; it needs ride files, which are personal data and are not in the repository.
* `tools/qmlcheck/render.py`: loads every page offscreen with stand-ins for Silica and a mock controller, fails on any QML warning, and writes a PNG per page. Needs PySide6.

## The vectors

Nothing here is a capture from an Edge. The CRC and COBS vectors are the ones the
other projects pinned; the messages are built byte by byte in the layouts
`docs/protocol.md` documents. Real captures are personal data and stay out of the
repository (`tests/fixtures/` and `*.fit` are ignored).
