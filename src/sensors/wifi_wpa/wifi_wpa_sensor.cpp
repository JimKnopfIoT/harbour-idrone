#include "wifi_wpa_sensor.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QVariantMap>

static const char *SERVICE = "fi.w1.wpa_supplicant1";

// --- D-Bus helpers (same shape as harbour-iwifi) ---------------------------
static QVariant getProp(const QString &path, const QString &iface, const QString &name)
{
    QDBusInterface props(SERVICE, path, "org.freedesktop.DBus.Properties",
                         QDBusConnection::systemBus());
    QDBusReply<QDBusVariant> r = props.call("Get", iface, name);
    return r.isValid() ? r.value().variant() : QVariant();
}

static QByteArray toBytes(const QVariant &v)
{
    if (v.type() == QVariant::ByteArray)
        return v.toByteArray();
    if (v.canConvert<QDBusArgument>())
        return qdbus_cast<QByteArray>(v.value<QDBusArgument>());
    return v.toByteArray();
}

static QString macFromBytes(const QByteArray &b)
{
    QStringList parts;
    for (int i = 0; i < b.size(); ++i)
        parts << QString("%1").arg((quint8)b.at(i), 2, 16, QChar('0')).toUpper();
    return parts.join(':');
}

// --- consumer-drone heuristics ---------------------------------------------
// SSID prefixes seen on toy/consumer Wi-Fi drones (case-insensitive).
static QString ssidVendor(const QString &ssid)
{
    struct P { const char *prefix; const char *vendor; };
    static const P pats[] = {
        {"TELLO-", "DJI/Ryze Tello"}, {"RMTT-", "DJI/Ryze Tello"},
        {"Mavic", "DJI"}, {"MAVIC", "DJI"}, {"DJI-", "DJI"}, {"DJI_", "DJI"},
        {"FIMI-", "FIMI"}, {"Hubsan", "Hubsan"}, {"Bebop", "Parrot"},
        {"ardrone", "Parrot"}, {"ANAFI-", "Parrot"}, {"SPARK-", "DJI"},
    };
    for (const P &p : pats)
        if (ssid.startsWith(QLatin1String(p.prefix), Qt::CaseInsensitive))
            return QLatin1String(p.vendor);
    return QString();
}

// A few drone-vendor OUIs (first 3 MAC bytes, upper hex "AABBCC").
static QString ouiVendor(const QString &bssid)
{
    const QString oui = bssid.left(8).remove(':').toUpper();  // "AABBCC"
    static const QHash<QString, QString> m = {
        {"60601F", "DJI"}, {"34D262", "DJI"}, {"482CA0", "DJI"}, {"D8FC38", "DJI"},
        {"90031F", "Parrot"}, {"00121C", "Parrot"}, {"A0143D", "Parrot"},
        {"90:03:B7", "Parrot"},
    };
    return m.value(oui);
}

WifiWpaSensor::WifiWpaSensor(QObject *parent) : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &WifiWpaSensor::refresh);
    m_timer.setInterval(4000);   // scan-interval driven; last state only
}

void WifiWpaSensor::start()
{
    refresh();
    if (!m_timer.isActive())
        m_timer.start();
}

void WifiWpaSensor::stop()
{
    m_timer.stop();
}

QString WifiWpaSensor::interfacePath()
{
    if (!m_ifPath.isEmpty())
        return m_ifPath;
    QVariant v = getProp("/fi/w1/wpa_supplicant1", "fi.w1.wpa_supplicant1", "Interfaces");
    QList<QDBusObjectPath> ifs;
    if (v.canConvert<QDBusArgument>())
        ifs = qdbus_cast<QList<QDBusObjectPath> >(v.value<QDBusArgument>());
    for (const QDBusObjectPath &p : ifs) {
        const QString name = getProp(p.path(),
                                     "fi.w1.wpa_supplicant1.Interface", "Ifname").toString();
        if (name == QStringLiteral("wlan0")) {
            m_ifPath = p.path();
            return m_ifPath;
        }
    }
    return ifs.isEmpty() ? QString() : ifs.first().path();
}

void WifiWpaSensor::scanIes(const QString &bssid, const QString &ssid,
                            const QByteArray &ies, int signalDbm)
{
    const unsigned char *d = (const unsigned char *)ies.constData();
    const int n = ies.size();
    int i = 0;
    while (i + 2 <= n) {
        const int type = d[i];
        const int len = d[i + 1];
        if (i + 2 + len > n)
            break;
        const unsigned char *v = d + i + 2;
        // Vendor-Specific IE (0xDD) with ASD-STAN OUI FA:0B:BC, vendor-type 0x0D.
        if (type == 221 && len >= 5 &&
            v[0] == 0xFA && v[1] == 0x0B && v[2] == 0xBC && v[3] == 0x0D) {
            // v[4] = message counter; the message pack follows.
            const QByteArray pack((const char *)(v + 5), len - 5);
            emit ridMessagePack(bssid, pack, signalDbm);
        }
        i += 2 + len;
    }

    // Heuristic: consumer drone AP by SSID / vendor OUI.
    QString vendor = ssidVendor(ssid);
    if (vendor.isEmpty())
        vendor = ouiVendor(bssid);
    if (!vendor.isEmpty())
        emit heuristicDrone(bssid, ssid, vendor, signalDbm);
}

void WifiWpaSensor::readBss(const QString &path)
{
    QDBusInterface props(SERVICE, path, "org.freedesktop.DBus.Properties",
                         QDBusConnection::systemBus());
    QDBusReply<QVariantMap> reply = props.call("GetAll", "fi.w1.wpa_supplicant1.BSS");
    if (!reply.isValid())
        return;
    const QVariantMap m = reply.value();
    if (m.isEmpty())
        return;

    const QString bssid = macFromBytes(toBytes(m.value("BSSID")));
    const QString ssid = QString::fromUtf8(toBytes(m.value("SSID")));
    const int signalDbm = m.value("Signal").toInt();
    scanIes(bssid, ssid, toBytes(m.value("IEs")), signalDbm);
}

void WifiWpaSensor::refresh()
{
    const QString ifp = interfacePath();
    const bool was = m_available;
    m_available = !ifp.isEmpty();
    if (m_available != was)
        emit statusChanged();
    if (ifp.isEmpty())
        return;

    QVariant v = getProp(ifp, "fi.w1.wpa_supplicant1.Interface", "BSSs");
    QList<QDBusObjectPath> bsss;
    if (v.canConvert<QDBusArgument>())
        bsss = qdbus_cast<QList<QDBusObjectPath> >(v.value<QDBusArgument>());
    for (const QDBusObjectPath &p : bsss)
        readBss(p.path());
}
