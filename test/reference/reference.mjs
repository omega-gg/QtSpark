// Renders every scene of test/scenes.json with the real Spark in SwiftShader Chromium and
// writes the reference dumps to build/reference/<ref>/<scene>/.
//
//   node reference.mjs --spark <dir> [--ref v2.3.1] [--scene name] [--build] [--headed]

import { copyFileSync, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import path from "node:path";
import { chromium } from "playwright";
import { PNG } from "pngjs";
import {
  BUILD, DEFAULT_REF, FIXTURES, REPO, SWIFTSHADER_ARGS, ensureSpark, logPage, parseArgs,
  readJson, sparkDirFrom, standardMounts, startServer,
} from "./common.mjs";
import { ensureFixtures } from "./fixtures.mjs";

const args = parseArgs();
const ref = args.ref ?? DEFAULT_REF;
const spark = ensureSpark(sparkDirFrom(args), ref, { build: !!args.build });
try {
  await ensureFixtures({ sparkBuildDir: spark.dir });
} catch (error) {
  console.error(`Fixtures: ${error.message}`);
}

const config = readJson(path.join(REPO, "test/scenes.json"));
const [width, height] = config.size;
const scenes = config.scenes.filter((s) => !args.scene || s.name === args.scene);
if (scenes.length === 0) throw new Error(`No scene named ${args.scene}`);

const outRoot = path.join(BUILD, "reference", ref);
const generatedDir = path.join(REPO, "upstream/generated");
const server = await startServer(standardMounts(spark.dir), { outDir: outRoot });
const browser = await chromium.launch({ headless: !args.headed, args: SWIFTSHADER_ARGS });

const shaderDumped = new Set();
const failures = [];
try {
  for (const scene of scenes) {
    if (!existsSync(path.join(FIXTURES, scene.file))) {
      console.warn(`${scene.name}: skipped, build/fixtures/${scene.file} is missing`);
      failures.push(scene.name);
      continue;
    }
    const outDir = path.join(outRoot, scene.name);
    rmSync(outDir, { recursive: true, force: true });
    mkdirSync(outDir, { recursive: true });

    const page = await browser.newPage({ viewport: { width, height } });
    logPage(page, { quiet: true });
    await page.goto(`${server.url}/harness.html`);
    await page.waitForFunction(() => window.Harness);
    const result = await page.evaluate(
      async ({ scene, width, height, extraMeta }) => {
        const h = new window.Harness(width, height);
        h.createSpark();
        h.createCamera(scene.camera);
        const mesh = h.addSplatMesh({ url: `/fixtures/${scene.file}`, ...scene.mesh });
        await h.settle();
        const png = h.getPixels();
        const { meta } = await h.dump(mesh, scene.name, extraMeta);
        return { png, meta };
      },
      {
        scene,
        width,
        height,
        extraMeta: { scene: scene.name, file: scene.file, spark: { ref, commit: spark.commit } },
      },
    );
    await page.close();

    const png = Buffer.from(result.png.split(",")[1], "base64");
    writeFileSync(path.join(outDir, "render.png"), png);
    const { meta } = result;
    console.log(
      `${scene.name}: numSplats ${meta.numSplats}, activeSplats ${meta.activeSplats}, ` +
        `numSh ${meta.numSh}, coverage ${(coverage(png) * 100).toFixed(1)}%, ` +
        `load ${meta.loadMs.toFixed(0)} ms`,
    );
    if (!meta.orderingTextureDataMatches) {
      console.log(`  note: orderingTexture.image.data is stale; ordering.bin is the last sort`);
    }

    // Keep one generated accumulator shader per SH degree under upstream/generated/.
    if (!shaderDumped.has(meta.numSh)) {
      shaderDumped.add(meta.numSh);
      mkdirSync(generatedDir, { recursive: true });
      for (const [src, dst] of [
        ["accumulator.frag.glsl", `accumulator-sh${meta.numSh}.frag.glsl`],
        ["accumulator.compiled.frag.glsl", `accumulator-sh${meta.numSh}.compiled.frag.glsl`],
      ]) {
        const file = path.join(outDir, src);
        if (existsSync(file)) copyFileSync(file, path.join(generatedDir, dst));
      }
    }
  }
} finally {
  await browser.close();
  await server.close();
}

if (failures.length) {
  console.error(`Missing scenes: ${failures.join(", ")}`);
  process.exitCode = 1;
}

/** Fraction of pixels that are not the black background, as a quick sanity check. */
function coverage(buffer) {
  const { data, width, height } = PNG.sync.read(buffer);
  let lit = 0;
  for (let i = 0; i < data.length; i += 4) if (data[i] | data[i + 1] | data[i + 2]) lit++;
  return lit / (width * height);
}
