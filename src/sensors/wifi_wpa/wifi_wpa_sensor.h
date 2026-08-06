// harbour-idrone — Tier-0 Wi-Fi sensor (wpa_supplicant beacon Remote ID).
//
// Reads scan results from wpa_supplicant over the system D-Bus (as harbour-iwifi
// does): for each BSS it walks the beacon information elements, and
//   * emits the Remote ID Message Pack from the ASD-STAN vendor IE
//     (OUI FA:0B:BC, vendor-type 0x0D), and
//   * flags consumer Wi-Fi drones by SSID pattern / vendor OUI (heuristic).
// No monitor mode, no root — last-state, scan-interval driven (CONCEPT §10.3).
#ifndef WIFI_WPA_SENSOR_H
#define WIFI_WPA_SENSOR_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTimer>

class WifiWpaSensor : public QObject
{
    Q_OBJECT
public:
    explicit WifiWpaSensor(QObject *parent = nullptr);

    bool available() const { return m_available; }

    void start();
    void stop();

signals:
    // A full Remote ID state in one beacon (vendor-IE message pack).
    void ridMessagePack(const QString &bssid, const QByteArray &pack, int signalDbm);
    // Consumer Wi-Fi drone recognised only by SSID/OUI (no Remote ID).
    void heuristicDrone(const QString &bssid, const QString &ssid,
                        const QString &vendor, int signalDbm);
    void statusChanged();

private slots:
    void refresh();

private:
    QTimer m_timer;
    QString m_ifPath;
    bool m_available = false;

    QString interfacePath();
    void readBss(const QString &path);
    void scanIes(const QString &bssid, const QString &ssid,
                 const QByteArray &ies, int signalDbm);
};

#endif // WIFI_WPA_SENSOR_H
