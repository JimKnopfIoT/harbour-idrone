// harbour-idrone — Remote ID parser wrapper (opendroneid-core-c).
//
// One parser for all cooperative channels (CONCEPT §4/§9). M1 feeds it Bluetooth
// Legacy service-data payloads; Wi-Fi beacon message packs plug in at M2.
#ifndef RID_PARSER_H
#define RID_PARSER_H

#include <QByteArray>
#include <QString>
#include <QVariantMap>

extern "C" {
#include "opendroneid.h"
}

namespace rid {

// Decode one Bluetooth Legacy RID advertisement payload — the bytes BlueZ stores
// as ServiceData for UUID 0xFFFA: [0x0D app-code][msg counter][25-byte message].
// The decoded message is merged into `uas` (call repeatedly to reassemble the
// per-drone state over successive adverts). Returns true if a message decoded.
bool feedBtServiceData(const QByteArray &payload, ODID_UAS_Data &uas);

// Decode a Remote ID Message Pack (Wi-Fi Beacon vendor IE payload / BT5 extended)
// into `uas`. `pack` starts at the ODID_MessagePack_encoded (i.e. after the Wi-Fi
// OUI FA:0B:BC, vendor-type 0x0D and the 1-byte message counter). Returns true on
// a successful decode of at least one contained message.
bool feedMessagePack(const QByteArray &pack, ODID_UAS_Data &uas);

// Flatten accumulated UAS data into display fields for the UI/contact model.
// Always sets "serial" (may be empty). Sets hasLocation / hasOperator flags.
QVariantMap toVariant(const ODID_UAS_Data &uas);

QString uaTypeName(int t);
QString idTypeName(int t);
QString statusName(int s);

} // namespace rid

#endif // RID_PARSER_H
