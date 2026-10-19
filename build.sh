#!/bin/sh
set -e

#--------------------------------------------------------------------------------------------------
# Settings
#--------------------------------------------------------------------------------------------------

Qt_version="6.10.1"

MinGW_version="13.1.0"

jom_version="1.1.4"

make_arguments="${MAKE_ARGUMENTS:--j 8}"

#--------------------------------------------------------------------------------------------------
# Syntax
#--------------------------------------------------------------------------------------------------

usage()
{
    echo "Usage: build <release | debug | clean> [mingw | msvc]"
    echo ""
    echo "mingw (default) outputs in bin/ and lib/, msvc in bin/msvc/ and lib/msvc/."
    echo "The MSVC kit has the Direct3D 12 backend, the MinGW kit does not."

    exit 1
}

config="${1:-release}"
kit="${2:-mingw}"

case "$config" in release|debug|clean) ;; *) usage ;; esac
case "$kit"    in mingw|msvc)          ;; *) usage ;; esac

#--------------------------------------------------------------------------------------------------
# Configuration
#--------------------------------------------------------------------------------------------------

cd "$(dirname "$0")"

root="$PWD"

case `uname` in
MINGW*|MSYS*|CYGWIN*) host="win64";;
Darwin*)              host="macOS";;
*)                    host="linux";;
esac

# NOTE: EXTERNAL points at the kits of 3rdparty.sh (../3rdparty/<platform>), the local installs
#       are used otherwise. Override each kit with an environment variable when it lives elsewhere.
if [ -n "$EXTERNAL" ]; then

    Qt="$EXTERNAL/Qt/$Qt_version"

    QT_MINGW="${QT_MINGW:-$Qt}"
    QT_MSVC="${QT_MSVC:-$Qt}"
    QT_DIR="${QT_DIR:-$Qt}"

    MINGW_DIR="${MINGW_DIR:-$EXTERNAL/MinGW/$MinGW_version}"

    JOM="${JOM:-$EXTERNAL/jom/$jom_version/jom.exe}"
fi

QT_MINGW="${QT_MINGW:-/c/Qt/$Qt_version/mingw_64}"
QT_MSVC="${QT_MSVC:-/c/Qt/$Qt_version/msvc2022_64}"

if [ $host = "macOS" ]; then

    QT_DIR="${QT_DIR:-$HOME/Qt/$Qt_version/macos}"
else
    QT_DIR="${QT_DIR:-$HOME/Qt/$Qt_version/gcc_64}"
fi

MINGW_DIR="${MINGW_DIR:-/c/Qt/Tools/mingw1310_64}"

VCVARS="${VCVARS:-C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat}"

JOM="${JOM:-/c/Qt/Tools/QtCreator/bin/jom/jom.exe}"

if [ $host != "win64" ]; then

    kit="default"
fi

#--------------------------------------------------------------------------------------------------
# Clean
#--------------------------------------------------------------------------------------------------

if [ "$config" = "clean" ]; then

    if [ "$kit" = "msvc" ]; then
        rm -rf build/qmake-msvc bin/msvc lib/msvc
    else
        rm -rf build/qmake bin/splat* bin/Splat* lib/libSplat* lib/Splat*
    fi

    exit 0
fi

#--------------------------------------------------------------------------------------------------
# Build
#--------------------------------------------------------------------------------------------------

if [ "$kit" = "default" ]; then

    export PATH="$QT_DIR/bin:$PATH"

    mkdir -p build/qmake

    cd build/qmake

    qmake -r ../../splat.pro "CONFIG+=$config"

    make $make_arguments

elif [ "$kit" = "mingw" ]; then

    export PATH="$QT_MINGW/bin:$MINGW_DIR/bin:$PATH"

    mkdir -p build/qmake

    cd build/qmake

    qmake -r ../../splat.pro -spec win32-g++ "CONFIG+=$config"

    mingw32-make $make_arguments
else
    mkdir -p build/qmake-msvc

    # NOTE: The MSVC environment only exists inside cmd, after vcvars64.bat.
    cat > build/qmake-msvc/build.bat << EOF
@echo off
call "$VCVARS" > nul || exit /b 1
cd /d "$(cygpath -w "$root/build/qmake-msvc")" || exit /b 1
"$(cygpath -w "$QT_MSVC/bin/qmake.exe")" -r "$(cygpath -w "$root/splat.pro")" -spec win32-msvc "CONFIG+=$config" || exit /b 1
"$(cygpath -w "$JOM")" || exit /b 1
EOF

    cmd //c "$(cygpath -w "$root/build/qmake-msvc/build.bat")"
fi
