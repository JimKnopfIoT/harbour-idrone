// harbour-idrone — external-hardware auto-detection (capability negotiation, CONCEPT §8).
//
// Scans USB for known optional sensors (Ubertooth BLE sniffer, Alfa/monitor-mode
// Wi-Fi adapters) and reports which tiers are available, so the fusion backend can
// auto-enable the matching sensor when a device is plugged in and disable it when
// removed. Detection only — the heavy Tier-2/3 capture paths attach separately and
// must never be required for the Tier-0 build.
#ifndef HWDETECT_H
#define HWDETECT_H

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

class HwDetect : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ubertoothPresent READ ubertoothPresent NOTIFY changed)
    Q_PROPERTY(bool wifiMonitorPresent READ wifiMonitorPresent NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)  // [{name,kind,tier,vidpid}]
public:
    explicit HwDetect(QObject *parent = nullptr);

    bool ubertoothPresent() const { return m_ubertooth; }
    bool wifiMonitorPresent() const { return m_wifiMonitor; }
    QVariantList devices() const { return m_devices; }

    Q_INVOKABLE void rescan();

signals:
    void changed();
    // Fired on transitions so the backend can attach/detach a sensor.
    void ubertoothChanged(bool present);
    void wifiMonitorChanged(bool present);

private:
    QTimer m_timer;
    bool m_ubertooth = false;
    bool m_wifiMonitor = false;
    QVariantList m_devices;
};

#endif // HWDETECT_H
