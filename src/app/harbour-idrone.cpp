// harbour-idrone — Silica UI entrypoint (M1: Tier-0 Bluetooth).
//
// Instantiates the fusion backend (BlueZ RID sensor + contact model + optional-
// hardware auto-detection) and exposes it to QML as `drone`. The fusion daemon /
// D-Bus split (CONCEPT §9) is a later refactor; for now the backend lives in-app.
//
// Invariant: the Tier-0 core builds and runs without any optional (root / SDR /
// sniffer) dependency.
#ifdef QT_QML_DEBUG
#include <QtQuick>
#endif

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>
#include <QScopedPointer>

#include <sailfishapp.h>

#include "dronebackend.h"
#include "osmfetch.h"
#include "sensorreader.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QScopedPointer<QQuickView> view(SailfishApp::createView());

    QScopedPointer<DroneBackend> drone(new DroneBackend(view.data()));
    view->rootContext()->setContextProperty("drone", drone.data());

    // OSM map background for the radar page. Two tiers (like catchercatcher) so
    // the street network is always present up to ~1 km regardless of zoom:
    //   osm      = near 500 m  (streets + buildings)
    //   osmWide  = 1 km        (streets + parcels, no buildings)
    QScopedPointer<OsmFetch> osm(new OsmFetch(view.data()));
    view->rootContext()->setContextProperty("osm", osm.data());
    QScopedPointer<OsmFetch> osmWide(new OsmFetch(view.data()));
    view->rootContext()->setContextProperty("osmWide", osmWide.data());

    // Compass/orientation for heading-up map alignment, reused from iBT.
    QScopedPointer<SensorReader> sensor(new SensorReader(view.data()));
    view->rootContext()->setContextProperty("sensor", sensor.data());

    view->setSource(SailfishApp::pathTo("qml/harbour-idrone.qml"));
    view->show();

    return app->exec();
}
