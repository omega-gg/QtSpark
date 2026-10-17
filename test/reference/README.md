# Spark reference harness

A Node + Playwright harness that runs the **real Spark** (sparkjsdev/spark, default ref
`v2.3.1`) in Chromium to produce:

- reference dumps the native port (`test/splattest`) compares against, byte for byte or within
  a tolerance;
- performance baselines on the hardware GPU.

It never modifies the Spark checkout. `git archive <ref> dist examples/assets.json` is extracted
into `build/spark-<ref>/`; the dist bundle is committed upstream, so no npm or wasm build is
needed. Everything generated goes under `build/`, which is gitignored.

## Setup

```sh
cd test/reference
npm install
npx playwright install chromium
```

The Spark checkout is passed with `--spark <dir>` or the `SPARK_DIR` environment variable.

## Commands

| Command | What it does |
| --- | --- |
| `node fixtures.mjs [--spark <dir>] [--force]` | Writes the synthetic PLYs and downloads the sample `.spz` files into `build/fixtures/`. |
| `node reference.mjs --spark <dir> [--ref v2.3.1] [--scene name] [--headed]` | Renders every scene of `test/scenes.json` in SwiftShader Chromium and writes `build/reference/<ref>/<scene>/`. Exits 1 if a scene's fixture is missing. |
| `npm run viewer -- <fixture or file> [--port 8095] [--no-open]` | Serves `www/viewer.html` (SparkControls, full window, hardware GPU) and opens it in the default browser. |
| `node bench.mjs --spark <dir> [--width 1920 --height 1080] [--angle d3d11\|vulkan\|gl] [--repeats 3] [--frames N] [--warmup N] [--scene a,b] [--in-flight 2] [--low-power] [--output file]` | Benchmarks Spark headed on the hardware GPU and writes `build/bench/spark-<ref>-<angle>-<UTC timestamp>.json`, or `--output`. |
| `node bench.mjs --compare native.json... [--baseline spark.json]` | Prints one table per scene: Spark against each native file. The baseline defaults to the newest `build/bench/spark-*.json`. Add `--spark` to run a fresh benchmark first. |
| `node summary.mjs result.json...` | Medians over rounds of the wall time, GPU time and frame interval p95, per scene and backend. `tools/bench.sh` runs it. |

`reference.mjs` and `bench.mjs` call the fixture step themselves. `--build` (on `reference.mjs`
and `bench.mjs`) is an **untested TODO** for refs whose committed dist is stale. It sets up a
git worktree in `build/spark-src-<ref>/`, runs `npm ci`, `npm run build:wasm` and
`npm run build` there, and then copies the results to `build/spark-<ref>/`.

The static server maps `/spark/` to `build/spark-<ref>/dist`, `/three/` to
`node_modules/three` (three@0.180.0), `/fixtures/` to `build/fixtures` and `/` to `www/`.
The pages use an import map for `three`, `three/addons/` and `@sparkjsdev/spark`.
`PUT /out/<path>` writes dump files under the output directory.

## Fixtures (`build/fixtures/`)

| File | Content |
| --- | --- |
| `furry-logo-pedestal.spz`, `butterfly.spz` | Downloaded from the URLs in Spark's `examples/assets.json` (sparkjs.dev). |
| `synthetic-sh0.ply` | 20000 splats, standard 3DGS: `x y z nx ny nz f_dc_0..2 opacity scale_0..2 rot_0..3`, all float. Log scales are -4.5 to -1, logit opacity -4 to 4, quaternions are not normalized and `f_dc` is -1 to 2. |
| `synthetic-sh3.ply` | The same splats, plus `f_rest_0..44` (planar, ±0.3) between `f_dc_2` and `opacity`. |
| `synthetic-compressed.ply` | 10240 splats (40 chunks) in the SuperSplat/PlayCanvas compressed format, with an `sh` element (45 uchar `f_rest_*`). |
| `synthetic-points.ply` | 50000 points, `x y z` float and `red green blue` uchar. Spark gives each point a scale of 0.001, so the render is faint. |

All PLYs are `binary_little_endian 1.0` and come from a seeded PRNG (mulberry32), so they are
byte-identical on every run.

The compressed layout follows `rust/spark-lib/src/ply.rs` at v2.3.1:

