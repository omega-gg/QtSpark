// Shared helpers for the reference harness: CLI args, repo paths, Spark extraction,
// a tiny static file server and the Chromium launcher.

import { execFileSync, spawnSync } from "node:child_process";
import { cpSync, createReadStream, existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { stat } from "node:fs/promises";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const HERE = path.dirname(fileURLToPath(import.meta.url));
export const REPO = path.resolve(HERE, "../..");
export const BUILD = path.join(REPO, "build");
export const FIXTURES = path.join(BUILD, "fixtures");
export const DEFAULT_REF = "v2.3.1";

/**
 * Parse `--key value` and `--flag` arguments; bare values go to `_`. Keys listed in
 * `multi` take every value up to the next `--key` and always yield an array.
 */
export function parseArgs({ multi = [], argv = process.argv.slice(2) } = {}) {
  const args = { _: [] };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (!arg.startsWith("--")) {
      args._.push(arg);
      continue;
    }
    const key = arg.slice(2);
    const isValue = (v) => v !== undefined && !v.startsWith("--");
    if (multi.includes(key)) {
      args[key] = args[key] ?? [];
      while (isValue(argv[i + 1])) args[key].push(argv[++i]);
    } else if (isValue(argv[i + 1])) {
      args[key] = argv[++i];
    } else {
      args[key] = true;
    }
  }
  return args;
}

/** The Spark checkout from `--spark` or the SPARK_DIR environment variable. */
export function sparkDirFrom(args) {
  const dir = args.spark ?? process.env.SPARK_DIR;
  if (!dir || dir === true) {
    throw new Error("Pass --spark <dir> or set SPARK_DIR to a Spark git checkout");
  }
  return path.resolve(dir);
}

export function git(sparkDir, ...args) {
  return execFileSync("git", ["-C", sparkDir, ...args], { encoding: "utf8" }).trim();
}

/**
 * Extract `dist` and `examples/assets.json` at `ref` into build/spark-<ref>/ with
 * `git archive`, without touching the checkout. Returns the extracted directory.
 */
export function ensureSpark(sparkDir, ref, { build = false } = {}) {
  const dest = path.join(BUILD, `spark-${ref}`);
  const commit = git(sparkDir, "rev-parse", `${ref}^{commit}`);
  const stamp = path.join(dest, "COMMIT");
  if (build) {
    buildSpark(sparkDir, ref, dest);
  } else if (!existsSync(stamp)) {
    mkdirSync(dest, { recursive: true });
    const paths = ["dist", "examples/assets.json"];
    const archive = spawnSync("git", ["-C", sparkDir, "archive", ref, ...paths], {
      maxBuffer: 1 << 30,
    });
    if (archive.status !== 0) throw new Error(`git archive failed: ${archive.stderr}`);
    // Run tar in `dest`: GNU tar reads "C:" in a -C argument as a remote host.
    const untar = spawnSync("tar", ["-x"], { cwd: dest, input: archive.stdout });
    if (untar.status !== 0) throw new Error(`tar failed: ${untar.stderr}`);
  }
  writeFileSync(stamp, `${ref} ${commit}\n`);
  return { dir: dest, commit };
}

/**
 * TODO: build dist from source for refs whose committed dist is stale. Untested sketch:
 * a git worktree under build/, then npm ci, npm run build:wasm (cargo and wasm-pack) and
 * npm run build, and copy dist + examples/assets.json into `dest`.
 */
function buildSpark(sparkDir, ref, dest) {
  const tree = path.join(BUILD, `spark-src-${ref}`);
  if (!existsSync(tree)) git(sparkDir, "worktree", "add", "--detach", tree, ref);
  const npm = process.platform === "win32" ? "npm.cmd" : "npm";
  for (const script of [["ci"], ["run", "build:wasm"], ["run", "build"]]) {
    execFileSync(npm, script, { cwd: tree, stdio: "inherit", shell: process.platform === "win32" });
  }
  cpSync(path.join(tree, "dist"), path.join(dest, "dist"), { recursive: true });
  cpSync(path.join(tree, "examples/assets.json"), path.join(dest, "examples/assets.json"));
}

