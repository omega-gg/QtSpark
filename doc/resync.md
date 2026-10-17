# Resyncing with Spark

Splat follows [Spark](https://github.com/sparkjsdev/spark) closely, so an upstream release becomes a
list of hunks to port. This file is the procedure. [implementation.md](implementation.md) explains
the code.

## Where things are

- `upstream/spark.pin`: the synced Spark tag and commit. Today it is `v2.3.1`
  (`2e9c2e74a486b1ba3a31025a22b231b8e4fccde3`).
- `upstream/files.txt`: every tracked upstream file and the port files it maps to.
- `upstream/generated/`: Spark's runtime generated accumulator GLSL (the dyno program), dumped by
  the reference harness. Spark builds that shader at runtime, so this is the only way to diff it.
- `tools/resync.sh`: the report of what changed upstream.
- In the code:
  - Every ported file opens with `// SPARK v2.3.1: <upstream path>`.
  - Every ported function has `// SPARK: <upstream name>`.
  - Changes inside verbatim shader bodies are tagged `// SPLAT:`.

## Procedure

You need a Spark checkout (`$SPARK_DIR`) with tags fetched.

1. **Pick the target and read the report.**

   ```sh
   git -C $SPARK_DIR fetch --tags
   sh tools/resync.sh $SPARK_DIR v2.4.0           # stats per tracked file
   sh tools/resync.sh $SPARK_DIR v2.4.0 --full    # full diffs
   ```

   For each changed tracked file it prints the port files to update. It also lists files added,
   deleted or renamed under `src/` and `rust/`, and modified files that are not tracked. Decide for
   each whether it is in scope (see "Scope" in implementation.md).

2. **Port the hunks**, file by file, keeping the upstream names. Use the mapping below.

3. **Regenerate the reference** at the new tag, which also refreshes `upstream/generated/`:

   ```sh
   cd test/reference
   node reference.mjs --spark $SPARK_DIR --ref v2.4.0
   git diff ../../upstream/generated
   ```

   A diff in the generated accumulator GLSL means the dyno graph changed. Port it into
   `src/shaders/accumulate.frag`.

4. **Run the tests** on every backend against the new reference:

   ```sh
   for b in d3d11 d3d12 vulkan gl; do
       bin/splattest --backend $b --reference build/reference/v2.4.0
   done
   ```

5. **Run the benchmark** (see test/reference/README.md) and check native still leads.

6. **Bump the pin and the markers:**
   - `upstream/spark.pin` (tag, ref, sha).
   - `SPLAT_SPARK_VERSION` in `src/Splat.h`.
   - The `SPARK v2.3.1` header lines (search and replace).
   - The fixed list in "Pending upstream changes" below.

## Mapping

| Upstream | Port | Notes |
|---|---|---|
| `src/shaders/splatDefines.glsl` | `src/shaders/splatDefines.glsl` | verbatim, copy the new file under the header |
| `src/shaders/splatVertex.glsl` | `src/shaders/splat.vert` | same body, ext / cov / 2DGS branches cut |
| `src/shaders/splatFragment.glsl` | `src/shaders/splat.frag` | same body |
| `src/shaders/computeUvec4_Vec4.glsl` | `src/shaders/accumulate.frag` (`main`) | template |
| `src/PackedSplats.ts` `defineEvalPackedSH*` | `src/shaders/packedSH.glsl` | verbatim |
| `src/PackedSplats.ts` `fetchSplat` | `accumulate.frag` `fetchSplat` | |
| `src/dyno/splats.ts` | `accumulate.frag` `readPackedSplat`, `transformGsplat` | |
| `src/dyno/output.ts` | `accumulate.frag` `outputPackedSplat`, `outputSplatDepth` | |
| `src/SplatMesh.ts` `constructGenerator`, `update` | `accumulate.frag` `generator`, `WSplatRendererPrivate::updateMeshVersion` | |
| `src/SplatAccumulator.ts` | `src/WSplatAccumulator.cpp`, `accumulate.frag` `produceSplat` | |
| `src/SparkRenderer.ts` | `src/WSplatRenderer.cpp` (`updateInternal`, `driveSort`, `readbackDepth`, uniforms), `WSplatRenderOptions` | defaults in `WSplatRenderer.h` |
| `src/SplatGeometry.ts` | `quadVertices` / `quadIndices` in `WSplatRenderer.cpp` | |
| `src/controls.ts` | `src/WSplatControls.cpp` | constants at the top |
| `src/defines.ts` | `src/Splat.h` | |
| `src/utils.ts` `getTextureSize` | `WPackedSplats::getTextureSize` | |
| `rust/spark-lib/src/splat_encode.rs` | `src/WPackedSplats.cpp` | encoders |
| `libm` crate (`rust/Cargo.lock`, 0.2.16 at v2.3.1) | `src/WSplatMath.cpp` | only if the version changes and the decoders drift |
| `rust/spark-rs/src/packed_splats.rs` | `src/WPackedSplats.cpp` | receiver |
| `rust/spark-lib/src/decoder.rs` | `src/WSplatReceiver.h`, `src/WSplatLoader.cpp` | |
| `rust/spark-lib/src/ply.rs` | `src/WSplatPly.cpp` | |
| `rust/spark-lib/src/spz.rs` | `src/WSplatSpz.cpp` | |
| `rust/spark-rs/src/sort.rs`, `lib.rs` `sort32_splats` | `src/WSplatSort.cpp` | |

## Rules of thumb

- **Keep verbatim files verbatim.** For `splatDefines.glsl` and `packedSH.glsl`, copy the new
  upstream text instead of editing by hand.
- **Shader bodies:** keep the upstream text and the `SPLAT:` tags. If upstream touches a cut branch
  (ext, cov, 2DGS), nothing needs porting.
- **New uniforms:** add them to `splatUniforms.glsl`, to `WSplatDrawUniforms` (same order, std140
  rules: a `vec3` takes 12 bytes and the next float packs after it), and to
  `WSplatRenderOptions` with the upstream default.
- **Math:** `WSplatMath` ports the Rust `libm` crate, not a Spark file. When `rust/Cargo.lock`
  bumps `libm` and the byte exact tests fail, diff the crate between the two versions.
- **Encoders and decoders** must stay byte exact: `splattest` compares `packed.bin` and `sh*.bin`
  word by word against Spark.
- **New upstream features** (LoD, formats) are out of scope until decided. List them below so they
  are not lost.

## Pending upstream changes

Changes after the pinned tag that are not ported yet. Refresh this list with `tools/resync.sh`.

From `v2.3.1` to `a403b6d` (main, 2026-10-16):

- **`splatVertex.glsl` (#486):** the eigen decomposition is rewritten to avoid cancellation
  (`halfDiagDiff`, `eigen2 = max(0, det / eigen1)`). Port it into `splat.vert`.
- **`splatDefines.glsl` (#465):** `encodeExtRgb` rounds the exponent up. Copy the file; ExtSplats
  only, no visible change.
- **`spz.rs`:**
  - SPZ v4 (NGSP header + zstd streams), which needs a zstd decoder.
  - miniz replaced by flate2; no format change.
  - The quaternion path becomes `version >= 3`.
- **`SparkRenderer.ts`:**
  - render size from `camera.viewport`;
  - sort skipped while the splat mapping is changing (`mappingChanged`);
  - LoD fov from the projection matrix.
- **`SplatPager.ts`:** split into `PagedSplats.ts` and `PagedSplatsFetcher.ts`. Out of scope (LoD).
- **`decoder.rs`, `packed_splats.rs`, `splat_encode.rs`, `ply.rs`:** refactors. Check them with the
  byte exact tests.
