import QtQuick 2.0
// Stand-in for the C++ MapCanvas: a grey panel with the same properties.
Rectangle { property var track: []; property string tileUrl; property bool darkMode: true; property color routeColor: "orange"
    property bool loading: false; property string error: ""; color: "#40586f"
    Text { anchors.centerIn: parent; color: "white"; text: "MapCanvas: " + track.length + " points\n" + tileUrl } }
