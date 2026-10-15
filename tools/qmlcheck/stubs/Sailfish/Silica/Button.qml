import QtQuick 2.0
Rectangle { property string text; signal clicked(); width: 300; height: 80; color: "#335"; border.color: "#88a"
    Text { anchors.centerIn: parent; text: parent.text; color: "white" } }
