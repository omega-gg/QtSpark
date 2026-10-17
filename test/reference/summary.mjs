// Summarizes benchmark result files (bench.mjs and splatviewer --bench) over rounds: per scene
// and backend, the median over the files of the wall time per frame, the GPU time and the p95
// of the frame interval (stutter).
//
//   node summary.mjs result.json ...

import path from "node:path";
import { readJson } from "./common.mjs";

const rows = new Map();
const tools = [];
const scenes = [];

for (const file of process.argv.slice(2)) {
  const result = readJson(path.resolve(file));
  const tool = result.tool === "spark" ? "spark" : result.backend;
  if (!tools.includes(tool)) tools.push(tool);
  for (const scene of result.scenes) {
    if (!scenes.includes(scene.name)) scenes.push(scene.name);
    const key = `${scene.name} ${tool}`;
    if (!rows.has(key)) rows.set(key, { wall: [], gpu: [], p95: [] });
    const row = rows.get(key);
    const m = scene.metrics;
    row.wall.push(m.wallFrameMs?.median);
    row.gpu.push(m.gpuMs?.median);
    row.p95.push(m.frameMs?.p95);
  }
}

const median = (values) => {
  const s = values.filter((v) => v != null).sort((a, b) => a - b);
  if (s.length === 0) return null;
  const half = Math.floor(s.length / 2);
  return s.length % 2 ? s[half] : (s[half - 1] + s[half]) / 2;
};
const fmt = (v) => (v == null ? "n/a" : v.toFixed(2)).padStart(7);

const head = (...cells) => cells.map((c) => c.padStart(7)).join("");
const title = head("wall", "gpu", "p95");
console.log(`${"scene".padEnd(16)}${"backend".padEnd(9)}${title}  wall per round`);
for (const scene of scenes) {
  for (const tool of tools) {
    const row = rows.get(`${scene} ${tool}`);
    if (!row) continue;
    const values = [row.wall, row.gpu, row.p95].map((v) => fmt(median(v))).join("");
    const rounds = row.wall.map((v) => v?.toFixed(1)).join(" ");
    console.log(`${scene.padEnd(16)}${tool.padEnd(9)}${values}  ${rounds}`);
  }
}
