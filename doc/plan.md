# Splat: port of Spark (v2.3.1) to Qt 6.10.1 RHI

## Context

We want to view and navigate 3D Gaussian splats (PLY, SPZ) inside a standard QML item. It must be very
fast and run on every Qt RHI backend (Vulkan, D3D11/12, Metal, OpenGL 4.2+, OpenGL ES 3.0).

The reference implementation is Spark (sparkjsdev/spark, MIT, THREE.js). The port follows Spark 2.x
**faithfully**, using the same two passes, the same encodings, the same shader math and the same sort.
This keeps upstream resyncs mechanical: every ported unit names its upstream source.

We also run the real Spark to compare data and images with ours.

Decisions:
- **Pinned upstream:** tag `v2.3.1` (`2e9c2e74a486b1ba3a31025a22b231b8e4fccde3`).
- **Code style:** Sky conventions.
- **Build:** qmake.
- **Spherical harmonics:** degrees 1 to 3 included.

Out of scope for v1: LoD and paging, ExtSplats and CovSplats, 2DGS, depth of field, dyno edits and
modifiers, WebXR, and the other formats (ksplat, splat, sog, rad).

The target repository is empty: `.git` (branch `main`, no commits) and an empty `doc/`.

## Spark 2.x pipeline and its port

| Stage | Spark v2.3.1 | Port |
|---|---|---|
| Parse | Rust/WASM `rust/spark-lib/src/ply.rs`, `spz.rs`, run in a worker | `WSplatPly`, `WSplatSpz` (C++), on a worker thread |
| Encode | `splat_encode.rs`, `rust/spark-rs/src/packed_splats.rs`, `src/utils.ts` | `WPackedSplats` |
| GPU storage | RGBA32UI 2D array, 2048x2048 per layer, 16 B per splat; SH1 RG32UI, SH2/SH3 RGBA32UI | identical bytes and layout |
| Accumulate (per view) | `SplatAccumulator.ts` + dyno graph (`dyno/splats.ts`, `dyno/output.ts`) in `computeUvec4_Vec4.glsl` | `WSplatAccumulator` + `accumulate.frag` (dyno output expanded by hand) |
| Depth metric | float bits in an RGBA8 MRT attachment, `readRenderTargetPixelsAsync` per layer | same RGBA8 attachment, `readBackTexture` per layer (only the used rows) |
| Sort | `rust/spark-rs/src/sort.rs` `sort32`: 2 passes of 16-bit LSD radix on `!key`, back to front, keys >= 0x7f800000 dropped | `WSplatSort`, line-for-line port, on a worker thread |
| Ordering | RGBA32UI 4096 x rows, 4 indices per texel | identical |
| Draw | `splatVertex.glsl` / `splatFragment.glsl`, instanced quad, premultiplied, no depth write | `splat.vert` / `splat.frag`, same bodies |
| Drive | `SparkRenderer.updateInternal` / `driveSort` (3 accumulators: display, current, free list) | `WSplatRenderer::update` / `driveSort`, same state machine, accumulators allocated lazily |
| Controls | `controls.ts` `FpsMovement` + `PointerControls` | `WSplatControls` |

Data details (verified at v2.3.1):
- **Packed word layout:**
  - w0: RGBA8.
  - w1: half x, half y.
  - w2: half z, quat u8, quat v8.
  - w3: three log-scale u8 (lnMin -12, lnMax 9; 0 means zero scale), then quat angle u8.
- **Quaternion:** encoded with `encodeQuatOctXy88R8`.
- **SH:** sh1 is 9 x signed 7 bit, sh2 is 15 x int8, sh3 is 21 x signed 6 bit, all coefficient-major
  with RGB interleaved, max 1.
- **Accumulator output:** writes center minus `viewOrigin` (fp16, relative to the camera) and opacity x
  0.5. The draw pass multiplies it back by 2.
- **SH evaluation:** happens in the accumulator, with object-space
  `viewDir = normalize(center - cameraInObject)`. RGB is clamped to [0,1].
- **Mesh transform:** the scale is uniform (the average of the three axes, as in `SplatGenerator.ts`).
- **Metric:** radial `length(center - viewCenter)` by default. Inactive splats get
  `uintToVec4(0x7f800000u)`.

