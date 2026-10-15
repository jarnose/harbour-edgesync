# Edge Sync

A Sailfish OS app that downloads the activities recorded by a **Garmin Edge** bike
computer, over Bluetooth LE, straight to the phone. No Garmin account, no cloud,
no Internet permission.

* Pairs with an Edge and syncs on request (pull down, *Sync now*).
* Keeps each ride as the original **FIT** file, and shows it: distance, moving
  time, speed, climb, heart rate, power, cadence, temperature, energy, laps, the
  route, and a chart for each thing the Edge recorded.
* Exports a ride as **GPX** (with heart rate, cadence, temperature and power) or
  as the FIT file itself, to `~/Documents/EdgeSync`.
* Leaves the Edge's own flags alone by default, so Garmin Connect keeps working.
* Keeps a log of the conversation with the Edge, because the protocol is
  reverse-engineered and differs between models.

> **Status: written, built, and tested against a scripted Edge. Not yet run
> against a real one.** The Bluetooth protocol is a port of what the Gadgetbridge
> project found; `docs/protocol.md` lists what is assumed. If a sync fails, the
> log (*Settings > Open the log*, with *Record Bluetooth traffic* on) is what to
> send.

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
