#include "hwdetect.h"

#include <QDir>
#include <QFile>

namespace {

struct KnownDevice {
    const char *vid;
    const char *pid;   // nullptr = match any product for this vendor entry
    const char *name;
    const char *kind;  // "ubertooth" | "wifi-monitor"
    int tier;
};

// Extend freely. Wi-Fi entries are monitor-mode-capable Alfa-class chipsets.
const KnownDevice kKnown[] = {
    // CatSniffer v3 (RP2040 USB bridge + CC1352). Presents the generic RP2040
    // VID:PID (2e8a:00c0) — heuristic match; good enough for the connected-device
    // indicator. Never persist its USB serial (anonymity).
    {"2e8a", "00c0", "CatSniffer",               "catsniffer",   1},
    {"1d50", "6002", "Ubertooth One",            "ubertooth",    3},
    {"1d50", "6000", "Ubertooth One (bootloader)","ubertooth",   3},
    {"0bda", "8812", "RTL8812AU (Alfa AC)",       "wifi-monitor", 2},
    {"0bda", "8813", "RTL8814AU (Alfa AC1900)",   "wifi-monitor", 2},
    {"0cf3", "9271", "AR9271 (Alfa AWUS036NHA)",  "wifi-monitor", 2},
    {"0e8d", "7612", "MT7612U (Alfa AWUS036ACM)", "wifi-monitor", 2},
    {"0e8d", "7610", "MT7610 (Alfa)",             "wifi-monitor", 2},
};

QString readId(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromLatin1(f.readAll()).trimmed().toLower();
}

} // namespace

HwDetect::HwDetect(QObject *parent) : QObject(parent)
{
    m_timer.setInterval(5000);   // catch hotplug without udev
    connect(&m_timer, &QTimer::timeout, this, &HwDetect::rescan);
    m_timer.start();
    rescan();
}

void HwDetect::rescan()
{
    bool uber = false, wifi = false;
    QVariantList found;

    QDir usb(QStringLiteral("/sys/bus/usb/devices"));
    const QStringList entries = usb.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &e : entries) {
        const QString base = usb.absoluteFilePath(e);
        const QString vid = readId(base + QStringLiteral("/idVendor"));
        if (vid.isEmpty())
            continue;
        const QString pid = readId(base + QStringLiteral("/idProduct"));
        for (const KnownDevice &k : kKnown) {
            if (vid != QLatin1String(k.vid))
                continue;
            if (k.pid && pid != QLatin1String(k.pid))
                continue;
            QVariantMap d;
            d.insert("name", QLatin1String(k.name));
            d.insert("kind", QLatin1String(k.kind));
            d.insert("tier", k.tier);
            d.insert("vidpid", vid + ":" + pid);
            found << d;
            if (QLatin1String(k.kind) == QLatin1String("ubertooth")) uber = true;
            if (QLatin1String(k.kind) == QLatin1String("wifi-monitor")) wifi = true;
            break;
        }
    }

    const bool uberWas = m_ubertooth, wifiWas = m_wifiMonitor;
    m_devices = found;
    m_ubertooth = uber;
    m_wifiMonitor = wifi;

    if (uber != uberWas)
        emit ubertoothChanged(uber);
    if (wifi != wifiWas)
        emit wifiMonitorChanged(wifi);
    emit changed();   // always refresh the device list in the UI
}
