#!/bin/sh
# Builds and runs every Qt-free test with plain g++, once per language mode.
#
# Both modes on purpose. -std=c++17 (strict) is what a bare g++ gives; the
# Sailfish build uses gnu++17, where `unix` is a predefined macro for 1 and
# turns any variable of that name into a compile error far from the cause.
#
# Usage: tests/run.sh      (from anywhere)
set -e
cd "$(dirname "$0")"

GFDI="../src/gfdi/crc.cpp ../src/gfdi/cobs.cpp ../src/gfdi/message.cpp ../src/gfdi/messages.cpp"
FIT="../src/fit/fitdecoder.cpp ../src/fit/activity.cpp ../src/gfdi/crc.cpp"

run() {
    name=$1; shift
    for std in c++17 gnu++17; do
        echo "== $name ($std)"
        g++ -std=$std -Wall -Wextra -Werror -I ../src "test_$name.cpp" "$@" -o "/tmp/edgesync_$name"
        "/tmp/edgesync_$name"
    done
}

run framing $GFDI
run messages $GFDI ../src/gfdi/multilink.cpp
run session $GFDI ../src/gfdi/session.cpp
run fit $FIT
run gpx ../src/export/gpx.cpp
run garmin ../src/garmin/json.cpp ../src/garmin/zip.cpp ../src/garmin/http.cpp ../src/garmin/auth.cpp ../src/garmin/api.cpp -lz
run mercator ../src/map/mercator.cpp
run vectortile ../src/map/vectortile.cpp
