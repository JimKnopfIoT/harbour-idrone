// harbour-idrone — Silica UI entrypoint (M1).
// Passive drone detector: contact list, cover, GPS-based distance. Radar/OSM map
// page (reused from iBT/iWifi/catchercatcher) follows.
import QtQuick 2.0
import Sailfish.Silica 1.0
import QtPositioning 5.0
import Nemo.Configuration 1.0

ApplicationWindow {
    allowedOrientations: defaultAllowedOrientations
    initialPage: Qt.resolvedUrl("pages/MainPage.qml")
    cover: Qt.resolvedUrl("cover/CoverPage.qml")

    // GPS on/off (persisted, toggled from the pages). When off, distances/map
    // centring are unavailable but the passive RID scan keeps running.
    ConfigurationValue {
        id: cfgGps
        key: "/apps/harbour-idrone/gpsEnabled"
        defaultValue: true
    }

    // Phone GPS → real distance to each drone (computed from its broadcast
    // position, not an RSSI guess). Passive: we only read our own location.
    PositionSource {
        id: gps
        active: cfgGps.value
        updateInterval: 2000
        onPositionChanged: {
            if (position.latitudeValid && position.longitudeValid)
                drone.setObserver(position.coordinate.latitude,
                                  position.coordinate.longitude)
        }
    }

    // Scan only while the app is in the foreground — passive and battery-friendly.
    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationActive)
                drone.start()
            else
                drone.stop()
        }
    }
    Component.onCompleted: drone.start()
}
