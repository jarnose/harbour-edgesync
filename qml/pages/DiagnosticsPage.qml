import QtQuick 2.0
import Sailfish.Silica 1.0

// What the app and the Edge said to each other. The Edge's protocol is
// reverse-engineered and varies between models and firmware, so when a sync
// fails this log is the thing worth attaching to a report.
Page {
    id: page

    property string saved: ""

    function refresh() {
        logArea.text = AppController.logText()
    }

    Component.onCompleted: refresh()

    Connections {
        target: AppController
        onLogChanged: page.refresh()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Clear the log")
                onClicked: AppController.clearLog()
            }
            MenuItem {
                text: qsTr("Save the log as a file")
                onClicked: page.saved = AppController.exportLog()
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader { title: qsTr("Log") }

            Label {
                visible: page.saved.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Saved to %1").arg(page.saved)
            }

            TextArea {
                id: logArea
                width: parent.width
                readOnly: true
                font.family: "monospace"
                font.pixelSize: Theme.fontSizeTiny
                labelVisible: false
                placeholderText: qsTr("Nothing has happened yet")
            }
        }
        VerticalScrollDecorator { }
    }
}
