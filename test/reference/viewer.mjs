// Serves the Spark viewer page for a fixture name or a local splat file and opens it in the
// default browser (hardware GPU, no SwiftShader).
//
//   npm run viewer -- <file> [--spark <dir>] [--ref v2.3.1] [--port 8095] [--no-open]

import { exec } from "node:child_process";
import { existsSync } from "node:fs";
import path from "node:path";
import { BUILD, DEFAULT_REF, FIXTURES, ensureSpark, parseArgs, sparkDirFrom, standardMounts, startServer } from "./common.mjs";

const args = parseArgs();
const file = args._[0];
if (!file) throw new Error("Usage: npm run viewer -- <fixture name or splat file>");
const ref = args.ref ?? DEFAULT_REF;

// Reuse an extracted Spark when present, so the viewer needs no checkout.
let sparkBuildDir = path.join(BUILD, `spark-${ref}`);
if (args.spark || !existsSync(path.join(sparkBuildDir, "COMMIT"))) {
  sparkBuildDir = ensureSpark(sparkDirFrom(args), ref).dir;
}

const mounts = standardMounts(sparkBuildDir);
let splatUrl;
if (existsSync(file)) {
  const full = path.resolve(file);
  mounts["/local/"] = path.dirname(full);
  splatUrl = `/local/${encodeURIComponent(path.basename(full))}`;
} else if (existsSync(path.join(FIXTURES, file))) {
  splatUrl = `/fixtures/${encodeURIComponent(file)}`;
} else {
  throw new Error(`${file} is neither a file nor a fixture in build/fixtures`);
}

const server = await startServer(mounts, { port: Number(args.port ?? 8095) });
const url = `${server.url}/viewer.html?url=${encodeURIComponent(splatUrl)}`;
console.log(`Spark viewer: ${url}\nPress Ctrl+C to stop.`);
if (!args["no-open"]) {
  const opener = { win32: 'start ""', darwin: "open" }[process.platform] ?? "xdg-open";
  exec(`${opener} "${url}"`);
}
