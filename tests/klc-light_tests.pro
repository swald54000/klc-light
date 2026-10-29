QT += core gui quick quickcontrols2 testlib
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += hidapi-hidraw xkbcommon
TARGET = klc-light_tests
TEMPLATE = app

INCLUDEPATH += ../src
DEFINES += KLC_FIXTURES=\\\"$$PWD/fixtures\\\"

# Every source except the entry point, so tests exercise the real units.
HEADERS += $$files(../src/*.h)
SOURCES += klc-light_tests.cpp $$files(../src/*.cpp)
SOURCES -= ../src/main.cpp
RESOURCES += ../src/resources.qrc
