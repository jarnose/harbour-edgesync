import QtQuick 2.0
Item { property real contentHeight: 100; property bool highlighted: false; signal clicked(); width: parent ? parent.width : 0; height: contentHeight }
