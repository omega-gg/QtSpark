// Benchmarks the real Spark on the hardware GPU (headed Chromium, vsync off) over the
// scripted camera paths of test/bench.json, and compares against native result files.
//
//   node bench.mjs --spark <dir> [--ref v2.3.1] [--width 1920 --height 1080]
//                  [--angle d3d11|vulkan|gl] [--repeats 3] [--frames N] [--warmup N]
//                  [--scene name] [--in-flight 2] [--low-power] [--compare native.json ...]
//   node bench.mjs --compare native.json ... [--baseline spark.json]
//
// Without --spark on the command line, --compare only compares: against --baseline, or
// else the newest build/bench/spark-*.json.

import { existsSync, mkdirSync, readdirSync, statSync, writeFileSync } from "node:fs";
import path from "node:path";
import { chromium } from "playwright";
import {
  BUILD, DEFAULT_REF, FIXTURES, REPO, ensureSpark, logPage, parseArgs, readJson, sparkDirFrom,
  standardMounts, startServer,
} from "./common.mjs";
import { ensureFixtures } from "./fixtures.mjs";

const BENCH_DIR = path.join(BUILD, "bench");
const args = parseArgs({ multi: ["compare"] });

let sparkResult;
if (args.spark || !args.compare) {
  sparkResult = await runBench();
} else {
  const file = args.baseline ?? newestSparkResult();
  console.log(`Spark baseline: ${file}`);
  sparkResult = readJson(file);
}
if (args.compare?.length) compare(sparkResult, args.compare);

async function runBench() {
  const ref = args.ref ?? DEFAULT_REF;
  const angle = args.angle ?? "d3d11";
  const width = Number(args.width ?? 1920);
  const height = Number(args.height ?? 1080);
  const repeats = Number(args.repeats ?? 3);
  const config = readJson(path.join(REPO, "test/bench.json"));
  const frames = Number(args.frames ?? config.frames);
  const warmup = Number(args.warmup ?? config.warmup);
  const maxInFlight = Number(args["in-flight"] ?? 2);

  const spark = ensureSpark(sparkDirFrom(args), ref, { build: !!args.build });
  try {
    await ensureFixtures({ sparkBuildDir: spark.dir });
  } catch (error) {
    console.error(`Fixtures: ${error.message}`);
  }

  const scenes = config.scenes.filter((s) => {
    if (args.scene && s.name !== args.scene) return false;
    if (existsSync(path.join(FIXTURES, s.file))) return true;
    console.warn(`${s.name}: skipped, build/fixtures/${s.file} is missing`);
    return false;
  });

  const server = await startServer(standardMounts(spark.dir));
  const browser = await chromium.launch({
    headless: false,
    args: [
      "--use-gl=angle",
      `--use-angle=${angle}`,
      "--ignore-gpu-blocklist",
      "--disable-gpu-vsync",
      "--disable-frame-rate-limit",
      // Hybrid laptops: ask for the discrete GPU (ANGLE Vulkan picks it on its own).
      ...(args["low-power"] ? [] : ["--force_high_performance_gpu"]),
      // Keep rAF running at full rate even if the window is covered or unfocused.
      "--disable-renderer-backgrounding",
      "--disable-background-timer-throttling",
      "--disable-backgrounding-occluded-windows",
    ],
  });

  const runs = Object.fromEntries(scenes.map((s) => [s.name, []]));
  let gl = null;
  try {
    for (let repeat = 0; repeat < repeats; repeat++) {
      for (const scene of scenes) {
        const page = await browser.newPage({ viewport: { width, height } });
        logPage(page, { quiet: true });
        await page.goto(`${server.url}/harness.html`);
        await page.waitForFunction(() => window.Harness);
        const run = await page.evaluate(
          async ({ scene, camera, width, height, frames, warmup, maxInFlight }) => {
            const h = new window.Harness(width, height);
            h.createSpark({ onDirty: undefined });
            h.createCamera(camera);
            window.orbitPose(h.camera, scene.path, 0, frames);
            const mesh = h.addSplatMesh({ url: `/fixtures/${scene.file}`, ...scene.mesh });
            const loadMs = await mesh.loadMs;
            await h.settle();
            const result = await h.bench({ path: scene.path, frames, warmup, maxInFlight });
            return { loadMs, numSplats: mesh.packedSplats.numSplats, gl: h.glInfo(), ...result };
          },
          { scene, camera: config.camera, width, height, frames, warmup, maxInFlight },
        );
        await page.close();
        gl = run.gl;
        runs[scene.name].push(run);
        const med = (key) => fmt(summarize(run[key])?.median);
        console.log(
          `[${repeat + 1}/${repeats}] ${scene.name}: ${run.numSplats} splats, median ms: ` +
            `frame ${med("frameMs")}, cpu ${med("cpuMs")}, gpu ${med("gpuMs")}, ` +
            `readback ${med("readbackMs")}, sort ${med("sortMs")} (${run.sortMs.length} sorts), ` +
            `wall ${fmt(run.wallMs / frames)} ms/frame, load ${fmt(run.loadMs)}`,
        );
      }
    }
  } finally {
    await browser.close();
    await server.close();
  }

  const result = {
    tool: "spark",
    backend: `webgl2-angle-${angle}`,
    ref,
    commit: spark.commit,
    gpu: gl?.renderer ?? null,
    glVersion: gl?.version ?? null,
    timestamp: new Date().toISOString(),
    width,
    height,
    frames,
    warmup,
    repeats,
    maxInFlight,
    scenes: scenes.map((scene) => {
      const list = runs[scene.name];
      const pool = (key) => (list.every((r) => r[key]) ? list.flatMap((r) => r[key]) : null);
      return {
        name: scene.name,
        file: scene.file,
        numSplats: list[0]?.numSplats ?? 0,
        metrics: {
          frameMs: summarize(pool("frameMs")),
          cpuMs: summarize(pool("cpuMs")),
          gpuMs: summarize(pool("gpuMs")),
          readbackMs: summarize(pool("readbackMs")),
          sortMs: summarize(pool("sortMs")),
          loadMs: summarize(list.map((r) => r.loadMs)),
          wallFrameMs: summarize(list.map((r) => r.wallMs / frames)),
        },
        runs: list.map((r) => ({
          frameMs: summarize(r.frameMs),
          cpuMs: summarize(r.cpuMs),
          gpuMs: summarize(r.gpuMs),
          readbackMs: summarize(r.readbackMs),
          sortMs: summarize(r.sortMs),
          loadMs: r.loadMs,
          wallFrameMs: r.wallMs / frames,
        })),
      };
    }),
  };
  mkdirSync(BENCH_DIR, { recursive: true });
  const stamp = result.timestamp.replace(/[-:]/g, "").replace("T", "-").slice(0, 15);
  const file = path.join(BENCH_DIR, `spark-${ref}-${angle}-${stamp}.json`);
  writeFileSync(file, JSON.stringify(result, null, 2));
  console.log(`GPU: ${result.gpu}\nWrote ${file}`);
  return result;
}

