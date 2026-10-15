import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: dialog
    property alias name: field.text

    canAccept: true

    Column {
        width: parent.width
        DialogHeader { title: qsTr("Rename") }
        TextField {
            id: field
            width: parent.width
            label: qsTr("Name")
            placeholderText: qsTr("Name")
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: dialog.accept()
        }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            text: qsTr("Leave empty to show the kind of activity instead.")
        }
    }
}
