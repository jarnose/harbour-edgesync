import QtQuick 2.0
import Sailfish.Silica 1.0

// A line over time or over a sequence. Give it `ys` and, for a time axis, `xs`
// in milliseconds. With no `xs` the points are evenly spaced and `labels`
// (optional, same length) name them.
Item {
    id: chart

    property var xs: []
    property var ys: []
    property var labels: []
    property color lineColor: Theme.highlightColor
    property bool fillUnder: true
    property bool showDots: false
    property string unit: ""
    property int decimals: 0
    // Fixed vertical range; leave NaN to fit the data with a little headroom.
    property real fixedMin: NaN
    property real fixedMax: NaN
    // Dashed horizontal reference, e.g. a personal baseline.
    property real reference: NaN

    implicitHeight: Theme.itemSizeLarge * 2
    height: implicitHeight

    onXsChanged: canvas.requestPaint()
    onYsChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()
    onReferenceChanged: canvas.requestPaint()

    readonly property real leftMargin: Theme.paddingLarge * 2
    readonly property real bottomMargin: Theme.fontSizeExtraSmall * 1.6

    function range() {
        var lo = Infinity, hi = -Infinity
        for (var i = 0; i < ys.length; ++i) {
            if (ys[i] === null || ys[i] === undefined || isNaN(ys[i]))
                continue
            lo = Math.min(lo, ys[i])
            hi = Math.max(hi, ys[i])
        }
        if (!isFinite(lo)) { lo = 0; hi = 1 }
        if (!isNaN(reference)) { lo = Math.min(lo, reference); hi = Math.max(hi, reference) }
        if (hi - lo < 1e-9) { lo -= 1; hi += 1 }
        var pad = (hi - lo) * 0.12
        var a = isNaN(fixedMin) ? lo - pad : fixedMin
        var b = isNaN(fixedMax) ? hi + pad : fixedMax
        return { min: a, max: b }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        // A page that was left and is back again has lost the canvas's contents
        // (the texture is released while it is not shown): paint again.
        onVisibleChanged: if (visible) requestPaint()
        onAvailableChanged: if (available) requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var n = chart.ys.length
            var plotW = width - chart.leftMargin
            var plotH = height - chart.bottomMargin
            var r = chart.range()
            function yOf(v) { return plotH - (v - r.min) / (r.max - r.min) * plotH }

            // grid and y labels
            ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
            ctx.fillStyle = Theme.secondaryColor
            ctx.strokeStyle = Theme.rgba(Theme.primaryColor, 0.12)
            ctx.lineWidth = 1
            for (var g = 0; g <= 2; ++g) {
                var gv = r.min + (r.max - r.min) * g / 2
                var gy = yOf(gv)
                ctx.beginPath()
                ctx.moveTo(chart.leftMargin, gy)
                ctx.lineTo(width, gy)
                ctx.stroke()
                ctx.fillText(Number(gv).toFixed(chart.decimals), 0, Math.max(gy - 2, Theme.fontSizeExtraSmall))
            }
            if (n === 0) {
                ctx.fillText(qsTr("No data"), chart.leftMargin + Theme.paddingMedium, plotH / 2)
                return
            }

            var useTime = chart.xs.length === n && n > 1
            var x0 = useTime ? chart.xs[0] : 0
            var x1 = useTime ? chart.xs[n - 1] : Math.max(1, n - 1)
            if (x1 - x0 < 1e-9) x1 = x0 + 1
            function xOf(i) {
                var xv = useTime ? chart.xs[i] : i
                return chart.leftMargin + (xv - x0) / (x1 - x0) * plotW
            }

            if (!isNaN(chart.reference)) {
                ctx.save()
                ctx.setLineDash([6, 6])
                ctx.strokeStyle = Theme.rgba(Theme.primaryColor, 0.45)
                var ry = yOf(chart.reference)
                ctx.beginPath()
                ctx.moveTo(chart.leftMargin, ry)
                ctx.lineTo(width, ry)
                ctx.stroke()
                ctx.restore()
            }

            // line, breaking at missing values
            ctx.lineWidth = Theme.paddingSmall / 2
            ctx.lineJoin = "round"
            ctx.strokeStyle = chart.lineColor
            var open = false
            var firstX = 0, lastX = 0
            ctx.beginPath()
            for (var i = 0; i < n; ++i) {
                var v = chart.ys[i]
                if (v === null || v === undefined || isNaN(v)) { open = false; continue }
                var px = xOf(i), py = yOf(v)
                if (!open) { ctx.moveTo(px, py); open = true } else ctx.lineTo(px, py)
            }
            ctx.stroke()

            if (chart.fillUnder && n > 1) {
                ctx.beginPath()
                var started = false
                for (var j = 0; j < n; ++j) {
                    var w = chart.ys[j]
                    if (w === null || w === undefined || isNaN(w)) continue
                    if (!started) { ctx.moveTo(xOf(j), plotH); ctx.lineTo(xOf(j), yOf(w)); started = true }
                    else ctx.lineTo(xOf(j), yOf(w))
                    lastX = xOf(j)
                }
                ctx.lineTo(lastX, plotH)
                ctx.closePath()
                ctx.fillStyle = Theme.rgba(chart.lineColor, 0.18)
                ctx.fill()
            }

            if (chart.showDots || n <= 31) {
                ctx.fillStyle = chart.lineColor
                for (var k = 0; k < n; ++k) {
                    var dv = chart.ys[k]
                    if (dv === null || dv === undefined || isNaN(dv)) continue
                    ctx.beginPath()
                    ctx.arc(xOf(k), yOf(dv), n > 60 ? 1.5 : 3.5, 0, Math.PI * 2)
                    ctx.fill()
                }
            }

            // x labels: first and last
            ctx.fillStyle = Theme.secondaryColor
            ctx.font = Theme.fontSizeExtraSmall + "px sans-serif"
            var firstLabel, lastLabel
            if (chart.labels.length === n) {
                firstLabel = chart.labels[0]; lastLabel = chart.labels[n - 1]
            } else if (useTime) {
                var a = new Date(chart.xs[0]), b = new Date(chart.xs[n - 1])
                firstLabel = a.getHours() + ":" + (a.getMinutes() < 10 ? "0" : "") + a.getMinutes()
                lastLabel = b.getHours() + ":" + (b.getMinutes() < 10 ? "0" : "") + b.getMinutes()
            } else {
                firstLabel = ""; lastLabel = ""
            }
            ctx.fillText(firstLabel, chart.leftMargin, height - 2)
            var tw = ctx.measureText(lastLabel).width
            ctx.fillText(lastLabel, width - tw, height - 2)
        }
    }
}
