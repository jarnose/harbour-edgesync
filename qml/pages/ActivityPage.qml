import QtQuick 2.0
import Sailfish.Silica 1.0
import io.github.jarnose.edgesync 1.0
import "../components"
import "../components/Format.js" as Format

// One ride: the numbers, the route, and a chart for each thing the Edge
// recorded. A chart appears only if the ride has data for it; a ride without a
// power meter shows no power chart rather than a flat line at zero.
Page {
    id: page

    property int activityId: 0
    property var d: ({})
    property string exported: ""
    readonly property var series: d.series ? d.series : ({})

    function load() {
        d = AppController.activityDetail(activityId)
    }

    Timer {
        // Let the page finish sliding in before the file is parsed.
        interval: 120
        running: true
        onTriggered: page.load()
    }

    RemorsePopup { id: remorse }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Delete")
                onClicked: remorse.execute(qsTr("Deleting"), function() {
                    if (AppController.deleteActivity(page.activityId))
                        pageStack.pop()
                })
            }
            MenuItem {
                visible: d.onGarmin === false && AppController.garmin.signedIn
                enabled: !AppController.garmin.busy
                text: qsTr("Send to Garmin Connect")
                onClicked: AppController.garmin.uploadActivity(page.activityId)
            }
            MenuItem {
                text: qsTr("Save the FIT file")
                onClicked: page.exported = AppController.exportFit(page.activityId)
            }
            MenuItem {
                visible: d.track !== undefined && d.track.length > 1
                text: qsTr("Save as GPX")
                onClicked: page.exported = AppController.exportGpx(page.activityId)
            }
            MenuItem {
                text: qsTr("Rename")
                onClicked: {
                    var dialog = pageStack.push(Qt.resolvedUrl("RenameDialog.qml"), { name: d.name ? d.name : "" })
                    dialog.accepted.connect(function() {
                        AppController.renameActivity(page.activityId, dialog.name)
                        page.load()
                    })
                }
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: d.title ? d.title : ""
                description: d.startMs ? Format.longDate(d.startMs) : ""
            }

            Row {
                visible: d.origin !== undefined
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                OriginIcons {
                    anchors.verticalCenter: parent.verticalCenter
                    origin: d.origin ? d.origin : ""
                    onGarmin: d.onGarmin === true
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.secondaryColor
                    font.pixelSize: Theme.fontSizeExtraSmall
                    text: (d.origin === "garmin" ? qsTr("Downloaded from Garmin Connect")
                           : d.origin === "import" ? qsTr("Imported from a file")
                           : qsTr("Downloaded from the Edge"))
                          + (d.origin !== "garmin" ? " · " + (d.onGarmin ? qsTr("on Garmin Connect")
                                                                         : qsTr("not on Garmin Connect")) : "")
                }
            }
            Label {
                visible: AppController.garmin.status.length > 0 && AppController.garmin.busy
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                color: Theme.secondaryHighlightColor
                text: AppController.garmin.status
            }
            Label {
                visible: page.exported.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Saved to %1").arg(page.exported)
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
            Label {
                visible: d.error !== undefined
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.errorColor
                text: d.error ? qsTr("This file could not be read: %1. It is kept on the phone, and "
                                     + "a later version of the app may read it.").arg(d.error) : ""
            }
            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Medium
                running: d.id === undefined
                visible: running
            }

            // ---- the headline numbers ----
            Grid {
                visible: d.timer !== undefined || d.distance !== undefined
                columns: page.width > Theme.itemSizeHuge * 5 ? 4 : 2
                width: parent.width
                StatTile {
                    width: parent.width / parent.columns
                    value: Format.km(d.distance)
                    unit: "km"
                    caption: qsTr("Distance")
                }
                StatTile {
                    width: parent.width / parent.columns
                    value: Format.duration(d.timer)
                    caption: qsTr("Moving time")
                }
                StatTile {
                    width: parent.width / parent.columns
                    value: Format.kmh(d.avgSpeed)
                    unit: "km/h"
                    caption: qsTr("Average speed")
                }
                StatTile {
                    width: parent.width / parent.columns
                    value: Format.kmh(d.maxSpeed)
                    unit: "km/h"
                    caption: qsTr("Top speed")
                }
                StatTile {
                    visible: d.ascent !== undefined && d.ascent !== null
                    width: parent.width / parent.columns
                    value: Format.metres(d.ascent)
                    unit: "m"
                    caption: qsTr("Climbed")
                }
                StatTile {
                    visible: d.avgHr !== undefined && d.avgHr !== null
                    width: parent.width / parent.columns
                    value: Format.number(d.avgHr)
                    unit: "bpm"
                    caption: qsTr("Average heart rate")
                }
                StatTile {
                    visible: d.maxHr !== undefined && d.maxHr !== null
                    width: parent.width / parent.columns
                    value: Format.number(d.maxHr)
                    unit: "bpm"
                    caption: qsTr("Max heart rate")
                }
                StatTile {
                    visible: d.avgPower !== undefined && d.avgPower !== null
                    width: parent.width / parent.columns
                    value: Format.number(d.avgPower)
                    unit: "W"
                    caption: qsTr("Average power")
                }
                StatTile {
                    visible: d.normalizedPower !== undefined && d.normalizedPower !== null
                    width: parent.width / parent.columns
                    value: Format.number(d.normalizedPower)
                    unit: "W"
                    caption: qsTr("Normalized power")
                }
                StatTile {
                    visible: d.avgCadence !== undefined && d.avgCadence !== null
                    width: parent.width / parent.columns
                    value: Format.number(d.avgCadence)
                    unit: "rpm"
                    caption: qsTr("Average cadence")
                }
                StatTile {
                    visible: d.calories !== undefined && d.calories !== null
                    width: parent.width / parent.columns
                    value: Format.number(d.calories)
                    unit: "kcal"
                    caption: qsTr("Energy")
                }
                StatTile {
                    visible: d.elapsed !== undefined && d.timer !== undefined && d.elapsed - d.timer > 60
                    width: parent.width / parent.columns
                    value: Format.duration(d.elapsed)
                    caption: qsTr("Elapsed time")
                }
            }

            // ---- the route ----
            SectionHeader {
                visible: d.track !== undefined && d.track.length > 1
                text: qsTr("Route")
            }
            Item {
                visible: d.track !== undefined && d.track.length > 1
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: visible ? width * 0.75 : 0

                // The base map, when it is switched on. It draws the route itself,
                // so the bare route below stands down rather than both drawing one.
                MapCanvas {
                    id: baseMap
                    anchors.fill: parent
                    visible: AppController.baseMapEnabled && d.mapTrack !== undefined && d.mapTrack.length > 1
                    track: visible ? d.mapTrack : []
                    tileUrl: AppController.mapTileUrl
                    darkMode: Theme.colorScheme === Theme.LightOnDark
                    routeColor: Theme.highlightColor

                    BusyIndicator {
                        anchors.centerIn: parent
                        size: BusyIndicatorSize.Medium
                        running: baseMap.loading
                    }
                }
                RouteMap {
                    anchors.fill: parent
                    visible: !baseMap.visible
                    track: d.track ? d.track : []
                }
            }
            // Why the map is blank, when it is: a source that cannot be used is
            // worth saying out loud, or the switch looks broken.
            Label {
                visible: baseMap.visible && baseMap.error.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                text: baseMap.error
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            // ---- the charts ----
            SectionHeader {
                visible: page.series.hasSpeed === true
                text: qsTr("Speed (km/h)")
            }
            LineChart {
                visible: page.series.hasSpeed === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.series.time ? page.series.time : []
                ys: page.series.speed ? page.series.speed : []
                labels: page.series.labels ? page.series.labels : []
                fillUnder: true
                lineColor: Theme.highlightColor
                decimals: 0
            }
            SectionHeader {
                visible: page.series.hasAltitude === true
                text: qsTr("Elevation (m)")
            }
            LineChart {
                visible: page.series.hasAltitude === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.series.time ? page.series.time : []
                ys: page.series.altitude ? page.series.altitude : []
                labels: page.series.labels ? page.series.labels : []
                lineColor: Theme.secondaryHighlightColor
                decimals: 0
            }
            SectionHeader {
                visible: page.series.hasHeartRate === true
                text: qsTr("Heart rate (bpm)")
            }
            LineChart {
                visible: page.series.hasHeartRate === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.series.time ? page.series.time : []
                ys: page.series.heartRate ? page.series.heartRate : []
                labels: page.series.labels ? page.series.labels : []
                lineColor: "#e53935"
                decimals: 0
            }
            SectionHeader {
                visible: page.series.hasPower === true
                text: qsTr("Power (W)")
            }
            LineChart {
                visible: page.series.hasPower === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.series.time ? page.series.time : []
                ys: page.series.power ? page.series.power : []
                labels: page.series.labels ? page.series.labels : []
                lineColor: "#fb8c00"
                decimals: 0
            }
            SectionHeader {
                visible: page.series.hasCadence === true
                text: qsTr("Cadence (rpm)")
            }
            LineChart {
                visible: page.series.hasCadence === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.series.time ? page.series.time : []
                ys: page.series.cadence ? page.series.cadence : []
                labels: page.series.labels ? page.series.labels : []
                lineColor: "#43a047"
                decimals: 0
            }
            SectionHeader {
                visible: page.series.hasTemperature === true
                text: qsTr("Temperature (°C)")
            }
            LineChart {
                visible: page.series.hasTemperature === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                xs: page.series.time ? page.series.time : []
                ys: page.series.temperature ? page.series.temperature : []
                labels: page.series.labels ? page.series.labels : []
                lineColor: Theme.secondaryColor
                decimals: 0
            }

            // ---- laps ----
            SectionHeader {
                visible: d.laps !== undefined && d.laps.length > 1
                text: qsTr("Laps")
            }
            Repeater {
                model: d.laps !== undefined && d.laps.length > 1 ? d.laps : []
                delegate: Item {
                    width: column.width
                    height: Theme.itemSizeSmall
                    Label {
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.number
                        color: Theme.highlightColor
                    }
                    Label {
                        x: Theme.horizontalPageMargin + Theme.itemSizeExtraSmall
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - Theme.horizontalPageMargin
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        text: {
                            var parts = []
                            if (modelData.distance !== null)
                                parts.push(Format.km(modelData.distance) + " km")
                            if (modelData.timer !== null)
                                parts.push(Format.duration(modelData.timer))
                            if (modelData.avgSpeed !== null)
                                parts.push(Format.kmh(modelData.avgSpeed) + " km/h")
                            if (modelData.avgHr !== null)
                                parts.push(Format.number(modelData.avgHr) + " bpm")
                            return parts.join(" · ")
                        }
                    }
                }
            }

            // ---- about the file ----
            SectionHeader {
                visible: d.id !== undefined
                text: qsTr("File")
            }
            Label {
                visible: d.id !== undefined
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                text: (d.device ? d.device + " · " : "") + (d.records !== undefined ? d.records + " " + qsTr("records") + " · " : "")
                      + (d.fileSize !== undefined ? Math.round(d.fileSize / 1024) + " kB" : "") + "\n"
                      + (d.fitPath ? d.fitPath : "")
                      + (d.crcOk === false ? "\n" + qsTr("The file's checksum does not match: it may be damaged.") : "")
                      + (d.truncated === true ? "\n" + qsTr("The file ends early.") : "")
            }
        }
        VerticalScrollDecorator { }
    }
}
