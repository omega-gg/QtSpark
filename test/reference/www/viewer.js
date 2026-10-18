// Full-window Spark viewer with SparkControls, for side-by-side checks against the native
// viewer. Query: ?url=<splat url>. The mesh gets quaternion [1, 0, 0, 0] (180 degrees about
// X), like Spark's examples, and the camera starts where the native viewer starts it.

import * as THREE from "three";
import { SparkControls, SparkRenderer, SplatMesh } from "@sparkjsdev/spark";

const url = new URLSearchParams(location.search).get("url");
const renderer = new THREE.WebGLRenderer();
renderer.setSize(window.innerWidth, window.innerHeight);
document.body.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.add(new SparkRenderer({ renderer }));
const camera = new THREE.PerspectiveCamera(60, window.innerWidth / window.innerHeight, 0.01, 1000);
camera.position.set(0, 0, 3);

const hud = document.createElement("div");
hud.style.cssText = "position:fixed;left:8px;top:8px;color:#ccc;font:12px monospace";
document.body.appendChild(hud);
hud.textContent = `Loading ${url}`;

const mesh = new SplatMesh({ url });
mesh.quaternion.set(1, 0, 0, 0);
scene.add(mesh);
mesh.initialized.then(() => {
  // WSplatView::resetCamera: frame the bounding sphere for the vertical fov, looking down -Z.
  const { center, radius } = robustBounds(mesh.packedSplats);
  center.applyQuaternion(mesh.quaternion);
  const distance = radius / Math.tan(THREE.MathUtils.degToRad(camera.fov) / 2);
  camera.position.copy(center).add(new THREE.Vector3(0, 0, distance));
  camera.quaternion.identity();
});

// computeBounds in WSplatView.cpp: the median of the centers and the 90th percentile distance to
// it, on a sample of at most 100k splats. Outliers are common in 3DGS captures.
function robustBounds(splats) {
  const step = Math.max(1, Math.floor(splats.numSplats / 100000));
  const xs = [], ys = [], zs = [];
  for (let i = 0; i < splats.numSplats; i += step) {
    const { center } = splats.getSplat(i);
    xs.push(center.x);
    ys.push(center.y);
    zs.push(center.z);
  }
  const nth = (values, k) => Float64Array.from(values).sort()[k];
  const middle = xs.length >> 1;
  const center = new THREE.Vector3(nth(xs, middle), nth(ys, middle), nth(zs, middle));
  const distances = xs.map((x, i) => Math.hypot(x - center.x, ys[i] - center.y, zs[i] - center.z));
  const radius = Math.max(nth(distances, Math.floor((distances.length * 9) / 10)), 0.001);
  return { center, radius };
}

window.addEventListener("resize", () => {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
});

const controls = new SparkControls({ canvas: renderer.domElement });
let frames = 0;
let since = performance.now();
renderer.setAnimationLoop(() => {
  controls.update(camera);
  renderer.render(scene, camera);
  frames++;
  const now = performance.now();
  if (now - since > 500 && mesh.isInitialized) {
    const fps = (frames * 1000) / (now - since);
    hud.textContent = `${url}  ${mesh.packedSplats.numSplats} splats  ${fps.toFixed(0)} fps`;
    frames = 0;
    since = now;
  }
});
