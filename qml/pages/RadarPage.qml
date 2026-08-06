// harbour-idrone — radar/map of nearby drones.
// Each drone is placed by its REAL broadcast GPS position relative to the phone
// (RID gives true coordinates, not an RSSI guess) and drawn as a small quadcopter
// whose nose points along its broadcast travel direction. Optional OSM street
// background (reused from iBT). Map can be North-up or compass Heading-up.
import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0

Page {
    id: radar
    allowedOrientations: Orientation.All

    property real dmax: 500                          // metres plotted at zoom 1.0
    property real zoom: 1.0
    property real dEff: Math.max(20, dmax / zoom)
    property bool osmShow: false
    property bool headingUp: true

    // --- persisted settings (restored on open, saved on change) ---
    ConfigurationValue { id: cfgHeadingUp; key: "/apps/harbour-idrone/radar/headingUp"; defaultValue: true }
    ConfigurationValue { id: cfgOsmShow;   key: "/apps/harbour-idrone/radar/showMap";   defaultValue: true }
    ConfigurationValue { id: cfgZoom;      key: "/apps/harbour-idrone/radar/zoom";       defaultValue: 1.0 }
    ConfigurationValue { id: cfgGps;       key: "/apps/harbour-idrone/gpsEnabled";       defaultValue: true }
    ConfigurationValue { id: cfgShowCoords; key: "/apps/harbour-idrone/radar/showCoords"; defaultValue: true }
    Component.onCompleted: {
        headingUp = cfgHeadingUp.value
        osmShow = cfgOsmShow.value
        zoom = cfgZoom.value
    }
    onHeadingUpChanged: cfgHeadingUp.value = headingUp
    onOsmShowChanged: cfgOsmShow.value = osmShow
    // re-load the map when the zoom level settles, so zooming far out switches to
    // coarse (major-roads-only) data and zooming in adds detail — like catchercatcher.
    // Two fixed tiers (catchercatcher approach): streets stay present up to ~1 km
    // regardless of zoom. Refetch only when we first get a fix or move > 250 m —
    // NOT on zoom (zoom just rescales the already-loaded data).
    property real _fetchLat: 0
    property real _fetchLon: 0
    onHaveObsChanged: maybeFetch()

    function maybeFetch() {
        if (!haveObs || !osmShow) return
        if (_fetchLat !== 0 || _fetchLon !== 0) {
            var dN = (obsLat - _fetchLat) * 111320
            var dE = (obsLon - _fetchLon) * 111320 * Math.cos(obsLat * Math.PI / 180)
            if (Math.sqrt(dN * dN + dE * dE) < 250) return   // hasn't moved enough
        }
        fetchMap()
    }
    function fetchMap() {
        if (!haveObs) return
        _fetchLat = obsLat; _fetchLon = obsLon
        osm.fetch(obsLat, obsLon, 500, 2)         // near: streets + buildings
        osmWide.fetch(obsLat, obsLon, 1000, 1)    // wide: streets + parcels to 1 km
    }
    // rotation applied to the geographic frame to get the screen frame.
    // Container rotation -heading == catchercatcher's proven ENU-rotate-by-heading
    // convention (same SensorReader), verified against its MAP-DATA-ORIENTATION doc.
    property real effHeading: headingUp ? sensor.heading : 0

    readonly property bool haveObs: drone.observerValid
    readonly property real obsLat: drone.observerLat
    readonly property real obsLon: drone.observerLon

    function fmtDist(m) {
        if (m < 0) return ""
        return m < 1000 ? Math.round(m) + " m" : (m / 1000).toFixed(1) + " km"
    }

    onStatusChanged: {
        if (status === PageStatus.Active) {
            sensor.start()
            if (osmShow) maybeFetch()
        } else if (status === PageStatus.Inactive) {
            sensor.stop()
            cfgZoom.value = zoom          // persist last zoom
        }
    }

    SilicaFlickable {
        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: hwBar.top }
        contentHeight: height          // non-scrolling → steadier pulldown

        PullDownMenu {
            MenuItem {
                text: radar.headingUp ? qsTr("North up") : qsTr("Heading up (compass)")
                onClicked: radar.headingUp = !radar.headingUp
            }
            MenuItem {
                text: osmShow ? qsTr("Hide map") : qsTr("Show map")
                onClicked: {
                    osmShow = !osmShow
                    if (osmShow) fetchMap()
                }
            }
            MenuItem {
                text: cfgGps.value ? qsTr("GPS off") : qsTr("GPS on")
                onClicked: cfgGps.value = !cfgGps.value
            }
            MenuItem {
                text: cfgShowCoords.value ? qsTr("Hide coordinates") : qsTr("Show coordinates")
                onClicked: cfgShowCoords.value = !cfgShowCoords.value
            }
            MenuItem { text: qsTr("Clear"); onClicked: drone.clear() }
        }

        PageHeader {
            id: hdr
            title: qsTr("Radar")
            description: (headingUp ? qsTr("heading-up ") + Math.round(sensor.heading) + "°"
                                    : qsTr("north-up")) + "   ⌀ " + fmtDist(dEff)
        }

        // own coordinates at the top (like catchercatcher), toggleable
        Label {
            id: coords
            visible: cfgShowCoords.value
            anchors { top: hdr.bottom; horizontalCenter: parent.horizontalCenter }
            text: haveObs ? (obsLat.toFixed(5) + ", " + obsLon.toFixed(5))
                          : (cfgGps.value ? qsTr("Waiting for GPS…") : qsTr("GPS off"))
            font.pixelSize: Theme.fontSizeExtraSmall
            font.family: Theme.fontFamilyHeading
            color: haveObs ? Theme.highlightColor : Theme.secondaryColor
        }
        Item {
            id: scope
            anchors { top: coords.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
            property real cx: width / 2
            property real cy: height / 2
            property real radiusPx: Math.min(width, height) / 2 - Theme.paddingLarge
            property real ppm: radiusPx / dEff

            // north-up local projection (east → +x, north → −y); rotor applies heading
            function localX(lat, lon) {
                var coslat = Math.cos(obsLat * Math.PI / 180)
                return (lon - obsLon) * 111320 * coslat * ppm
            }
            function localY(lat, lon) {
                return -((lat - obsLat) * 111320) * ppm
            }

            // fixed range rings + crosshair (screen axes)
            Canvas {
                id: rings
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d"); ctx.reset()
                    var ccx = scope.cx, ccy = scope.cy, R = scope.radiusPx
                    ctx.strokeStyle = Theme.rgba(Theme.secondaryColor, 0.35); ctx.lineWidth = 1
                    for (var k = 1; k <= 3; ++k) {
                        ctx.beginPath(); ctx.arc(ccx, ccy, R * k / 3, 0, 2 * Math.PI); ctx.stroke()
                    }
                    ctx.beginPath()
                    ctx.moveTo(ccx - R, ccy); ctx.lineTo(ccx + R, ccy)
                    ctx.moveTo(ccx, ccy - R); ctx.lineTo(ccx, ccy + R)
                    ctx.stroke()
                }
                Connections { target: radar; onDEffChanged: rings.requestPaint() }
            }

            // rotated geographic frame: OSM + drone markers + north indicator
            Item {
                id: rotor
                anchors.fill: parent
                transformOrigin: Item.Center
                rotation: -radar.effHeading

                Canvas {
                    id: osmCanvas
                    anchors.fill: parent
                    onPaint: {
                        var ctx = getContext("2d"); ctx.reset()
                        if (!radar.osmShow || !haveObs) return
                        var near = osm.ways, wide = osmWide.ways, i, j, pts, x, y

                        function trace(w) {
                            pts = w.pts
                            ctx.beginPath()
                            for (j = 0; j < pts.length; ++j) {
                                x = scope.cx + scope.localX(pts[j].lat, pts[j].lon)
                                y = scope.cy + scope.localY(pts[j].lat, pts[j].lon)
                                if (j === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y)
                            }
                        }

                        // a real, named road vs a field track / footpath / service way
                        function isRoad(hw) {
                            switch (hw) {
                            case "motorway": case "trunk": case "primary": case "secondary":
                            case "tertiary": case "unclassified": case "residential":
                            case "living_street": case "road": case "pedestrian":
                            case "motorway_link": case "trunk_link": case "primary_link":
                            case "secondary_link": case "tertiary_link":
                                return true
                            default: return false   // track, service, path, footway, cycleway, steps…
                            }
                        }

                        // 1. faint layer: parcels + minor tracks/paths (dezent)
                        ctx.strokeStyle = Theme.rgba(Theme.secondaryColor, 0.25); ctx.lineWidth = 1
                        for (i = 0; i < wide.length; ++i) {
                            var w = wide[i]
                            if (w.kind === "building") continue
                            if (w.kind === "highway" && isRoad(w.hw)) continue  // real road → bright later
                            trace(w); ctx.stroke()
                        }
                        // 2. buildings from the near tier — light fill + faint outline
                        for (i = 0; i < near.length; ++i) {
                            if (near[i].kind !== "building") continue
                            trace(near[i]); ctx.closePath()
                            ctx.fillStyle = Theme.rgba(Theme.secondaryColor, 0.12); ctx.fill()
                            ctx.strokeStyle = Theme.rgba(Theme.secondaryColor, 0.3); ctx.lineWidth = 1
                            ctx.stroke()
                        }
                        // 3. real streets from the 1 km tier — bright, on top
                        ctx.strokeStyle = Theme.rgba(Theme.primaryColor, 0.4); ctx.lineWidth = 2.6
                        for (i = 0; i < wide.length; ++i) {
                            if (wide[i].kind !== "highway" || !isRoad(wide[i].hw)) continue
                            trace(wide[i]); ctx.stroke()
                        }
                    }
                    Connections { target: osm;     onChanged: osmCanvas.requestPaint() }
                    Connections { target: osmWide; onChanged: osmCanvas.requestPaint() }
                    Connections { target: radar; onOsmShowChanged: osmCanvas.requestPaint()
                                                 onDEffChanged: osmCanvas.requestPaint() }
                }

                // north indicator (lives in the geographic frame → always true north)
                Label {
                    text: "N"
                    color: Theme.highlightColor
                    font.pixelSize: Theme.fontSizeSmall; font.bold: true
                    x: scope.cx - width / 2
                    y: scope.cy - scope.radiusPx - height
                    rotation: radar.effHeading   // keep the glyph upright on screen
                }

                Repeater {
                    model: drone.contacts
                    delegate: Item {
                        id: mk
                        visible: haveObs && modelData.hasLocation === true
                                 && (modelData.lat !== 0 || modelData.lon !== 0)
                        property real lx: scope.localX(modelData.lat, modelData.lon)
                        property real ly: scope.localY(modelData.lat, modelData.lon)
                        property real rr: Math.sqrt(lx * lx + ly * ly)
                        property bool outside: rr > scope.radiusPx
                        property real k: outside ? scope.radiusPx / rr : 1.0
                        width: Theme.iconSizeSmall; height: width
                        x: scope.cx + lx * k - width / 2
                        y: scope.cy + ly * k - height / 2

                        // identified (has Remote ID serial) → orange, slightly
                        // transparent; unidentified drone → solid red.
                        property bool identified: modelData.serial !== undefined
                                                  && modelData.serial.length > 0
                        property color mkColor: identified ? "#ff9800" : "#ff3b30"

                        // alarm ping (expanding ring) behind the glyph
                        Rectangle {
                            visible: modelData.isAlarm === true
                            anchors.centerIn: parent
                            width: parent.width; height: width; radius: width / 2
                            color: "transparent"
                            border.color: mk.mkColor; border.width: 2
                            SequentialAnimation on scale {
                                running: modelData.isAlarm === true
                                loops: Animation.Infinite
                                NumberAnimation { from: 0.5; to: 2.0; duration: 1300
                                                  easing.type: Easing.OutQuad }
                            }
                            SequentialAnimation on opacity {
                                running: modelData.isAlarm === true
                                loops: Animation.Infinite
                                NumberAnimation { from: 0.7; to: 0.0; duration: 1300 }
                            }
                        }

                        // top-down quadcopter (X-frame), nose along travel direction
                        Item {
                            anchors.fill: parent
                            opacity: mk.identified ? 0.8 : 1.0
                            rotation: modelData.direction !== undefined ? modelData.direction : 0

                            // X arms
                            Rectangle {
                                width: parent.width * 0.9; height: 2.5; radius: 1.2
                                color: mk.mkColor; anchors.centerIn: parent; rotation: 45
                            }
                            Rectangle {
                                width: parent.width * 0.9; height: 2.5; radius: 1.2
                                color: mk.mkColor; anchors.centerIn: parent; rotation: -45
                            }
                            // 4 rotor discs
                            Repeater {
                                model: [[0.16,0.16],[0.84,0.16],[0.16,0.84],[0.84,0.84]]
                                delegate: Rectangle {
                                    width: parent.width * 0.3; height: width; radius: width / 2
                                    color: Theme.rgba(mk.mkColor, 0.35)
                                    border.color: mk.mkColor; border.width: 2
                                    x: parent.width * modelData[0] - width / 2
                                    y: parent.height * modelData[1] - height / 2
                                }
                            }
                            // body
                            Rectangle {
                                width: parent.width * 0.3; height: width; radius: 3
                                color: mk.mkColor; anchors.centerIn: parent
                            }
                            // nose (forward = up in this local frame)
                            Rectangle {
                                width: 3; height: parent.height * 0.32; radius: 1.5
                                color: mk.mkColor
                                x: parent.width / 2 - 1.5; y: -parent.height * 0.06
                            }
                        }

                        Label {
                            visible: !mk.outside
                            anchors { top: parent.bottom; horizontalCenter: parent.horizontalCenter }
                            rotation: radar.effHeading   // stay upright regardless of map spin
                            text: modelData.hasDistance ? fmtDist(modelData.distanceM) : ""
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.secondaryColor
                        }

                        // tap a drone → detail page
                        MouseArea {
                            anchors.centerIn: parent
                            width: Theme.itemSizeSmall; height: width
                            onClicked: pageStack.push(Qt.resolvedUrl("DronePage.qml"),
                                                      { contactKey: modelData.key })
                        }
                    }
                }
            }

            // range labels on each ring (screen-fixed; distance is rotation-invariant)
            Repeater {
                model: 3
                delegate: Label {
                    property real ringR: scope.radiusPx * (index + 1) / 3
                    text: fmtDist(dEff * (index + 1) / 3)
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.rgba(Theme.secondaryColor, 0.9)
                    x: scope.cx + ringR * 0.7071 - width / 2
                    y: scope.cy - ringR * 0.7071 - height / 2
                }
            }

            // own position (centre, rotation-invariant)
            Rectangle {
                width: Theme.paddingMedium; height: width; radius: width / 2
                color: Theme.highlightColor
                x: scope.cx - width / 2; y: scope.cy - height / 2
                visible: haveObs
            }

            ViewPlaceholder {
                enabled: !haveObs
                text: qsTr("Waiting for GPS")
                hintText: qsTr("The map needs your position to place drones by distance.")
            }

            // map loading indicator
            Row {
                anchors { horizontalCenter: parent.horizontalCenter
                          top: parent.top; topMargin: Theme.paddingLarge }
                spacing: Theme.paddingSmall
                visible: osmShow && (osm.busy || osmWide.busy)
                BusyIndicator { size: BusyIndicatorSize.Small; running: osm.busy || osmWide.busy }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Loading map…")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
            }

            PinchArea {
                anchors.fill: parent
                onPinchUpdated: {
                    var z = radar.zoom * (1 + (pinch.scale - pinch.previousScale))
                    radar.zoom = Math.max(0.2, Math.min(10, z))
                }
            }
        }
    }

    // Connected-hardware indicator — docked at the bottom (same as the list page).
    HwIndicator {
        id: hwBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
    }
}