- `chunk` (one per 256 splats): float `min_x..max_z`, `min_scale_x..max_scale_z` (log scale) and
  `min_r..max_b` (color `0.5 + f_dc * SH_C0`).
- `vertex`: uint `packed_position`, `packed_rotation`, `packed_scale` and `packed_color`.
  - `packed_position` and `packed_scale` are 11/10/11 bits (x in the high bits), normalized to
    the chunk range.
  - `packed_rotation` holds the index of the largest component in bits 30..31, in (w, x, y, z)
    order and made positive. The other three follow in that order as 10 bits each, mapped from
    [-1/sqrt2, 1/sqrt2].
  - `packed_color` is r, g, b and opacity, 8 bits each from the high byte down. The opacity is
    linear, not logit.
- `sh`: uchar `f_rest_*` in planar layout, decoded as `v * 8 / 255 - 4`.

As a check, a standard PLY of the same splats was loaded in Spark and compared with the
compressed file. They agree within quantization: centers within 1e-3, log scales within one
8-bit step, colors within 1/255, quaternion |dot| at least 0.9997.

## Scenes (`test/scenes.json`)

This file is shared with the C++ side.

- `size`: [width, height] of the render.
- `scenes[]`:
  - `name`, `file` (in `build/fixtures`).
  - `camera`:
    - `fov`: vertical, in degrees.
    - `near`, `far`.
    - `position`.
    - Either `lookAt`, which uses three.js `Object3D.lookAt` (camera -Z toward the target,
      up +Y), or `quaternion` [x, y, z, w].
  - `mesh`: `position`, `quaternion` [x, y, z, w] and a uniform `scale`.

`furry-transformed` is Spark's `basic.test.ts` "object and camera transforms" case. Its Euler
XYZ rotation [180, -20, 8] degrees was converted with three.js `Quaternion.setFromEuler`.

## Reference output (`build/reference/<ref>/<scene>/`)

Each scene uses:

- SwiftShader Chromium with Spark's own flags: `--use-angle=swiftshader --use-gl=angle
  --enable-unsafe-swiftshader --ignore-gpu-blocklist`;
- a 256x256 canvas with pixelRatio 1 and `preserveDrawingBuffer`;
- `new SparkRenderer({ renderer, onDirty })` with default options.

The harness waits as Spark's `test/browser/harness.ts` `settle()` does. All meshes must be
initialized, with no render scheduled, no sort running or dirty, no LoD or pager work, and
`activeSplats` unchanged, for two consecutive frames.

All `.bin` files are raw little-endian, with no header. Here `n` is `numSplats`.

| File | Layout |
| --- | --- |
| `render.png` | 256x256 RGBA PNG of the canvas from `canvas.toDataURL()`, top row first (WebGL's bottom-up rows are already flipped). |
| `packed.bin` | `mesh.packedSplats.packedArray`, `n * 4` u32. See the PackedSplat word layout below. |
| `sh1.bin` | `packedSplats.extra.sh1`, `n * 2` u32 (only when SH degree is at least 1). 9 coefficients (`coef * 3 + channel`), 7-bit signed each, bit-packed from bit 0 of word 0. Value = `code * sh1Max / 63`. |
| `sh2.bin` | `extra.sh2`, `n * 4` u32. 15 coefficients as int8 bytes 0..14 (byte 15 is 0). Value = `code * sh2Max / 127`. |
| `sh3.bin` | `extra.sh3`, `n * 4` u32. 21 coefficients, 6-bit signed each, bit-packed. Value = `code * sh3Max / 31`. |
| `accum.bin` | The displayed accumulator's splat target (`spark.display.target`, attachment 0, RGBA32UI 2D array). `accumulator.numSplats * 4` u32. With the default `accumExtSplats: false`, these are PackedSplat words in accumulator space: centers are relative to `viewOrigin`, the transform is applied and SH is evaluated into the color. |
| `metric.bin` | The accumulator's attachment 1 (RGBA8). `accumulator.numSplats` u32, where each u32 is the bit pattern of a float32 sort metric (larger means farther). |
| `ordering.bin` | `activeSplats` u32 splat indices, back to front. This is the last `sortSplats32` result, which is what Spark uploaded to `orderingTexture`. |
| `accumulator.frag.glsl`, `accumulator.vert.glsl` | `SplatAccumulator.fullScreenQuad.material` sources (dyno-generated, with `#include <splatDefines>`). |
| `accumulator.compiled.frag.glsl` | The fragment source WebGL compiled (`gl.getShaderSource`), with three's prefix and includes resolved. |
| `meta.json` | See below. |

