QT += core gui qml quick quickcontrols2 network

CONFIG += c++17 release
TARGET = vow
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/cli.h \
    src/client.h \
    src/engine.h \
    src/model.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/cli.cpp \
    src/client.cpp \
    src/engine.cpp \
    src/model.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
