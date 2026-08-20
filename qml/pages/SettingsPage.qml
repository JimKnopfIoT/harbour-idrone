// harbour-idrone — settings: the two external-helper switches (default off).
import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

Page {
    allowedOrientations: Orientation.All

    // Persisted; re-applied on launch by the main window.
    ConfigurationValue {
        id: cfgCat
        key: "/apps/harbour-idrone/catSnifferEnabled"
        defaultValue: false
    }
    ConfigurationValue {
        id: cfgWifiMon
        key: "/apps/harbour-idrone/wifiMonitorEnabled"
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
                    drone.setCatSniffer(!drone.catSnifferEnabled)
                    // Persist what actually took effect.
                    cfgCat.value = drone.catSnifferEnabled
                }
            }

            SectionHeader { text: qsTr("External Wi-Fi monitor") }

            TextSwitch {
                text: qsTr("Use a USB Wi-Fi monitor adapter")
                description: qsTr("For Wi-Fi Remote ID on all channels (beacon and "
                    + "NAN), beyond what the phone's own Wi-Fi sees. Turning this on "
                    + "starts a small root helper service that puts the USB adapter "
                    + "(Alfa etc.) into monitor mode and hands decoded data to the "
                    + "app; it is receive-only and never transmits. Off, nothing of "
                    + "it runs.")
                checked: drone.wifiMonitorEnabled
                automaticCheck: false
                onClicked: {
                    drone.setWifiMonitor(!drone.wifiMonitorEnabled)
                    cfgWifiMon.value = drone.wifiMonitorEnabled
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WordWrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("No external hardware? Leave these off. Removing the app "
                    + "removes the helpers with it.")
            }
        }
    }
}
