import QtQuick 2.0
import Sailfish.Silica 1.0

// The ride's route drawn on its own, without a map underneath: fitted to the
// item, north up, with the longitude scaled by the cosine of the latitude so a
// bend is a bend and not a stretched one. `track` is a list of [lat, lon].
Item {
    id: map

    property var track: []
    property color lineColor: Theme.highlightColor

    implicitHeight: Theme.itemSizeHuge * 2
    height: implicitHeight

    onTrackChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent
        onVisibleChanged: if (visible) requestPaint()
        onAvailableChanged: if (available) requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var t = map.track
            var n = t ? t.length : 0
            if (n < 2)
                return

            var minLat = 90, maxLat = -90, minLon = 180, maxLon = -180
            for (var i = 0; i < n; ++i) {
                minLat = Math.min(minLat, t[i][0]); maxLat = Math.max(maxLat, t[i][0])
                minLon = Math.min(minLon, t[i][1]); maxLon = Math.max(maxLon, t[i][1])
            }
            var midLat = (minLat + maxLat) / 2
            var k = Math.cos(midLat * Math.PI / 180)
            var spanX = Math.max((maxLon - minLon) * k, 1e-6)
            var spanY = Math.max(maxLat - minLat, 1e-6)

            var pad = Theme.paddingLarge
            var scale = Math.min((width - 2 * pad) / spanX, (height - 2 * pad) / spanY)
            var offX = (width - spanX * scale) / 2
            var offY = (height - spanY * scale) / 2
            function px(lon) { return offX + (lon - minLon) * k * scale }
            function py(lat) { return height - (offY + (lat - minLat) * scale) }

            ctx.lineWidth = Theme.paddingSmall / 2
            ctx.lineJoin = "round"
            ctx.lineCap = "round"
            ctx.strokeStyle = map.lineColor
            ctx.beginPath()
            ctx.moveTo(px(t[0][1]), py(t[0][0]))
            for (var j = 1; j < n; ++j)
                ctx.lineTo(px(t[j][1]), py(t[j][0]))
            ctx.stroke()

            // start and finish
            ctx.fillStyle = "#4caf50"
            ctx.beginPath()
            ctx.arc(px(t[0][1]), py(t[0][0]), Theme.paddingSmall, 0, Math.PI * 2)
            ctx.fill()
            ctx.fillStyle = "#e53935"
            ctx.beginPath()
            ctx.arc(px(t[n - 1][1]), py(t[n - 1][0]), Theme.paddingSmall, 0, Math.PI * 2)
            ctx.fill()
        }
    }
}
