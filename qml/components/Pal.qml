pragma Singleton
import QtQuick 2.0

// Contact palette (from harbour-sysmetrics' Diag.qml). Red is not in the
// rotation — it is the alarm colour.
QtObject {
    readonly property color amber:  "#ffb44a"
    readonly property color cyan:   "#19d2ff"
    readonly property color teal:   "#31e0a0"
    readonly property color green:  "#8ef94a"
    readonly property color violet: "#b58cff"
    readonly property color red:    "#ff5a52"

    readonly property var ring: [amber, cyan, teal, green, violet]

    function forIndex(i) {
        if (i === undefined || i < 0) return amber
        return ring[i % ring.length]
    }
}
