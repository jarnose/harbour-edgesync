#!/usr/bin/env python3
"""Renders every page offscreen against stand-ins for Sailfish.Silica.

The phone shows a QML mistake (a misspelt property, a missing import, a binding
that throws) as a blank or half-drawn page and a line in a log nobody reads. The
C++ build cannot see it, and `lupdate` and tools/check_qml.py only read the text.
This loads each page for real, with a mock AppController, and fails on any QML
warning. It also writes a PNG per page, which is how layout gets looked at
without a phone: the stand-ins are crude, so judge structure, not pixels.

    python3 -m venv /tmp/qv && /tmp/qv/bin/pip install PySide6-Essentials
    /tmp/qv/bin/python tools/qmlcheck/render.py [outdir]
"""
import os
import sys

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtCore import (QAbstractListModel, QModelIndex, QObject, QTimer, QUrl, Property, Qt, Signal, Slot,
                            qInstallMessageHandler)
from PySide6.QtGui import QColor, QGuiApplication, QImage, QPainter, QFont
from PySide6.QtQuick import QQuickImageProvider, QQuickView

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OUT = sys.argv[1] if len(sys.argv) > 1 else "/tmp/edgesync-qml"
os.makedirs(OUT, exist_ok=True)

messages = []


def handler(mode, ctx, msg):
    messages.append(msg)
    print("QML:", msg)


qInstallMessageHandler(handler)


class ThemeIcons(QQuickImageProvider):
    """Stands in for image://theme/: a coloured tile with the icon's first letter after "icon-s-"."""

    def __init__(self):
        super().__init__(QQuickImageProvider.Image)

    def requestImage(self, ident, size, requested):
        name, _, color = ident.partition("?")
        img = QImage(32, 32, QImage.Format_ARGB32)
        img.fill(QColor(0, 0, 0, 0))
        p = QPainter(img)
        p.setPen(QColor(color or "#ffffff"))
        f = QFont()
        f.setPixelSize(26)
        f.setBold(True)
        p.setFont(f)
        letter = name.replace("icon-s-", "")[:1].upper() or "?"
        if "cloud-upload" in name:
            letter = "↑"
        elif "cloud-download" in name:
            letter = "☁"
        p.drawText(img.rect(), Qt.AlignCenter, letter)
        p.end()
        return img


class ListModel(QAbstractListModel):
    def __init__(self, names, rows):
        super().__init__()
        self._names = [n.encode() for n in names]
        self._rows = rows

    def rowCount(self, parent=QModelIndex()):
        return len(self._rows)

    def data(self, index, role):
        if not index.isValid():
            return None
        key = self._names[role - Qt.UserRole - 1].decode()
        return self._rows[index.row()].get(key)

    def roleNames(self):
        return {Qt.UserRole + 1 + i: n for i, n in enumerate(self._names)}


ACTIVITY_ROLES = ["activityId", "startMs", "sport", "sportLabel", "name", "title", "month", "distance", "timer",
                  "elapsed", "avgSpeed", "avgHr", "avgPower", "ascent", "calories", "parsed", "origin", "garminId", "onGarmin"]


def activity(i, month, title, km, secs, kmh, parsed=True, origin="edge", garmin=0):
    return dict(activityId=i, startMs=1785740505000 - i * 86400000, sport="cycling", sportLabel="Cycling",
                name="", title=title, month=month, distance=km * 1000, timer=secs, elapsed=secs + 300,
                avgSpeed=kmh / 3.6, avgHr=130 + i, avgPower=-1, ascent=120, calories=700, parsed=parsed,
                origin=origin, garminId=garmin, onGarmin=garmin != 0)


