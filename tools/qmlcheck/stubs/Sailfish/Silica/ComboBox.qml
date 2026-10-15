import QtQuick 2.0
Item { property string label; property int currentIndex: 0; property Item menu; width: parent ? parent.width : 300; height: 80
    Text { x: 24; color: "white"; text: parent.label + " ▾" } }
