#!/bin/sh
set -e

# NOTE: Starts the deployed viewer with the libraries and plugins of its own folder.

path=$(dirname "$(readlink -f "$0")")

export LD_LIBRARY_PATH="$path"

export QT_PLUGIN_PATH="$path"

"$path/splatviewer" "$@"