ROWS = [
    activity(5, "2026-08", "Cycling", 42.3, 5400, 28.2, origin="edge", garmin=22000000001),
    activity(4, "2026-08", "Mountain biking", 18.04, 4000, 16.2, origin="edge", garmin=0),
    activity(3, "2026-08", "Running", 10.1, 3000, 12.0, origin="garmin", garmin=22000000003),
    activity(2, "2026-07", "Walking", 3.2, 2300, 5.0, origin="import", garmin=0),
    activity(1, "2026-07", "Cycling", 5.4, 2300, 8.5, parsed=False, origin="import", garmin=-1),
]
DEVICES = [
    dict(address="C0:FF:EE:00:11:22", name="Edge 1040", paired=True, connected=False, rssi=-60),
    dict(address="C0:FF:EE:33:44:55", name="Edge 530", paired=False, connected=False, rssi=-80),
]


def series(n=300):
    import math
    t = [i * 20.0 for i in range(n)]
    f = lambda fn: [fn(i) for i in range(n)]
    return {
        "time": t,
        "labels": ["%d:%02d" % (x // 60, x % 60) for x in map(int, t)],
        "distance": f(lambda i: i * 0.14),
        "speed": f(lambda i: 25 + 8 * math.sin(i / 20.0)),
        "heartRate": f(lambda i: None if i < 5 else 130 + 20 * math.sin(i / 30.0)),
        "power": f(lambda i: 180 + 60 * math.sin(i / 10.0)),
        "cadence": f(lambda i: 85 + 5 * math.sin(i / 7.0)),
        "altitude": f(lambda i: 40 + 25 * math.sin(i / 50.0)),
        "temperature": f(lambda i: 18.0 + i / 100.0),
        "hasSpeed": True, "hasHeartRate": True, "hasPower": True, "hasCadence": True, "hasAltitude": True,
        "hasTemperature": True,
    }


def detail(indoor=False, bad=False):
    import math
    d = dict(id=3, name="", title="Cycling", sport="cycling", sportLabel="Cycling", startMs=1785740505000, parsed=True,
             origin="edge", onGarmin=False, garminId=0, fitPath="/home/user/.local/share/io.github.jarnose/edgesync/fit/2026/x.fit", fileSize=53896)
    if bad:
        d["error"] = "the file holds neither a session nor any records"
        return d
    d.update(timer=5400.0, elapsed=5900.0, distance=42300.0, avgSpeed=7.8, maxSpeed=14.2, avgHr=141, maxHr=172,
             avgPower=190, maxPower=640, normalizedPower=212, avgCadence=86, ascent=312.0, descent=300.0,
             minAltitude=12.0, maxAltitude=88.0, avgTemperature=19.0, calories=1020, trainingStress=61.5,
             intensityFactor=0.8, device="Edge 1040", crcOk=True, truncated=False, records=5400)
    s = series()
    d["series"] = s
    d["mapTrack"] = [] if indoor else [dict(latitude=t[0], longitude=t[1]) for t in [[60.17 + 0.01 * math.sin(i / 25.0), 24.94 + 0.02 * math.cos(i / 25.0)] for i in range(150)]]
    d["track"] = [] if indoor else [[60.17 + 0.01 * math.sin(i / 25.0), 24.94 + 0.02 * math.cos(i / 25.0)] for i in range(150)]
    d["laps"] = [dict(number=i + 1, timer=600.0, distance=7000.0, avgSpeed=7.5, avgHr=140, avgPower=None, ascent=40.0)
                 for i in range(4)]
    return d


class PageStack(QObject):
    @Slot("QVariant", "QVariant")
    def push(self, url, props=None):
        pass

    @Slot()
    def pop(self):
        pass


class MockGarmin(QObject):
    never = Signal()

    def __init__(self, **state):
        super().__init__()
        self.s = dict(signedIn=True, email="me@example.com", needsMfa=False, busy=False, status="", progress=-1.0,
                      lastError="", lastSyncMs=1785740505000, rangeDays=30, unsentCount=2)
        self.s.update(state)

    signedIn = Property(bool, lambda s: s.s["signedIn"], notify=never)
    email = Property(str, lambda s: s.s["email"], notify=never)
    needsMfa = Property(bool, lambda s: s.s["needsMfa"], notify=never)
    busy = Property(bool, lambda s: s.s["busy"], notify=never)
    status = Property(str, lambda s: s.s["status"], notify=never)
    progress = Property(float, lambda s: s.s["progress"], notify=never)
    lastError = Property(str, lambda s: s.s["lastError"], notify=never)
    lastSyncMs = Property("qlonglong", lambda s: s.s["lastSyncMs"], notify=never)
    rangeDays = Property(int, lambda s: s.s["rangeDays"], lambda s, v: None, notify=never)
    unsentCount = Property(int, lambda s: s.s["unsentCount"], notify=never)

    @Slot(str, str)
    def signIn(self, a, b): pass
    @Slot(str)
    def submitMfa(self, c): pass
    @Slot()
    def signOut(self): pass
    @Slot()
    def cancel(self): pass
    @Slot()
    def clearError(self): pass
    @Slot()
    def downloadNew(self): pass
    @Slot()
    def uploadUnsent(self): pass
    @Slot(int)
    def uploadActivity(self, i): pass


class App(QObject):
    changed = Signal()
    _never = Signal()

    def __init__(self, garmin=None, **state):
        super().__init__()
        garmin = garmin or {}
        self.s = dict(busy=False, status="", progress=-1.0, lastError="", hasDevice=True, deviceName="Edge 1040",
                      deviceAddress="C0:FF:EE:00:11:22", lastSyncMs=1785740505000, scanning=False,
                      activityCount=len(ROWS), archiveOnEdge=False, onlyNewFiles=False, syncClock=False,
                      disconnectAfterSync=True, traceFrames=False, baseMapEnabled=True,
                      mapTileUrl="https://tiles.openfreemap.org/planet")
        self.s.update(state)
        self._acts = ListModel(ACTIVITY_ROLES, ROWS if self.s["activityCount"] else [])
        self._devs = ListModel(["address", "name", "paired", "connected", "rssi"], DEVICES)
        self.detail = detail()
        self._garmin = MockGarmin(**garmin)

    def _p(name, typ):
        return Property(typ, lambda self: self.s[name], notify=lambda: None)

    busy = Property(bool, lambda s: s.s["busy"], constant=True)
    status = Property(str, lambda s: s.s["status"], constant=True)
    progress = Property(float, lambda s: s.s["progress"], constant=True)
    lastError = Property(str, lambda s: s.s["lastError"], constant=True)
    hasDevice = Property(bool, lambda s: s.s["hasDevice"], constant=True)
    deviceName = Property(str, lambda s: s.s["deviceName"], constant=True)
    deviceAddress = Property(str, lambda s: s.s["deviceAddress"], constant=True)
    lastSyncMs = Property("qlonglong", lambda s: s.s["lastSyncMs"], constant=True)
    scanning = Property(bool, lambda s: s.s["scanning"], constant=True)
    activityCount = Property(int, lambda s: s.s["activityCount"], constant=True)
    archiveOnEdge = Property(bool, lambda s: s.s["archiveOnEdge"], lambda s, v: None, notify=_never)
    onlyNewFiles = Property(bool, lambda s: s.s["onlyNewFiles"], lambda s, v: None, notify=_never)
    syncClock = Property(bool, lambda s: s.s["syncClock"], lambda s, v: None, notify=_never)
    disconnectAfterSync = Property(bool, lambda s: s.s["disconnectAfterSync"], lambda s, v: None, notify=_never)
    traceFrames = Property(bool, lambda s: s.s["traceFrames"], lambda s, v: None, notify=_never)
    baseMapEnabled = Property(bool, lambda s: s.s["baseMapEnabled"], lambda s, v: None, notify=_never)
    mapTileUrl = Property(str, lambda s: s.s["mapTileUrl"], lambda s, v: None, notify=_never)
    defaultMapTileUrl = Property(str, lambda s: "https://tiles.openfreemap.org/planet", constant=True)
    garmin = Property(QObject, lambda s: s._garmin, constant=True)
    activities = Property(QObject, lambda s: s._acts, constant=True)
    devices = Property(QObject, lambda s: s._devs, constant=True)

    activitiesChanged = Signal()
    logChanged = Signal()

    @Slot(str)
    def importFitFile(self, p): pass
    @Slot()
    def startScan(self): pass
    @Slot()
    def stopScan(self): pass
    @Slot()
    def syncNow(self): pass
    @Slot()
    def cancelSync(self): pass
    @Slot()
    def clearError(self): pass
    @Slot()
    def forgetDevice(self): pass
    @Slot(str, str)
    def chooseDevice(self, a, b): pass
    @Slot(str, str)
    def pairDevice(self, a, b): pass
    @Slot(int, result="QVariantMap")
    def activityDetail(self, i): return self.detail
    @Slot(result="QVariantMap")
    def latestActivity(self): return dict(id=3, startMs=1785740505000, title="Cycling", distance=42300.0, timer=5400.0)
    @Slot(int, result=str)
    def exportGpx(self, i): return "/home/user/Documents/EdgeSync/x.gpx"
    @Slot(int, result=str)
    def exportFit(self, i): return "/home/user/Documents/EdgeSync/x.fit"
    @Slot(int, str, result=bool)
    def renameActivity(self, i, n): return True
    @Slot(int, result=bool)
    def deleteActivity(self, i): return True
    @Slot(result=str)
    def logText(self): return "12:00:01.100 --- sync with Edge 1040\n12:00:01.200 link ready (multi-link 2810/2820)"
    @Slot()
    def clearLog(self): pass
    @Slot(result=str)
    def exportLog(self): return "/home/user/Documents/EdgeSync/log.txt"
    @Slot(result=str)
    def dataDirectory(self): return "/home/user/.local/share/io.github.jarnose/edgesync"


CASES = [
    ("main", "pages/MainPage.qml", {}, {}),
    ("main_empty_nodevice", "pages/MainPage.qml", {"hasDevice": False, "deviceName": "", "deviceAddress": "", "activityCount": 0}, {}),
    ("main_empty_device", "pages/MainPage.qml", {"activityCount": 0}, {}),
    ("main_busy", "pages/MainPage.qml", {"busy": True, "status": "Downloading activity 2 of 5…", "progress": 0.4}, {}),
    ("main_error", "pages/MainPage.qml", {"lastError": "Could not reach the Edge. Is it on, nearby, and not connected to another phone?"}, {}),
    ("device", "pages/DevicePage.qml", {"scanning": True}, {}),
    ("activity", "pages/ActivityPage.qml", {}, {"activityId": 3}),
    ("activity_indoor", "pages/ActivityPage.qml", {}, {"activityId": 3, "_indoor": True}),
    ("activity_unreadable", "pages/ActivityPage.qml", {}, {"activityId": 3, "_bad": True}),
    ("activity_nomap", "pages/ActivityPage.qml", {"baseMapEnabled": False}, {"activityId": 3}),
    ("settings", "pages/SettingsPage.qml", {"traceFrames": True}, {}),
    ("settings_nomap", "pages/SettingsPage.qml", {"baseMapEnabled": False}, {}),
    ("log", "pages/DiagnosticsPage.qml", {}, {}),
    ("garmin_signedout", "pages/GarminPage.qml", {"garmin": {"signedIn": False, "email": ""}}, {}),
    ("garmin_mfa", "pages/GarminPage.qml", {"garmin": {"signedIn": False, "needsMfa": True, "status": "Garmin sent a verification code (sms). Enter it below."}}, {}),
    ("garmin_in", "pages/GarminPage.qml", {}, {}),
    ("garmin_busy", "pages/GarminPage.qml", {"garmin": {"busy": True, "status": "Downloading activity 2 of 5…", "progress": 0.4}}, {}),
    ("garmin_error", "pages/GarminPage.qml", {"garmin": {"signedIn": False, "lastError": "Garmin answered 429: it is rate-limiting or blocking this client. Try again later."}}, {}),
    ("garmin_allsent", "pages/GarminPage.qml", {"garmin": {"unsentCount": 0}}, {}),
    ("about", "pages/AboutPage.qml", {}, {}),
    ("cover", "cover/CoverPage.qml", {}, {}),
]


def strip_attached(text):
    """Removes `EnterKey.*` bindings: Silica's attached property has no stand-in here."""
    import re
    out, i = [], 0
    lines = text.split("\n")
    while i < len(lines):
        line = lines[i]
        if re.match(r"\s*EnterKey\.", line):
            depth = line.count("{") - line.count("}")
            while depth > 0:
                i += 1
                depth += lines[i].count("{") - lines[i].count("}")
            i += 1
            continue
        out.append(line)
        i += 1
    return "\n".join(out)


def prepared_qml():
    """A copy of qml/ with the unstubbable bits stripped; the real files are untouched."""
    import shutil
    dst = os.path.join(OUT, "qml")
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, "qml"), dst)
    for base, _, names in os.walk(dst):
        for n in names:
            if n.endswith(".qml"):
                p = os.path.join(base, n)
                text = strip_attached(open(p).read())
                open(p, "w").write(text)
    return dst


