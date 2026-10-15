# Garmin's Bluetooth protocol, as this app speaks it

Garmin has never published this. What follows is what the open-source community
worked out, and what was checked against it. Where this document says
*Gadgetbridge*, it means its Garmin support
(`service/devices/garmin/`, AGPL-3.0, <https://codeberg.org/Freeyourgadget/Gadgetbridge>),
which every other project below derives from.

## What the four starting points turned out to be

| Project | What it is | Used for |
|---|---|---|
| [gwerneckp/garmin-ble](https://github.com/gwerneckp/garmin-ble) | Python library, GFDI V2: handshake, MLR, live sensor streams. File transfer (FIT/GPX) is on its roadmap as v0.6, *not done*. | CRC and COBS test vectors; the confirmation that the ML handshake needs no more than the control messages implemented here. |
| [mjsir911/GarminBLE](https://github.com/mjsir911/GarminBLE) | Notes and a Wireshark dissector for a vivofit 3 (old, V0 transport). Goal: set the clock. | Two independent CRC vectors. |
| [wh1le/garmin-bridge](https://github.com/wh1le/garmin-bridge) | Python daemon for a Fenix 6 Pro: weather, calendar, todos over BlueZ. No file transfer. | The shortest readable form of the handshake and of a working BlueZ pairing agent. |
| [tonysilvasa/stark-varg-garmin-bridge](https://github.com/tonysilvasa/stark-varg-garmin-bridge) | Android app plus a Connect IQ data field that shows an e-motorbike's battery on an Edge. Unrelated to GFDI. | Nothing in the protocol. Its one useful lesson is about *Connect IQ* links, which this app does not use: the Edge's CIQ scanner only finds a bonded phone, and cannot connect while Garmin Connect holds the link. |

**None of the four downloads a workout.** The part this app is for, listing the
Edge's files and fetching the activities, comes from Gadgetbridge only:
`FileTransferHandler`, `DownloadRequestMessage`, `FileTransferDataMessage`,
`FilterMessage`, `SynchronizationMessage`, `SetFileFlagsMessage` and the status
messages under `messages/status/`. The Edge models are listed in
`devices/garmin/bike/`; Gadgetbridge marks the 540, 840, 1040, 1050, Explore 2,
130, 130 Plus and 25 as supported and the 510, 520, 530, 810, 820, 830, 1000,
1030 and Explore 820/1000 as experimental, and none of the Edge coordinators
turns on the *new sync protocol* (the protobuf `FileSyncService`) that some
newer watches use. So the **legacy GFDI directory sync** is the path built here.

## Layers

```
FIT activity file                    <- fit/
  GFDI message  [size][type][payload][crc]   <- gfdi/message.*, messages.*
    COBS frame  00 <stuffed bytes> 00        <- gfdi/cobs.*
      multi-link channel (1 handle byte)     <- gfdi/multilink.*
        BLE GATT write / notify              <- ble/edgelink.*
```

### GATT

A device offers one of three services (`ble/edgelink.cpp`, `discoverCharacteristics`):

* **Multi-link** (every recent Edge): service `6A4E2800-667B-11E3-949A-0800200C9A66`.
  The receive (notify) characteristic is `6A4E2810..2814` and the send one the same
  number plus `0x10`. The first pair that exists is used.
* **Direct V1**: service `6A4E2401-...`, send `6A4E4C80-...`, receive `6A4ECD28-...`.
* **Direct V0**: service `9B012401-BC30-CE9A-E111-0F67E491ABDE`, send `DF334C80-E6A7-D082-274D-78FC66F85E16`, receive `4ACBCD28-7425-868E-F447-915C8F00D0CB`.

The link must be **encrypted**: the characteristics are unreadable until the
phone and the Edge are bonded, which is why the app pairs first (`Device1.Pair`).

### Multi-link

Every write and notification starts with a handle byte. Handle `0` is the control
channel, with messages `[0][type][client id: u64][...]`:

| type | meaning | rest |
|---|---|---|
| 0 | register request | service `u16`, reliable `u8` |
| 1 | register response | service `u16`, status `u8`, handle `u8`, reliable `u8` |
| 2 | close handle request | service `u16`, handle `u8` |
| 3 | close handle response | service `u16`, handle `u8`, status `u8` |
| 5 | close all request | `u16` zero |
| 6 | close all response | |

The sequence: write *close all* (client id 2), receive *close-all response*, write
*register GFDI* (service 1, reliable 0), receive *register response* with the
handle, and from then on frames go to and come from that handle.

**Reliable is 0.** Gadgetbridge opens GFDI with `reliable = 2` by default, which
turns on *MLR*, an application-level sliding window with its own acknowledgements
(`MlrCommunicator.java`, 390 lines). garmin-bridge and garmin-ble open it with 0
and work on a Fenix 6 and a handful of watches; the Edge accepts either. MLR is
not implemented here. See "Open assumptions".

### COBS

A leading `0x00`, the usual consistent-overhead-byte-stuffed payload (a block
code `n` followed by `n-1` bytes, standing for those bytes and then a zero; `0xFF`
blocks carry 254 bytes and imply no zero), a trailing `0x00`. A payload ending in
zero gets an empty `0x01` block. Verified against garmin-ble's `test_cobs.py`
vectors and by 400 randomised round trips with random cuts (`tests/test_framing.cpp`).

### GFDI message

`[size: u16][type: u16][payload][crc: u16]`, little-endian, `size` counting
everything including itself and the CRC, the CRC (below) over everything before
it. A type with bit 15 set carries a sequence number in bits 8-14 and the type
minus 5000 in the low byte.

CRC-16: the nibble-table form of CRC-16/ARC. The same function is the FIT file
CRC. Vectors from garmin-ble's `test_crc.py` and GarminBLE's `crc.py`
(`crc("A") = 12480`, `crc("Hello") = 62291`, `crc(0..255) = 0xBAD3`,
`crc(07 00 8f 13 03) = 0x1ac9`).

## Message types used

| Type | Name | Direction | Payload |
|---|---|---|---|
| 5000 | Response | both | `original type u16, status u8, ...` |
| 5002 | DownloadRequest | to Edge | `index u16, offset u32, request u8 (1 new), crc seed u16, size u32 (0 = all), flags u8` |
| 5004 | FileTransferData | from Edge | `flags u8, crc u16 (running), offset u32, data` |
| 5007 | Filter | to Edge | `type u8` (0 all, 3 only new) |
| 5008 | SetFileFlag | to Edge | `index u16, flags u8` (0x10 archive) |
| 5009 | FileAvailable | from Edge | a directory entry, then a flag |
| 5024 | DeviceInformation | from Edge | protocol, product, unit, software, max packet, three strings, ... |
| 5026 | DeviceSettings | to Edge | count, then `id u8, length u8, value` |
| 5030 | SystemEvent | to Edge | `event u8, value u8` |
| 5031 | SupportedFileTypes | to Edge | empty |
| 5037 | Synchronization | from Edge | `type u8, width u8, file-type mask (4 or 8 bytes)` |
| 5050 | Configuration | both | `length u8, capability bitmask` |
| 5052 | CurrentTimeRequest | from Edge | `reference u32` |
| 5101 | AuthNegotiation | from Edge | `u8, flags u32` |

Responses worth knowing: to DownloadRequest, `status, download status u8 (0 ok, 1 index
unknown, 2 not readable, 3 no space, 4 invalid, 5 not ready, 6 crc), size u32`; to
FileTransferData (ours), `status, transfer status u8 (0 ok, 1 resend, 2 abort, 3 crc,
4 offset), next offset u32`.

### Directory entry (16 bytes, little-endian)

`index u16, data type u8, sub type u8, file number u16, specific flags u8, file
flags u8, size u32, timestamp u32 (Garmin seconds; 0 = none)`. Data type 128 is a
FIT file and sub type 4 an activity. The directory itself is "file" 0 and is
downloaded like any other; it is a bare run of these entries.

## The conversation

```
Edge -> DEVICE_INFORMATION        we: Response ACK, then Response with who we are
Edge -> CONFIGURATION             we: Response ACK, then our CONFIGURATION
we   -> SUPPORTED_FILE_TYPES, DEVICE_SETTINGS (auto-upload on, weather off), [TIME_UPDATED], SYNC_READY
Edge -> SYNCHRONIZATION           (or nothing, in which case after 2.5 s we proceed)
we   -> [FILTER only-new]         only if the Edge started the sync, or the setting is on
we   -> DOWNLOAD_REQUEST index 0
Edge -> Response (size), FILE_TRANSFER_DATA ...   we: Response per chunk, with the next offset
we   -> DOWNLOAD_REQUEST per wanted activity, the same way
we   -> [SET_FILE_FLAG archive]   only if the setting is on
we   -> SYSTEM_EVENT SYNC_COMPLETE
```

Every message the Edge sends that we do not implement is answered with a Response
`UNSUPPORTED`, as Gadgetbridge does for unknown types.

Our CONFIGURATION is Gadgetbridge's `OUR_CAPABILITIES`: 120 capability bits, all
set except GPS ephemeris download, LiveTrack (two bits) and the unknown bits
104-111 and 114-119. It claims much the app does not do (weather, notifications);
it is the one set known to make Garmin firmware offer its files. A narrower set
may well work and would stop the Edge asking for things that get "unsupported"
back, but that is an experiment for a real Edge.

## Open assumptions (nothing here has been run against an Edge)

1. **The whole conversation.** It is a port of Gadgetbridge's behaviour for
   Edge devices, checked against a scripted Edge written from the same reading
   (`tests/scripted_edge.h`). That proves the state machine, not the protocol.
2. **MLR is not needed.** If an Edge refuses unreliable GFDI or stalls on large
   files, `MlrCommunicator.java` is the thing to port.
3. **The self-start path.** Gadgetbridge's user-initiated fetch sends
   DOWNLOAD_REQUEST 0 without FILTER; its device-initiated one answers
   SYNCHRONIZATION with FILTER(only new). Both are reproduced. Which one an Edge
   prefers after SYNC_READY is not known.
4. **Not archiving is safe.** The default is to leave the Edge's flags alone and
   de-duplicate here (by timestamp and size), so Garmin Connect keeps working.
5. **Re-sent chunks.** On an out-of-order chunk we answer RESEND with the offset we
   expect; Gadgetbridge instead throws. The enum value exists in its status
   message; whether an Edge acts on it is unverified.
6. **Bonding.** `Device1.Pair` with Sailfish's system agent is assumed enough; a
   passkey entry the Edge shows should surface through that agent.
