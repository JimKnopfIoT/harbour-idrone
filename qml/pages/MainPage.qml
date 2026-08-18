// harbour-idrone — contact list (M1).
// Live list of detected drones (Bluetooth Legacy Remote ID, Tier 0). Each row:
// serial (ID), aircraft type, status, real distance, broadcast coordinates, operator.
// A status bar at the bottom shows which sniffer hardware is connected (auto-detected).
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All

    // Attach the map/radar to the right — swipe left to reach it. Re-attach after
    // returning from a pushed detail page (which detaches it).
    onStatusChanged: {
        if (status === PageStatus.Active && pageStack.depth === 1
                && !pageStack.nextPage(page))
            pageStack.pushAttached(Qt.resolvedUrl("RadarPage.qml"))
    }

    function fmtDist(m) {
        if (m < 0) return ""
        if (m < 1000) return Math.round(m) + " m"
        return (m / 1000).toFixed(m < 10000 ? 2 : 1) + " km"
    }

    SilicaListView {
        id: list
        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: hwBar.top }

        // Decouple the live model from the view: only refresh the snapshot when
        // the user isn't interacting, so frequent updates don't fight the
        // pulldown/scroll gesture (avoids the twitch).
        property var rows: drone.contacts
        model: rows
        function refreshRows() {
            if (!dragging && !moving && !(pdm.active))
                rows = drone.contacts
        }
        Connections { target: drone; onUpdated: list.refreshRows() }
        onDraggingChanged: if (!dragging) refreshRows()
        onMovingChanged: if (!moving) refreshRows()

        header: Column {
            width: page.width

            PageHeader {
                title: qsTr("iDrone")
                description: drone.scanning ? qsTr("scanning — passive")
                            : drone.available ? qsTr("idle")
                            : qsTr("no Bluetooth adapter")
            }

            Item { width: 1; height: Theme.paddingMedium }
        }

        PullDownMenu {
            id: pdm
            onActiveChanged: if (!active) list.refreshRows()
            MenuItem {
                text: qsTr("About")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: drone.demo ? qsTr("Stop demo") : qsTr("Demo mode")
                onClicked: drone.setDemo(!drone.demo)
            }
            MenuItem {
                text: qsTr("Clear")
                onClicked: drone.clear()
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: drone.powered ? qsTr("No drones in range")
                                 : qsTr("Bluetooth is off")
            hintText: qsTr("Cooperatively-broadcasting drones (Remote ID) appear here.")
        }

        delegate: ListItem {
            id: item
            contentHeight: col.height + Theme.paddingMedium
            onClicked: pageStack.push(Qt.resolvedUrl("DronePage.qml"),
                                      { contactKey: modelData.key })

            Column {
                id: col
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                y: Theme.paddingSmall
                spacing: 2

                // ID text (Remote-ID serial), or a probable/unknown fallback
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: (modelData.serial && modelData.serial.length > 0)
                          ? modelData.serial
                          : (modelData.probable
                             ? (modelData.uaTypeName + " " + qsTr("(probable)"))
                             : qsTr("unknown drone"))
                    color: modelData.isAlarm ? "#ff3b30"
                         : (item.highlighted ? Theme.highlightColor : Theme.primaryColor)
                }

                Row {
                    spacing: Theme.paddingMedium
                    Label {
                        text: modelData.uaTypeName ? modelData.uaTypeName : qsTr("—")
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryColor
                    }
                    Label {
                        visible: !!modelData.statusName
                        text: modelData.statusName ? modelData.statusName : ""
                        font.pixelSize: Theme.fontSizeSmall
                        color: modelData.status === 2 ? Theme.highlightColor
                                                      : Theme.secondaryColor
                    }
                    Label {
                        visible: modelData.hasDistance === true
                        text: page.fmtDist(modelData.distanceM)
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                    }
                    Label {
                        visible: modelData.hasLocation === true
                        text: Math.round(modelData.height) + " m " + qsTr("alt")
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryColor
                    }
                }

                // Broadcast coordinates of the aircraft (public Remote-ID data)
                Label {
                    visible: modelData.hasLocation === true
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: "▸ " + Number(modelData.lat).toFixed(6)
                          + ", " + Number(modelData.lon).toFixed(6)
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }

                Label {
                    visible: modelData.hasOperator === true
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: qsTr("Operator: ") + modelData.operatorId
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
            }
        }

        VerticalScrollDecorator {}
    }

    // Connected-hardware indicator — docked at the bottom.
    HwIndicator {
        id: hwBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
    }

    // proximity alarm banner — page overlay (does not scroll with the list)
    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: alarmLbl.height + 2 * Theme.paddingMedium
        color: Theme.rgba("#ff3b30", 0.9)
        visible: drone.alarmActive
        z: 100
        Label {
            id: alarmLbl
            anchors { verticalCenter: parent.verticalCenter
                      left: parent.left; right: parent.right
                      margins: Theme.horizontalPageMargin }
            wrapMode: Text.WordWrap
            color: "white"
            text: qsTr("⚠ Drone nearby — ") + drone.alarmText
        }
    }
}
