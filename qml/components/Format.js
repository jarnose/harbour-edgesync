.pragma library

// Formatting helpers shared by the pages. QML's JavaScript is ES5 (Qt 5.6):
// no let/const, arrow functions or template strings in this file.

function pad2(n) {
    return n < 10 ? "0" + n : "" + n
}

function valid(v) {
    return v !== undefined && v !== null && !isNaN(v)
}

// 1:23:45 or 23:45, from seconds.
function duration(seconds) {
    if (!valid(seconds))
        return "–"
    var total = Math.round(seconds)
    var h = Math.floor(total / 3600)
    var m = Math.floor(total % 3600 / 60)
    var s = total % 60
    if (h > 0)
        return h + ":" + pad2(m) + ":" + pad2(s)
    return m + ":" + pad2(s)
}

// 2 h 05 min, from seconds: the form that reads in a list.
function durationWords(seconds) {
    if (!valid(seconds))
        return "–"
    var minutes = Math.round(seconds / 60)
    var h = Math.floor(minutes / 60)
    var m = minutes % 60
    if (h === 0)
        return m + " min"
    return h + " h " + pad2(m) + " min"
}

// Kilometres, from metres. Under 10 km one decimal is worth having; over, it is noise.
function km(metres) {
    if (!valid(metres))
        return "–"
    var k = metres / 1000
    return k < 10 ? k.toFixed(2) : k.toFixed(1)
}

function kmh(metresPerSecond) {
    if (!valid(metresPerSecond))
        return "–"
    return (metresPerSecond * 3.6).toFixed(1)
}

function number(value, decimals) {
    if (!valid(value))
        return "–"
    return Number(value).toFixed(decimals === undefined ? 0 : decimals)
}

function metres(value) {
    if (!valid(value))
        return "–"
    return Math.round(value).toString()
}

// "Tue 4.8. 09:01"
function dateTime(ms) {
    var d = new Date(ms)
    return Qt.formatDate(d, "ddd d.M.") + " " + pad2(d.getHours()) + ":" + pad2(d.getMinutes())
}

function longDate(ms) {
    var d = new Date(ms)
    return Qt.formatDate(d, "dddd d.M.yyyy") + ", " + pad2(d.getHours()) + ":" + pad2(d.getMinutes())
}

// "August 2026", from the model's "2026-08"
function monthLabel(month) {
    var p = month.split("-")
    return Qt.formatDate(new Date(parseInt(p[0], 10), parseInt(p[1], 10) - 1, 1), "MMMM yyyy")
}

// How long ago, as a number and a unit: "5 min", "3 h", "2 d". "now" under a minute.
function ago(ms) {
    var s = Math.max(0, (Date.now() - ms) / 1000)
    if (s < 60)
        return "now"
    if (s < 3600)
        return Math.floor(s / 60) + " min"
    if (s < 86400)
        return Math.floor(s / 3600) + " h"
    return Math.floor(s / 86400) + " d"
}
