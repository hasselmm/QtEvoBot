QT += bluetooth quick

CONFIG += c++14

DEFINES += \
    QT_DEPRECATED_WARNINGS \
    QT_DISABLE_DEPRECATED_BEFORE=0x060000

HEADERS += \
    controller.h \
    robotservice.h \
    utilities.h

SOURCES += \
    controller.cpp \
    main.cpp \
    robotservice.cpp \
    utilities.cpp

RESOURCES += \
    main.qrc
