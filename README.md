# Splat

A Gaussian splat viewer for Qt Quick: a C++ port of [Spark](https://github.com/sparkjsdev/spark)
on Qt 6.10 RHI. It renders PLY and SPZ files in a standard QML item on Vulkan, Direct3D 11 and 12,
Metal, OpenGL 4.2+ and OpenGL ES 3.0.

```qml
import Splat 1.0

SplatView {
    anchors.fill: parent
    source: "file:///path/scene.spz"
    focus: true
}
```

## Build

Qt 6.10.1, qmake, with the MinGW 13.1 or the MSVC 2022 kit:

```sh
sh build.sh release            # MinGW, outputs in bin/ and lib/
sh build.sh release msvc       # MSVC, outputs in bin/msvc/ and lib/msvc/ (has Direct3D 12)
bin/splatviewer scene.spz --backend vulkan
bin/msvc/splatviewer scene.spz --backend d3d12
```

`QT_MINGW`, `QT_MSVC`, `MINGW_DIR`, `VCVARS` and `JOM` override the tool locations.

## Test

```sh
cd test/reference && npm install && npx playwright install chromium
node reference.mjs --spark $SPARK_DIR --ref v2.3.1       # Spark reference dumps
cd ../..
bin/splattest --backend d3d11 --reference build/reference/v2.3.1   # also vulkan, gl
bin/splatviewer --backend vulkan --bench test/bench.json           # windowed benchmark (--fast)
node test/reference/bench.mjs --spark $SPARK_DIR --angle vulkan    # Spark benchmark
node test/reference/bench.mjs --compare build/bench/viewer-*.json
```

Under Git Bash, set `QT_FORCE_STDERR_LOGGING=1` to see the output of the Qt tools.

## Navigation

| Input | Action |
|---|---|
| W A S D / arrows | move |
| Q E / PageDown PageUp | down / up |
| Shift, Ctrl | faster, slower |
| Left drag | rotate |
| Right / middle drag | slide |
| Wheel | forward / back |

The viewer also has O (open), R (reset camera) and 1 / 2 / 3 (OpenCV / OpenGL / Z up orientation).

## Documentation

- [doc/plan.md](doc/plan.md): the plan.
- [doc/implementation.md](doc/implementation.md): how it works, file by file.
- [doc/resync.md](doc/resync.md): how to follow upstream Spark.
- [doc/benchmark.md](doc/benchmark.md): performance against Spark in the browser.
- [test/reference/README.md](test/reference/README.md): comparing with the real Spark, and the
  benchmark.

## License

Splat is LGPLv3 (see LICENSE.md). The ported Spark code keeps its MIT notice (see LICENSE.spark).
