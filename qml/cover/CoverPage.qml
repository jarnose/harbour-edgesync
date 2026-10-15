import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components/Format.js" as Format

CoverBackground {
    id: cover

    property var latest: ({})

    function refresh() {
        latest = AppController.latestActivity()
    }

    Component.onCompleted: refresh()

    Connections {
        target: AppController
        onActivitiesChanged: cover.refresh()
    }

    Label {
        anchors {
            top: parent.top
            topMargin: Theme.paddingLarge
            horizontalCenter: parent.horizontalCenter
        }
        text: "Edge Sync"
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeExtraSmall
    }

    Column {
        anchors {
            centerIn: parent
            verticalCenterOffset: Theme.paddingMedium
        }
        width: parent.width - 2 * Theme.paddingMedium
        spacing: Theme.paddingSmall

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: latest.distance !== undefined && latest.distance !== null
            font.pixelSize: Theme.fontSizeHuge
            text: latest.distance !== undefined && latest.distance !== null ? Format.km(latest.distance) : ""
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: latest.distance !== undefined && latest.distance !== null
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            text: "km"
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: latest.startMs !== undefined
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            truncationMode: TruncationMode.Fade
            text: latest.startMs !== undefined ? Format.dateTime(latest.startMs) : ""
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: latest.startMs === undefined && !AppController.busy
            wrapMode: Text.Wrap
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeSmall
            text: AppController.hasDevice ? qsTr("No rides yet") : qsTr("No Edge set up")
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: AppController.busy
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeExtraSmall
            wrapMode: Text.Wrap
            text: AppController.status
        }
    }

    CoverActionList {
        enabled: AppController.hasDevice && !AppController.busy
        CoverAction {
            iconSource: "image://theme/icon-cover-sync"
            onTriggered: AppController.syncNow()
        }
    }
}