Texel `i` of the accumulator target is at layer `i / 2048^2`, row `(i / 2048) % 2048` and
column `i % 2048`. It is read back with `readRenderTargetPixelsAsync` on that layer (rows
bottom-up, which is texture order), so the dumps are in splat index order.

The PackedSplat word layout (`src/utils.ts` `setPackedSplat` / `unpackSplat`):

- word 0: R, G, B and A as u8 from the low byte up. RGB is mapped to [rgbMin, rgbMax] and A is
  the opacity.
- word 1: center.x as f16 in the low 16 bits, center.y as f16 in the high 16 bits.
- word 2: center.z as f16 in the low 16 bits, quaternion bits 0..15 in the high 16 bits.
- word 3: scale x, y and z as u8 in bytes 0..2. A value of 0 means a zero scale; otherwise the
  scale is `exp(lnScaleMin + (u - 1) * (lnScaleMax - lnScaleMin) / 254)`. Byte 3 holds
  quaternion bits 16..23.
- The 24-bit quaternion is `encodeQuatOctXy88R8`: an octahedral axis in 8 + 8 bits, then an
  8-bit angle.

`meta.json` (matrices are column-major, as three's `.elements`):

```text
numSplats, maxSplats, numSh (0..3), encoding { rgbMin, rgbMax, lnScaleMin, lnScaleMax,
sh1Max, sh2Max, sh3Max, lodOpacity }, activeSplats,
accumulator { numSplats, maxSplats, extSplats, covSplats, viewOrigin [3], viewDirection [3],
targetSize [w, h, layers] }, orderingTextureDataMatches,
camera { matrixWorld [16], projectionMatrix [16] }, mesh { matrixWorld [16] },
renderSize [2], loadMs, sorts, gl { vendor, renderer, version },
scene, file, spark { ref, commit }
```

One generated accumulator shader per SH degree is also copied to
`upstream/generated/accumulator-sh<degree>.frag.glsl` (plus `.compiled.frag.glsl`), for resync
diffs. The scenes cover SH degree 0 and 3. A `git diff upstream/generated` after a new reference
run shows any change of the dyno graph.

## Benchmark

### `test/bench.json`

```json
{ "frames": 600, "warmup": 60, "camera": { "fov": 60, "near": 0.01, "far": 1000 },
  "scenes": [ { "name": "furry", "file": "furry-logo-pedestal.spz", "optional": false,
                "mesh": { "position": [0,0,0], "quaternion": [1,0,0,0], "scale": 1 },
                "path": { "type": "orbit", "center": [0,0,0], "radius": 5, "height": 0.8,
                          "turns": 1 } } ] }
```

Scenes whose file is missing are skipped. The `large` entry expects a capture you copy to
`build/fixtures/large.spz`.

**Orbit pose for pose index i** (both sides must use exactly this):

```text
a        = 2 * pi * turns * i / frames
position = center + (radius * sin(a), height, radius * cos(a))
rotation = three.js camera.lookAt(center): camera -Z toward center, up +Y
camera   = perspective, vertical fov from bench.json camera, aspect width / height
```

A run renders `warmup` frames at poses `k % frames` for k = 0..warmup-1, unmeasured. It then
renders `frames` measured frames at poses i = 0..frames-1.

### What Spark's run measures

Chromium runs headed with these flags:

- `--use-gl=angle --use-angle=<angle> --ignore-gpu-blocklist`;
- `--disable-gpu-vsync --disable-frame-rate-limit`;
- `--force_high_performance_gpu` (unless `--low-power` is passed);
- flags that stop the renderer from being backgrounded.

The page uses a `width x height` canvas with pixelRatio 1 and SparkRenderer defaults.

| Metric | Meaning |
| --- | --- |
| `frameMs` | rAF timestamp deltas between rendered measured frames. |
| `cpuMs` | JS time inside `renderer.render()`: Spark's update, accumulate dispatch and draw submission. |
| `gpuMs` | `EXT_disjoint_timer_query_webgl2` `TIME_ELAPSED` around each `renderer.render()`. Disjoint samples are dropped; the value is null when the extension is not exposed. |
| `readbackMs` | Duration of each `SparkRenderer.readbackDepth()` call (wrapped on the instance), from readPixels issue to data available. |
| `sortMs` | Duration of each worker `sortSplats32` call (the lazily created `sortWorker` is intercepted with an accessor). |
| `loadMs` | From `new SplatMesh()` to `mesh.initialized` resolving. |
| `wallFrameMs` | Wall time of the measured frames until the GPU drained (fence), divided by `frames`. This is the throughput, and the most robust number. |

### Result JSON

The C++ side writes the same schema (extra metric keys are allowed and are shown by
`--compare`):

```json
{
  "tool": "spark | splattest | viewer",
  "backend": "webgl2-angle-d3d11 | d3d11 | d3d12 | vulkan | gl",
  "ref": "v2.3.1", "commit": "<sha>", "gpu": "<adapter name>", "timestamp": "<ISO 8601>",
  "width": 1920, "height": 1080, "frames": 600, "warmup": 60, "repeats": 3,
  "scenes": [
    {
      "name": "furry", "file": "furry-logo-pedestal.spz", "numSplats": 0,
      "metrics": {
        "frameMs":     { "median": 0, "p95": 0, "mean": 0, "min": 0, "max": 0, "count": 0 },
        "cpuMs": {}, "gpuMs": {}, "readbackMs": {}, "sortMs": {}, "loadMs": {}, "wallFrameMs": {}
      },
      "runs": [ { "frameMs": {}, "loadMs": 0 } ]
    }
  ]
}
```

- Each metric is `{ median, p95, ... }` or `null`. `--compare` reads `median` and `p95` only.
- `metrics` pool the samples of all repeats. `runs` holds the per-repeat summaries; it is
  informational and optional.
- p95 is nearest rank: `sorted[ceil(0.95 * count) - 1]`.

## Caveats

- **VPN.** Downloads fail with `SELF_SIGNED_CERT_IN_CHAIN` when the corporate VPN is on.
  Disable it and rerun `node fixtures.mjs`. The synthetic fixtures are written before the
  downloads, so they are always available.
- **Hybrid GPUs.** Without `--force_high_performance_gpu`, ANGLE D3D11 and GL run on the
  integrated GPU, while ANGLE Vulkan picks the discrete one. ANGLE GL stays on the integrated
  GPU even with the flag. Always check the `gpu` field.
- **Chrome frame pacing.** With vsync and the frame rate limit off, Chrome fires rAF with no
  GPU backpressure: frames pile up and Spark's fenced readbacks starve, so the sort almost
  never runs. The bench therefore keeps at most `--in-flight` (default 2) frames queued, using
  a fence per frame. Chrome only refreshes fence status about every 16 ms, so `frameMs` is
  bimodal (pairs of ~1 ms and ~18 ms). Read its mean and `wallFrameMs`, not its median. Light
  scenes are capped by this fence latency. Deeper queues raise throughput on light scenes, but
  inflate `gpuMs` (the timer then includes queueing) and cut the number of sorts.
- **Laptop throttling.** Interleave Spark and native runs, use 3 or more repeats, and rerun
  before claiming a result.
- **Ordering texture.** Spark keeps the first sort's array as `orderingTexture.image.data` and
  uploads later sorts by rows, so `image.data` can be stale. `ordering.bin` comes from the last
  sort result instead (`meta.orderingTextureDataMatches` reports whether they agree).
- **Integer readback.** three's `readRenderTargetPixelsAsync` checks
  `IMPLEMENTATION_COLOR_READ_FORMAT` before it selects the attachment, and Spark leaves the
  read buffer on the metric attachment. The harness therefore calls `gl.readBuffer` first.
- **Determinism.** SwiftShader output was byte-identical across runs on this machine. Spark's
  own snapshot tests allow 0.1% differing pixels across platforms.
- Dist is built without `SPARK_ENABLE_HOOKS`, so Spark's hook-based holds are not available.
  They are not needed here.
