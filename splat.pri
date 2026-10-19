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

# NOTE: One output folder per kit, so the MinGW and MSVC builds can live side by side.
msvc {
    SPLAT_BIN = $$SPLAT/bin/msvc
    SPLAT_LIB = $$SPLAT/lib/msvc

    # NOTE: M_PI, and UTF-8 sources.
    DEFINES += _USE_MATH_DEFINES

    QMAKE_CXXFLAGS += /utf-8
} else {
    SPLAT_BIN = $$SPLAT/bin
    SPLAT_LIB = $$SPLAT/lib
}

# NOTE: The executables find the library next to them once deployed, in lib/ when built.
macx {
    QMAKE_SONAME_PREFIX = @rpath

    QMAKE_RPATHDIR += $$SPLAT_LIB
} else:unix {
    QMAKE_LFLAGS += "-Wl,-rpath,'\$$ORIGIN:\$$ORIGIN/../lib'"
}
