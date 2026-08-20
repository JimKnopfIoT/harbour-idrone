TEMPLATE = app
TARGET = harbour-idrone-wifimon

QT = core dbus
QT -= gui
CONFIG += c++17
CONFIG -= app_bundle

SOURCES += wifimon_helper.cpp

target.path = /usr/bin
INSTALLS += target
