// harbour-idrone — cover (M0).
// Family style (cf. iWifi): the app logo enlarged as a faint single-tone
// watermark behind a live count. The contact count is wired to the fusion
// daemon in a later milestone; for now it shows the passive-detector identity.
import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    // oversized app-icon watermark (the drone-in-crosshair logo, monochrome-ish)
    Image {
        anchors.centerIn: parent
        source: "/usr/share/icons/hicolor/172x172/apps/harbour-idrone.png"
        sourceSize { width: 172; height: 172 }
        width: parent.width * 1.5
        height: width
        fillMode: Image.PreserveAspectFit
        opacity: 0.16
        smooth: true
        asynchronous: true
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.paddingSmall

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: drone.count
            font.pixelSize: Theme.fontSizeHuge
            font.bold: true
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: drone.count === 1 ? qsTr("drone") : qsTr("drones")
            color: Theme.secondaryColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: drone.scanning
            text: qsTr("scanning…")
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeExtraSmall
        }
    }

    CoverActionList {
        enabled: drone.available
        CoverAction {
            iconSource: drone.scanning ? "image://theme/icon-cover-pause"
                                       : "image://theme/icon-cover-play"
            onTriggered: drone.scanning ? drone.stop() : drone.start()
        }
    }
}
