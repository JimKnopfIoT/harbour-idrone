// harbour-idrone — Tier-0 BLE sensor (BlueZ / Bluetooth Legacy Remote ID).
//
// Talks to BlueZ over the system D-Bus directly (as harbour-iBT does — the SDK
// target lacks Qt5Bluetooth). Scans LE with DuplicateData on and a 0xFFFA UUID
// filter, polls ObjectManager.GetManagedObjects, and emits every device's raw
// Remote ID service-data payload. Decoding/aggregation lives in the fusion
// backend; this class only produces observations. No root, no accessory.
#ifndef BLE_BLUEZ_SENSOR_H
#define BLE_BLUEZ_SENSOR_H

#include <QObject>
#include <QByteArray>
#include <QDBusConnection>
#include <QString>
#include <QTimer>
#include <QVariantMap>

class BleBluezSensor : public QObject
{
    Q_OBJECT
public:
    explicit BleBluezSensor(QObject *parent = nullptr);

    bool available() const { return !m_adapterPath.isEmpty(); }
    bool powered() const { return m_powered; }
    bool scanning() const { return m_scanning; }

    void start();   // power on + StartDiscovery (LE, 0xFFFA filter) + poll timer
    void stop();    // StopDiscovery + stop timer

signals:
    // One Bluetooth Legacy RID advert: raw 0xFFFA service-data bytes.
    void ridObservation(const QString &mac, const QByteArray &payload, int rssi);
    void statusChanged();

private slots:
    void poll();

private:
    QDBusConnection m_bus;
    QTimer m_timer;
    QString m_adapterPath;
    bool m_powered = false;
    bool m_scanning = false;

    void refresh();
    void findAdapter(const QVariantMap &objects);
    void callAdapter(const QString &method, const QVariantList &args = QVariantList());
    void setAdapterPowered(bool on);
    void setDiscoveryFilter();
};

#endif // BLE_BLUEZ_SENSOR_H
