// harbour-idrone — active-sensor indicator (bottom bar).
// Shows every ACTIVE reception source as a coloured dot + name: external sniffers
// auto-detected via HwDetect (drone.hw), plus the phone's internal BLE chip when it
// is scanning (they run in parallel). Order: CatSniffer > Ubertooth > Wi-Fi > Phone.
import QtQuick 2.0
import Sailfish.Silica 1.0

Rectangle {
    id: root
    height: Theme.itemSizeExtraSmall
    color: Theme.rgba(Theme.secondaryColor, 0.12)

    function kindColor(kind) {
        return kind === "catsniffer"   ? "#2ecc71"   // green
             : kind === "ubertooth"    ? "#f5c518"   // yellow
             : kind === "wifi-monitor" ? "#ff8c42"   // orange
             :                           "#8a8a8a"
    }
    function kindRank(kind) {
        return kind === "catsniffer" ? 0
             : kind === "ubertooth" ? 1
             : kind === "wifi-monitor" ? 2 : 3
    }

    // Build the ordered list of active sources: [{color, text}, ...]
    function sources() {
        var out = []
        var devs = drone.hw.devices.slice()
        devs.sort(function (a, b) { return kindRank(a.kind) - kindRank(b.kind) })
        for (var i = 0; i < devs.length; ++i)
            out.push({ color: kindColor(devs[i].kind), text: devs[i].name })
        // Internal phone BLE chip, shown AFTER the external sniffers, when active.
        // Blue = Bluetooth (semantic + clearly active); grey is reserved for "off".
        if (drone.internalBleActive)
            out.push({ color: "#3a8dff", text: qsTr("Phone BLE") })
        if (out.length === 0)
            out.push({ color: "#8a8a8a", text: qsTr("no sensor") })
        return out
    }
    // Re-evaluated when HwDetect changes (devices NOTIFY) or the scan state changes.
    property var srcs: (drone.hw.devices, drone.internalBleActive, sources())

    Row {
        anchors {
            left: parent.left; leftMargin: Theme.horizontalPageMargin
            right: parent.right; rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        spacing: Theme.paddingLarge

        Repeater {
            model: root.srcs
            delegate: Row {
                spacing: Theme.paddingSmall
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.paddingMedium; height: width; radius: width / 2
                    color: modelData.color
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.text
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.primaryColor
                }
            }
        }
    }
}