Defaults to mirror (from `SparkRenderer.ts`):
- maxStdDev sqrt(8), minAlpha 0.5/255, blurAmount 0.3, preBlurAmount 0.
- minPixelRadius 0, maxPixelRadius 512, clipXY 1.4, focalAdjustment 1, falloff 1, sortRadial true.
- encodeLinear false, since Qt's target is RGBA8 and is not sRGB-flagged.
- Re-accumulate when the camera moved more than 0.001, or when dot(dir) < 0.999.

## Repository layout (Sky-like qmake)

```
splat.pro                     TEMPLATE = subdirs, CONFIG += ordered: src, viewer, test/splattest
splat.pri                     common: CONFIG += c++17, DESTDIR lib/bin, D suffix for debug targets
src/
  src.pro                     shared lib "Splat" (QT += quick gui-private zlib-private), SPLAT_EXPORT
  shaders.pri                 QMAKE_EXTRA_COMPILERS qsb rule + RESOURCES variable listing the .qsb outputs
  WSplatView.h/_p.h/.cpp      QQuickRhiItem, QML type "SplatView"; thin, delegates to WSplatRenderer
  WSplatRenderer.h/.cpp       port of SparkRenderer: accumulators, readback, sort driving, ordering, draw
  WSplatAccumulator.h/.cpp    port of SplatAccumulator: one MRT render target per layer, generate()
  WPackedSplats.h/.cpp        port of PackedSplats + splat_encode.rs + packed_splats.rs (CPU and GPU)
  WSplatLoader.h/.cpp         port of SplatLoader.getSplatFileType + async load
  WSplatPly.h/.cpp            port of ply.rs (standard 3DGS, SuperSplat compressed, point cloud)
  WSplatSpz.h/.cpp            port of spz.rs v1 to v3 (gzip via Qt's bundled zlib, #include <zlib.h>)
  WSplatSort.h/.cpp           port of sort.rs sort32 + latest-wins worker thread
  WSplatControls.h/.cpp       port of controls.ts FpsMovement + PointerControls
  shaders/
    splatDefines.glsl         port of splatDefines.glsl (functions verbatim)
    packedSH.glsl             port of PackedSplats.ts defineEvalPackedSH1/2/3 (verbatim)
    accumulate.vert/.frag     port of computeUvec4_Vec4.glsl + the expanded default dyno chain
    splat.vert / splat.frag   port of splatVertex.glsl / splatFragment.glsl
viewer/                       demo app: main.cpp + qml (SplatView, open file, drag and drop, stats)
test/
  splattest/                  headless Qt CLI: same scenes as Spark; dumps data, renders, compares
  reference/                  Node + Playwright harness running the real Spark
  scenes.json                 shared scene list (file, camera, mesh transform, size)
upstream/
  spark.pin                   tag + sha of the synced Spark version
  files.txt                   tracked upstream file -> our file mapping (read by tools/resync.sh)
  generated/                  Spark runtime snapshots (generated accumulator GLSL) for diffing
tools/resync.sh               diff of tracked upstream files between the pin and a new ref
implementation.md, doc/plan.md, resync.md
LICENSE, LICENSE.spark        Spark MIT notice kept for the ported code
```

Code style follows Sky (`WTextureVideo.cpp` in Sky):
- Every file opens with a `//====` banner and license header, and code is split into `//----`
  sections.
- Allman braces, 4-space indent, `Type * name`, aligned parameter lists, `NULL`.
- Public classes use the d-pointer pattern: Qt `Q_DECLARE_PRIVATE` with a local private class and
  `d->init()`, standalone without a dependency on Sky.
- Overrides are tagged `/* virtual */` in the .cpp.
- Every ported unit starts with a `// SPARK v2.3.1: <upstream path>` note, and each ported function
  keeps its upstream name.
- Comments use American spelling and never use `--` or a spaced dash. Conditions put one comparison per
  line with a leading `&&`.

## Key design points

1. **The RHI core is independent of Qt Quick.** `WSplatRenderer` takes a `QRhi *`, a
   `QRhiCommandBuffer *` and a `QRhiRenderTarget *`. `WSplatView` (through `QQuickRhiItemRenderer`)
   and `splattest` (headless QRhi) drive the same code.
