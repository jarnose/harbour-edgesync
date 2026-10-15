import QtQuick 2.0
Item { property string text; property string hintText; anchors.centerIn: parent; width: 400; height: 100
    Text { color: "white"; text: parent.text; visible: parent.enabled } }
