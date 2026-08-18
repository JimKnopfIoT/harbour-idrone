#include "dronebackend.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QtGlobal>
#include <QtMath>
#include <QVariantMap>
#include <algorithm>
#include <cstring>

static const qint64 kContactTtlMs = 20000;   // drop a drone unseen for 20 s
static const qint64 kHeurTtlMs = 12000;

DroneBackend::DroneBackend(QObject *parent) : QObject(parent)
{
    connect(&m_ble, &BleBluezSensor::ridObservation, this, &DroneBackend::onObservation);
    connect(&m_ble, &BleBluezSensor::statusChanged, this, &DroneBackend::onSensorStatus);
    connect(&m_wifi, &WifiWpaSensor::ridMessagePack, this, &DroneBackend::onWifiPack);
    connect(&m_wifi, &WifiWpaSensor::heuristicDrone, this, &DroneBackend::onHeuristic);
    connect(&m_wifi, &WifiWpaSensor::statusChanged, this, &DroneBackend::onSensorStatus);

    // Auto-enable optional sensors when their hardware appears (Ubertooth/Alfa
    // capture attach here in M3/M4). Tier 0 never depends on them.
    connect(&m_hw, &HwDetect::ubertoothChanged, this, [](bool present) {
        qInfo("harbour-idrone: Ubertooth %s", present ? "attached" : "removed");
    });
    connect(&m_hw, &HwDetect::wifiMonitorChanged, this, [](bool present) {
        qInfo("harbour-idrone: Wi-Fi monitor adapter %s", present ? "attached" : "removed");
    });

    // External CatSniffer: its root system-service helper decodes BLE-5 Long Range
    // Remote ID and hands each pack to us over the SYSTEM bus (see the shipped
    // D-Bus policy + CatSniffer sailjail permission).
    QDBusConnection::systemBus().connect(
        QStringLiteral("org.idrone.CatSniffer"),
        QStringLiteral("/org/idrone/CatSniffer"),
        QStringLiteral("org.idrone.CatSniffer"),
        QStringLiteral("remoteId"),
        this, SLOT(onCatSniffer(QString, QByteArray, int)));

    // Re-arbitrate internal-vs-external whenever the connected hardware changes.
    connect(&m_hw, &HwDetect::changed, this, &DroneBackend::applyArbitration);

    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(250);
    connect(&m_rebuild, &QTimer::timeout, this, &DroneBackend::rebuild);

    m_demoTimer.setInterval(500);
    connect(&m_demoTimer, &QTimer::timeout, this, &DroneBackend::demoTick);
}

void DroneBackend::onSensorStatus() { emit statusChanged(); }

void DroneBackend::start()
{
    if (m_demo)
        return;                 // demo drives its own feed
    m_wifi.start();
    m_scanning = true;
    applyArbitration();         // starts the internal BLE only if no external sniffer
    emit statusChanged();
}

void DroneBackend::stop()
{
    m_ble.stop();
    m_bleActive = false;
    m_wifi.stop();
    if (m_scanning) { m_scanning = false; emit statusChanged(); }
}

void DroneBackend::clear()
{
    m_accum.clear();
    m_heur.clear();
    m_prevDist.clear();
    m_contacts.clear();
    m_alarmText.clear();
    emit updated();
}

void DroneBackend::setObserver(double lat, double lon)
{
    m_obsLat = lat;
    m_obsLon = lon;
    m_haveObserver = (lat != 0.0 || lon != 0.0);
    if (m_haveObserver)
        m_demoObs = false;   // a real GPS fix supersedes the demo observer
    emit observerChanged();
    scheduleRebuild();
}

void DroneBackend::setAlarm(double meters, bool requireApproach)
{
    m_alarmMeters = meters;
    m_alarmApproach = requireApproach;
    scheduleRebuild();
}

void DroneBackend::setCatSniffer(bool on)
{
    if (on == m_catSnifferOn)
        return;

    // StartUnit / StopUnit on systemd's manager, over the SYSTEM bus. A polkit
    // rule (data/50-harbour-idrone-catsniffer.rules) lets defaultuser control
    // exactly this one unit without a password; no other unit, and nothing is
    // enabled at boot. "replace" is systemd's ordinary job mode.
    const QString method = on ? QStringLiteral("StartUnit") : QStringLiteral("StopUnit");
    QDBusMessage call = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.systemd1"),
        QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"),
        method);
    call << QStringLiteral("harbour-idrone-catsniffer.service")
         << QStringLiteral("replace");
    const QDBusMessage reply = QDBusConnection::systemBus().call(call);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        qWarning("harbour-idrone: CatSniffer %s failed: %s",
                 on ? "start" : "stop", qPrintable(reply.errorMessage()));
        // Leave the flag as it was so the UI reflects reality, and let the
        // caller see the unchanged state.
        emit catSnifferChanged();
        return;
    }
    m_catSnifferOn = on;
    qInfo("harbour-idrone: CatSniffer helper %s", on ? "started" : "stopped");
    emit catSnifferChanged();
    emit statusChanged();
}

