// harbour-idrone — drone detail page.
// Live details for one contact (looked up by key so it keeps updating). The
// operator/pilot position is deliberately NOT shown (privacy — see LEGAL.md).
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All

    property string contactKey
    property var contact: null

    function fmtDist(m) {
        if (m === undefined || m < 0) return "—"
        return m < 1000 ? Math.round(m) + " m" : (m / 1000).toFixed(2) + " km"
    }

    function refresh() {
        var list = drone.contacts
        for (var i = 0; i < list.length; ++i) {
            if (list[i].key === contactKey) { contact = list[i]; return }
        }
        contact = null   // dropped out of range
    }
    Component.onCompleted: refresh()
    Connections { target: drone; onUpdated: page.refresh() }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height + Theme.paddingLarge

        VerticalScrollDecorator {}

        Column {
            id: col
            width: page.width
            spacing: Theme.paddingSmall

            PageHeader {
                title: contact
                       ? ((contact.serial && contact.serial.length > 0) ? contact.serial
                          : (contact.probable ? qsTr("Probable drone") : qsTr("Unknown drone")))
                       : qsTr("Drone")
            }

            Label {
                visible: !contact
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                color: Theme.secondaryColor
                text: qsTr("Signal lost — this drone is no longer in range.")
            }

            Column {
                width: parent.width
                visible: !!contact

                Repeater {
                    model: page.rows
                    delegate: Item {
                        width: col.width
                        height: valLbl.paintedHeight + Theme.paddingMedium
                        Label {
                            id: keyLbl
                            x: Theme.horizontalPageMargin
                            width: parent.width * 0.42 - Theme.horizontalPageMargin
                            text: modelData.k
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.WordWrap
                        }
                        Label {
                            id: valLbl
                            anchors.left: keyLbl.right
                            width: parent.width - keyLbl.width - 2 * Theme.horizontalPageMargin
                            text: modelData.v
                            color: modelData.alarm ? "#ff3b30" : Theme.primaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }

    // build the label/value rows from the live contact
    property var rows: []
    onContactChanged: {
        var r = []
        var c = contact
        if (c) {
            if (c.idTypeName) r.push({ k: qsTr("ID type"), v: c.idTypeName })
            if (c.uaTypeName) r.push({ k: qsTr("Aircraft"), v: c.uaTypeName })
            if (c.statusName)  r.push({ k: qsTr("Status"), v: c.statusName,
                                        alarm: c.status === 3 })
            r.push({ k: qsTr("Source"), v: c.source ? c.source : "—" })
            if (c.hasDistance)
                r.push({ k: qsTr("Distance"), v: fmtDist(c.distanceM), alarm: c.isAlarm })
            if (c.hasLocation) {
                r.push({ k: qsTr("Position"), v: c.lat.toFixed(5) + ", " + c.lon.toFixed(5) })
                r.push({ k: qsTr("Height"), v: Math.round(c.height) + " m" })
                r.push({ k: qsTr("Altitude (WGS84)"), v: Math.round(c.altGeo) + " m" })
                r.push({ k: qsTr("Speed"), v: (c.speed !== undefined ? c.speed.toFixed(1) : "?") + " m/s" })
                r.push({ k: qsTr("Course"), v: Math.round(c.direction) + "°" })
            }
            if (c.hasOperator) r.push({ k: qsTr("Operator"), v: c.operatorId })
            if (c.ssid)        r.push({ k: qsTr("SSID"), v: c.ssid })
            if (c.rssi !== undefined) r.push({ k: qsTr("Signal"), v: c.rssi + " dBm" })
        }
        rows = r
    }
}
