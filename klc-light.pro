QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += hidapi-hidraw xkbcommon libpipewire-0.3 libsystemd
TARGET = klc-light
TEMPLATE = app

HEADERS += $$files(src/*.h)
SOURCES += $$files(src/*.cpp)

RESOURCES += src/resources.qrc
