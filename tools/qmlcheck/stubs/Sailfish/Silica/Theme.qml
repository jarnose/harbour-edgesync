pragma Singleton
import QtQuick 2.0
QtObject {
    readonly property real paddingSmall: 6
    readonly property real paddingMedium: 12
    readonly property real paddingLarge: 24
    readonly property real horizontalPageMargin: 24
    readonly property real itemSizeExtraSmall: 48
    readonly property real itemSizeSmall: 80
    readonly property real itemSizeMedium: 100
    readonly property real itemSizeLarge: 120
    readonly property real itemSizeHuge: 180
    readonly property real fontSizeTiny: 16
    readonly property real fontSizeExtraSmall: 20
    readonly property real fontSizeSmall: 26
    readonly property real fontSizeMedium: 30
    readonly property real fontSizeLarge: 36
    readonly property real fontSizeHuge: 64
    readonly property color primaryColor: "#ffffff"
    readonly property color secondaryColor: "#b0b0b0"
    readonly property color highlightColor: "#3da9ff"
    readonly property color secondaryHighlightColor: "#2f7fbf"
    readonly property color errorColor: "#ff4d4d"
    function rgba(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
}
