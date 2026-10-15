# Edge Sync

A Sailfish OS app that downloads the activities recorded by a **Garmin Edge** bike
computer, over Bluetooth LE, straight to the phone. No Garmin account, no cloud,
no cloud of its own. The Internet permission is for Garmin Connect (only after you sign in, and only when you press a button) and the optional base map.

* Pairs with an Edge and syncs on request (pull down, *Sync now*).
* Keeps each ride as the original **FIT** file, and shows it: distance, moving
  time, speed, climb, heart rate, power, cadence, temperature, energy, laps, the
  route, and a chart for each thing the Edge recorded.
* Optionally draws a **base map** under the route, from vector tiles (OpenFreeMap
  by default, any other source in Settings). Off by default, because the tile
  server learns the area of every ride you open; it is the only use of the network.
* **Imports a FIT file** from any device (*Import a FIT file* in the pull-down
  menu); a ride already here, by its start time, is not added twice.
* Exports a ride as **GPX** (with heart rate, cadence, temperature and power) or
  as the FIT file itself, to `~/Documents/EdgeSync`.
* Leaves the Edge's own flags alone by default, so Garmin Connect keeps working.
* **Garmin Connect**: downloads the rides that are on your account and not on the
  phone, and sends the rides that are on the phone and not on your account (one
  ride, or all of them). Marks every ride in the list with where it came from
  (Edge, Garmin Connect, imported file) and whether Garmin Connect has it. Garmin
  has no public API for this; see `docs/garmin-connect.md`, including why it may
  stop working.
* Keeps a log of the conversation with the Edge, because the protocol is
  reverse-engineered and differs between models.

> **Status:** works on a real **Garmin Edge Explore 2**: pairing, the file list,
> downloading activities (including a long ride with GPS, heart rate and power) and
> storing and showing them. Other models are untested. The Bluetooth protocol is a
> port of what the Gadgetbridge project found; `docs/protocol.md` lists what is
> still assumed. If a sync fails, the log (*Settings > Open the log*, with *Record
> Bluetooth traffic* on, also kept in `sync.log` in the app's data folder) is what to
> send.
>
> **Known limits.** The Edge lists only activities that no app has marked as synced,
> so a ride already synced with the Android app cannot be fetched here: sync with
> this app first, or use it alone. Models that serve their files only over Classic
> Bluetooth (the Edge 520 Plus, for one) are not supported.

## Where it comes from

Garmin has not published the protocol (GFDI). The starting points were
[garmin-ble](https://github.com/gwerneckp/garmin-ble),
[GarminBLE](https://github.com/mjsir911/GarminBLE),
[garmin-bridge](https://github.com/wh1le/garmin-bridge) and
[stark-varg-garmin-bridge](https://github.com/tonysilvasa/stark-varg-garmin-bridge).
None of them downloads a workout; all of them stand on
[Gadgetbridge](https://codeberg.org/Freeyourgadget/Gadgetbridge), whose Garmin
support is where the file transfer comes from. `docs/protocol.md` says which piece
came from where.

Licensed **AGPL-3.0-or-later**, as Gadgetbridge and two of those projects are, since
the protocol layer follows Gadgetbridge's.

## Building

```
~/SailfishOS/bin/sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
```

The RPM lands in `RPMS/`. Install on the phone with `devel-su pkcon install-local`.
Tests need only `g++`: `tests/run.sh`. `tests/README.md` has the rest.

Translations: English is the source; Finnish is in `translations/`. After adding a
`qsTr()` or `tr()`:

```
lupdate -locations absolute -no-obsolete src qml -ts translations/harbour-edgesync.ts
python3 tools/fill_fi.py          # add the Finnish text to the dict first; it names what is missing
```

## Using it

1. On the Edge: *Settings > Connectivity > Phone > Pair Smartphone* (the exact menu
   differs by model).
2. In the app: pull down, *Set up your Edge*, pick it from the list. It pairs.
3. Pull down, *Sync now*. New activities are fetched oldest first.

`docs/design.md` has the layout and the reasoning behind the choices.
