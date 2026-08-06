// harbour-idrone — about + honest expectation-setting (LEGAL.md).
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    allowedOrientations: Orientation.All
    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height + Theme.paddingLarge

        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("About iDrone") }

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "../harbour-idrone.png"
                width: Theme.iconSizeExtraLarge; height: width
                fillMode: Image.PreserveAspectFit; smooth: true
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Passive drone detector")
                color: Theme.highlightColor
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                text: qsTr("Receive-only. iDrone listens for the Remote ID that drones broadcast by law (Bluetooth + Wi-Fi) and warns when one comes close. It never transmits, never connects.")
            }

            SectionHeader { text: qsTr("What it can and cannot see") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Sees: drones broadcasting Remote ID (the large " +
                      "majority of legally-operated consumer/commercial drones). " +
                      "With extra hardware also DJI DroneID and radio-only aircraft.\n\n" +
                      "Cannot see: radio-silent / fully autonomous drones that emit " +
                      "nothing. This is a passive receiver, not a radar.")
                color: Theme.primaryColor
            }

            SectionHeader { text: qsTr("Privacy") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                text: qsTr("No logging by default. No cloud, no telemetry. The " +
                      "operator position is not shown unless explicitly enabled.")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: "GPL-3.0-or-later · parser: opendroneid-core-c (Apache-2.0)"
            }
        }
    }
}
