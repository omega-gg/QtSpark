QtSpark
---
[![LGPLv3](https://img.shields.io/badge/License-LGPLv3-blue.svg)](https://www.gnu.org/licenses/lgpl.html)

QtSpark is a native C / C++ port of [Spark](https://github.com/sparkjsdev/spark) to Qt. It renders
3D Gaussian splats in a standard QML item, on every Qt RHI backend: Direct3D 11 and 12, Vulkan,
Metal, OpenGL 4.2+ and OpenGL ES 3.0.
<br>

The port is faithful to Spark: the same encodings, the same shader math and the same sort, with
images matching Spark's from the same viewpoint. Every ported unit names its upstream source, so
following Spark's evolutions stays a mechanical process.
<br>

It runs natively, without a browser: it matches or beats Spark in Chrome on frame time, with
steadier frames and faster loading.

<img src="dist/pictures/QtSpark.png" alt="The QtSpark viewer" width="640px">

*The viewer showing the butterfly of Spark's examples.*

- [Manual](MANUAL.md)
- [Implementation](implementation.md)
- [Resync with Spark](resync.md)
- [Benchmark](doc/benchmark.md)

## Usage

    import Splat 1.0

    SplatView
    {
        anchors.fill: parent

        source: "file:///path/scene.spz"

        focus: true
    }

The viewer opens PLY and SPZ files:

    splatviewer [file] [options]

    --backend <d3d11 | d3d12 | vulkan | metal | opengl>   Graphics API (default: the Qt default)
    --fast                                                QtSpark performance options
    --bench <file>                                        Windowed benchmark

The [manual](MANUAL.md) covers the keys, the mouse, and every SplatView property.

## Technology

QtSpark is built in C++ on [Qt](https://www.qt.io) Quick and its RHI. It ports Spark v2.3.1, and
the three.js OrbitControls used by Spark's editor.

## Platforms

- Windows 64 bit.
- macOS and Linux 64 bit: built by the CI, not tested yet.
- iOS and Android: the backends are supported, not built yet.

## Requirements

- [Qt](https://download.qt.io/official_releases/qt) 6.10.1 or later.

On Windows:
- [Git for Windows](https://git-for-windows.github.io).
- MinGW 13.1, or the MSVC 2022 Build Tools (needed for Direct3D 12).

For the tests and the comparisons with Spark:
- [Node.js](https://nodejs.org) and a checkout of [Spark](https://github.com/sparkjsdev/spark).

## 3rdparty

You can install Qt and the Windows compilers in ../3rdparty, like Sky does, with:

    sh 3rdparty.sh <win64 | macOS | linux> [mingw | msvc]

## Build

You can build QtSpark with:

    sh build.sh <release | debug | clean> [mingw | msvc]

The local Qt installs are used by default. Point EXTERNAL at ../3rdparty/<platform> to build with
the 3rdparty kits instead.

## Deploy

You can deploy the viewer and its Qt runtime in deploy/ with:

    sh deploy.sh [mingw | msvc | clean]

## Test

You can compare QtSpark with Spark with:

    cd test/reference && npm install && npx playwright install chromium
    node reference.mjs --spark <spark checkout> --ref v2.3.1
    cd ../..
    bin/splattest --backend <d3d11 | d3d12 | vulkan | gl> --reference build/reference/v2.3.1

You can benchmark QtSpark against Spark with:

    SPARK_DIR=<spark checkout> sh tools/bench.sh <quick | full>

## License

Copyright (C) 2026 QtSpark authors | https://omega.gg

### Authors

- Benjamin Arnaud aka [bunjee](https://bunjee.me) | <bunjee@omega.gg>

### GNU Lesser General Public License Usage

QtSpark may be used under the terms of the GNU Lesser General Public License version 3 as published
by the Free Software Foundation and appearing in the LICENSE.md file included in the packaging of
this file. Please review the following information to ensure the GNU Lesser General Public License
requirements will be met: https://www.gnu.org/licenses/lgpl.html.

### Private License Usage

QtSpark licensees holding valid private licenses may use this file in accordance with the private
license agreement provided with the Software or, alternatively, in accordance with the terms
contained in written agreement between you and QtSpark authors. For further information contact us
at contact@omega.gg.

### Third party

The ported Spark code keeps its MIT notice (see LICENSE.spark), and so does the ported three.js
code (see LICENSE.three).