void DroneBackend::scheduleRebuild()
{
    if (!m_rebuild.isActive())
        m_rebuild.start();
}

void DroneBackend::feedAccum(const QString &key, const QString &source,
                             bool isPack, const QByteArray &bytes, int rssi)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Accum &a = m_accum[key];
    if (a.first == 0) {
        std::memset(&a.uas, 0, sizeof(a.uas));
        a.first = now;
    }
    const bool ok = isPack ? rid::feedMessagePack(bytes, a.uas)
                           : rid::feedBtServiceData(bytes, a.uas);
    if (ok) {
        a.last = now;
        a.source = source;
        if (rssi != 0) { a.rssi = rssi; a.haveRssi = true; }
        scheduleRebuild();
    }
}

void DroneBackend::onObservation(const QString &mac, const QByteArray &payload, int rssi)
{
    feedAccum(mac, QStringLiteral("BT"), false, payload, rssi);
}

void DroneBackend::onWifiPack(const QString &bssid, const QByteArray &pack, int signalDbm)
{
    feedAccum(QStringLiteral("wifi:") + bssid, QStringLiteral("WiFi"), true, pack, signalDbm);
}

void DroneBackend::onCatSniffer(const QString &key, const QByteArray &pack, int rssi)
{
    // A BLE-5 Long Range Remote-ID message pack forwarded by the CatSniffer helper.
    feedAccum(QStringLiteral("cat:") + key, QStringLiteral("BT(CatSniffer)"), true, pack, rssi);
}

bool DroneBackend::hasExternalSniffer() const
{
    const QVariantList devs = m_hw.devices();
    for (const QVariant &v : devs) {
        const QString kind = v.toMap().value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("catsniffer") || kind == QLatin1String("ubertooth"))
            return true;
    }
    return false;
}

void DroneBackend::applyArbitration()
{
    if (m_demo)
        return;
    // Internal BlueZ (Bluetooth 4 / 1M legacy RID) and the external CatSniffer
    // (Bluetooth 5 Long Range / Coded PHY) run in PARALLEL — they cover the two
    // different Bluetooth RID variants and complement each other. rebuild() de-dups
    // contacts by RID serial, so a drone broadcasting BOTH still appears only once.
    const bool wantBle = m_scanning;
    if (wantBle != m_bleActive) {
        m_bleActive = wantBle;
        if (wantBle)
            m_ble.start();
        else
            m_ble.stop();
        qInfo("harbour-idrone: internal BLE %s (external sniffer %s, running in parallel)",
              wantBle ? "on" : "off", hasExternalSniffer() ? "present" : "absent");
    }
    emit statusChanged();
}

void DroneBackend::onHeuristic(const QString &bssid, const QString &ssid,
                               const QString &vendor, int signalDbm)
{
    Heur &h = m_heur[QStringLiteral("wifi:") + bssid];
    h.ssid = ssid;
    h.vendor = vendor;
    h.signalDbm = signalDbm;
    h.last = QDateTime::currentMSecsSinceEpoch();
    scheduleRebuild();
}

double DroneBackend::haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    const double R = 6371000.0;
    const double dLat = qDegreesToRadians(lat2 - lat1);
    const double dLon = qDegreesToRadians(lon2 - lon1);
    const double a = qSin(dLat / 2) * qSin(dLat / 2)
                   + qCos(qDegreesToRadians(lat1)) * qCos(qDegreesToRadians(lat2))
                   * qSin(dLon / 2) * qSin(dLon / 2);
    return R * 2 * qAtan2(qSqrt(a), qSqrt(1 - a));
}

