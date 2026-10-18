# Splat implementation

Splat is a C++ port of [Spark](https://github.com/sparkjsdev/spark) (MIT, THREE.js), a 3D Gaussian
splat renderer. It runs on Qt 6.10 RHI, so the same code renders with Vulkan, Direct3D 11 and 12,
Metal, OpenGL 4.2+ and OpenGL ES 3.0. The port is faithful: the encodings, the shader math, the two
pass pipeline and the sort are Spark's, so an upstream change maps to a known place in this code.

The synced Spark version is in `upstream/spark.pin` (currently `v2.3.1`). How to follow upstream is
in [resync.md](doc/resync.md).

## Scope

Ported:

- **Loading:**
  - PLY: standard 3DGS, SuperSplat / PlayCanvas compressed, point clouds.
  - SPZ: versions 1 to 3.
- **Storage:** PackedSplats, 16 bytes per splat, with spherical harmonics degree 1 to 3.
- **Rendering:** the accumulator pass, GPU metric readback, the CPU radix sort, and the instanced
  splat draw.
- **Navigation:** the keyboard and mouse parts of SparkControls.

Not ported, out of scope for now:

- LoD trees and paged splats (`SplatPager`, `PagedSplats`, `.rad`).
- ExtSplats and CovSplats.
- 2DGS, depth of field, and SplatEdit / dyno modifiers.
- WebXR, gamepads and touch.
- The other formats: ksplat, splat, sog, rad.
- SPZ v4: zstd, added after 2.3.1.

## Layout

| Path | Role | Upstream |
|---|---|---|
| `src/Splat.h` | export macro, texture constants | `src/defines.ts` |
| `src/WSplatReceiver.h` | decoder to storage interface | `rust/spark-lib/src/decoder.rs` |
| `src/WSplatMath.*` | Rust float semantics and libm ports, for byte exact decoding | the Rust `libm` crate |
| `src/WPackedSplats.*` | 16 byte encoding, SH packing, texture sizes | `splat_encode.rs`, `packed_splats.rs`, `utils.ts` |
| `src/WSplatPly.*` | PLY decoder | `rust/spark-lib/src/ply.rs` |
| `src/WSplatSpz.*` | SPZ decoder | `rust/spark-lib/src/spz.rs` |
| `src/WSplatLoader.*` | file type detection, loading | `src/SplatLoader.ts`, `decoder.rs` |
| `src/WSplatSort.*` | 32 bit radix sort | `rust/spark-rs/src/sort.rs` |
| `src/WSplatAccumulator.*` | per view accumulator targets and pass | `src/SplatAccumulator.ts` |
| `src/WSplatRenderer.*` | the renderer core, independent of Qt Quick | `src/SparkRenderer.ts` |
| `src/WSplatRendererCapture.cpp` | readback of the displayed accumulator, for `splattest` | |
| `src/WSplatControls.*` | keyboard and mouse navigation | `src/controls.ts` |
| `src/WSplatView.*` | the QML item, a `QQuickRhiItem` | |
| `src/shaders/` | GLSL baked by qsb at build time | `src/shaders/*.glsl` |
| `viewer/` | demo application | |
| `test/splattest/` | headless tests and benchmark (`rhi`, `scene`, `compare`, `bench`) | |
| `test/common/WSplatBench.h` | the camera orbit and statistics shared by both benchmarks | |
| `test/reference/` | runs the real Spark for comparisons | |

Every ported unit opens with a `// SPARK v2.3.1: <upstream path>` line, and every ported function
carries a `// SPARK: <upstream name>` comment. Changes inside verbatim shader bodies are tagged
`// SPLAT:`.

## Pipeline

```
 file ──► WSplatLoader (worker thread)
            │  WSplatPly / WSplatSpz decode into WSplatReceiver batches
            ▼
          WPackedSplats  (CPU: packed u32x4 per splat + sh1/sh2/sh3 words)
            │  uploaded once (render thread)
            ▼
          source textures: RGBA32UI array (splats), RG32UI / RGBA32UI arrays (SH)
            │
  per view  ▼
          accumulate.frag ──► accumulator: RGBA32UI splats + RGBA8 metric (MRT, one pass/layer)
            │                                    │
            │                                    └─► readBackTexture ─► WSplatSort (worker)
            │                                                              │
            ▼                                                              ▼
          splat.vert / splat.frag ◄──────────────────────── ordering texture (RGBA32UI)
            │  instanced quads, back to front, premultiplied blending
            ▼
          render target (QQuickRhiItem texture, or offscreen in splattest)
```

### 1. Loading

`WSplatLoader::getSplatFileType` detects the format like `SplatLoader.getSplatFileType`: the `ply`
magic, a gzip stream starting with `NGSP` for SPZ, then the extension. `WSplatLoader::loadFile` maps
the file in memory with `QFile::map`, and the decoders read it in place.

The decoders are line by line ports of the Rust ones and push batches into a `WSplatReceiver`, the
port of the Rust `SplatReceiver` trait. `WPackedSplats` implements it the way `PackedSplatsData`
does:

- `initSplats` sizes the arrays for a full texture array.
- `setBatch` encodes each splat with `encodePackedSplat`.
- `setSh1` / `setSh2` / `setSh3` pack the SH coefficients.

`WSplatView` runs the load on a worker thread with `QtConcurrent`.

- **Switching scenes:** a new `source` frees the current splats before loading, so two scenes
  never live together. Without a mesh, the renderer also frees the accumulators and the ordering
  (`releaseTargets`), which only grow while a scene is shown, like in Spark.
- **Aborting:** a new or empty `source` cancels the load in progress. `WPackedSplats::canceled`
  is polled once per batch, and throws `WSplatCanceled` from the receiver, so the ported decoders
  stay unchanged: `WSplatLoader::decode` catches it, and the decoders unwind through their RAII
  buffers. A load stops within about 25 ms.

### 2. Encoding

Each splat is 4 x uint32, the PackedSplats layout:

| Word | Bits |
|---|---|
| 0 | r, g, b, a as 8 bit, `(v - rgbMin) / (rgbMax - rgbMin)` (alpha 0..1, or 0..2 with `lodOpacity`) |
| 1 | half(x) low 16, half(y) high 16 |
| 2 | half(z) low 16, quat u bits 16..23, quat v bits 24..31 |
| 3 | scale x, y, z as 8 bit, quat angle bits 24..31 |

- **Scales:** an 8 bit log encoding. 0 means a zero scale; otherwise
  `1 + round((ln s - lnMin) / (lnMax - lnMin) * 254)`, with lnMin -12 and lnMax 9.
- **Quaternion:** `encodeQuatOctXy88R8`. The axis goes through a folded octahedral mapping into u and
  v, the angle into 8 bits.
- **Spherical harmonics:** coefficient major with rgb interleaved (`sh[k * 3 + channel]`):
  - sh1: 9 signed 7 bit values in 2 words.
  - sh2: 15 signed bytes in 4 words.
  - sh3: 21 signed 6 bit values in 4 words.
  - All are scaled by `shNMax`, which is 1 for PLY and SPZ.

Textures are 2D arrays, 2048 wide. `getTextureSize` picks the height (up to 2048) and the number of
layers. Index `i` lives at `(i & 2047, (i >> 11) & 2047, i >> 22)` (`splatTexCoord`). The CPU
arrays are sized to the whole texture so every layer uploads in one piece.

### 3. Accumulate (per view)

`WSplatRendererPrivate::updateInternal` is the port of `SparkRenderer.updateInternal`. A new accumulator is
generated when the camera moved more than 0.001, turned by more than `acos(0.999)`, or when the
mesh version changed:

- the transform or recolor changed;
- the splats changed;
- with SH, the camera position in object space changed.

The accumulator pass is a fullscreen triangle per array layer, scissored to the rows that hold
splats. One fragment is one splat:
`index = layer * 2048 * 2048 + int(gl_FragCoord.y) * 2048 + int(gl_FragCoord.x)`.

`accumulate.frag` is the program Spark generates at runtime with its dyno graph, for a SplatMesh
reading PackedSplats, expanded by hand. Each dyno node is a function with the node name:

1. `readPackedSplat`: texelFetch and `unpackSplatEncoding`. Zero scales mark the splat inactive.
2. `fetchSplat` / `evaluatePackedSH`: adds the SH color for
   `viewDir = normalize(center - cameraInObject)`.
3. `transformGsplat`: object to world, with a uniform scale (the SplatTransformer averages the
   axes), rotation and translation.
4. Recolor: `rgba *= (recolor, opacity)`.
5. `outputPackedSplat`: packs the center relative to the view origin (fp16 stays precise near the
   camera) and alpha * 0.5 (the expanded LoD opacity encoding).
6. `outputSplatDepth`: the sort metric, `length(center - viewCenter)` by default
   (`dot(center - viewCenter, viewDir) + 100` when `sortRadial` is false), written as float bits
   into the RGBA8 attachment. Inactive splats get +INF.

The generated upstream GLSL is kept in `upstream/generated/` (dumped by the reference harness), so
a dyno change upstream shows up as a diff there.

Three accumulators rotate exactly as in Spark:

- `display` is drawn.
- `current` is the newest.
- The rest is a free list.

A new accumulator with the same mapping (same splat count) is displayed immediately, reusing the
previous sort order: its center offsets are fresh, only the order lags. When the mapping changed, the
new one waits for its own sort before it is displayed.

#### Backend notes

- **Integer clears:** integer attachments are never cleared, because a GL clear on them is
  undefined. The render targets use `PreserveColorContents`, and the pass writes every texel of the
  rows it covers (a zero splat and +INF metric past the count), like the upstream template.
- **Scissor:** `QRhiScissor` has a bottom left origin. Texel rows follow `gl_FragCoord.y` on every
  backend, so the scissor rows are flipped when the framebuffer is not y up
  (`y = height - rowEnd`).
- **Blending:** off for both attachments: the pipeline has two default `TargetBlend` entries.

### 4. Readback and sort

`WSplatRendererPrivate::driveSort` is the port of `SparkRenderer.driveSort` and `readbackDepth`:

1. One sort runs at a time. When an accumulator was generated, the metric of every layer is read
   back with `QRhiResourceUpdateBatch::readBackTexture`, limited to the used rows.
2. The completed callback runs on the render thread. It starts the sort on a worker thread
   (`QtConcurrent`).
3. The sort is `sort32`: two LSD radix passes of 16 bits on the inverted keys, which gives a
   descending order (back to front). Keys >= +INF are dropped, and the count kept is `activeSplats`.
4. On a later frame the ordering is uploaded into the ordering texture: RGBA32UI, 4096 wide, 4
   indices per texel, so 16384 per row.

Readback timing per backend:

| Backend | Completed callback |
|---|---|
| OpenGL | same frame, `glReadPixels` blocks the render thread |
| D3D11 | when a poll finds the copy done, usually 1 to 3 frames later |
| Vulkan, D3D12, Metal | when the frame slot is reused, about 2 frames later |

- **D3D11:** QRhi ends a D3D11 readback with a blocking `Map` at the next frame. The CPU queues a
  few frames ahead, so that `Map` waited for all of them: 90 to 200 ms freezes on large scenes.
  `WSplatReadback::requestNative` copies the rows into staging textures through the native
  device, and `pollNative` maps them with `D3D11_MAP_FLAG_DO_NOT_WAIT` every frame. This is what
  WebGL2 does for Spark: a copy into a buffer, then a fence polled every frame (three.js
  `probeAsync`).
- **Frames:** while a readback or sort is pending, `needsUpdate()` is true and the view requests
  frames, since without new frames the callbacks never run. But only while the camera is still:
  when it moves, whoever moves it already requests the next frame, and one more would render the
  same pose twice. On Vulkan and D3D12 that duplicated every other frame, so the motion jumped by
  two poses, then stood still. `WSplatViewPrivate::motionTimer` restarts on every move and
  requests one frame 100 ms after the last, so a pending sort still completes.

### 5. Draw

`splat.vert` and `splat.frag` are the upstream `splatVertex.glsl` and `splatFragment.glsl` bodies.
The ExtSplats, CovSplats and 2DGS branches and the three.js logdepth chunks are cut, and every
change is tagged `SPLAT:`.

- **Geometry:** one instanced quad (corners -1..1, 6 indices), with `instanceCount = activeSplats`.
  The instance index reads the ordering texture, then the displayed accumulator.
- **Projection:** the shader math stays in OpenGL clip space, so the upstream frustum tests and
  `focal = 0.5 * renderSize * (P[0][0], P[1][1])` are unchanged. `projectionMatrix` is a GL
  perspective, and only the final `gl_Position` is multiplied by `QRhi::clipSpaceCorrMatrix()`.
- **Covariance and footprint:** the 2D covariance, the 0.3 pixel antialias blur with opacity
  compensation, the eigen decomposition and the quad sized to `maxStdDev` standard deviations are
  upstream's.
- **Blending:** premultiplied (`One, OneMinusSrcAlpha`) with no depth. The colors are written as is,
  like `encodeLinear = false` for an sRGB output: the Qt Quick texture is RGBA8 without sRGB
  encoding.

Uniforms live in one std140 block, `splatUniforms.glsl`, with no instance name, so the upstream
bodies keep their names. Booleans are ints wrapped by macros. The C++ mirror is
`WSplatDrawUniforms`.

### 6. Shaders

The shaders are written in Vulkan GLSL 440, and `src/shaders.pri` bakes them with qsb at build time
into `:/splat/shaders/*.qsb`:

- **Targets:** `--glsl "300 es,420" --hlsl 50 --msl 12`. Desktop GL needs GLSL 4.20, because
  SPIRV-Cross emits `packHalf2x16` / `unpackHalf2x16`.
- **Includes:** files use `#include "splatDefines.glsl"`, with
  `GL_GOOGLE_include_directive`.
- **`splatDefines.glsl`:** a verbatim copy of upstream, apart from a header comment.
- **`packedSH.glsl`:** the three `defineEvalPackedSH*` template strings, verbatim.
- **Branches on D3D:** fxc flattens small `if` / `else` blocks into selects, so both sides run
  for every pixel. In `splat.frag` that is the two falloff formulas and the sRGB conversion,
  about ten transcendental operations where one or two are needed. The NVIDIA D3D driver does not
  restore the branches, unlike its Vulkan compiler. `[[dont_flatten]]`
  (`GL_EXT_control_flow_attributes`) on these two branches becomes `[branch]` in the HLSL. The
  math is unchanged and the images are byte identical, but the fragment shader costs half as
  much on D3D11 and D3D12. ANGLE flattens them too, so Spark pays this cost on Windows.

### 7. Threads (QQuickRhiItem)

| Thread | Work |
|---|---|
| GUI | properties, input, controls (`QQuickWindow::afterAnimating`), load completion |
| Loader (`QtConcurrent`) | file decode, encode, bounds |
| Render | `WSplatViewRenderer::initialize / synchronize / render`, all RHI calls, readback callbacks |
| Sort (`QtConcurrent`) | `sort32` |

- `synchronize()` runs while the GUI thread is blocked. It copies the mesh (a
  `QSharedPointer<WPackedSplats>`), camera and options into the `WSplatRenderer`, and copies the
  stats back.
- Sort results reach the render thread through the `QFuture` of the job, polled in `render()`.

### 8. Navigation

`WSplatControls` ports `FpsMovement` and `PointerControls` with every upstream constant and
equation. The camera looks down -Z with +Y up, and Euler angles are YXZ as in three.js.

| Input | Action |
|---|---|
| W A S D, arrows | move forward, left, back, right |
| Q / E, PageDown / PageUp | down / up |
| Home / End, Insert / Delete | yaw / pitch |
| Shift, Ctrl | speed x5, x1/5 |
| Left drag | rotate, with inertia |
| Right drag | slide in the view plane |
| Middle drag | slide in the horizontal plane |
| Wheel | move along the view axis (one notch = 0.15) |

Qt keys follow the keyboard layout (on AZERTY, forward is Z), while browser key codes are physical
keys.

`SplatView.orientation` rotates the mesh:

- `OpenCV` (the default) is 180 degrees about X, like `quaternion.set(1, 0, 0, 0)` in Spark's
  examples.
- `OpenGL` is the identity.
- `ZUp` is Spark's editor Z up preset.

`resetCamera()` frames a robust bounding sphere of the centers: the median, and the 90th percentile
distance.

## QML

```qml
import Splat 1.0

SplatView {
    anchors.fill: parent
    source: "file:///path/scene.spz"
    focus: true
}
```

| Group | Members |
|---|---|
| Properties | `source`, `backgroundColor`, `orientation`, `cameraPosition`, `cameraRotation`, `fov`, `zNear`, `zFar`, `controlsEnabled`, `moveSpeed`, `maxStdDev`, `blurAmount`, `minAlpha`, `shDegree` |
| Read only | `status` (Null, Loading, Ready, Error), `error`, `splatCount`, `activeSplats`, `fps`, `sortTime`, `readbackTime`, `loadTime`, `backendName` |
| Methods | `resetCamera()`, `lookAt(point)` |

`backgroundColor` accepts alpha: with a translucent color (`"transparent"`), the scene blends
over the items behind the view, soft splat edges included. Qt Quick only adds that blend when the
alpha is below 255.

The application registers the type with
`qmlRegisterType<WSplatView>("Splat", 1, 0, "SplatView")`. The backend is chosen by Qt Quick
(`QSG_RHI_BACKEND`, or `QQuickWindow::setGraphicsApi`). For OpenGL, request a 4.3 core context (see
`viewer/main.cpp`).

## Deviations from Spark

Each deviation is deliberate and small:

- **Ordering:** read from the ordering texture with `gl_InstanceIndex` (Vulkan GLSL), instead of
  `gl_InstanceID`. The base instance is always 0.
- **Uniforms:** booleans are ints in the uniform block.
- **Accumulate pass:** a single triangle instead of three.js' `FullScreenQuad` mesh. Same coverage.
- **Clip space:** the final `clipSpaceCorrMatrix` multiply maps OpenGL clip space to the backend's.
- **Accumulator rotation:** a frame based state machine instead of `async` / `await`. The
  readbacks complete through QRhi callbacks, the sort through a `QFuture`. The decisions (when to
  generate, when to swap `display`) follow upstream line by line.
- **Sort cycle:** Spark starts the next readback as soon as a sort lands, between frames, and
  polls its readback fence every few milliseconds. Qt records GPU work only inside a frame, so
  each step waits for the next one: a cycle takes about 3 frames, against about 1.5 for Spark.
  While the camera moves, the order trails it a little more; once it stops, both converge to the
  same image. `SplatView.fast` sorts more often than Spark.
- **Depth test:** off, since the splats are the only content of their render target.
- **Mesh:** a single mesh per view. Spark's multi mesh mapping reduces to one generator at base 0.
- **Shaders:** `readPackedArray` takes `inout Gsplat` (`out` upstream), and `readPackedSplat`
  zero-initializes its struct. HLSL rejects an `out` parameter left unwritten on one branch.
  `splat.frag` marks its falloff and `encodeLinear` branches `[[dont_flatten]]` (see
  [Shaders](#6-shaders)).

## Performance options

`WSplatRenderOptions` has three Splat-only options. **They are off by default: the default is the
exact Spark code path.** None changes the image. The `fast` property of `SplatView`, and the
`--fast` flag of `splatviewer` and `splattest`, turn all three on. `splattest --enable a,b` turns on
single ones, for A/B runs.

- **`tightBounds`:** empty quad culling in `splat.vert`. A quad whose pixel bounding box (grown
  by 1/64 pixel, coarser than the rasterizer snapping) holds no pixel center produces no
  fragment. It is culled before the rasterizer.
  - In large scenes most splats are below a pixel, and their 4M triangles cost more than the
    vertex shading: about 5 to 7 ms out of 13 to 15 for 2M splats at 1080p on this laptop.
  - The image is bit identical (`splattest` checks it on every scene).
  - Opacity aware bounds, which shrink the quads of faint splats, were tried and dropped. They
    changed up to 3 levels on edge pixels (interpolation and snapping at the new edges), for a 2%
    gain.
- **`cpuSortMetric`:** the sort metric (`outputSplatDepth`: the world center minus the view
  origin, as a length) is computed on the sort thread instead of being read back from the GPU.
  - It uses the float centers decoded once at load (`WPackedSplats::finish`) and the transform
    the accumulator was generated with.
  - The sort starts in the same frame, and the readback stalls are gone: a blocking
    `glReadPixels` on OpenGL, and a blocking `Map` at the next frame on Direct3D 11.
  - The order is the same as with the readback, up to float rounding between ties.
  - The accumulator still writes the metric attachment, so `splattest` can compare it.
- **`fastSort`:** a faster radix sort, `WSplatSort::sort32SplatsFast`, plus a multithreaded CPU
  metric.
  - A stable LSD radix over the inverted keys yields one unique ordering, whatever the digit
    size. So three passes of 11 bit digits (2048 buckets, which fit in the L1 cache) give
    exactly the result of upstream's two 16 bit passes, whose 65536 bucket scatter is memory
    bound.
  - It is used from a million splats, on up to 8 threads of a persistent pool: per thread
    histograms, with bucket major then thread minor offsets, which keeps every pass stable.
    Below that, upstream's serial `sort32` is faster and is used as is.
  - `splattest --sort-bench` times both (on this laptop: 12.6 ms against 7.1 ms at 2M splats,
    51 ms against 27 ms at 6M; Spark's wasm sort takes about 26 ms at 2M).
  - `splattest` checks the ordering against `sort32` on every scene, on 1 and 8 threads.

## Exact math

Spark's decoders run as Rust compiled to WASM, so byte exact output needs Rust's float behavior,
not the C runtime's:

- **`exp`, `ln`, `acos`, `sin`:** Rust links the libm crate (FreeBSD msun ports). The MinGW CRT
  differs on many inputs. `WSplatMath::expf` / `logf` / `acosf` / `sinf` are exact ports, checked
  against Rust over all 2^32 inputs.
- **Rounding:** MinGW's `roundf(0.49999997f)` returns 1. The encoders use `WSplatMath::round`, half
  away from zero, which matches `f32::round`.
- **Half floats:** `floatToHalf` / `halfToFloat` port the `half` crate fallbacks bit for bit (round
  to nearest even, subnormals, inf, NaN).
- **Casts:** saturating `as u8` / `as i8` casts are mirrored (`WSplatMath::castU8` / `castI8`),
  NaN goes to 0.
- **FMA contraction:** disabled per file, so builds with FMA stay exact.
- **Gzip:** the gzip header is parsed like upstream, and the stream is inflated raw. Like
  Spark's miniz path, the CRC and size trailer are not checked.

## Platform notes

- **Direct3D 12:** the MinGW build of Qt 6.10.1 has no D3D12 backend (its SDK headers are too
  old). The MSVC kit has it: `sh build.sh release msvc` builds into `bin/msvc` and `lib/msvc`,
  next to the MinGW build. Both kits pass the same tests, and the loaders are byte exact with both
  compilers.
- **Vulkan headers:** the viewer needs none, since Qt Quick creates the Vulkan instance.
  `splattest` creates its own `QVulkanInstance`, which needs `vulkan/vulkan.h`. The headers come
  from `$VULKAN_SDK`, or from the Khronos Vulkan-Headers in `build/3rdparty`.
- **Hybrid GPUs:** the viewer and `splattest` export `NvOptimusEnablement` and
  `AmdPowerXpressRequestHighPerformance`, so laptops run them on the discrete GPU.
  `splattest --adapter N` picks another D3D adapter or Vulkan device.

## Testing

- `test/reference` runs the real Spark (the committed `dist` bundle of the pinned tag) in Chromium.
  It writes reference dumps and images, and benchmark baselines on the hardware GPU.
- `splattest` renders the same scenes on any QRhi backend and compares them:
  - packed and SH words must be identical;
  - the accumulator output is compared after decoding, within one quantization step;
  - the metric within 1e-4 relative;
  - the ordering must be back to front on Spark's metric;
  - images by PSNR.
- `test/reference/bench.mjs` and the native benchmarks play the same camera orbit for the
  performance comparison. There are two native benchmarks:
  - `splatviewer --bench`: windowed, in a real swapchain with vsync off
    (`QSurfaceFormat::swapInterval 0`) and GPU timestamps (`QSG_RHI_PROFILE`). This is the one
    comparable to Spark's headed Chromium run, since both keep frames in flight.
  - `splattest --bench`: offscreen frames, which wait for the GPU every frame. It is useful for
    the per-frame latency, but it is not a throughput measure.

See [../test/reference/README.md](test/reference/README.md) for the commands and file formats.
