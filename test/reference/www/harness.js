// In-page harness driving the real Spark from its dist bundle. Adapted from Spark's own
// test/browser/harness.ts (v2.3.1): same settle() logic and canvas readback, without the
// hook points (dist is built without SPARK_ENABLE_HOOKS).

import * as THREE from "three";
import { SparkRenderer, SplatAccumulator, SplatMesh, defines } from "@sparkjsdev/spark";

// Accumulator texel layout (SPLAT_TEX_WIDTH/HEIGHT in Spark's defines).
const TEX_WIDTH = 2048;
const TEX_HEIGHT = 2048;

function nextFrame() {
  return new Promise((resolve) => requestAnimationFrame(() => resolve()));
}

/**
 * Wrap SparkRenderer's private sort steps to time them. JS has no real private methods
 * here, so `readbackDepth` is shadowed on the instance and the lazily created sort worker
 * is intercepted through an accessor to wrap its `sortSplats32` call. The last sort
 * result is kept: it is exactly what Spark uploaded to `orderingTexture`.
 */
function instrument(spark) {
  const stats = { readbackMs: [], sortMs: [], lastSort: null };
  const readbackDepth = spark.readbackDepth;
  spark.readbackDepth = async function (...args) {
    const start = performance.now();
    const result = await readbackDepth.apply(this, args);
    stats.readbackMs.push(performance.now() - start);
    return result;
  };
  let worker = spark.sortWorker;
  const wrap = (w) => {
    if (!w || w.__timed) return;
    const call = w.call;
    w.call = async function (name, args, options) {
      const start = performance.now();
      const result = await call.call(this, name, args, options);
      if (name === "sortSplats32") {
        stats.sortMs.push(performance.now() - start);
        stats.lastSort = { ordering: result.ordering, activeSplats: result.activeSplats };
      }
      return result;
    };
    w.__timed = true;
  };
  wrap(worker);
  Object.defineProperty(spark, "sortWorker", {
    configurable: true,
    get: () => worker,
    set: (w) => {
      worker = w;
      wrap(w);
    },
  });
  return stats;
}

function setTransform(obj, { position, quaternion, lookAt, scale }) {
  if (position) obj.position.set(...position);
  if (quaternion) obj.quaternion.set(...quaternion);
  if (lookAt) obj.lookAt(...lookAt);
  if (typeof scale === "number") obj.scale.setScalar(scale);
  else if (scale) obj.scale.set(...scale);
  obj.updateMatrixWorld(true);
}

export class Harness {
  constructor(width = 256, height = 256) {
    this.scene = new THREE.Scene();
    this.meshes = [];
    this.renderScheduled = false;
    this.renderer = new THREE.WebGLRenderer({ preserveDrawingBuffer: true });
    this.renderer.setPixelRatio(1);
    this.renderer.setSize(width, height);
    document.body.appendChild(this.renderer.domElement);
  }

  /** SparkRenderer with default options; pass `{ onDirty: undefined }` to drive frames yourself. */
  createSpark(options = {}) {
    this.spark = new SparkRenderer({
      renderer: this.renderer,
      onDirty: () => this.requestRender(),
      ...options,
    });
    this.stats = instrument(this.spark);
    this.scene.add(this.spark);
    return this.spark;
  }

  /** Perspective camera from a scenes.json camera: vertical fov, then lookAt or quaternion. */
  createCamera({ fov = 60, near = 0.01, far = 1000, ...transform } = {}) {
    const { width, height } = this.renderer.getSize(new THREE.Vector2());
    this.camera = new THREE.PerspectiveCamera(fov, width / height, near, far);
    setTransform(this.camera, transform);
    return this.camera;
  }

  addSplatMesh({ url, position, quaternion, scale }) {
    const start = performance.now();
    const mesh = new SplatMesh({ url });
    mesh.loadMs = mesh.initialized.then(() => performance.now() - start);
    setTransform(mesh, { position, quaternion, scale });
    this.scene.add(mesh);
    this.meshes.push(mesh);
    return mesh;
  }

