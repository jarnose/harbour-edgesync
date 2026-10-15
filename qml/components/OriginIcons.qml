import QtQuick 2.0
import Sailfish.Silica 1.0

// Where a ride came from, and whether Garmin Connect has it, as two small symbols:
//
//   device-download  recorded by an Edge and downloaded from it over Bluetooth
//   cloud-download   downloaded from Garmin Connect
//   attach           imported from a FIT file
//
// then, for the two that did not come from Garmin Connect (which has them by
// definition), a cloud-upload: lit when Garmin Connect has the ride, dimmed and
// struck through when it does not yet. There is no crossed-out cloud in the
// theme, hence the bar drawn on top; the two states differ by colour as well as by
// the bar, because at this size one cue alone is easy to miss.
Row {
    id: icons

    property string origin: ""
    property bool onGarmin: false
    property bool highlighted: false

    spacing: Theme.paddingSmall

    Image {
        anchors.verticalCenter: parent.verticalCenter
        source: {
            var name = icons.origin === "garmin" ? "icon-s-cloud-download"
                     : icons.origin === "import" ? "icon-s-attach"
                     : "icon-s-device-download"
            return "image://theme/" + name + "?"
                   + (icons.highlighted ? Theme.highlightColor : Theme.primaryColor)
        }
    }

    Item {
        width: cloud.width
        height: cloud.height
        visible: icons.origin !== "garmin"
        anchors.verticalCenter: parent.verticalCenter

        Image {
            id: cloud
            source: "image://theme/icon-s-cloud-upload?"
                    + (icons.onGarmin ? Theme.highlightColor : Theme.secondaryColor)
        }
        Rectangle {
            anchors.centerIn: parent
            visible: !icons.onGarmin
            width: Math.round(parent.width * 1.25)
            height: Math.max(1, Math.round(parent.height / 12))
            radius: height / 2
            rotation: -45
            color: Theme.highlightColor
        }
    }
}
