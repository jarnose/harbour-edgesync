#!/bin/sh
# Runs what .github/workflows/build.yml runs before it builds the RPMs, in the same
# order and with the same commands. Run it before every push: a translation file
# whose line numbers went stale (any edit to a file with a qsTr() or tr() above it)
# fails CI exactly like a failing test, and nothing else catches it.
#
# Needs g++, python3 and Qt's lupdate/lrelease (qttools5-dev-tools). The working
# tree is left as it was found only if it is already clean in translations/: a
# stale translation file is rewritten, which is the fix, and shows up in `git status`.
set -e
cd "$(dirname "$0")/.."
export PATH=/usr/lib/x86_64-linux-gnu/qt5/bin:$PATH

echo "== Qt-free test suites"
tests/run.sh >/dev/null && echo ok

echo "== QML"
python3 tools/check_qml.py

echo "== translations"
lupdate -locations absolute -no-obsolete src qml -ts translations/harbour-edgesync.ts >/dev/null
python3 tools/fill_fi.py
if ! git diff --exit-code -- translations >/dev/null; then
    echo "translations/ was out of date and has been regenerated: commit it." >&2
    git diff --stat -- translations >&2
    exit 1
fi
lrelease translations/harbour-edgesync-fi.ts -qm /tmp/edgesync-fi.qm | grep "0 unfinished"
echo "all clear"