2. **Accumulate pass.**
   - There is one `QRhiTextureRenderTarget` per array layer, each with two color attachments:
     - 0: an RGBA32UI splat array with `RenderTarget | TextureArray`.
     - 1: an RGBA8 metric array, also with `UsedAsTransferSource`.
   - All layers share one render pass descriptor and one pipeline. It has exactly two `TargetBlend`
     entries, both with blending off.
   - **Integer attachments are never cleared**, since the GL clear is undefined on them. Targets use
     `PreserveColorContents`, and the fullscreen triangle covers whole rows. Every texel is written:
     a zero splat and +INF metric past `numSplats`, as in Spark.
   - The row-range scissor (`UsesScissor`) converts from QRhi's bottom-left origin:
     `y = isYUpInFramebuffer() ? a : 2048 - b`.
   - The index is `layer*2048*2048 + int(gl_FragCoord.y)*2048 + x`.
   - The shader body is the dyno chain expanded by hand:
     1. `readPackedSplat`
     2. `evaluatePackedSH`
     3. `transformGsplat`
     4. Subtract `viewOrigin`.
     5. Halve the alpha.
     6. `packSplatEncoding`
     7. Write the metric.

     Each step is kept as a named function to match upstream.
3. **Readback and sort.**
   - `readBackTexture` reads the metric layer, limited to the used rows with `setRect`.
   - The heap-allocated `QRhiReadbackResult` callback runs on the render thread. It only moves the
     `QByteArray` to the sort thread.
   - While a readback or sort is pending, `render()` calls `update()`. On Vulkan, D3D12 and Metal the
     callback fires about 2 frames later, so frames must keep coming.
   - On GL the readback is a blocking `glReadPixels`. We keep it, since the port is faithful, and
     measure it.
   - The sort result reaches `render()` through a mutex-protected shared pointer. The ordering rows are
     uploaded there, and `activeSplats` becomes the instance count. One sort is in flight at a time.
4. **Draw.**
   - An instanced quad (4 vertices, 6 indices); `gl_InstanceIndex` (qsb uses Vulkan GLSL; base instance
     0 is fine on GL).
   - `splat.vert` keeps the upstream body for the packed path. The ext, cov and 2DGS branches and the
     three.js logdepth chunks are removed, and each cut is marked.
   - Blending is premultiplied `ONE, ONE_MINUS_SRC_ALPHA`, with no depth write.
   - Integer textures use a nearest sampler with no mipmaps.
   - The UBO uses only `vec4`/`mat4`/`int`, mirrored by a C++ struct.
5. **Projection.** The shader math uses a GL-convention projection, so Spark's clip and focal code
   stays verbatim. The final `gl_Position` is multiplied by `rhi->clipSpaceCorrMatrix()`, and
   `mirrorVertically` stays off.
6. **Shaders.**
   - qsb targets `--glsl "300 es,420" --hlsl 50 --msl 12`. Desktop GL needs 4.2 because SPIRV-Cross
     emits `packHalf2x16`.
   - Every file that includes another declares `#extension GL_GOOGLE_include_directive : require`, and
     includes are written `#include "splatDefines.glsl"`.
   - The .qsb outputs are listed through a `RESOURCES` variable so rcc runs after qsb.
7. **Capabilities checked at startup.**
   - Features: `Instancing`, `IntAttributes`, `TexelFetch`, `TextureArrays`.
   - Sizes: `TextureSizeMax >= 4096` (for the ordering texture) and `TextureArraySizeMax`.
   - Integer format support only warns: GL reports the 32UI formats as unsupported below ES 3.1, even
     though ES 3.0 can use them.
8. **QQuickRhiItem rules.**
   - `initialize()` runs on every sync, so it is guarded on rhi and size changes.
   - All passes are recorded in `render()`.
   - `synchronize()` copies the camera and parameters, and takes over newly loaded splat data.
   - Input: accepts all mouse buttons, `StrongFocus`, `forceActiveFocus` on press. Movement is driven
     from `QQuickWindow::afterAnimating`, with `update()` called only while keys or inertia are
     active.
9. **Loading.** `WSplatLoader` runs on a worker thread: detect the type from magic bytes or the
   extension, parse, then encode in batches into `WPackedSplats` CPU arrays. These are uploaded once on
   the render thread.
10. **Orientation.** The default mesh rotation is 180 degrees about X, matching Spark's examples. A
    `SplatView.orientation` enum covers OpenCV, OpenGL and ZUp.