  requestRender() {
    if (this.renderScheduled) return;
    this.renderScheduled = true;
    requestAnimationFrame(() => {
      this.renderScheduled = false;
      this.render();
    });
  }

  render() {
    this.renderer.render(this.scene, this.camera);
  }

  /**
   * Render until Spark has nothing more to show (Spark's settle()): meshes loaded, no render
   * pending, no sort or LoD work in flight, and `activeSplats` unchanged, for two frames.
   */
  async settle({ timeoutMs = 120_000 } = {}) {
    const { spark } = this;
    await Promise.all(this.meshes.map((mesh) => mesh.initialized));
    this.requestRender();
    let lastActive = -1;
    await this.waitUntil(() => {
      const { pager } = spark;
      const fetching = pager?.isFetching?.() ?? false;
      const pendingLod = spark.lodDirty || (pager?.hasQueued?.() ?? false);
      const stable = spark.activeSplats === lastActive;
      lastActive = spark.activeSplats;
      return !(
        this.renderScheduled ||
        spark.sorting ||
        spark.sortDirty ||
        spark.lodWorker?.queue != null ||
        fetching ||
        pendingLod ||
        !stable
      );
    }, timeoutMs);
  }

  async waitUntil(predicate, timeoutMs) {
    const deadline = performance.now() + timeoutMs;
    let quietFrames = 0;
    while (quietFrames < 2) {
      await nextFrame();
      quietFrames = predicate() ? quietFrames + 1 : 0;
      if (performance.now() > deadline) throw new Error("Timed out waiting for Spark to settle");
    }
  }

  /** Canvas as a PNG data URL; toDataURL already flips WebGL's bottom-up rows. */
  getPixels() {
    return this.renderer.domElement.toDataURL("image/png");
  }

  /**
   * Read `count` texels of attachment `textureIndex` of a 2D array render target, layer by
   * layer, as Spark's readbackDepth() does. Texel i lives at layer i / 2048^2, row
   * (i / 2048) % 2048, column i % 2048; rows come back bottom-up, which is texture order.
   */
  async readArrayTarget(target, textureIndex, count, ArrayType) {
    const { renderer } = this;
    const gl = renderer.getContext();
    const layerSize = TEX_WIDTH * TEX_HEIGHT;
    const out = new ArrayType(Math.ceil(count / TEX_WIDTH) * TEX_WIDTH * 4);
    const previous = renderer.getRenderTarget();
    const reads = [];
    for (let base = 0; base < count; base += layerSize) {
      const rows = Math.min(TEX_HEIGHT, Math.ceil((count - base) / TEX_WIDTH));
      const view = out.subarray(base * 4, (base + rows * TEX_WIDTH) * 4);
      renderer.setRenderTarget(target, base / layerSize);
      // three checks IMPLEMENTATION_COLOR_READ_FORMAT before selecting the attachment, and
      // Spark leaves the read buffer on the metric attachment, so select it first.
      gl.readBuffer(gl.COLOR_ATTACHMENT0 + textureIndex);
      reads.push(
        renderer.readRenderTargetPixelsAsync(target, 0, 0, TEX_WIDTH, rows, view, undefined, textureIndex),
      );
    }
    renderer.setRenderTarget(previous);
    await Promise.all(reads);
    return out.subarray(0, count * 4);
  }

  /** The accumulator program sources: the dyno-generated GLSL and what WebGL compiled. */
  accumulatorShaders() {
    const material = SplatAccumulator.fullScreenQuad.material;
    const gl = this.renderer.getContext();
    const program = this.renderer.properties.get(material).currentProgram?.program;
    const compiled = {};
    for (const shader of program ? gl.getAttachedShaders(program) : []) {
      const type = gl.getShaderParameter(shader, gl.SHADER_TYPE) === gl.FRAGMENT_SHADER ? "frag" : "vert";
      compiled[type] = gl.getShaderSource(shader);
    }
    return { frag: material.fragmentShader, vert: material.vertexShader, compiled };
  }

