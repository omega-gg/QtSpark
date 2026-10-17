// Full-window Spark viewer with SparkControls, for side-by-side checks against the native
// viewer. Query: ?url=<splat url>. The mesh gets quaternion [1, 0, 0, 0] (180 degrees about
// X), like Spark's examples, and the camera starts in front of its bounding box.

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
  mesh.updateMatrixWorld(true);
  const box = mesh.getBoundingBox().applyMatrix4(mesh.matrixWorld);
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3()).length();
  camera.position.copy(center).add(new THREE.Vector3(0, 0, size * 0.9));
  camera.lookAt(center);
});

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