const MIME = {
  ".html": "text/html",
  ".js": "text/javascript",
  ".mjs": "text/javascript",
  ".json": "application/json",
  ".map": "application/json",
  ".wasm": "application/wasm",
  ".png": "image/png",
};

/**
 * Serve `mounts` ({ "/prefix/": dir }, longest prefix wins) on localhost (`port` 0: any).
 * With `outDir`, `PUT /out/<path>` writes the request body to `<outDir>/<path>`.
 */
export async function startServer(mounts, { outDir, port = 0 } = {}) {
  const prefixes = Object.keys(mounts).sort((a, b) => b.length - a.length);
  const server = http.createServer(async (req, res) => {
    try {
      const url = decodeURIComponent(new URL(req.url, "http://x").pathname);
      if (req.method === "PUT" && outDir && url.startsWith("/out/")) {
        const file = safeJoin(outDir, url.slice(5));
        const chunks = [];
        for await (const chunk of req) chunks.push(chunk);
        mkdirSync(path.dirname(file), { recursive: true });
        writeFileSync(file, Buffer.concat(chunks));
        res.writeHead(204).end();
        return;
      }
      const prefix = prefixes.find((p) => url.startsWith(p));
      if (!prefix) return res.writeHead(404).end();
      let file = safeJoin(mounts[prefix], url.slice(prefix.length));
      if ((await stat(file).catch(() => null))?.isDirectory()) file = path.join(file, "index.html");
      const info = await stat(file).catch(() => null);
      if (!info) return res.writeHead(404).end();
      res.writeHead(200, {
        "Content-Type": MIME[path.extname(file)] ?? "application/octet-stream",
        "Content-Length": info.size,
        "Cache-Control": "no-store",
      });
      createReadStream(file).pipe(res);
    } catch (error) {
      res.writeHead(500).end(String(error));
    }
  });
  await new Promise((resolve) => server.listen(port, "127.0.0.1", resolve));
  const url = `http://127.0.0.1:${server.address().port}`;
  return { url, close: () => new Promise((resolve) => server.close(resolve)) };
}

function safeJoin(root, rel) {
  const base = path.resolve(root);
  const file = path.resolve(base, rel.replace(/^\/+/, ""));
  if (file !== base && !file.startsWith(base + path.sep)) throw new Error(`Bad path ${rel}`);
  return file;
}

/** Standard mounts: the Spark dist, three from node_modules, fixtures and the www pages. */
export function standardMounts(sparkBuildDir) {
  return {
    "/spark/": path.join(sparkBuildDir, "dist"),
    "/three/": path.join(HERE, "node_modules/three"),
    "/fixtures/": FIXTURES,
    "/": path.join(HERE, "www"),
  };
}

/** Spark's own SwiftShader flags (test/browser/playwright.config.ts) for deterministic output. */
export const SWIFTSHADER_ARGS = [
  "--use-angle=swiftshader",
  "--use-gl=angle",
  "--enable-unsafe-swiftshader",
  "--ignore-gpu-blocklist",
];

/** Forward page console output and errors to the terminal. */
export function logPage(page, { quiet = false } = {}) {
  page.on("pageerror", (error) => console.error(`[page error] ${error.message}`));
  page.on("console", (msg) => {
    const type = msg.type();
    if (type === "error" || type === "warning") {
      // Skip GPU performance notices and HLSL compiler warnings echoed by three.
      const text = msg.text();
      const warningsOnly = text.includes("Program Info Log") && !/error/i.test(text);
      const noise = text.includes("GL Driver Message") || warningsOnly;
      if (!noise) console.error(`[page ${type}] ${text}`);
    } else if (!quiet) {
      console.log(`[page] ${msg.text()}`);
    }
  });
}

export function readJson(file) {
  return JSON.parse(readFileSync(file, "utf8"));
}