/** Median, p95 (nearest rank), mean, min, max and count; null for no samples. */
function summarize(samples) {
  if (!samples || samples.length === 0) return null;
  const s = [...samples].sort((a, b) => a - b);
  const rank = (p) => s[Math.min(s.length - 1, Math.ceil(p * s.length) - 1)];
  const mean = s.reduce((a, b) => a + b, 0) / s.length;
  return { median: rank(0.5), p95: rank(0.95), mean, min: s[0], max: s.at(-1), count: s.length };
}

function fmt(v) {
  return v == null ? "n/a" : v.toFixed(v < 10 ? 2 : 1);
}

function newestSparkResult() {
  const files = existsSync(BENCH_DIR)
    ? readdirSync(BENCH_DIR).filter((f) => f.startsWith("spark-") && f.endsWith(".json"))
    : [];
  if (files.length === 0) throw new Error("No build/bench/spark-*.json; run a benchmark or pass --baseline");
  const full = files.map((f) => path.join(BENCH_DIR, f));
  return full.sort((a, b) => statSync(b).mtimeMs - statSync(a).mtimeMs)[0];
}

/** One table per scene: median and p95 of every metric, Spark against each native file. */
function compare(base, files) {
  const others = files.map((f) => ({ ...readJson(f), label: readJson(f).backend ?? path.basename(f) }));
  const all = [{ ...base, label: `spark ${base.backend}` }, ...others];
  for (const scene of base.scenes) {
    const entries = all.map((r) => r.scenes.find((s) => s.name === scene.name));
    const metrics = [...new Set(entries.flatMap((e) => Object.keys(e?.metrics ?? {})))];
    console.log(`\n${scene.name} (${scene.numSplats} splats, ${base.width}x${base.height})`);
    const header = ["metric", ...all.flatMap((r) => [`${r.label} med`, "p95"]), ...others.map((r) => `${r.label}/spark`)];
    const rows = metrics.map((m) => {
      const values = entries.map((e) => e?.metrics?.[m]);
      const ratio = values.slice(1).map((v) =>
        v?.median != null && values[0]?.median ? `${(v.median / values[0].median).toFixed(2)}x` : "n/a",
      );
      return [m, ...values.flatMap((v) => [fmt(v?.median), fmt(v?.p95)]), ...ratio];
    });
    const widths = header.map((h, i) => Math.max(h.length, ...rows.map((r) => r[i].length)));
    const line = (cells) => cells.map((c, i) => (i === 0 ? c.padEnd(widths[i]) : c.padStart(widths[i]))).join("  ");
    console.log(line(header));
    for (const row of rows) console.log(line(row));
  }
}