def main():
    app = QGuiApplication(sys.argv)
    qml_root = prepared_qml()
    failures = 0
    keep = []
    for name, path, state, props in CASES:
        before = len(messages)
        controller = App(**state)
        if props.pop("_indoor", False):
            controller.detail = detail(indoor=True)
        if props.pop("_bad", False):
            controller.detail = detail(bad=True)

        view = QQuickView()
        view.setColor(QColor('#1b2430'))  # the stand-in Theme draws light text
        view.engine().addImportPath(os.path.join(HERE, "stubs"))
        view.engine().addImportPath(qml_root)
        view.engine().addImageProvider("theme", ThemeIcons())
        view.rootContext().setContextProperty("AppController", controller)
        view.rootContext().setContextProperty("pageStack", PageStack())
        wide = path.startswith("cover")
        tall = name == "activity"  # long enough to show the charts
        host = os.path.join(OUT, "_host.qml")
        with open(host, "w") as f:
            f.write('import QtQuick 2.0\nItem { width: %d; height: %d\n  Loader { id: l; anchors.fill: parent }\n}\n'
                    % ((400, 640) if wide else (540, 3400 if tall else 960)))
        view.setSource(QUrl.fromLocalFile(host))
        view.resize(400 if wide else 540, 640 if wide else (3400 if tall else 960))
        view.show()
        loader = view.rootObject().findChild(QObject, "") or view.rootObject().children()[0]
        # Loader with initial properties: set them through the page once created.
        loader.setProperty("source", QUrl.fromLocalFile(os.path.join(qml_root, path)))
        for _ in range(8):
            app.processEvents()
            QTimer.singleShot(60, lambda: None)
            app.processEvents()
        item = loader.property("item")
        if item is None:
            print("FAIL %s: the page did not load" % name)
            failures += 1
            continue
        for k, v in props.items():
            item.setProperty(k, v)
        # let timers (ActivityPage parses after 120 ms) and bindings run
        import time
        end = time.time() + 0.6
        while time.time() < end:
            app.processEvents()
            time.sleep(0.02)
        image = view.grabWindow()
        image.save(os.path.join(OUT, name + ".png"))
        new = [m for m in messages[before:] if "deprecated" not in m]  # Qt 6 notes on Qt 5 syntax
        bad = [m for m in new if any(w in m for w in (
            "Error", "error", "TypeError", "ReferenceError", "is not a type", "Unable to assign", "Cannot read",
            "not defined", "Binding loop", "undefined", "is not installed"))]
        print("%-22s %s" % (name, "FAIL" if bad else "ok"))
        if bad:
            failures += 1
        # Tear the page down before its controller can go: bindings that outlive
        # their context property log errors that are the harness's, not the page's.
        loader.setProperty("source", QUrl())
        for _ in range(4):
            app.processEvents()
        view.close()
        keep.append((controller, view))
        del messages[before + len(new):]
    print("%d of %d pages failed" % (failures, len(CASES)))
    return 1 if failures else 0


sys.exit(main())
