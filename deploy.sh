#!/bin/sh
set -e

#--------------------------------------------------------------------------------------------------
# Settings
#--------------------------------------------------------------------------------------------------

Qt_version="6.10.1"

MinGW_version="13.1.0"

#--------------------------------------------------------------------------------------------------
# Functions
#--------------------------------------------------------------------------------------------------

# NOTE: Copies the plugins (matching $2) and qmldir files of the QML modules the viewer imports.
copyQml()
{
    (cd "$1/qml"; find QtQml QtQuick Qt/labs/folderlistmodel -name "$2" -o -name qmldir) |
    while read -r file; do

        mkdir -p "deploy/$(dirname "$file")"

        cp "$1/qml/$file" "deploy/$file"
    done
}

#--------------------------------------------------------------------------------------------------
# Syntax
#--------------------------------------------------------------------------------------------------

usage()
{
    echo "Usage: deploy [mingw | msvc | clean]"
    echo ""
    echo "Copies the viewer built by build.sh, with its Qt runtime, in deploy/."

    exit 1
}

kit="${1:-mingw}"

case "$kit" in mingw|msvc|clean) ;; *) usage ;; esac

#--------------------------------------------------------------------------------------------------
# Configuration
#--------------------------------------------------------------------------------------------------

cd "$(dirname "$0")"

case `uname` in
MINGW*|MSYS*|CYGWIN*) host="win64";;
Darwin*)              host="macOS";;
*)                    host="linux";;
esac

# NOTE: The same kits as build.sh.
if [ -n "$EXTERNAL" ]; then

    Qt="$EXTERNAL/Qt/$Qt_version"

    QT_MINGW="${QT_MINGW:-$Qt}"
    QT_MSVC="${QT_MSVC:-$Qt}"
    QT_DIR="${QT_DIR:-$Qt}"

    MINGW_DIR="${MINGW_DIR:-$EXTERNAL/MinGW/$MinGW_version}"
fi

QT_MINGW="${QT_MINGW:-/c/Qt/$Qt_version/mingw_64}"
QT_MSVC="${QT_MSVC:-/c/Qt/$Qt_version/msvc2022_64}"

if [ $host = "macOS" ]; then

    QT_DIR="${QT_DIR:-$HOME/Qt/$Qt_version/macos}"
else
    QT_DIR="${QT_DIR:-$HOME/Qt/$Qt_version/gcc_64}"
fi

MINGW_DIR="${MINGW_DIR:-/c/Qt/Tools/mingw1310_64}"

#--------------------------------------------------------------------------------------------------
# Clean
#--------------------------------------------------------------------------------------------------

rm -rf deploy/*

touch deploy/.gitignore

if [ "$kit" = "clean" ]; then

    exit 0
fi

#--------------------------------------------------------------------------------------------------
# Deploy
#--------------------------------------------------------------------------------------------------
# NOTE: The viewer with the Qt libraries, plugins and QML modules it loads, without Qt Quick 3D.
#       QtQuick.Dialogs needs Qt.labs.folderlistmodel.

echo "DEPLOYING splatviewer"

if [ $host = "win64" ]; then

    if [ "$kit" = "mingw" ]; then

        Qt="$QT_MINGW"

        bin="bin"

        cp "$MINGW_DIR"/bin/libgcc_s_seh-1.dll  deploy
        cp "$MINGW_DIR"/bin/libstdc++-6.dll     deploy
        cp "$MINGW_DIR"/bin/libwinpthread-1.dll deploy
    else
        Qt="$QT_MSVC"

        bin="bin/msvc"
    fi

    cp $bin/splatviewer.exe $bin/Splat.dll deploy

    for library in Core Gui Network OpenGL Concurrent Qml QmlMeta QmlModels QmlWorkerScript \
                   LabsFolderListModel; do

        cp "$Qt/bin/Qt6$library.dll" deploy
    done

    cp "$Qt"/bin/Qt6Quick*.dll deploy

    rm deploy/Qt6Quick3D*

    mkdir deploy/platforms

    cp "$Qt"/plugins/platforms/qwindows.dll deploy/platforms

    copyQml "$Qt" "*.dll"

elif [ $host = "macOS" ]; then

    cp -R bin/splatviewer.app deploy

    "$QT_DIR"/bin/macdeployqt deploy/splatviewer.app -qmldir=viewer/qml

    codesign --force --deep --sign - deploy/splatviewer.app
else
    Qt="$QT_DIR"

    cp bin/splatviewer deploy

    cp -P lib/libSplat.so* deploy

    for library in Core Gui Network OpenGL Concurrent Qml QmlMeta QmlModels QmlWorkerScript \
                   LabsFolderListModel DBus XcbQpa; do

        cp "$Qt/lib/libQt6$library.so.6" deploy
    done

    cp "$Qt"/lib/libQt6Quick*.so.6 deploy

    rm deploy/libQt6Quick3D*

    cp "$Qt"/lib/libicu*.so.* deploy

    mkdir deploy/platforms deploy/xcbglintegrations

    cp "$Qt"/plugins/platforms/libqxcb.so deploy/platforms

    cp "$Qt"/plugins/xcbglintegrations/*.so deploy/xcbglintegrations

    copyQml "$Qt" "*.so"

    # NOTE: The plugins look for the Qt libraries in their own lib folder, the script points the
    #       loader at the deploy folder.
    cp dist/start.sh deploy

    chmod +x deploy/start.sh
fi