  /** Write every reference dump for `mesh` through the harness server (PUT /out/<prefix>/...). */
  async dump(mesh, prefix, extraMeta = {}) {
    const { spark, camera, renderer } = this;
    const packed = mesh.packedSplats;
    const n = packed.numSplats;
    const put = (name, body) => fetch(`/out/${prefix}/${name}`, { method: "PUT", body }).then((r) => {
      if (!r.ok) throw new Error(`PUT ${name}: ${r.status}`);
    });

    const files = { "packed.bin": packed.packedArray.slice(0, n * 4) };
    const shWords = { sh1: 2, sh2: 4, sh3: 4 };
    for (const [key, words] of Object.entries(shWords)) {
      if (packed.extra[key]) files[`${key}.bin`] = packed.extra[key].slice(0, n * words);
    }

    const display = spark.display;
    const accumCount = display.numSplats;
    files["accum.bin"] = await this.readArrayTarget(display.target, 0, accumCount, Uint32Array);
    files["metric.bin"] = await this.readArrayTarget(display.target, 1, accumCount, Uint8Array);

    const sort = this.stats.lastSort;
    const ordering = sort.ordering.slice(0, spark.activeSplats);
    files["ordering.bin"] = ordering;

    const shaders = this.accumulatorShaders();
    files["accumulator.frag.glsl"] = shaders.frag;
    files["accumulator.vert.glsl"] = shaders.vert;
    if (shaders.compiled.frag) files["accumulator.compiled.frag.glsl"] = shaders.compiled.frag;

    const imageData = spark.orderingTexture?.image?.data;
    const meta = {
      numSplats: n,
      maxSplats: packed.maxSplats,
      numSh: packed.getNumSh(),
      encoding: { ...defines.DEFAULT_SPLAT_ENCODING, ...packed.splatEncoding },
      activeSplats: spark.activeSplats,
      accumulator: {
        numSplats: accumCount,
        maxSplats: display.maxSplats,
        extSplats: display.extSplats,
        covSplats: display.covSplats,
        viewOrigin: display.viewOrigin.toArray(),
        viewDirection: display.viewDirection.toArray(),
        targetSize: [display.target.width, display.target.height, display.target.depth],
      },
      // Spark keeps the first sort's array as orderingTexture.image.data and uploads later
      // sorts by rows, so image.data can be stale; ordering.bin is the last uploaded sort.
      orderingTextureDataMatches: imageData ? ordering.every((v, i) => v === imageData[i]) : false,
      camera: {
        matrixWorld: camera.matrixWorld.toArray(),
        projectionMatrix: camera.projectionMatrix.toArray(),
      },
      mesh: { matrixWorld: mesh.matrixWorld.toArray() },
      renderSize: spark.renderSize.toArray(),
      loadMs: await mesh.loadMs,
      sorts: this.stats.sortMs.length,
      gl: this.glInfo(),
      ...extraMeta,
    };
    files["meta.json"] = JSON.stringify(meta, null, 2);
    for (const [name, body] of Object.entries(files)) await put(name, body);
    return { meta, shaders };
  }

