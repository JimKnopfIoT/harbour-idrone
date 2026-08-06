#include "ble_bluez_sensor.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusSignature>
#include <QDBusVariant>
#include <QVariantList>

// ASTM Remote ID service UUID (16-bit 0xFFFA in full 128-bit form).
static const char *RID_UUID = "0000fffa-0000-1000-8000-00805f9b34fb";

// --- D-Bus demarshalling (same approach as harbour-iBT) --------------------
static QVariant demarshall(const QDBusArgument &arg);

static QVariant unwrap(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>())
        return demarshall(v.value<QDBusArgument>());
    if (v.canConvert<QDBusObjectPath>())
        return v.value<QDBusObjectPath>().path();
    if (v.canConvert<QDBusSignature>())
        return v.value<QDBusSignature>().signature();
    return v;
}

static QVariant demarshall(const QDBusArgument &arg)
{
    switch (arg.currentType()) {
    case QDBusArgument::BasicType:
        return unwrap(arg.asVariant());
    case QDBusArgument::VariantType: {
        QDBusVariant dv;
        arg >> dv;
        return unwrap(dv.variant());
    }
    case QDBusArgument::ArrayType: {
        if (arg.currentSignature() == QLatin1String("ay")) {
            QByteArray ba;
            arg >> ba;
            return ba;
        }
        QVariantList list;
        arg.beginArray();
        while (!arg.atEnd())
            list << demarshall(arg);
        arg.endArray();
        return list;
    }
    case QDBusArgument::MapType: {
        QVariantMap map;
        arg.beginMap();
        while (!arg.atEnd()) {
            arg.beginMapEntry();
            const QVariant key = demarshall(arg);
            const QVariant val = demarshall(arg);
            arg.endMapEntry();
            map.insert(key.toString(), val);
        }
        arg.endMap();
        return map;
    }
    case QDBusArgument::StructureType: {
        QVariantList list;
        arg.beginStructure();
        while (!arg.atEnd())
            list << demarshall(arg);
        arg.endStructure();
        return list;
    }
    default:
        return QVariant();
    }
}

// ---------------------------------------------------------------------------
BleBluezSensor::BleBluezSensor(QObject *parent)
    : QObject(parent), m_bus(QDBusConnection::systemBus())
{
    m_timer.setInterval(2500);   // poll cadence; DuplicateData refreshes RSSI/RID
    connect(&m_timer, &QTimer::timeout, this, &BleBluezSensor::poll);
    refresh();                   // learn adapter presence up front
}

void BleBluezSensor::callAdapter(const QString &method, const QVariantList &args)
{
    if (m_adapterPath.isEmpty())
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), m_adapterPath,
        QStringLiteral("org.bluez.Adapter1"), method);
    if (!args.isEmpty())
        m.setArguments(args);
    m_bus.call(m, QDBus::NoBlock);
}

void BleBluezSensor::setAdapterPowered(bool on)
{
    if (m_adapterPath.isEmpty())
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), m_adapterPath,
        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    m << QStringLiteral("org.bluez.Adapter1") << QStringLiteral("Powered")
      << QVariant::fromValue(QDBusVariant(on));
    m_bus.call(m, QDBus::NoBlock);
}

void BleBluezSensor::setDiscoveryFilter()
{
    if (m_adapterPath.isEmpty())
        return;
    QVariantMap filter;
    filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
    filter.insert(QStringLiteral("DuplicateData"), true);   // every re-advert
    filter.insert(QStringLiteral("UUIDs"),
                  QStringList() << QLatin1String(RID_UUID)); // scope to RID
    QDBusMessage m = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), m_adapterPath,
        QStringLiteral("org.bluez.Adapter1"), QStringLiteral("SetDiscoveryFilter"));
    m << filter;
    m_bus.call(m, QDBus::NoBlock);
}

void BleBluezSensor::start()
{
    refresh();
    if (m_adapterPath.isEmpty())
        return;
    if (!m_powered)
        setAdapterPowered(true);
    setDiscoveryFilter();
    callAdapter(QStringLiteral("StartDiscovery"));
    if (!m_scanning) {
        m_scanning = true;
        emit statusChanged();
    }
    if (!m_timer.isActive())
        m_timer.start();
    poll();
}

void BleBluezSensor::stop()
{
    if (m_timer.isActive())
        m_timer.stop();
    if (!m_adapterPath.isEmpty())
        callAdapter(QStringLiteral("StopDiscovery"));
    if (m_scanning) {
        m_scanning = false;
        emit statusChanged();
    }
}

void BleBluezSensor::poll()
{
    refresh();
}

void BleBluezSensor::findAdapter(const QVariantMap &objects)
{
    const QString oldPath = m_adapterPath;
    const bool oldPowered = m_powered;
    QString found;
    bool poweredNow = false;
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QVariantMap ifaces = it.value().toMap();
        if (ifaces.contains(QStringLiteral("org.bluez.Adapter1"))) {
            found = it.key();
            const QVariantMap a = ifaces.value(QStringLiteral("org.bluez.Adapter1")).toMap();
            poweredNow = a.value(QStringLiteral("Powered")).toBool();
            break;
        }
    }
    m_powered = poweredNow;
    if (found != oldPath || m_powered != oldPowered) {
        m_adapterPath = found;
        emit statusChanged();
    }
}

void BleBluezSensor::refresh()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.bluez"), QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"),
        QStringLiteral("GetManagedObjects"));
    QDBusMessage reply = m_bus.call(call);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return;

    const QDBusArgument arg = reply.arguments().at(0).value<QDBusArgument>();
    const QVariantMap objects = demarshall(arg).toMap();

    findAdapter(objects);
    if (!m_powered)
        return;

    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QVariantMap ifaces = it.value().toMap();
        if (!ifaces.contains(QStringLiteral("org.bluez.Device1")))
            continue;
        const QVariantMap p = ifaces.value(QStringLiteral("org.bluez.Device1")).toMap();

        const QVariant svcVar = p.value(QStringLiteral("ServiceData"));
        if (!svcVar.canConvert<QVariantMap>())
            continue;
        const QVariantMap sm = svcVar.toMap();

        QByteArray payload;
        for (auto s = sm.constBegin(); s != sm.constEnd(); ++s) {
            // key is the full 128-bit UUID; match the 16-bit 0xFFFA slice.
            if (s.key().mid(4, 4).compare(QLatin1String("fffa"), Qt::CaseInsensitive) == 0) {
                payload = s.value().toByteArray();
                break;
            }
        }
        if (payload.isEmpty())
            continue;

        const QString mac = p.value(QStringLiteral("Address")).toString();
        const int rssi = p.value(QStringLiteral("RSSI")).toInt();
        emit ridObservation(mac, payload, rssi);
    }
}
