QT += core gui quick quickcontrols2 testlib network
CONFIG += c++17 testcase
TARGET = vow_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/cli.h ../src/client.h ../src/engine.h ../src/model.h ../src/theme.h ../src/translator.h
SOURCES += vow_tests.cpp ../src/backend.cpp ../src/cli.cpp ../src/client.cpp ../src/engine.cpp ../src/model.cpp \
    ../src/theme.cpp ../src/translator.cpp
