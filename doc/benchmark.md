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

## Results (2026-10-17, Splat with its default options)

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

- **A projection pass:** compute each splat's projected ellipse once per frame, in a fullscreen
  pass like the accumulator, instead of 4 times (once per quad vertex). That saves about 3 ms of
  the 4.5 ms of vertex shading on 2M splats. The upstream body of `splatVertex.glsl` would move
  into one include shared by both paths, to keep resyncs simple.
- **Rendering without the intermediate texture:** draw directly into the window
  (`QQuickWindow::beforeRenderPassRecording`) instead of through `QQuickRhiItem`'s texture. That
  saves a full screen copy and a depth buffer per frame.
