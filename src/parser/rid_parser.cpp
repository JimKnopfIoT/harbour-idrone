#include "rid_parser.h"

#include <cstring>

namespace rid {

// AD Application Code for Open Drone ID (first byte of the 0xFFFA service data).
static const quint8 ODID_APP_CODE = 0x0D;

bool feedBtServiceData(const QByteArray &payload, ODID_UAS_Data &uas)
{
    if (payload.size() < 3)
        return false;
    const quint8 *d = reinterpret_cast<const quint8 *>(payload.constData());
    if (d[0] != ODID_APP_CODE)
        return false;

    // [0]=app code, [1]=message counter, [2..]=25-byte ASTM F3411 message.
    quint8 msg[ODID_MESSAGE_SIZE];
    std::memset(msg, 0, sizeof(msg));
    int avail = payload.size() - 2;
    if (avail > ODID_MESSAGE_SIZE)
        avail = ODID_MESSAGE_SIZE;
    std::memcpy(msg, d + 2, avail);

    // BT Legacy carries a single message per advert; a Message Pack (BT5/Wi-Fi)
    // is handled at M2. decodeOpenDroneID dispatches by type and sets Valid flags.
    ODID_messagetype_t t = decodeOpenDroneID(&uas, msg);
    return t != ODID_MESSAGETYPE_INVALID;
}

bool feedMessagePack(const QByteArray &pack, ODID_UAS_Data &uas)
{
    if (pack.size() < 3)
        return false;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(pack.constData());
    // odid_message_process_pack validates the pack header (type 0xF, sizes) and
    // decodes each contained message into uas, setting the Valid flags. It returns
    // the decoded pack SIZE (> 0) on success, or a negative errno on failure —
    // NOT ODID_SUCCESS (0). So test for a positive return, not == ODID_SUCCESS.
    return odid_message_process_pack(&uas, p, pack.size()) > 0;
}

static QString odidString(const char *s, int maxLen)
{
    int n = 0;
    while (n < maxLen && s[n] != '\0')
        ++n;
    return QString::fromLatin1(s, n).trimmed();
}

QString uaTypeName(int t)
{
    switch (t) {
    case ODID_UATYPE_AEROPLANE: return QStringLiteral("Fixed-wing");
    case ODID_UATYPE_HELICOPTER_OR_MULTIROTOR: return QStringLiteral("Multirotor");
    case ODID_UATYPE_GYROPLANE: return QStringLiteral("Gyroplane");
    case ODID_UATYPE_HYBRID_LIFT: return QStringLiteral("Hybrid-lift");
    case ODID_UATYPE_ORNITHOPTER: return QStringLiteral("Ornithopter");
    case ODID_UATYPE_GLIDER: return QStringLiteral("Glider");
    case ODID_UATYPE_KITE: return QStringLiteral("Kite");
    case ODID_UATYPE_FREE_BALLOON: return QStringLiteral("Free balloon");
    case ODID_UATYPE_CAPTIVE_BALLOON: return QStringLiteral("Captive balloon");
    case ODID_UATYPE_AIRSHIP: return QStringLiteral("Airship");
    case ODID_UATYPE_FREE_FALL_PARACHUTE: return QStringLiteral("Parachute");
    case ODID_UATYPE_ROCKET: return QStringLiteral("Rocket");
    case ODID_UATYPE_TETHERED_POWERED_AIRCRAFT: return QStringLiteral("Tethered");
    case ODID_UATYPE_GROUND_OBSTACLE: return QStringLiteral("Ground obstacle");
    case ODID_UATYPE_OTHER: return QStringLiteral("Other");
    default: return QString();
    }
}

QString idTypeName(int t)
{
    switch (t) {
    case ODID_IDTYPE_SERIAL_NUMBER: return QStringLiteral("Serial");
    case ODID_IDTYPE_CAA_REGISTRATION_ID: return QStringLiteral("CAA reg.");
    case ODID_IDTYPE_UTM_ASSIGNED_UUID: return QStringLiteral("UTM UUID");
    case ODID_IDTYPE_SPECIFIC_SESSION_ID: return QStringLiteral("Session ID");
    default: return QString();
    }
}

QString statusName(int s)
{
    switch (s) {
    case ODID_STATUS_GROUND: return QStringLiteral("on ground");
    case ODID_STATUS_AIRBORNE: return QStringLiteral("airborne");
    case ODID_STATUS_EMERGENCY: return QStringLiteral("emergency");
    case ODID_STATUS_REMOTE_ID_SYSTEM_FAILURE: return QStringLiteral("RID failure");
    default: return QString();
    }
}

QVariantMap toVariant(const ODID_UAS_Data &uas)
{
    QVariantMap m;

    // Basic ID: prefer a Serial-Number entry, else the first valid one.
    int chosen = -1;
    for (int i = 0; i < ODID_BASIC_ID_MAX_MESSAGES; ++i) {
        if (!uas.BasicIDValid[i])
            continue;
        if (chosen < 0)
            chosen = i;
        if (uas.BasicID[i].IDType == ODID_IDTYPE_SERIAL_NUMBER) {
            chosen = i;
            break;
        }
    }
    QString serial;
    if (chosen >= 0) {
        serial = odidString(uas.BasicID[chosen].UASID, ODID_ID_SIZE);
        m.insert("idType", (int)uas.BasicID[chosen].IDType);
        m.insert("idTypeName", idTypeName(uas.BasicID[chosen].IDType));
        m.insert("uaType", (int)uas.BasicID[chosen].UAType);
        m.insert("uaTypeName", uaTypeName(uas.BasicID[chosen].UAType));
    }
    m.insert("serial", serial);

    if (uas.OperatorIDValid) {
        m.insert("operatorId", odidString(uas.OperatorID.OperatorId, ODID_ID_SIZE));
        m.insert("hasOperator", true);
    } else {
        m.insert("hasOperator", false);
    }

    const bool hasLoc = uas.LocationValid;
    m.insert("hasLocation", hasLoc);
    if (hasLoc) {
        const ODID_Location_data &L = uas.Location;
        m.insert("lat", L.Latitude);
        m.insert("lon", L.Longitude);
        m.insert("altGeo", L.AltitudeGeo);
        m.insert("height", L.Height);
        m.insert("direction", L.Direction);
        m.insert("speed", L.SpeedHorizontal);
        m.insert("status", (int)L.Status);
        m.insert("statusName", statusName(L.Status));
    }

#ifdef IDRONE_ULTIMATE
    // Take-off point; LIVE_GNSS means it tracks the controller instead.
    if (uas.SystemValid) {
        m.insert("operatorLat", uas.System.OperatorLatitude);
        m.insert("operatorLon", uas.System.OperatorLongitude);
        m.insert("operatorLocType", (int)uas.System.OperatorLocationType);
        m.insert("hasOperatorPos", uas.System.OperatorLatitude != 0.0
                                || uas.System.OperatorLongitude != 0.0);
    } else {
        m.insert("hasOperatorPos", false);
    }
#else
    m.insert("hasOperatorPos", false);
#endif

    return m;
}

} // namespace rid
