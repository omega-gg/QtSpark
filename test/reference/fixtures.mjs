// Downloads Spark's sample splats and writes deterministic synthetic PLYs into build/fixtures.
//
//   node fixtures.mjs [--spark <dir> --ref v2.3.1] [--force]
//
// With --spark, download URLs come from examples/assets.json at the ref; otherwise the
// sparkjs.dev URLs below (copied from that file) are used.

import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import { DEFAULT_REF, FIXTURES, ensureSpark, parseArgs, sparkDirFrom } from "./common.mjs";

const DOWNLOADS = {
  "furry-logo-pedestal.spz": "https://sparkjs.dev/assets/splats/furry-logo-pedestal.spz",
  "butterfly.spz": "https://sparkjs.dev/assets/splats/butterfly.spz",
};

const SH_C0 = 0.28209479177387814;

/** mulberry32: a small seeded PRNG returning floats in [0, 1). */
function rng(seed) {
  let a = seed >>> 0;
  const next = () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  const uniform = (lo, hi) => lo + (hi - lo) * next();
  const gauss = () => {
    const u = Math.max(next(), 1e-12);
    return Math.sqrt(-2 * Math.log(u)) * Math.cos(2 * Math.PI * next());
  };
  return { next, uniform, gauss };
}

/**
 * Synthetic splats in a unit-ish blob: a few gaussian clusters plus a thin ring, so
 * the renders have structure and plenty of overlap for sorting. Centers stay within
 * roughly [-1.5, 1.5]^3; colors follow position.
 */
export function syntheticSplats(count, seed, { shDegree = 0 } = {}) {
  const r = rng(seed);
  const clusters = Array.from({ length: 6 }, () => ({
    c: [r.uniform(-0.8, 0.8), r.uniform(-0.6, 0.6), r.uniform(-0.8, 0.8)],
    s: r.uniform(0.15, 0.4),
  }));
  const numRest = [0, 9, 24, 45][shDegree];
  const splats = [];
  for (let i = 0; i < count; i++) {
    let p;
    if (i % 4 === 3) {
      const a = r.uniform(0, 2 * Math.PI);
      p = [1.2 * Math.cos(a), 0.05 * r.gauss(), 1.2 * Math.sin(a)];
    } else {
      const k = clusters[i % clusters.length];
      p = k.c.map((c) => c + k.s * r.gauss());
    }
    const fdc = p.map((v, d) => Math.min(2, Math.max(-1, 0.5 + 0.9 * v + 0.3 * r.gauss() - 0.2 * d)));
    const scale = [r.uniform(-4, -1.5), r.uniform(-4, -1.5), r.uniform(-4.5, -1)];
    const opacity = r.uniform(-4, 4);
    // Non-normalized quaternion (w, x, y, z), as in real 3DGS training output.
    const norm = r.uniform(0.5, 2);
    const rot = [r.gauss(), r.gauss(), r.gauss(), r.gauss()].map((v) => v * norm);
    const rest = Array.from({ length: numRest }, () => r.uniform(-0.3, 0.3));
    splats.push({ p, fdc, scale, opacity, rot, rest });
  }
  return splats;
}

/** Build a binary_little_endian PLY from `{ name, count, props: [[type, name]], write(view, offset, i) }`. */
function writePly(file, elements) {
  const sizes = { float: 4, uint: 4, uchar: 1 };
  let header = "ply\nformat binary_little_endian 1.0\ncomment synthetic splat fixture, seeded\n";
  let total = 0;
  for (const el of elements) {
    header += `element ${el.name} ${el.count}\n`;
    for (const [type, name] of el.props) header += `property ${type} ${name}\n`;
    el.stride = el.props.reduce((sum, [type]) => sum + sizes[type], 0);
    total += el.stride * el.count;
  }
  header += "end_header\n";
  const head = Buffer.from(header, "ascii");
  const body = new DataView(new ArrayBuffer(total));
  let offset = 0;
  for (const el of elements) {
    for (let i = 0; i < el.count; i++) {
      el.write(body, offset, i);
      offset += el.stride;
    }
  }
  writeFileSync(file, Buffer.concat([head, Buffer.from(body.buffer)]));
  return head.length + total;
}

export function standardPly(file, splats, numRest) {
  const props = [
    ...["x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"].map((n) => ["float", n]),
    ...Array.from({ length: numRest }, (_, k) => ["float", `f_rest_${k}`]),
    ...["opacity", "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"].map((n) => [
      "float",
      n,
    ]),
  ];
  return writePly(file, [
    {
      name: "vertex",
      count: splats.length,
      props,
      write(view, offset, i) {
        const s = splats[i];
        // f_rest is planar: all coefficients of R, then G, then B.
        const values = [...s.p, 0, 0, 0, ...s.fdc, ...s.rest, s.opacity, ...s.scale, ...s.rot];
        values.forEach((v, k) => view.setFloat32(offset + 4 * k, v, true));
      },
    },
  ]);
}

