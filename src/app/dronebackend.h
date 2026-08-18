// harbour-idrone — fusion backend (Tier 0: Bluetooth + Wi-Fi).
//
// Owns the sensors, aggregates Remote ID observations into a contact model keyed
// on RID serial (not MAC — MACs rotate; CONCEPT §9), computes real distance from
// the phone's GPS, raises proximity alarms, and exposes contacts to the Silica UI.
// A demo mode synthesises contacts so the UI works with no drone in the air.
// Optional sensors (Ubertooth, Alfa monitor mode) auto-attach via HwDetect and are
// strictly additive to the Tier-0 core.
#ifndef DRONEBACKEND_H
#define DRONEBACKEND_H

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "ble_bluez_sensor.h"
#include "wifi_wpa_sensor.h"
#include "hwdetect.h"
#include "rid_parser.h"

class DroneBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available   READ available   NOTIFY statusChanged)
    Q_PROPERTY(bool powered     READ powered     NOTIFY statusChanged)
    Q_PROPERTY(bool scanning    READ scanning    NOTIFY statusChanged)
    Q_PROPERTY(int  count       READ count       NOTIFY updated)
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY updated)
    Q_PROPERTY(bool demo        READ demo        NOTIFY demoChanged)
    Q_PROPERTY(bool alarmActive READ alarmActive NOTIFY updated)
    Q_PROPERTY(QString alarmText READ alarmText  NOTIFY updated)
    Q_PROPERTY(double observerLat READ observerLat NOTIFY observerChanged)
    Q_PROPERTY(double observerLon READ observerLon NOTIFY observerChanged)
    Q_PROPERTY(bool observerValid READ observerValid NOTIFY observerChanged)
    Q_PROPERTY(QObject *hw      READ hw          CONSTANT)
    // True while the phone's internal BlueZ chip is actively scanning (BT on).
    // Shown in the UI alongside external sniffers (they run in parallel).
    Q_PROPERTY(bool internalBleActive READ internalBleActive NOTIFY statusChanged)
    // The app's own version, shown on the About page. Injected from the .pro
    // (VERSION), so it always matches the package it was built into.
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)
    // Whether the external-CatSniffer helper unit is currently running. The
    // switch on the settings page drives it; nothing runs at boot.
    Q_PROPERTY(bool catSnifferEnabled READ catSnifferEnabled NOTIFY catSnifferChanged)
public:
    explicit DroneBackend(QObject *parent = nullptr);

    bool internalBleActive() const { return m_ble.scanning(); }
    QString appVersion() const { return QStringLiteral(APP_VERSION); }
    bool catSnifferEnabled() const { return m_catSnifferOn; }

    bool available() const { return m_ble.available() || m_wifi.available() || hasExternalSniffer(); }
    bool powered() const { return m_ble.powered() || hasExternalSniffer(); }
    bool scanning() const { return m_scanning; }
    int count() const { return m_contacts.size(); }
    QVariantList contacts() const { return m_contacts; }
    bool demo() const { return m_demo; }
    bool alarmActive() const { return !m_alarmText.isEmpty(); }
    QString alarmText() const { return m_alarmText; }
    double observerLat() const { return m_obsLat; }
    double observerLon() const { return m_obsLon; }
    bool observerValid() const { return m_haveObserver; }
    QObject *hw() { return &m_hw; }

    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setObserver(double lat, double lon);  // phone GPS
    Q_INVOKABLE void setDemo(bool on);
    // Alarm when a drone is within `meters`; if requireApproach, only while nearing.
    Q_INVOKABLE void setAlarm(double meters, bool requireApproach);
    // Turn the external-CatSniffer helper on or off. On starts the root helper
    // unit over the system bus (polkit lets defaultuser control only this one
    // unit); off stops it. Persisted by the caller; re-applied on launch.
    Q_INVOKABLE void setCatSniffer(bool on);

signals:
    void statusChanged();
    void catSnifferChanged();
    void updated();
    void demoChanged();
    void observerChanged();
    void alarm(const QString &key, double distanceM);

private slots:
    void onObservation(const QString &mac, const QByteArray &payload, int rssi);
    void onWifiPack(const QString &bssid, const QByteArray &pack, int signalDbm);
    // External CatSniffer (via the least-privilege helper on D-Bus): a BT5 pack.
    void onCatSniffer(const QString &key, const QByteArray &pack, int rssi);
    void onHeuristic(const QString &bssid, const QString &ssid,
                     const QString &vendor, int signalDbm);
    void onSensorStatus();
    void demoTick();
    void rebuild();

private:
    BleBluezSensor m_ble;
    WifiWpaSensor m_wifi;
    HwDetect m_hw;
    bool m_catSnifferOn = false;

    struct Accum {
        ODID_UAS_Data uas;
        qint64 first = 0;
        qint64 last = 0;
        int rssi = 0;
        bool haveRssi = false;
        QString source;         // "BT" | "WiFi"
    };
    QHash<QString, Accum> m_accum;   // keyed on sender MAC/BSSID (pre-fusion)

    struct Heur { QString ssid; QString vendor; int signalDbm = 0; qint64 last = 0; };
    QHash<QString, Heur> m_heur;     // keyed on BSSID (SSID/OUI heuristic)

    QVariantList m_contacts;
    QHash<QString, double> m_prevDist;   // per-key last distance (approach detection)

    QTimer m_rebuild;
    bool m_scanning = false;
    bool m_bleActive = false;    // is the internal BlueZ scan currently running?

    bool m_haveObserver = false;
    double m_obsLat = 0.0;
    double m_obsLon = 0.0;

    // demo mode
    bool m_demo = false;
    bool m_demoObs = false;      // observer synthesised for demo (no real GPS)
    QTimer m_demoTimer;
    int m_demoPhase = 0;

    // alarm config
    double m_alarmMeters = 150.0;
    bool m_alarmApproach = true;
    QString m_alarmText;

    // Source arbitration: when an external BT sniffer (CatSniffer/Ubertooth) is
    // connected, use it and switch the internal BlueZ chip off; otherwise use the
    // internal chip. Called on start and whenever HwDetect reports a change.
    void applyArbitration();
    bool hasExternalSniffer() const;

    void scheduleRebuild();
    void feedAccum(const QString &key, const QString &source,
                   bool isPack, const QByteArray &bytes, int rssi);
    static double haversineMeters(double lat1, double lon1, double lat2, double lon2);
};

#endif // DRONEBACKEND_H
