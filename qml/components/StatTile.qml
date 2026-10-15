import QtQuick 2.0
import Sailfish.Silica 1.0

// A number with a caption under it; the unit sits small next to the number.
Item {
    id: tile

    property string value: "–"
    property string unit: ""
    property string caption: ""
    property color valueColor: Theme.primaryColor

    width: Theme.itemSizeHuge
    height: valueRow.height + captionLabel.height + Theme.paddingSmall + Theme.paddingMedium

    Row {
        id: valueRow
        spacing: Theme.paddingSmall / 2
        anchors.horizontalCenter: parent.horizontalCenter
        Label {
            id: valueLabel
            text: tile.value
            color: tile.valueColor
            font.pixelSize: Theme.fontSizeLarge
        }
        Label {
            text: tile.unit
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
            anchors.baseline: valueLabel.baseline
        }
    }
    Label {
        id: captionLabel
        anchors {
            top: valueRow.bottom
            topMargin: Theme.paddingSmall
            horizontalCenter: parent.horizontalCenter
        }
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: tile.caption
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeExtraSmall
        truncationMode: TruncationMode.Fade
    }
}