const CHUNK = 256;

/** Quantize `v` in [lo, hi] to `bits` bits, as SuperSplat does. */
function quant(v, lo, hi, bits) {
  const max = (1 << bits) - 1;
  const t = hi > lo ? (v - lo) / (hi - lo) : 0;
  return Math.min(max, Math.max(0, Math.round(t * max)));
}

/**
 * SuperSplat/PlayCanvas compressed PLY, matching rust/spark-lib/src/ply.rs at v2.3.1:
 * - chunk: per 256 splats, float min/max of position, log scale and color (rgb, not f_dc);
 * - vertex: packed_position 11/10/11 bits, packed_rotation 2-bit largest index (w,x,y,z order)
 *   then the other three as 10 bits each in [-1/sqrt2, 1/sqrt2], packed_scale 11/10/11 bits
 *   of log scale, packed_color r,g,b,opacity 8 bits each (opacity linear, not logit);
 * - sh: uchar f_rest_*, planar, decoded as v * 8 / 255 - 4.
 */
function compressedPly(file, splats) {
  const count = splats.length;
  const sigmoid = (x) => 1 / (1 + Math.exp(-x));
  const rgb = splats.map((s) => s.fdc.map((f) => 0.5 + f * SH_C0));
  const chunks = [];
  for (let c = 0; c * CHUNK < count; c++) {
    const lo = Array(9).fill(Infinity);
    const hi = Array(9).fill(-Infinity);
    for (let i = c * CHUNK; i < Math.min(count, (c + 1) * CHUNK); i++) {
      const v = [...splats[i].p, ...splats[i].scale, ...rgb[i]];
      v.forEach((x, k) => {
        lo[k] = Math.min(lo[k], x);
        hi[k] = Math.max(hi[k], x);
      });
    }
    chunks.push({ lo, hi });
  }
  const chunkProps = [
    "min_x", "min_y", "min_z", "max_x", "max_y", "max_z",
    "min_scale_x", "min_scale_y", "min_scale_z", "max_scale_x", "max_scale_y", "max_scale_z",
    "min_r", "min_g", "min_b", "max_r", "max_g", "max_b",
  ].map((n) => ["float", n]);
  const numRest = splats[0].rest.length;
  const elements = [
    {
      name: "chunk",
      count: chunks.length,
      props: chunkProps,
      write(view, offset, c) {
        const { lo, hi } = chunks[c];
        const values = [...lo.slice(0, 3), ...hi.slice(0, 3), ...lo.slice(3, 6), ...hi.slice(3, 6), ...lo.slice(6), ...hi.slice(6)];
        values.forEach((v, k) => view.setFloat32(offset + 4 * k, v, true));
      },
    },
    {
      name: "vertex",
      count,
      props: ["packed_position", "packed_rotation", "packed_scale", "packed_color"].map((n) => ["uint", n]),
      write(view, offset, i) {
        const s = splats[i];
        const { lo, hi } = chunks[Math.floor(i / CHUNK)];
        const pack11_10_11 = (v, k) =>
          ((quant(v[0], lo[k], hi[k], 11) << 21) |
            (quant(v[1], lo[k + 1], hi[k + 1], 10) << 11) |
            quant(v[2], lo[k + 2], hi[k + 2], 11)) >>> 0;
        const len = Math.hypot(...s.rot);
        let q = s.rot.map((v) => v / len);
        let largest = 0;
        for (let k = 1; k < 4; k++) if (Math.abs(q[k]) > Math.abs(q[largest])) largest = k;
        if (q[largest] < 0) q = q.map((v) => -v);
        const rest = q.filter((_, k) => k !== largest);
        const r10 = (v) => quant(v, -Math.SQRT1_2, Math.SQRT1_2, 10);
        const rotation = ((largest << 30) | (r10(rest[0]) << 20) | (r10(rest[1]) << 10) | r10(rest[2])) >>> 0;
        const c = rgb[i];
        const color = ((quant(c[0], lo[6], hi[6], 8) << 24) |
          (quant(c[1], lo[7], hi[7], 8) << 16) |
          (quant(c[2], lo[8], hi[8], 8) << 8) |
          quant(sigmoid(s.opacity), 0, 1, 8)) >>> 0;
        view.setUint32(offset, pack11_10_11(s.p, 0), true);
        view.setUint32(offset + 4, rotation, true);
        view.setUint32(offset + 8, pack11_10_11(s.scale, 3), true);
        view.setUint32(offset + 12, color, true);
      },
    },
  ];
  if (numRest > 0) {
    elements.push({
      name: "sh",
      count,
      props: Array.from({ length: numRest }, (_, k) => ["uchar", `f_rest_${k}`]),
      write(view, offset, i) {
        splats[i].rest.forEach((v, k) => view.setUint8(offset + k, quant(v, -4, 4, 8)));
      },
    });
  }
  return writePly(file, elements);
}

