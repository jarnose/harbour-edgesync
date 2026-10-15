import QtQuick 2.0
Item { property string title; property string description; width: parent ? parent.width : 0; height: 120
    Text { color: "white"; text: parent.title; font.pixelSize: 36; x: 24; y: 10 }
    Text { color: "#aaa"; text: parent.description; font.pixelSize: 20; x: 24; y: 70 } }
