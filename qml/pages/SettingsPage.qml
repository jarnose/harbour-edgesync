import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader { title: qsTr("Settings") }

            SectionHeader { text: qsTr("Sync") }

            TextSwitch {
                text: qsTr("Mark rides as synced on the Edge")
                description: qsTr("Sets the Edge's archive flag on every ride once it is saved here. Leave this "
                                  + "off if you also use Garmin Connect or another sync: they decide what is "
                                  + "new by the same flag.")
                checked: AppController.archiveOnEdge
                automaticCheck: false
                onClicked: AppController.archiveOnEdge = !AppController.archiveOnEdge
            }
            TextSwitch {
                text: qsTr("Ask the Edge for new files only")
                description: qsTr("Asks the Edge to list only files not marked as synced. Faster, but it hides "
                                  + "rides that another app has already marked.")
                checked: AppController.onlyNewFiles
                automaticCheck: false
                onClicked: AppController.onlyNewFiles = !AppController.onlyNewFiles
            }
            TextSwitch {
                text: qsTr("Set the Edge's clock")
                description: qsTr("Tells the Edge the phone's time zone and time. An Edge with GPS keeps "
                                  + "its own time, so this is normally not needed.")
                checked: AppController.syncClock
                automaticCheck: false
                onClicked: AppController.syncClock = !AppController.syncClock
            }
            TextSwitch {
                text: qsTr("Disconnect after syncing")
                description: qsTr("Lets go of the Edge when done, so Garmin Connect or another phone can use it.")
                checked: AppController.disconnectAfterSync
                automaticCheck: false
                onClicked: AppController.disconnectAfterSync = !AppController.disconnectAfterSync
            }

            SectionHeader { text: qsTr("Help") }

            TextSwitch {
                text: qsTr("Record Bluetooth traffic in the log")
                description: qsTr("Adds every byte sent to and received from the Edge to the log. Turn on to "
                                  + "report a problem; the log can then be saved as a text file.")
                checked: AppController.traceFrames
                automaticCheck: false
                onClicked: AppController.traceFrames = !AppController.traceFrames
            }
            ButtonLayout {
                Button {
                    text: qsTr("Open the log")
                    onClicked: pageStack.push(Qt.resolvedUrl("DiagnosticsPage.qml"))
                }
            }
        }
        VerticalScrollDecorator { }
    }
}
