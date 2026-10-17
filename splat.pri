# Common settings for every Splat project.

SPLAT = $$PWD

# C++17
CONFIG += c++17

INCLUDEPATH += $$SPLAT/src

CONFIG(debug, debug|release) {
    SPLAT_SUFFIX = D
} else {
    SPLAT_SUFFIX =
}

unix:QMAKE_LFLAGS += "-Wl,-rpath,'\$$ORIGIN'"
