import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components/Format.js" as Format

// The rides on the phone, and the way to fetch more. Nothing here talks to the
// Edge unless "Sync now" is pressed: the app never connects on its own.
Page {
    id: page

    function syncedText() {
        if (!AppController.hasDevice)
            return ""
        if (AppController.lastSyncMs <= 0)
            return AppController.deviceName
        var a = Format.ago(AppController.lastSyncMs)
        return AppController.deviceName + " · "
                + (a === "now" ? qsTr("synced just now") : qsTr("synced %1 ago").arg(a))
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: AppController.activities

        PullDownMenu {
            busy: AppController.busy
            MenuItem {
                text: qsTr("About")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: AppController.hasDevice ? qsTr("Change Edge") : qsTr("Set up your Edge")
                onClicked: pageStack.push(Qt.resolvedUrl("DevicePage.qml"))
            }
            MenuItem {
                visible: AppController.hasDevice
                enabled: !AppController.busy
                text: qsTr("Sync now")
                onClicked: AppController.syncNow()
            }
        }

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Edge Sync")
                description: page.syncedText()
            }

            Column {
                width: parent.width
                visible: AppController.busy
                spacing: Theme.paddingSmall
                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: BusyIndicatorSize.Medium
                    running: AppController.busy
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.secondaryHighlightColor
                    text: AppController.status
                    wrapMode: Text.Wrap
                }
                ProgressBar {
                    width: parent.width
                    visible: AppController.progress >= 0
                    minimumValue: 0
                    maximumValue: 1
                    value: Math.max(AppController.progress, 0)
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Cancel")
                    onClicked: AppController.cancelSync()
                }
            }

            Label {
                visible: !AppController.busy && AppController.status.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                color: Theme.secondaryHighlightColor
                text: AppController.status
                wrapMode: Text.Wrap
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

            Column {
                width: parent.width
                visible: !AppController.hasDevice
                spacing: Theme.paddingLarge
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    color: Theme.highlightColor
                    font.pixelSize: Theme.fontSizeLarge
                    text: qsTr("Your rides, on your phone")
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    color: Theme.secondaryColor
                    text: qsTr("Edge Sync downloads the activities from a Garmin Edge bike computer over "
                               + "Bluetooth. There is no account and no cloud: the files stay on this phone, "
                               + "and you can export them as GPX or FIT.")
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Set up your Edge")
                    onClicked: pageStack.push(Qt.resolvedUrl("DevicePage.qml"))
                }
            }

            Button {
                visible: AppController.hasDevice && !AppController.busy
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Sync now")
                onClicked: AppController.syncNow()
            }
        }

        section.property: "month"
        section.criteria: ViewSection.FullString
        section.delegate: SectionHeader {
            text: Format.monthLabel(section)
        }

        delegate: ListItem {
            id: item
            contentHeight: Theme.itemSizeMedium + Theme.paddingSmall
            onClicked: pageStack.push(Qt.resolvedUrl("ActivityPage.qml"), { activityId: model.activityId })

            Label {
                id: titleLabel
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: distanceLabel.left
                    rightMargin: Theme.paddingMedium
                    top: parent.top
                    topMargin: Theme.paddingSmall
                }
                text: model.title
                color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                truncationMode: TruncationMode.Fade
            }
            Label {
                id: distanceLabel
                anchors {
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    baseline: titleLabel.baseline
                }
                text: model.distance >= 0 ? Format.km(model.distance) + " km" : ""
                color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            Label {
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    top: titleLabel.bottom
                }
                font.pixelSize: Theme.fontSizeExtraSmall
                color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                truncationMode: TruncationMode.Fade
                text: {
                    var parts = [Format.dateTime(model.startMs)]
                    if (model.timer >= 0)
                        parts.push(Format.durationWords(model.timer))
                    if (model.avgSpeed >= 0)
                        parts.push(Format.kmh(model.avgSpeed) + " km/h")
                    if (!model.parsed)
                        parts.push(qsTr("unreadable file"))
                    return parts.join(" · ")
                }
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0 && !AppController.busy && AppController.hasDevice
            text: qsTr("No rides yet")
            hintText: qsTr("Pull down and choose Sync now")
        }

        VerticalScrollDecorator { }
    }
}
