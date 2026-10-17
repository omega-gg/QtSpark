#!/bin/sh
set -e

#--------------------------------------------------------------------------------------------------
# Settings
#--------------------------------------------------------------------------------------------------

# NOTE: Override these with environment variables when your kits live elsewhere.
QT_MINGW="${QT_MINGW:-/c/Qt/6.10.1/mingw_64}"
QT_MSVC="${QT_MSVC:-/c/Qt/6.10.1/msvc2022_64}"

MINGW_DIR="${MINGW_DIR:-/c/Qt/Tools/mingw1310_64}"

VCVARS="${VCVARS:-C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat}"

JOM="${JOM:-/c/Qt/Tools/QtCreator/bin/jom/jom.exe}"

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
# Build
#--------------------------------------------------------------------------------------------------

cd "$(dirname "$0")"

root="$PWD"

if [ "$config" = "clean" ]; then

    if [ "$kit" = "msvc" ]; then
        rm -rf build/qmake-msvc bin/msvc lib/msvc
    else
        rm -rf build/qmake bin/*.exe bin/*.dll lib/*.a lib/*.dll
    fi

    exit 0
fi

if [ "$kit" = "mingw" ]; then

    export PATH="$QT_MINGW/bin:$MINGW_DIR/bin:$PATH"

    mkdir -p build/qmake

    cd build/qmake

    qmake ../../splat.pro -spec win32-g++ "CONFIG+=$config"

    mingw32-make $make_arguments
else
    mkdir -p build/qmake-msvc

    # NOTE: The MSVC environment only exists inside cmd, after vcvars64.bat.
    cat > build/qmake-msvc/build.bat << EOF
@echo off
call "$VCVARS" > nul || exit /b 1
cd /d "$(cygpath -w "$root/build/qmake-msvc")" || exit /b 1
"$(cygpath -w "$QT_MSVC/bin/qmake.exe")" "$(cygpath -w "$root/splat.pro")" -spec win32-msvc "CONFIG+=$config" || exit /b 1
"$(cygpath -w "$JOM")" || exit /b 1
EOF

    cmd //c "$(cygpath -w "$root/build/qmake-msvc/build.bat")"
fi