void DroneBackend::rebuild()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (auto it = m_accum.begin(); it != m_accum.end(); ) {
        if (now - it->last > kContactTtlMs) it = m_accum.erase(it); else ++it;
    }
    for (auto it = m_heur.begin(); it != m_heur.end(); ) {
        if (now - it->last > kHeurTtlMs) it = m_heur.erase(it); else ++it;
    }

    QHash<QString, QVariantMap> byKey;
    QHash<QString, qint64> keyLast;
    QHash<QString, bool> realBssid;   // bssids that yielded a real RID contact

    for (auto it = m_accum.constBegin(); it != m_accum.constEnd(); ++it) {
        const QString src = it.value().source;
        const Accum &a = it.value();
        QVariantMap v = rid::toVariant(a.uas);

        const QString serial = v.value(QStringLiteral("serial")).toString();
        const QString key = serial.isEmpty() ? (QStringLiteral("id:") + it.key()) : serial;
        v.insert("key", key);
        v.insert("source", src);
        v.insert("probable", false);
        if (a.haveRssi)
            v.insert("rssi", a.rssi);
        v.insert("ageMs", (int)(now - a.last));
        if (it.key().startsWith(QStringLiteral("wifi:")))
            realBssid.insert(it.key(), true);

        double distance = -1.0;
        if (m_haveObserver && v.value(QStringLiteral("hasLocation")).toBool()) {
            const double lat = v.value(QStringLiteral("lat")).toDouble();
            const double lon = v.value(QStringLiteral("lon")).toDouble();
            if (lat != 0.0 || lon != 0.0)
                distance = haversineMeters(m_obsLat, m_obsLon, lat, lon);
        }
        v.insert("distanceM", distance);
        v.insert("hasDistance", distance >= 0.0);

        if (!byKey.contains(key) || a.last > keyLast.value(key)) {
            byKey.insert(key, v);
            keyLast.insert(key, a.last);
        }
    }

    // Heuristic (SSID/OUI) contacts — only if that BSSID didn't already give RID.
    for (auto it = m_heur.constBegin(); it != m_heur.constEnd(); ++it) {
        if (realBssid.contains(it.key()))
            continue;
        const Heur &h = it.value();
        QVariantMap v;
        const QString key = QStringLiteral("heur:") + it.key();
        v.insert("key", key);
        v.insert("serial", QString());
        v.insert("probable", true);
        v.insert("source", QStringLiteral("WiFi"));
        v.insert("uaTypeName", h.vendor);
        v.insert("ssid", h.ssid);
        v.insert("rssi", h.signalDbm);
        v.insert("hasLocation", false);
        v.insert("hasDistance", false);
        v.insert("distanceM", -1.0);
        v.insert("ageMs", (int)(now - h.last));
        byKey.insert(key, v);
    }

    QVariantList list;
    list.reserve(byKey.size());
    for (const QVariantMap &v : byKey)
        list << v;

    std::sort(list.begin(), list.end(), [](const QVariant &A, const QVariant &B) {
        const QVariantMap a = A.toMap(), b = B.toMap();
        const bool da = a.value("hasDistance").toBool(), db = b.value("hasDistance").toBool();
        if (da != db) return da;
        if (da && db) {
            const double x = a.value("distanceM").toDouble(), y = b.value("distanceM").toDouble();
            if (x != y) return x < y;
        }
        const int ra = a.value("rssi", -999).toInt(), rb = b.value("rssi", -999).toInt();
        if (ra != rb) return ra > rb;
        return a.value("ageMs").toInt() < b.value("ageMs").toInt();
    });

    // ---- proximity alarm (threshold + optional "approaching") ----
    QString alarmText;
    QHash<QString, double> newPrev;
    for (int i = 0; i < list.size(); ++i) {
        QVariantMap v = list[i].toMap();
        const QString key = v.value("key").toString();
        bool isAlarm = false;
        if (v.value("hasDistance").toBool()) {
            const double d = v.value("distanceM").toDouble();
            newPrev.insert(key, d);
            const bool near = d <= m_alarmMeters;
            const double prev = m_prevDist.value(key, d);
            const bool approaching = d < prev - 1.0;
            if (near && (!m_alarmApproach || approaching)) {
                isAlarm = true;
                if (alarmText.isEmpty()) {
                    const QString who = v.value("serial").toString().isEmpty()
                                        ? tr("Drone") : v.value("serial").toString();
                    alarmText = tr("%1 — %2 m%3").arg(who).arg((int)d)
                                .arg(approaching ? tr(", approaching") : QString());
                    emit alarm(key, d);
                }
            }
        }
        v.insert("isAlarm", isAlarm);
        list[i] = v;
    }
    m_prevDist = newPrev;
    m_alarmText = alarmText;

    m_contacts = list;
    emit updated();
}

