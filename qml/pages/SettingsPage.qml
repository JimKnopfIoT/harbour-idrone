// harbour-idrone — settings. The one thing here that touches the system is the
// external-CatSniffer switch: off by default, it turns the root helper unit on
// and off. Nothing runs at boot; without the switch there is no root service.
import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

Page {
    allowedOrientations: Orientation.All

    // Persisted so the choice survives a restart; re-applied on launch by the
    // main window. Default off — the app is fully useful on the phone alone.
    ConfigurationValue {
        id: cfgCat
        key: "/apps/harbour-idrone/catSnifferEnabled"
        defaultValue: false
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: col.height + Theme.paddingLarge

        Column {
            id: col
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Settings") }

            SectionHeader { text: qsTr("External CatSniffer") }

            TextSwitch {
                text: qsTr("Use an external CatSniffer")
                description: qsTr("For Bluetooth 5 Long Range Remote ID beyond the "
                    + "phone's own range. Turning this on starts a small root helper "
                    + "service that reads the USB sniffer and hands decoded data to "
                    + "the app; it is receive-only and never transmits. Off, nothing "
                    + "of it runs — the phone-only detection (Bluetooth + Wi-Fi) is "
                    + "unaffected either way.")
                checked: drone.catSnifferEnabled
                automaticCheck: false
                onClicked: {
                    var want = !drone.catSnifferEnabled
                    drone.setCatSniffer(want)
                    // Persist what actually took effect (setCatSniffer leaves the
                    // state unchanged if the system call failed).
                    cfgCat.value = drone.catSnifferEnabled
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("No CatSniffer? Leave this off. Removing the app removes "
                    + "the helper with it.")
            }
        }
    }
}