11. **QML API (minimal).**
    - Properties: `source`, `status` / `progress` / `error`, `splatCount`, `fov`, `moveSpeed`,
      `maxStdDev`, `blurAmount`, `minAlpha`, `shDegree`, `orientation`, `backgroundColor`.
    - Invokables: `resetCamera()` and `lookAt()`, plus camera position and rotation.
    - Registered with `qmlRegisterType` (Sky style).

## Running Spark for comparison (test/reference)

This is a Node + Playwright harness in our repo. It never modifies the Spark checkout.

- `node test/reference/reference.mjs --spark $SPARK_DIR --ref v2.3.1`:
  1. Extracts `git archive <ref> dist` into `build/spark-<ref>/`. The dist bundle is committed
     upstream, so no npm or wasm build is needed. A `--build` option builds it from source in a
     temporary worktree when a ref has a stale dist; cargo and wasm-pack are available.
  2. Serves it, with `three@0.180.0` from npm, through a tiny static server.
  3. Launches Chromium with Spark's own SwiftShader flags (from `test/browser/playwright.config.ts`)
     for deterministic output.
- For each scene in `test/scenes.json`, it writes the following to `build/reference/<ref>/<scene>/`:
  - `render.png` (256x256, like Spark's own snapshot tests),
  - `packed.bin`, `sh1.bin`, `sh2.bin`, `sh3.bin` and `meta.json` (from `mesh.packedSplats`),
  - `accum.bin` and `metric.bin` (accumulator layer 0 read back),
  - `ordering.bin`.
- The generated accumulator GLSL (dyno program) is dumped to `upstream/generated/` for resync diffs.
- `npm run viewer -- <file>` serves a Spark viewer page (SparkControls) for manual side-by-side
  navigation against our viewer.
- **Fixtures:**
  - `furry-logo-pedestal.spz` (Spark's own test fixture) and `butterfly.spz`.
  - `fixtures.mjs` writes deterministic synthetic PLYs: standard SH0 and SH3, a SuperSplat compressed
    PLY, and a point cloud. Upstream ships no PLY.
- **`splattest` (Qt, headless; QRhi backend chosen with `--backend gl|vulkan|d3d11|d3d12`)** loads the
  same scenes, writes the same dumps and renders, then compares:
  - Packed and SH bytes must be **exact**.
  - The accumulator output is compared after decoding, within a small tolerance.
  - The metric is compared within a tolerance, and the ordering is checked to be back to front on it.
  - Images use PSNR and max-difference thresholds. The thresholds are set empirically, since we
    compare GPU output against SwiftShader.

  The process exits non-zero on failure.

## Performance comparison (goal: native beats Spark in the browser)

Spark 2.3.1 is WebGL2 only and has no WebGPU path. The baseline is therefore Spark on WebGL2 in Chrome,
on the **real GPU** (ANGLE D3D11 by default; `--use-angle=vulkan|gl` as variants). We compare it with
our native RHI backends.

- **`test/reference/bench.mjs`**
  - Runs Spark headed on hardware, with `--disable-gpu-vsync --disable-frame-rate-limit`.
  - Uses the same scenes and resolution as our side (1920x1080 by default), and a scripted camera path
    from `test/bench.json`: orbit plus a fly-through, N frames with fixed per-frame poses, so both sides
    see identical views.
  - Records:
    - frame time from rAF deltas;
    - GPU time via `EXT_disjoint_timer_query_webgl2` when exposed;
    - sort latency, by wrapping `readbackDepth` and the worker `sortSplats32` call;
    - load time, from `SplatMesh` creation to `initialized`.
- **`splattest --bench`**
  - Plays the same path on our renderer: offscreen at the same size, one run per backend.
  - Records:
    - CPU frame time;
    - GPU time (`QRhi::EnableTimestamps`, `lastCompletedGpuTime()`);
    - accumulate time;
    - readback latency;
    - sort time;
    - load time.
  - A windowed variant runs in the viewer (`--bench`, vsync off where the backend allows it) to include
    presentation.
- **Report.** `bench.mjs --compare` prints one table per scene with median and p95 for each metric,
  Spark against each backend, and writes JSON under `build/bench/`.
- **Protocol (laptop throttling).**
  - Interleave Spark and native runs (A/B/A/B), with at least 3 repeats.
  - Report medians, and re-run before claiming a result.
  - Use small and large captures, including a multi-million-splat scene.
- **Target.** Native must be at least as fast as Spark on every metric for every backend, with frame
  time clearly lower.
- **Performance levers, only if the numbers demand them.** Each is a documented deviation in
  `implementation.md`, can be switched off for faithful comparisons, and is re-checked with
  `splattest`.
  - A multithreaded radix sort.
  - A CPU-computed metric that skips the GL readback stall, for static sources.
  - Skipping re-accumulation when only the sort is stale.

## Resync (resync.md + tools/resync.sh)

- `upstream/spark.pin` holds the synced tag and sha. `upstream/files.txt` maps each tracked upstream
  file (shaders, `SparkRenderer.ts`, `SplatAccumulator.ts`, `dyno/splats.ts`, `dyno/output.ts`,
  `PackedSplats.ts`, `utils.ts`, `defines.ts`, `controls.ts`, `ply.rs`, `spz.rs`, `splat_encode.rs`,
  `packed_splats.rs`, `sort.rs`) to its port.
- `tools/resync.sh $SPARK_DIR <new-ref>` prints, per tracked file, `git diff <pin>..<new-ref>` and the
  port file to update. It also lists new or deleted upstream files under `src/` and `rust/`.
- Procedure:
  1. Run the diff.
  2. Port the hunks.
  3. Regenerate the reference at the new ref, re-dump the generated GLSL, and diff it.
  4. Run `splattest` on every backend.
  5. Bump the pin.
- Known first-resync items (v2.3.1 to HEAD):
  - `splatVertex.glsl`: the eigen decomposition was rewritten (#486).
  - `spz.rs`: SPZ v4 (needs zstd) and flate2.
  - SparkRenderer: render size and the `mappingChanged` sort guard.
  - `encodeExtRgb`: ext only, so it does not affect us.

## Docs

- `doc/plan.md`: this plan, without local paths.
- `implementation.md`: the pipeline in detail, with data layouts, thread model, frame flow, the
  accumulator rotation state machine, per-backend notes (scissor, readback timing, GL stall), shader
  walkthrough, controls and the deviations from Spark.
- `resync.md`: as above.

## Phases

1. **Skeleton.** qmake subdirs, the qsb rule, an empty `SplatView` clearing to a color in the viewer,
   and the capability checks, on every backend.
2. **Reference harness.** `test/reference` + fixtures + scenes; Spark dumps produced for v2.3.1.
3. **Loading.** `WPackedSplats`, `WSplatPly`, `WSplatSpz`, `WSplatLoader`; `splattest` byte-exact
   against Spark's packed and SH dumps.
4. **Rendering.** Accumulator, readback, sort, ordering and draw; `splattest` accumulator, metric and
   image compares on all backends.
5. **Navigation.** `WSplatControls` + the QML API + the viewer app.
6. **Benchmark.**
   - `bench.mjs` + `splattest --bench` + the viewer `--bench`.
   - First comparison against Spark/WebGL2. Apply performance levers until native leads on every
     backend.
   - D3D11 per-resource size limits: if a large array texture fails, fall back to one texture per
     layer.
7. **Docs and resync tooling.** `implementation.md`, `resync.md`, `tools/resync.sh`,
   `upstream/files.txt`, and a dry run of the resync against the upstream HEAD.

## Verification

- Build: `qmake splat.pro && mingw32-make` with the Qt 6.10.1 MinGW kit (mingw 13.1).
- `node test/reference/reference.mjs --spark $SPARK_DIR --ref v2.3.1` produces the reference dumps.
- Run `splattest --backend X --reference build/reference/v2.3.1` for each of gl, vulkan, d3d11 and
  d3d12. It must pass.
- `node test/reference/bench.mjs --spark $SPARK_DIR --ref v2.3.1 --compare` reports native ahead of
  Spark/WebGL2 on every metric, as interleaved medians over 3 or more repeats.
- Viewer, manually: open a PLY and an SPZ with `QSG_RHI_BACKEND=opengl|vulkan|d3d11|d3d12`. Navigate
  with WASD/QE plus Shift and Ctrl, mouse left drag (rotate), right and middle drag (slide), and the
  wheel. Compare side by side with `npm run viewer`, and check FPS on a multi-million-splat capture.
