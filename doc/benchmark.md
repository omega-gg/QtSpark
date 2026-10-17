# Benchmark: Splat vs Spark

Splat (native Qt RHI) against Spark v2.3.1 (WebGL2 in Chromium through ANGLE).

- **Hardware:** an NVIDIA RTX A1000 laptop GPU (hybrid with an Intel Iris Xe), with both sides on the
  NVIDIA GPU.
- **Run:** 1920x1080, 600 frames of the orbit of `test/bench.json`, vsync off.

How to run it, and the metric definitions, are in
[../test/reference/README.md](../test/reference/README.md).

## Method

- **Native:** `splatviewer --bench test/bench.json`. This is a `SplatView` in a real swapchain with
  vsync off (`swapInterval 0`) and GPU timestamps on.
- **Spark:** `node test/reference/bench.mjs`. Chromium runs headed with
  `--disable-gpu-vsync --disable-frame-rate-limit`, at most 2 frames in flight.
- **Order:** ABBA, over 4 rounds, so neither side always runs on the hotter GPU. The laptop GPU
  throttles to about half its clock after a few minutes, which moves single runs by up to 2x.
  The tables give the medians of the 4 rounds.
- **wall** is the throughput (wall time of the measured frames divided by the frame count). It is
  the comparable number. The GPU timers do not cover the same scope on both sides: Spark times
  `renderer.render()`, Splat times the whole Qt Quick frame.

## Results with `--fast` (2026-10-17)

The Splat performance options on (`SplatView.fast`, see implementation.md). Same image.

Wall time per frame, ms (lower is better):

| Scene | Splats | Spark D3D11 | Splat D3D11 | Spark Vulkan | Splat Vulkan |
|---|---|---|---|---|---|
| butterfly | 177k | 9.26 | **2.93** | 9.37 | **8.34** (1) |
| synthetic-sh3 | 20k | 13.25 | 13.22 | 12.65 | **11.79** |
| furry | 322k | **12.80** | 13.20 | **12.90** | 13.91 |
| large (greyscale-room) | 2.0M | **30.20** | 31.69 | 29.20 | **27.78** |

(1) Vulkan presentation goes through the integrated GPU on this hybrid laptop, which paces it at
the 120 Hz refresh rate even with vsync off. The GPU time of that run is 1.7 ms.

Sorting and loading, D3D11 (Vulkan is similar):

| Scene | Metric latency, Spark / Splat | Sort, Spark / Splat | Load, Spark / Splat |
|---|---|---|---|
| butterfly | 6.2 / **1.2** ms | 3.4 / **1.9** ms | 457 / **146** ms |
| furry | 15.5 / **2.2** ms | 5.3 / **4.4** ms | 481 / **351** ms |
| large | 44.5 / **5.5** ms | 25.6 / **12.7** ms | 1387 / **654** ms |

- **Metric latency** is the time from the sort request to the sort metric being available: the GPU
  readback in Spark, the CPU metric in Splat. Lower means the order catches up with the camera
  sooner.
- **Splat sorts more often** while moving: 482 sorts against 233 on large, so the order lags the
  camera less.
- **Sort time in isolation** (`splattest --sort-bench`): 2.1 ms at 322k and 5.8 ms at 2M, against
  Spark's wasm sort at about 26 ms for 2M.

## Results with the default, faithful options (2026-10-17)

This is the exact Spark code path: GPU readback of the sort metric and the upstream sort. Wall time
per frame, ms, medians of 4 ABBA rounds:

| Scene | Splats | Spark D3D11 | Splat D3D11 | Spark Vulkan | Splat Vulkan |
|---|---|---|---|---|---|
| butterfly | 177k | 9.27 | **2.83** | 9.39 | **8.34** (1) |
| synthetic-sh3 | 20k | **13.20** | 14.51 | 13.35 | **11.91** |
| furry | 322k | **12.80** | 14.21 | 13.50 | **12.83** |
| large (greyscale-room) | 2.0M | **30.55** | 31.42 | **28.25** | 28.41 |

- **Vulkan:** on par or ahead everywhere.
- **D3D11:** 3 to 11% behind. Qt completes the metric readback with a blocking `Map` at the next
  frame, so the latency is 54 to 116 ms against Spark's 15 to 45 ms, and the sort runs about
  half as often. An asynchronous readback (a staging copy plus a fence poll, like Spark's
  `readRenderTargetPixelsAsync`) would close this.

## Results at 2560x1440, faithful options (2026-10-17)

The MSVC build, so Direct3D 12 is included. Spark runs on ANGLE D3D11, its best Windows path:
ANGLE has no D3D12 backend. Wall time per frame, ms, medians of 4 ABBA rounds:

| Scene | Spark D3D11 | Splat D3D12 | Splat D3D11 | Splat Vulkan |
|---|---|---|---|---|
| butterfly | 13.80 | **3.76** | **3.48** | **8.34** (1) |
| synthetic-sh3 | 24.85 | 28.65 | 28.98 | **24.02** |
| furry | **22.30** | 23.45 | 23.21 | 23.48 |
| large (greyscale-room) | 45.30 | 49.29 | 47.20 | **42.55** |

- **The gap at this resolution:** on fill heavy scenes, our Direct3D paths are 4 to 17% behind
  Spark on ANGLE D3D11, while Vulkan is at parity or ahead. The GPU times show the same gap
  (synthetic: Spark 23.0 ms, Splat D3D 28.6 ms, Splat Vulkan 21.1 ms).
- **Same shaders, same GPU:** the difference is in how the shaders reach the D3D driver.
  SPIRV-Cross translates them to HLSL for Qt, and ANGLE has its own translator.
- **Ruled out:**
  - The FXC optimization level: `/O1` and `/O3` give the same bytecode.
  - The unused varyings `vNdc` and `vSplatIndex`: removing them changed nothing, the driver
    strips them.

  Finding the cause needs a GPU capture of both (PIX or Nsight Graphics).
- **Direct3D 12:** its first run in a session is slower (shader compile and GPU clock ramp),
  later runs match D3D11.

## Reading

- Splat is ahead wherever the browser or the CPU side costs something: light and medium scenes,
  sort latency and frequency, and loading.
- **When the GPU is the limit, Splat and Spark are at parity, within 5% either way.** Both run the
  same shaders, the port being faithful. Splat is slightly behind on D3D11 and slightly ahead on
  Vulkan.
- Since this run, empty quad culling (`tightBounds`) was added. It is bit exact, and it gains a
  few percent on large scenes.

## Next levers for GPU bound scenes

Measured on the large scene at boosted clocks: about 4.5 ms of vertex shading, 5 to 7 ms of
triangle setup for millions of sub-pixel quads, and 3 to 4 ms of fill at 1080p.

- **Tried and dropped: a projection pass.**
  - **What it did:** a fullscreen pass computed each splat's projected ellipse once per frame,
    into integer targets, and the vertex shader only placed the quad corners. The image was bit
    identical on every backend.
  - **Why it was dropped:** it was slower. On 2M splats it took 32.9 against 31.8 ms at 1080p,
    and 6.1 to 8.8 against 5.6 to 6.0 ms at 480x270, in ABBA runs on D3D11. Writing and reading
    52 bytes per splat costs more than the vertex shading it saves. It also split
    `splatVertex.glsl` away from the upstream structure, so it was not kept.
- **Rendering without the intermediate texture:** draw directly into the window
  (`QQuickWindow::beforeRenderPassRecording`) instead of through `QQuickRhiItem`'s texture. That
  saves a full screen copy and a depth buffer per frame.
