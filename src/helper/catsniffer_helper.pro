TEMPLATE = app
TARGET = harbour-idrone-catsniffer

QT = core dbus
QT -= gui
CONFIG += c++17
CONFIG -= app_bundle

SOURCES += catsniffer_helper.cpp

target.path = /usr/bin
INSTALLS += target
