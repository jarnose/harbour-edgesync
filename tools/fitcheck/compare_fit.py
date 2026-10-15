#!/usr/bin/env python3
"""Compares src/fit (through fitdump) with fitparse on the same files.

fitparse is an independent FIT decoder with its own copy of Garmin's profile, so
agreement on a digest of every record means the field numbers, scales, offsets
and invalid-value handling in src/fit are right for those files.

    python3 -m venv /tmp/fitvenv && /tmp/fitvenv/bin/pip install fitparse
    g++ -std=c++17 -I src tools/fitcheck/fitdump.cpp src/fit/*.cpp src/gfdi/crc.cpp -o /tmp/fitdump
    /tmp/fitvenv/bin/python tools/fitcheck/compare_fit.py /tmp/fitdump file.fit ...
"""
import calendar
import math
import subprocess
import sys

import fitparse

SEMI = 180.0 / 2 ** 31


def unix(dt):
    return calendar.timegm(dt.timetuple())


def expected(path):
    f = fitparse.FitFile(path)
    e = {}
    recs = [m for m in f.get_messages('record')]
    sess = [m for m in f.get_messages('session')]
    laps = [m for m in f.get_messages('lap')]
    s = {x.name: x.value for x in sess[0]} if sess else {}

    def g(name, alt=None):
        v = s.get(name)
        if v is None and alt:
            v = s.get(alt)
        return v

    e['start'] = unix(g('start_time'))
    e['elapsed'] = g('total_elapsed_time')
    e['timer'] = g('total_timer_time')
    e['distance'] = g('total_distance')
    e['avg_speed'] = g('enhanced_avg_speed', 'avg_speed')
    e['max_speed'] = g('enhanced_max_speed', 'max_speed')
    e['avg_hr'] = g('avg_heart_rate')
    e['max_hr'] = g('max_heart_rate')
    e['calories'] = g('total_calories')
    e['ascent'] = g('total_ascent')
    e['descent'] = g('total_descent')
    e['min_alt'] = g('enhanced_min_altitude', 'min_altitude')
    e['max_alt'] = g('enhanced_max_altitude', 'max_altitude')
    e['avg_temp'] = g('avg_temperature')
    e['records'] = len(recs)
    e['laps'] = len(laps)

    t = lat = lon = alt = dist = spd = 0.0
    hr = pw = cad = pos = 0
    for r in recs:
        d = {x.name: x.value for x in r}
        t += unix(d['timestamp'])
        la, lo = d.get('position_lat'), d.get('position_long')
        if la is not None and lo is not None:
            lat += la * SEMI
            lon += lo * SEMI
            pos += 1
        a = d.get('enhanced_altitude', d.get('altitude'))
        if a is not None:
            alt += a
        if d.get('distance') is not None:
            dist += d['distance']
        sp = d.get('enhanced_speed', d.get('speed'))
        if sp is not None:
            spd += sp
        if d.get('heart_rate') is not None:
            hr += d['heart_rate']
        if d.get('power') is not None:
            pw += d['power']
        if d.get('cadence') is not None:
            cad += d['cadence']
    e.update(sum_time=t, with_pos=pos, sum_lat=lat, sum_lon=lon, sum_alt=alt, sum_dist=dist,
             sum_speed=spd, sum_hr=hr, sum_power=pw, sum_cad=cad)
    return e


def actual(dump, path):
    out = subprocess.run([dump, path], capture_output=True, text=True, check=True).stdout
    a = {}
    for line in out.splitlines():
        k, _, v = line.partition(' ')
        a[k] = v
    return a


def same(k, want, got):
    if want is None:
        return got in ('nan', '-1', '0')
    if got == 'nan':
        return False
    try:
        g = float(got)
    except ValueError:
        return False
    w = float(want)
    tol = max(0.01, abs(w) * 1e-6) if k.startswith('sum_') else 0.011
    if k in ('sum_lat', 'sum_lon'):
        tol = 1e-3
    return abs(g - w) <= tol


def main():
    dump, files = sys.argv[1], sys.argv[2:]
    bad = 0
    for path in files:
        want, got = expected(path), actual(dump, path)
        for k, w in want.items():
            ok = same(k, w, got.get(k, ''))
            if not ok:
                bad += 1
            print(('ok   ' if ok else 'FAIL ') + '%-10s want %-24s got %s' % (k, w, got.get(k)))
        print('---', path)
    sys.exit(1 if bad else 0)


main()