// ---------------------------------------------------------------------------
//  Demo / simulation mode — synthesise drones so the UI works with none in the air.
// ---------------------------------------------------------------------------
void DroneBackend::setDemo(bool on)
{
    if (on == m_demo)
        return;
    m_demo = on;
    if (on) {
        stop();                 // silence real sensors while simulating
        m_accum.clear();
        m_heur.clear();
        m_demoPhase = 0;
        // Give the map a centre so the demo drones are visible even with no GPS.
        if (!m_haveObserver) {
            m_obsLat = 0.0;   // synthetic null-island centre for the demo
            m_obsLon = 0.0;
            m_haveObserver = true;
            m_demoObs = true;
            emit observerChanged();
        }
        m_demoTimer.start();
        if (!m_scanning) { m_scanning = true; emit statusChanged(); }
    } else {
        m_demoTimer.stop();
        if (m_demoObs) {          // drop the synthetic observer again
            m_haveObserver = false;
            m_demoObs = false;
            emit observerChanged();
        }
        clear();
        start();
    }
    emit demoChanged();
}

static QByteArray wrapBt(const void *enc25, quint8 counter)
{
    QByteArray p;
    p.append((char)0x0D);
    p.append((char)counter);
    p.append(reinterpret_cast<const char *>(enc25), ODID_MESSAGE_SIZE);
    return p;
}

void DroneBackend::demoTick()
{
    ++m_demoPhase;
    const double baseLat = m_haveObserver ? m_obsLat : 0.0;   // fallback: null island
    const double baseLon = m_haveObserver ? m_obsLon : 0.0;
    const double latRad = qDegreesToRadians(baseLat);

    struct D { const char *serial; const char *op; double bearing; double dist; ODID_uatype_t type; };
    // one approaching, one loitering, one far.
    const double approach = 420.0 - (m_demoPhase % 80) * 5.0;     // 420 → 20 m loop
    D drones[] = {
        {"TEST-000000000000001", "OP-TEST-0001",  30.0, approach,        ODID_UATYPE_HELICOPTER_OR_MULTIROTOR},
        {"TEST-000000000000002", "OP-TEST-0002", 200.0, 180.0,           ODID_UATYPE_HELICOPTER_OR_MULTIROTOR},
        {"TEST-000000000000003", "OP-TEST-0003", 310.0, 900.0,           ODID_UATYPE_AEROPLANE},
    };

    int idx = 0;
    for (const D &d : drones) {
        const double br = qDegreesToRadians(d.bearing);
        const double dLat = (d.dist * qCos(br)) / 111320.0;
        const double dLon = (d.dist * qSin(br)) / (111320.0 * qCos(latRad));
        const double lat = baseLat + dLat;
        const double lon = baseLon + dLon;
        const QString mac = QString("02:00:00:00:00:0%1").arg(idx + 1);

        ODID_BasicID_data b; odid_initBasicIDData(&b);
        b.IDType = ODID_IDTYPE_SERIAL_NUMBER; b.UAType = d.type;
        qstrncpy(b.UASID, d.serial, ODID_ID_SIZE + 1);
        ODID_BasicID_encoded be; encodeBasicIDMessage(&be, &b);
        feedAccum(mac, QStringLiteral("BT(demo)"), false, wrapBt(&be, m_demoPhase), -50);

        ODID_Location_data L; odid_initLocationData(&L);
        L.Status = ODID_STATUS_AIRBORNE;
        L.Direction = d.bearing; L.SpeedHorizontal = 8.0;
        L.Latitude = lat; L.Longitude = lon;
        L.AltitudeGeo = 100.0; L.Height = 80.0; L.HeightType = ODID_HEIGHT_REF_OVER_GROUND;
        ODID_Location_encoded le; encodeLocationMessage(&le, &L);
        feedAccum(mac, QStringLiteral("BT(demo)"), false, wrapBt(&le, m_demoPhase), -50);

        ODID_System_data S; odid_initSystemData(&S);
        S.OperatorLatitude = baseLat + 0.0005; S.OperatorLongitude = baseLon + 0.0005;
        ODID_System_encoded se; encodeSystemMessage(&se, &S);
        feedAccum(mac, QStringLiteral("BT(demo)"), false, wrapBt(&se, m_demoPhase), -50);

        ODID_OperatorID_data O; odid_initOperatorIDData(&O);
        qstrncpy(O.OperatorId, d.op, ODID_ID_SIZE + 1);
        ODID_OperatorID_encoded oe; encodeOperatorIDMessage(&oe, &O);
        feedAccum(mac, QStringLiteral("BT(demo)"), false, wrapBt(&oe, m_demoPhase), -50);

        ++idx;
    }
}
