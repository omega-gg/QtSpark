include(../splat.pri)

TARGET = Splat$$SPLAT_SUFFIX

DESTDIR = $$SPLAT/lib

TEMPLATE = lib

QT += quick concurrent gui-private zlib-private

win32:CONFIG += dll

DEFINES += SPLAT_LIBRARY

include(shaders.pri)

HEADERS += Splat.h \
           WSplatReceiver.h \
           WSplatMath.h \
           WPackedSplats.h \
           WSplatPly.h \
           WSplatSpz.h \
           WSplatLoader.h \
           WSplatSort.h \
           WSplatAccumulator.h \
           WSplatRenderer.h \
           WSplatRenderer_p.h \
           WSplatControls.h \
           WSplatView.h \
           WSplatView_p.h

SOURCES += WSplatMath.cpp \
           WPackedSplats.cpp \
           WSplatPly.cpp \
           WSplatSpz.cpp \
           WSplatLoader.cpp \
           WSplatSort.cpp \
           WSplatAccumulator.cpp \
           WSplatRenderer.cpp \
           WSplatRendererCapture.cpp \
           WSplatControls.cpp \
           WSplatView.cpp

# NOTE: The DLL lands next to the executables so they run without a deploy step.
win32:DLLDESTDIR = $$SPLAT/bin
