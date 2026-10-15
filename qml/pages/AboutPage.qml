import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("About Edge Sync") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("Edge Sync downloads the activities recorded by a Garmin Edge bike computer over "
                           + "Bluetooth LE and shows them. It is an independent project: it is not made, "
                           + "endorsed or supported by Garmin.")
            }
            SectionHeader { text: qsTr("Privacy") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("The app has no network permission. Rides are kept as the original FIT files in "
                           + "the app's own folder on this phone; exports go to Documents/EdgeSync.")
            }
            SectionHeader { text: qsTr("Credits") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                text: qsTr("Garmin's Bluetooth protocol was reverse engineered by the Gadgetbridge project "
                           + "(AGPL-3.0). garmin-ble, garmin-bridge and GarminBLE showed how to speak it "
                           + "from Linux. This app is free software under the same licence.")
            }
            SectionHeader { text: qsTr("Data folder") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: AppController.dataDirectory()
            }
        }
        VerticalScrollDecorator { }
    }
}
