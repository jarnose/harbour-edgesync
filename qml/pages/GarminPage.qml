import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"
import "../components/Format.js" as Format

// Garmin Connect: sign in, download the rides that are there and not here, send the
// rides that are here and not there. Nothing here runs by itself: every request to
// Garmin follows a button.
Page {
    id: page

    readonly property var garmin: AppController.garmin
    readonly property var ranges: [30, 90, 365, 0]

    function rangeIndex() {
        for (var i = 0; i < ranges.length; ++i)
            if (ranges[i] === garmin.rangeDays)
                return i
        return 0
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            visible: garmin.signedIn
            MenuItem {
                text: qsTr("Sign out")
                onClicked: garmin.signOut()
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Garmin Connect")
                description: garmin.signedIn ? garmin.email : ""
            }

            // ---- what is going on ----
            Column {
                width: parent.width
                visible: garmin.busy
                spacing: Theme.paddingSmall
                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: BusyIndicatorSize.Medium
                    running: garmin.busy
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.secondaryHighlightColor
                    wrapMode: Text.Wrap
                    text: garmin.status
                }
                ProgressBar {
                    width: parent.width
                    visible: garmin.progress >= 0
                    minimumValue: 0
                    maximumValue: 1
                    value: Math.max(garmin.progress, 0)
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Cancel")
                    onClicked: garmin.cancel()
                }
            }
            Label {
                visible: !garmin.busy && garmin.status.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                text: garmin.status
            }
            Label {
                visible: garmin.lastError.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                text: garmin.lastError
                MouseArea {
                    anchors.fill: parent
                    onClicked: garmin.clearError()
                }
            }

            // ---- signed out ----
            Column {
                width: parent.width
                visible: !garmin.signedIn && !garmin.needsMfa
                spacing: Theme.paddingMedium

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    text: qsTr("Garmin has no public way for an app like this to reach your rides, so Edge "
                               + "Sync signs in the way Garmin's own mobile app does. Garmin does not "
                               + "officially allow that for other apps: it may stop working without notice, "
                               + "and Garmin may slow it down or block it. Your password goes to Garmin and "
                               + "nowhere else and is not saved; what is kept is the sign-in token, in the "
                               + "Sailfish secrets vault.")
                }
                TextField {
                    id: emailField
                    width: parent.width
                    label: qsTr("Email address")
                    placeholderText: label
                    text: garmin.email
                    inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    EnterKey.iconSource: "image://theme/icon-m-enter-next"
                    EnterKey.onClicked: passwordField.focus = true
                }
                PasswordField {
                    id: passwordField
                    width: parent.width
                    label: qsTr("Password")
                    placeholderText: label
                    EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                    EnterKey.onClicked: {
                        passwordField.focus = false
                        garmin.signIn(emailField.text, passwordField.text)
                    }
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    enabled: !garmin.busy && emailField.text.length > 0 && passwordField.text.length > 0
                    text: qsTr("Sign in")
                    onClicked: {
                        passwordField.focus = false
                        garmin.signIn(emailField.text, passwordField.text)
                    }
                }
            }

            // ---- the verification code ----
            Column {
                width: parent.width
                visible: garmin.needsMfa
                spacing: Theme.paddingMedium

                TextField {
                    id: codeField
                    width: parent.width
                    label: qsTr("Verification code")
                    placeholderText: label
                    inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhNoPredictiveText
                    EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                    EnterKey.onClicked: {
                        codeField.focus = false
                        garmin.submitMfa(codeField.text)
                    }
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    enabled: !garmin.busy && codeField.text.length > 0
                    text: qsTr("Verify")
                    onClicked: {
                        codeField.focus = false
                        garmin.submitMfa(codeField.text)
                    }
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Cancel")
                    onClicked: garmin.cancel()
                }
            }

            // ---- signed in ----
            Column {
                width: parent.width
                visible: garmin.signedIn
                spacing: Theme.paddingMedium

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                    text: garmin.lastSyncMs > 0 ? qsTr("Last used %1").arg(Format.dateTime(garmin.lastSyncMs))
                                                : qsTr("Not used yet")
                }

                SectionHeader { text: qsTr("From Garmin Connect") }
                ComboBox {
                    width: parent.width
                    label: qsTr("Look back")
                    currentIndex: page.rangeIndex()
                    menu: ContextMenu {
                        MenuItem { text: qsTr("30 days"); onClicked: garmin.rangeDays = 30 }
                        MenuItem { text: qsTr("90 days"); onClicked: garmin.rangeDays = 90 }
                        MenuItem { text: qsTr("A year"); onClicked: garmin.rangeDays = 365 }
                        MenuItem { text: qsTr("Everything"); onClicked: garmin.rangeDays = 0 }
                    }
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    enabled: !garmin.busy
                    text: qsTr("Download new activities")
                    onClicked: garmin.downloadNew()
                }

                SectionHeader { text: qsTr("To Garmin Connect") }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                    text: garmin.unsentCount > 0
                          ? qsTr("%n activities here are not on Garmin Connect.", "", garmin.unsentCount)
                          : qsTr("Every activity here is on Garmin Connect.")
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    enabled: !garmin.busy && garmin.unsentCount > 0
                    text: qsTr("Send them")
                    onClicked: garmin.uploadUnsent()
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                    wrapMode: Text.Wrap
                    text: qsTr("Garmin Connect says no to a ride it already has, and that is noted. A ride "
                               + "you imported from a file is sent as an import, so it is not passed on to "
                               + "services linked to your Garmin account. Signing out removes the sign-in "
                               + "from this phone; it does not revoke it at Garmin.")
                }

                SectionHeader { text: qsTr("The symbols in the list") }
                Repeater {
                    model: [
                        { o: "edge", g: true, t: qsTr("Downloaded from the Edge; on Garmin Connect") },
                        { o: "edge", g: false, t: qsTr("Downloaded from the Edge; not on Garmin Connect") },
                        { o: "garmin", g: true, t: qsTr("Downloaded from Garmin Connect") },
                        { o: "import", g: false, t: qsTr("Imported from a file; not on Garmin Connect") }
                    ]
                    delegate: Row {
                        x: Theme.horizontalPageMargin
                        spacing: Theme.paddingMedium
                        OriginIcons {
                            anchors.verticalCenter: parent.verticalCenter
                            origin: modelData.o
                            onGarmin: modelData.g
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: column.width - 2 * Theme.horizontalPageMargin - Theme.itemSizeSmall * 1.5
                            wrapMode: Text.Wrap
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeExtraSmall
                            text: modelData.t
                        }
                    }
                }
            }
        }
        VerticalScrollDecorator { }
    }
}
