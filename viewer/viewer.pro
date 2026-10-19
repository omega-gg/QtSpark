include(../splat.pri)

TARGET = splatviewer$$SPLAT_SUFFIX

DESTDIR = $$SPLAT_BIN

TEMPLATE = app

QT += quick

LIBS += -L$$SPLAT_LIB -lSplat$$SPLAT_SUFFIX

# NOTE: The bench helpers shared with splattest.
INCLUDEPATH += $$SPLAT/test/common

HEADERS += WViewerBench.h \
           ../test/common/WSplatBench.h

SOURCES += main.cpp \
           WViewerBench.cpp

RESOURCES += viewer.qrc

OTHER_FILES += qml/Main.qml
