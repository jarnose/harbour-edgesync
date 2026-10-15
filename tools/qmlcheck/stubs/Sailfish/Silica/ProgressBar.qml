import QtQuick 2.0
Rectangle { property real minimumValue: 0; property real maximumValue: 1; property real value: 0; height: 40; color: "#222"
    Rectangle { width: parent.width * (parent.value - parent.minimumValue) / (parent.maximumValue - parent.minimumValue); height: parent.height; color: "#3da9ff" } }
