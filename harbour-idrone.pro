# harbour-idrone — SailfishOS qmake project (skeleton)
#
# NOTE: skeleton only. Fill in as milestones land (see HANDOFF.md).
# The Tier-0 build must not depend on any optional (root/SDR/sniffer) component.

TARGET = harbour-idrone
VERSION = 0.1.1
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

CONFIG += sailfishapp sailfishapp_i18n c++17

QT += dbus positioning network sensors

# Bilingual: English source strings + German. libsailfishapp auto-loads the .qm
# matching the device locale (German → de, otherwise the English source).
TRANSLATIONS += translations/harbour-idrone-de.ts
# help lupdate find the QML strings
lupdate_only {
    SOURCES += qml/*.qml qml/cover/*.qml qml/pages/*.qml
}

# Installed to /usr/share/icons/hicolor/<size>/apps/ (see sailfishapp.prf).
SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

# --- Bundled parser (opendroneid-core-c, Apache-2.0, git submodule) ---------
# Single parser for all cooperative Remote ID channels (BT service data + Wi-Fi
# vendor IE). Compiled straight into the app — no optional dependency.
INCLUDEPATH += lib/opendroneid-core-c/libopendroneid
SOURCES += \
    lib/opendroneid-core-c/libopendroneid/opendroneid.c \
    lib/opendroneid-core-c/libopendroneid/wifi.c

# --- Core sources (Tier 0) --------------------------------------------------
# The installed binary is the Silica UI app. src/daemon/main.cpp is the future
# fusion-daemon entrypoint (built as a separate target in a later milestone).
INCLUDEPATH += \
    src/parser \
    src/sensors/ble_bluez \
    src/sensors/wifi_wpa \
    src/hw \
    src/osm \
    src/sensor \
    src/app

HEADERS += \
    src/parser/rid_parser.h \
    src/sensors/ble_bluez/ble_bluez_sensor.h \
    src/sensors/wifi_wpa/wifi_wpa_sensor.h \
    src/hw/hwdetect.h \
    src/osm/osmfetch.h \
    src/sensor/sensorreader.h \
    src/app/dronebackend.h

SOURCES += \
    src/app/harbour-idrone.cpp \
    src/app/dronebackend.cpp \
    src/parser/rid_parser.cpp \
    src/sensors/ble_bluez/ble_bluez_sensor.cpp \
    src/sensors/wifi_wpa/wifi_wpa_sensor.cpp \
    src/hw/hwdetect.cpp \
    src/osm/osmfetch.cpp \
    src/sensor/sensorreader.cpp

# --- Sailjail permission + wpa_supplicant system-bus policy ------------------
sjperm.files = data/WpaScan.permission data/CatSniffer.permission
sjperm.path = /etc/sailjail/permissions
INSTALLS += sjperm

dbuspolicy.files = data/harbour-idrone-wpa.conf data/harbour-idrone-catsniffer.conf
dbuspolicy.path = /etc/dbus-1/system.d
INSTALLS += dbuspolicy

# polkit rule: defaultuser may start/stop only the CatSniffer helper unit.
polkitrule.files = data/50-harbour-idrone-catsniffer.rules
polkitrule.path = /usr/share/polkit-1/rules.d
INSTALLS += polkitrule

# --- QML / assets -----------------------------------------------------------
OTHER_FILES += \
    qml/harbour-idrone.qml \
    qml/cover/CoverPage.qml \
    qml/pages/MainPage.qml \
    qml/pages/RadarPage.qml \
    qml/pages/AboutPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/DronePage.qml \
    qml/pages/HwIndicator.qml \
    harbour-idrone.desktop \
    rpm/harbour-idrone.spec \
    systemd/harbour-idrone.service \
    systemd/harbour-idrone-catsniffer.service \
    src/helper/catsniffer_helper.pro

# --- Optional tiers ---------------------------------------------------------
# Enable per build with e.g.  CONFIG+=tier_sdr
# tier_sdr {
#     LIBS += -lhackrf
#     SOURCES += src/sensors/sdr_droneid/*.cpp
# }

DISTFILES += \
    README.md
