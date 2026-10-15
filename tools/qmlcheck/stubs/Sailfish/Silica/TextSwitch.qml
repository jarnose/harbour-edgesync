import QtQuick 2.0
import Sailfish.Silica 1.0
Item { property string text; property string description; property bool checked: false; property bool automaticCheck: true; signal clicked(); width: parent ? parent.width : 0; height: 140
    Text { x: 24; color: "white"; text: parent.text; font.pixelSize: 26 }
    Text { x: 24; y: 40; width: parent.width - 48; wrapMode: Text.Wrap; color: "#aaa"; text: parent.description; font.pixelSize: 18 } }