/** Colored point cloud on a twisted torus knot: x y z float, red green blue uchar. */
function pointsPly(file, count, seed) {
  const r = rng(seed);
  const points = [];
  for (let i = 0; i < count; i++) {
    const t = (2 * Math.PI * i) / count;
    const a = r.uniform(0, 2 * Math.PI);
    const tube = 0.25 + 0.02 * r.gauss();
    const cx = Math.cos(2 * t) * (1 + 0.4 * Math.cos(3 * t));
    const cy = 0.4 * Math.sin(3 * t);
    const cz = Math.sin(2 * t) * (1 + 0.4 * Math.cos(3 * t));
    const p = [cx + tube * Math.cos(a), cy + tube * Math.sin(a), cz + tube * Math.cos(a + 1)];
    const c = [0.5 + 0.5 * Math.cos(t), 0.5 + 0.5 * Math.cos(t + 2.1), 0.5 + 0.5 * Math.cos(t + 4.2)];
    points.push({ p, c: c.map((v) => Math.round(v * 255)) });
  }
  return writePly(file, [
    {
      name: "vertex",
      count,
      props: [
        ["float", "x"], ["float", "y"], ["float", "z"],
        ["uchar", "red"], ["uchar", "green"], ["uchar", "blue"],
      ],
      write(view, offset, i) {
        const { p, c } = points[i];
        p.forEach((v, k) => view.setFloat32(offset + 4 * k, v, true));
        c.forEach((v, k) => view.setUint8(offset + 12 + k, v));
      },
    },
  ]);
}

async function download(name, url, force) {
  const file = path.join(FIXTURES, name);
  if (existsSync(file) && !force) return;
  console.log(`Downloading ${name} from ${url}`);
  let response;
  try {
    response = await fetch(url);
  } catch (error) {
    const cause = String(error.cause?.code ?? error.cause ?? error);
    if (/CERT|SSL|self.signed/i.test(cause)) {
      throw new Error(`SSL error downloading ${url} (${cause}); is a VPN on? Disable it and retry.`);
    }
    throw error;
  }
  if (!response.ok) throw new Error(`${url}: ${response.status} ${response.statusText}`);
  writeFileSync(file, Buffer.from(await response.arrayBuffer()));
}

/** Download and generate every fixture; existing files are kept unless `force`. */
export async function ensureFixtures({ sparkBuildDir, force = false } = {}) {
  mkdirSync(FIXTURES, { recursive: true });
  let urls = DOWNLOADS;
  const assetsFile = sparkBuildDir && path.join(sparkBuildDir, "examples/assets.json");
  if (assetsFile && existsSync(assetsFile)) {
    const assets = JSON.parse(readFileSync(assetsFile, "utf8"));
    urls = Object.fromEntries(Object.keys(DOWNLOADS).map((n) => [n, assets[n]?.url ?? DOWNLOADS[n]]));
  }
  const synth = {
    "synthetic-sh0.ply": (f) => standardPly(f, syntheticSplats(20000, 1), 0),
    "synthetic-sh3.ply": (f) => standardPly(f, syntheticSplats(20000, 1, { shDegree: 3 }), 45),
    "synthetic-compressed.ply": (f) => compressedPly(f, syntheticSplats(10240, 2, { shDegree: 3 })),
    "synthetic-points.ply": (f) => pointsPly(f, 50000, 3),
  };
  for (const [name, make] of Object.entries(synth)) {
    const file = path.join(FIXTURES, name);
    if (existsSync(file) && !force) continue;
    console.log(`Writing ${name} (${make(file)} bytes)`);
  }
  for (const [name, url] of Object.entries(urls)) await download(name, url, force);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  const args = parseArgs();
  let sparkBuildDir;
  if (args.spark || process.env.SPARK_DIR) {
    sparkBuildDir = ensureSpark(sparkDirFrom(args), args.ref ?? DEFAULT_REF).dir;
  }
  await ensureFixtures({ sparkBuildDir, force: !!args.force });
  console.log(`Fixtures in ${FIXTURES}`);
}