  glInfo() {
    const gl = this.renderer.getContext();
    const debug = gl.getExtension("WEBGL_debug_renderer_info");
    return {
      vendor: debug ? gl.getParameter(debug.UNMASKED_VENDOR_WEBGL) : gl.getParameter(gl.VENDOR),
      renderer: debug ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER),
      version: gl.getParameter(gl.VERSION),
    };
  }

  /**
   * Benchmark: play `frames` orbit poses (after replaying the first `warmup` poses) as fast
   * as the GPU sustains. With vsync and the frame rate limit off, Chrome fires rAF without
   * GPU backpressure (frames just queue up and Spark's fenced readbacks starve), so a fence
   * per frame keeps at most `maxInFlight` frames queued, like a native swapchain; rAF ticks
   * that find the queue full render nothing. Records rAF deltas between rendered frames,
   * JS time in renderer.render(), GPU time per render (EXT_disjoint_timer_query_webgl2, if
   * exposed), Spark's readback and sort times, and the wall time of the measured frames
   * until the GPU drained.
   */
  async bench({ path, frames, warmup, maxInFlight = 2 }) {
    const { renderer, camera, scene } = this;
    const gl = renderer.getContext();
    const timer = gl.getExtension("EXT_disjoint_timer_query_webgl2");
    const queries = [];
    const fences = [];
    const gpuMs = [];
    const frameMs = [];
    const cpuMs = [];
    const signaled = (sync) => gl.getSyncParameter(sync, gl.SYNC_STATUS) === gl.SIGNALED;
    const poll = () => {
      while (fences.length && signaled(fences[0])) gl.deleteSync(fences.shift());
      while (queries.length && gl.getQueryParameter(queries[0].q, gl.QUERY_RESULT_AVAILABLE)) {
        const { q, measured } = queries.shift();
        const ns = gl.getQueryParameter(q, gl.QUERY_RESULT);
        if (measured && !gl.getParameter(timer.GPU_DISJOINT_EXT)) gpuMs.push(ns / 1e6);
        gl.deleteQuery(q);
      }
    };

    let start = 0;
    await new Promise((resolve) => {
      let k = 0;
      let last = 0;
      const tick = (now) => {
        poll();
        if (fences.length >= maxInFlight) return requestAnimationFrame(tick);
        const measured = k >= warmup;
        if (measured && k > 0) frameMs.push(now - last);
        last = now;
        if (k === warmup) {
          start = performance.now();
          this.stats.readbackMs.length = 0;
          this.stats.sortMs.length = 0;
        }
        orbitPose(camera, path, measured ? k - warmup : k % frames, frames);
        const q = timer ? gl.createQuery() : null;
        if (q) gl.beginQuery(timer.TIME_ELAPSED_EXT, q);
        const cpuStart = performance.now();
        renderer.render(scene, camera);
        if (measured) cpuMs.push(performance.now() - cpuStart);
        if (q) {
          gl.endQuery(timer.TIME_ELAPSED_EXT);
          queries.push({ q, measured });
        }
        fences.push(gl.fenceSync(gl.SYNC_GPU_COMMANDS_COMPLETE, 0));
        gl.flush();
        if (++k < warmup + frames) requestAnimationFrame(tick);
        else resolve();
      };
      requestAnimationFrame(tick);
    });
    // Sync status only updates between tasks, so poll across frames until the GPU drained.
    while (fences.length || queries.length) {
      await nextFrame();
      poll();
    }
    const wallMs = performance.now() - start;
    return {
      frameMs,
      cpuMs,
      gpuMs: timer ? gpuMs : null,
      readbackMs: [...this.stats.readbackMs],
      sortMs: [...this.stats.sortMs],
      wallMs,
    };
  }
}

/**
 * Camera pose for frame i of `frames` on an orbit path (see test/bench.json):
 *   a = 2 * PI * turns * i / frames
 *   position = center + (radius * sin(a), height, radius * cos(a))
 *   camera.lookAt(center) with up +Y (three.js convention: camera -Z toward the center)
 */
export function orbitPose(camera, path, i, frames) {
  const [cx, cy, cz] = path.center;
  const a = (2 * Math.PI * (path.turns ?? 1) * i) / frames;
  camera.position.set(cx + path.radius * Math.sin(a), cy + path.height, cz + path.radius * Math.cos(a));
  camera.lookAt(cx, cy, cz);
  camera.updateMatrixWorld(true);
}
