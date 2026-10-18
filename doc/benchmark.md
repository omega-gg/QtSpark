# Benchmark: Splat vs Spark

Splat (native Qt RHI) against Spark v2.3.1 (WebGL2 in Chromium through ANGLE).

- **Hardware:** an NVIDIA RTX A1000 laptop GPU (hybrid with an Intel Iris Xe), with both sides on the
  NVIDIA GPU.
- **Run:** 1920x1080, 600 frames of the orbit of `test/bench.json`, vsync off.

How to run it, and the metric definitions, are in
[../test/reference/README.md](../test/reference/README.md).

## Method

`tools/bench.sh` runs both sides, interleaved, and prints the summary:

- **`quick`:** about 3 minutes for two scenes. An unmeasured warm-up brings the GPU to its
  throttled clock first, then 2 rounds (AB, then BA) of 200 frames. Use it for experiments and
  A/B checks, with `--scene` and `--backends` to narrow it down.
- **`full`:** 4 ABBA rounds of every scene, about 30 minutes. Use it for the tables below.

The summary gives, per scene and backend, the medians over the rounds of the wall time per frame,
the GPU time and the p95 of the frame interval (how much it stutters).

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

The Splat performance options on (`SplatView.fast`, see ../implementation.md). Same image.

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

- **The gap at this resolution:** on fill heavy scenes, our Direct3D paths were 4 to 17% behind
  Spark on ANGLE D3D11, while Vulkan was at parity or ahead.
- **Direct3D 12:** its first run in a session is slower (shader compile and GPU clock ramp).

The next section explains and fixes the gap.

## Direct3D and stutter fixes (2026-10-17)

### Fragment shader branches

**Finding the cause.** A per pass split, on the synthetic scene at 1440p in `splattest`:

- **Same cost on both APIs:** rasterization, blending, instancing and the vertex shader.
  Synthetic quads with a trivial fragment shader took 6.45 ms on both D3D11 and Vulkan, and the
  real vertex shader with a trivial fragment shader 10.6 ms on both.
- **Twice the cost on D3D:** only the splat fragment shader, 7.2 ms against 3.8 ms on Vulkan.

**Why.** fxc flattens its small branches into selects, so every pixel evaluates both falloff
formulas and the unused sRGB conversion. ANGLE's HLSL compiles to the same DXBC, so Spark pays
this cost too.

**Fix.** `[[dont_flatten]]` on the two branches (see `implementation.md`, Shaders). The
images are byte identical. GPU time, synthetic at 1440p: D3D11 from 23.5 to 18.8 ms, D3D12 from
22.4 to 19.5 ms, Vulkan unchanged at 18.2 ms.

### Stutter

The frame interval p95 showed it, while the medians did not:

- **D3D11, blocking readback.** Most frames came 2 ms apart, then the readback `Map` waited for
  every queued frame: a p95 of 91 ms on furry, 205 ms on large. The readback now polls, like
  Spark's.
- **Vulkan and D3D12, duplicate frames.** The view requested one more frame per pending sort,
  even while the bench or the controls already requested one. Every other frame showed the same
  pose, so the motion jumped by two poses at a time. The view now requests extra frames only
  while the camera is still.

### Results

Quick preset at 2560x1440, the median of 2 rounds after the warm-up (the rounds agreed within
1 ms). Wall time per frame, then the frame interval p95, ms:

| Scene | Spark D3D11 | Splat D3D11 | Splat D3D12 | Splat Vulkan |
|---|---|---|---|---|
| furry | 23.16 / 36.2 | **22.69 / 25.3** | 23.45 / **25.3** | 24.07 / **26.0** |
| large | 46.37 / 70.0 | **45.78 / 60.9** | 51.74 / **68.5** | 50.20 / **67.7** |

- **Smoothness:** Splat now stutters less than Spark on every backend.
- **Speed:** D3D11 is at parity or ahead. On the 2M splat scene, D3D12 and Vulkan are 8 to 12%
  behind, GPU bound (50 ms of GPU time against 43.5 ms on D3D11).

### Earlier tables and bench pacing

- **Duplicate frames:** before the stutter fix, every other Vulkan and D3D12 frame repeated the
  previous pose without accumulating. Those cheap frames lowered the averages, so the earlier
  Vulkan and D3D12 figures look faster than the views they showed.
- **Pacing:** the bench requests each frame from the GUI thread. Qt delivers these requests on
  the display vblank for D3D windows, and after a short timer otherwise, which capped light
  scenes at 60 Hz once the duplicates were gone. In bench mode the viewer turns both off
  (`QT_D3D_NO_VBLANK_THREAD`, `QT_QPA_UPDATE_IDLE_TIME`), like Spark's unthrottled harness.
  Butterfly at 1440p: 2.4 ms on D3D11 and D3D12, against 9.5 ms for Spark. Vulkan stays at
  8.3 ms, the 120 Hz cap of presenting through the integrated GPU.

## Reading

- **Light scenes:** Splat is far ahead (butterfly: 2.4 against 9.5 ms on D3D), as is loading.
- **GPU bound scenes:** at parity on furry and synthetic on every backend, and on large with
  D3D11. Both run the same shaders, the port being faithful.
- **Smoothness:** the frame interval p95 is lower than Spark's on every backend.
- **Sorting:** with the faithful options Splat sorts about half as often as Spark while moving
  (see Known gaps). With `--fast` it sorts about twice as often.

## Known gaps

- **Sort rate:** each step of the sort cycle waits for the next Qt frame (see `implementation.md`,
  Deviations). A fence polled from the sort worker was tried on D3D11 and gained nothing: the GPU
  reaches the copy 2 to 3 frames after it is recorded. Waiting for the sort within a frame would
  trade frame time, so it was not kept.
- **D3D12 and Vulkan on large:** 15 to 20% more GPU time than D3D11 offscreen, with the same
  shaders (D3D12 even runs the same DXBC). Ruled out: the fragment shader (a trivial one keeps the
  gap), the readback, the vertex work (tiny splats are slightly faster there) and sub-pixel quads
  (`tightBounds` keeps the gap). What remains is rasterizing and blending millions of overlapping
  splats, a difference between the NVIDIA driver paths. On large, D3D11 is the backend to use on
  this hardware.
- **OpenGL on large:** about 15% behind Spark, GPU bound (51 ms of GPU time against 35 to 40 ms
  on the other backends). Not the readback: with QRhi's blocking readback the GPU time is the
  same (51.1 against 51.3 ms).

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
