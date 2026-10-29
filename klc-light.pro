QT += core gui qml quick quickcontrols2

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += hidapi-hidraw xkbcommon
TARGET = klc-light
TEMPLATE = app

HEADERS += $$files(src/*.h)
SOURCES += $$files(src/*.cpp)

RESOURCES += src/resources.qrc
