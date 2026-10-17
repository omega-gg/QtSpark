#!/bin/sh
set -e

#--------------------------------------------------------------------------------------------------
# Settings
#--------------------------------------------------------------------------------------------------

# NOTE: Override these with environment variables when your kit lives elsewhere.
QT_DIR="${QT_DIR:-/c/Qt/6.10.1/mingw_64}"

MINGW_DIR="${MINGW_DIR:-/c/Qt/Tools/mingw1310_64}"

make_arguments="${MAKE_ARGUMENTS:--j 8}"

#--------------------------------------------------------------------------------------------------
# Syntax
#--------------------------------------------------------------------------------------------------

if [ $# != 0 ] && [ "$1" != "release" ] && [ "$1" != "debug" ] && [ "$1" != "clean" ]; then

    echo "Usage: build <release | debug | clean>"

    exit 1
fi

config="${1:-release}"

#--------------------------------------------------------------------------------------------------
# Build
#--------------------------------------------------------------------------------------------------

cd "$(dirname "$0")"

export PATH="$QT_DIR/bin:$MINGW_DIR/bin:$PATH"

if [ "$config" = "clean" ]; then

    rm -rf build/qmake bin lib

    exit 0
fi

mkdir -p build/qmake

cd build/qmake

qmake ../../splat.pro -spec win32-g++ "CONFIG+=$config"

mingw32-make $make_arguments
