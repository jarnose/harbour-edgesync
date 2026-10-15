import QtQuick 2.0
import Sailfish.Silica 1.0

// Choosing the Edge. The Edge has to be in pairing mode the first time, and
// phone and Edge have to be paired before the Edge will talk: Bluetooth's own
// pairing does that, started from here.
Page {
    id: page

    Component.onCompleted: AppController.startScan()
    Component.onDestruction: AppController.stopScan()

    SilicaListView {
        id: list
        anchors.fill: parent
        model: AppController.devices

        PullDownMenu {
            busy: AppController.scanning
            MenuItem {
                visible: AppController.hasDevice
                text: qsTr("Forget this Edge")
                onClicked: {
                    AppController.forgetDevice()
                    pageStack.pop()
                }
            }
            MenuItem {
                text: qsTr("Scan again")
                onClicked: AppController.startScan()
            }
        }

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium
            PageHeader {
                title: qsTr("Your Edge")
                description: AppController.hasDevice ? AppController.deviceName : ""
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("On the Edge, open Settings > Connectivity > Phone and choose Pair Smartphone, "
                           + "then pick it below. If it is already paired with your phone in Bluetooth "
                           + "settings, you can pick it straight away.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                visible: AppController.busy
                text: AppController.status
            }
            Label {
                visible: AppController.lastError.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                text: AppController.lastError
                MouseArea {
                    anchors.fill: parent
                    onClicked: AppController.clearError()
                }
            }
        }

        delegate: ListItem {
            id: item
            enabled: !AppController.busy
            contentHeight: Theme.itemSizeMedium
            onClicked: {
                if (model.paired) {
                    AppController.chooseDevice(model.address, model.name)
                    pageStack.pop()
                } else {
                    AppController.pairDevice(model.address, model.name)
                }
            }

            Label {
                id: nameLabel
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    top: parent.top
                    topMargin: Theme.paddingSmall
                }
                text: model.name
                color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                truncationMode: TruncationMode.Fade
            }
            Label {
                anchors {
                    left: nameLabel.left
                    right: nameLabel.right
                    top: nameLabel.bottom
                }
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: model.address + " · "
                      + (model.paired ? qsTr("paired") : qsTr("not paired"))
                      + (model.connected ? " · " + qsTr("connected") : "")
                      + (model.address === AppController.deviceAddress ? " · " + qsTr("selected") : "")
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: AppController.scanning ? qsTr("Looking for Edges…") : qsTr("No Edge found")
            hintText: qsTr("Switch the Edge on and keep it next to the phone")
        }

        VerticalScrollDecorator { }
    }
}
