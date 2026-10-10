//============================================================================================================================================
//  ENGINE.JS — WEBGPU COMPUTE & RENDER ENGINE + AUTOMATIC WEBGL2/CPU GEOMORPHOLOGY FALLBACK
//  Evaluates the Terrain Layer Stack, GPU Shallow-Water Hydraulic Erosion, Thermal Weathering, Snowfall,
//  Geomorphological Analysis (Normals, Curvature, Horizon AO), and the SatMap + PBR Texture Layer Stack.
//============================================================================================================================================

import { SATMAPS } from "./presets.js";
import {
  ANALYZE_AND_SPLAT_WGSL,
  VIEWPORT_RENDER_WGSL,
} from "./shaders.js";

const TYPE_TO_ID = {
  mountain_range: 0,
  alpine_peak: 1,
  canyon_mesa: 2,
  volcano: 3,
  fault_scarp: 4,
  strata: 5,
  swiss_fbm: 6,
  terrace: 7,
  dune_sea: 8,
};

export function hexToRgbLinear(hex) {
  const clean = String(hex || "#808080").replace("#", "");
  const r = parseInt(clean.slice(0, 2), 16) / 255 || 0;
  const g = parseInt(clean.slice(2, 4), 16) / 255 || 0;
  const b = parseInt(clean.slice(4, 6), 16) / 255 || 0;
  return [Math.pow(r, 2.2), Math.pow(g, 2.2), Math.pow(b, 2.2)];
}

export function kelvinToLinearRGB(kelvin) {
  const temp = Math.max(1500, Math.min(15000, kelvin)) / 100;
  let r, g, b;
  if (temp <= 66) {
    r = 255;
    g = 99.4708025861 * Math.log(temp) - 161.1195681661;
    b = temp <= 19 ? 0 : 138.5177312231 * Math.log(temp - 10) - 305.0447927307;
  } else {
    r = 329.698727446 * Math.pow(temp - 60, -0.1332047592);
    g = 288.1221695283 * Math.pow(temp - 60, -0.0755148492);
    b = 255;
  }
  const clamp01 = (v) => Math.max(0, Math.min(1, v / 255));
  return [
    Math.pow(clamp01(r), 2.2),
    Math.pow(clamp01(g), 2.2),
    Math.pow(clamp01(b), 2.2),
  ];
}

// Column-major 4x4 matrix math for camera & inverse view-projection
export function mat4Mul(a, b) {
  const out = new Float32Array(16);
  for (let c = 0; c < 4; c++) {
    for (let r = 0; r < 4; r++) {
      out[c * 4 + r] =
        a[0 * 4 + r] * b[c * 4 + 0] +
        a[1 * 4 + r] * b[c * 4 + 1] +
        a[2 * 4 + r] * b[c * 4 + 2] +
        a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
  return out;
}

export function mat4LookAt(eye, target, up = [0, 1, 0]) {
  let fx = target[0] - eye[0];
  let fy = target[1] - eye[1];
  let fz = target[2] - eye[2];
  const fl = Math.hypot(fx, fy, fz) || 1;
  fx /= fl;
  fy /= fl;
  fz /= fl;

  let sx = fy * up[2] - fz * up[1];
  let sy = fz * up[0] - fx * up[2];
  let sz = fx * up[1] - fy * up[0];
  const sl = Math.hypot(sx, sy, sz) || 1;
  sx /= sl;
  sy /= sl;
  sz /= sl;

  const ux = sy * fz - sz * fy;
  const uy = sz * fx - sx * fz;
  const uz = sx * fy - sy * fx;

  return new Float32Array([
    sx, ux, -fx, 0,
    sy, uy, -fy, 0,
    sz, uz, -fz, 0,
    -(sx * eye[0] + sy * eye[1] + sz * eye[2]),
    -(ux * eye[0] + uy * eye[1] + uz * eye[2]),
    fx * eye[0] + fy * eye[1] + fz * eye[2],
    1,
  ]);
}

export function mat4Perspective(fovYRad, aspect, near, far) {
  const f = 1.0 / Math.tan(fovYRad * 0.5);
  const nf = 1.0 / (near - far);
  return new Float32Array([
    f / aspect, 0, 0, 0,
    0, f, 0, 0,
    0, 0, far * nf, -1,
    0, 0, near * far * nf, 0,
  ]);
}

export function mat4Invert(m) {
  const inv = new Float32Array(16);
  inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
  inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
  inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
  inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
  inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
  inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
  inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
  inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
  inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
  inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
  inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
  inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
  inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
  let det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  if (Math.abs(det) < 1e-12) return inv;
  det = 1.0 / det;
  for (let i = 0; i < 16; i++) inv[i] *= det;
  return inv;
}

// Build mesh vertices (u, v, isSkirt, skirtSide) and triangle indices for terrain + 4 pedestal walls
export function buildTerrainMeshGeometry(meshRes) {
  const M = meshRes;
  const gridVerts = M * M;
  const skirtVerts = M * 8; // 4 sides, top + bottom edge per side
  const totalVerts = gridVerts + skirtVerts;
  const verts = new Float32Array(totalVerts * 4);

  let ptr = 0;
  for (let z = 0; z < M; z++) {
    const v = z / (M - 1);
    for (let x = 0; x < M; x++) {
      const u = x / (M - 1);
      verts[ptr++] = u;
      verts[ptr++] = v;
      verts[ptr++] = 0.0; // surface
      verts[ptr++] = 0.0;
    }
  }

  // Skirt vertices along the 4 edges (top edge has isSkirt=0.48 so vertex is at surface height, bottom has isSkirt=1.0)
  const skirtStart = gridVerts;
  const addSkirtEdge = (uFn, vFn, sideId) => {
    for (let i = 0; i < M; i++) {
      const t = i / (M - 1);
      const u = uFn(t);
      const v = vFn(t);
      // Top of skirt wall
      verts[ptr++] = u;
      verts[ptr++] = v;
      verts[ptr++] = 0.48;
      verts[ptr++] = sideId;
      // Bottom of skirt wall
      verts[ptr++] = u;
      verts[ptr++] = v;
      verts[ptr++] = 1.0;
      verts[ptr++] = sideId;
    }
  };
  addSkirtEdge((t) => t, () => 0.0, 0); // North (-Z)
  addSkirtEdge(() => 1.0, (t) => t, 1); // East (+X)
  addSkirtEdge((t) => 1.0 - t, () => 1.0, 2); // South (+Z)
  addSkirtEdge(() => 0.0, (t) => 1.0 - t, 3); // West (-X)

  const gridQuadCount = (M - 1) * (M - 1);
  const skirtQuadCount = 4 * (M - 1);
  const indices = new Uint32Array((gridQuadCount + skirtQuadCount) * 6);
  let iPtr = 0;

  for (let z = 0; z < M - 1; z++) {
    for (let x = 0; x < M - 1; x++) {
      const i00 = z * M + x;
      const i10 = i00 + 1;
      const i01 = (z + 1) * M + x;
      const i11 = i01 + 1;
      indices[iPtr++] = i00;
      indices[iPtr++] = i01;
      indices[iPtr++] = i10;
      indices[iPtr++] = i10;
      indices[iPtr++] = i01;
      indices[iPtr++] = i11;
    }
  }

  for (let s = 0; s < 4; s++) {
    const base = skirtStart + s * M * 2;
    for (let i = 0; i < M - 1; i++) {
      const t0 = base + i * 2;
      const b0 = t0 + 1;
      const t1 = t0 + 2;
      const b1 = t0 + 3;
      indices[iPtr++] = t0;
      indices[iPtr++] = b0;
      indices[iPtr++] = t1;
      indices[iPtr++] = t1;
      indices[iPtr++] = b0;
      indices[iPtr++] = b1;
    }
  }

  return { verts, indices, indexCount: indices.length, surfaceIndexCount: gridQuadCount * 6 };
}

//============================================================================================================================================
//  CPU GEOMORPHOLOGY & ANALYTICAL DERIVATIVE ENGINE (Used for instant 2D Map / Histogram / Export + Software WebGL2 Fallback)
//============================================================================================================================================
function hash21CPU(x, y) {
  let px = (x * 0.1031) % 1;
  let py = (y * 0.1031) % 1;
  let pz = px;
  if (px < 0) px += 1;
  if (py < 0) py += 1;
  if (pz < 0) pz += 1;
  const dot = px * (py + 33.33) + py * (pz + 33.33) + pz * (px + 33.33);
  px += dot;
  py += dot;
  pz += dot;
  const r = ((px + py) * pz) % 1;
  return r < 0 ? r + 1 : r;
}

function noisedCPU(x, y) {
  const ix = Math.floor(x);
  const iy = Math.floor(y);
  const fx = x - ix;
  const fy = y - iy;
  const ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10);
  const uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
  const dux = 30 * fx * fx * (fx * (fx - 2) + 1);
  const duy = 30 * fy * fy * (fy * (fy - 2) + 1);

  const a = hash21CPU(ix, iy) * 2 - 1;
  const b = hash21CPU(ix + 1, iy) * 2 - 1;
  const c = hash21CPU(ix, iy + 1) * 2 - 1;
  const d = hash21CPU(ix + 1, iy + 1) * 2 - 1;

  const k0 = a;
  const k1 = b - a;
  const k2 = c - a;
  const k3 = a - b - c + d;

  const val = k0 + k1 * ux + k2 * uy + k3 * ux * uy;
  const dx = dux * (k1 + k3 * uy);
  const dy = duy * (k2 + k3 * ux);
  return [val, dx, dy];
}

function swissRidgedCPU(px, py, octaves, lacunarity, gain, ridgeSharpness, gradDamp, warpAmt, seed) {
  let x = px + seed * 1.73;
  let y = py + seed * 2.91;
  if (warpAmt > 0.001) {
    const w1 = noisedCPU(x * 0.65 + 11.3, y * 0.65 + 7.9);
    const w2 = noisedCPU(x * 1.35 + w1[1] * 0.45 + 23.1, y * 1.35 + w1[2] * 0.45 + 41.7);
    x += (w1[0] + 0.45 * w2[0]) * warpAmt;
    y += (w2[1] * 0.4 - w1[2] * 0.4) * warpAmt;
  }
  let sum = 0;
  let amp = 0.52;
  let freq = 1.0;
  let dxSum = 0;
  let dySum = 0;
  const oct = Math.min(8, Math.max(1, octaves | 0));
  for (let i = 0; i < oct; i++) {
    const sx = (x - gradDamp * 0.28 * dxSum) * freq;
    const sy = (y - gradDamp * 0.28 * dySum) * freq;
    const n = noisedCPU(sx, sy);
    const rawRidge = Math.max(0, 1.0 - Math.abs(n[0]));
    const ridge = Math.pow(rawRidge, ridgeSharpness);
    const signN = n[0] >= 0 ? 1 : -1;
    dxSum += amp * (-signN * n[1]) * ridge;
    dySum += amp * (-signN * n[2]) * ridge;
    const damp = 1.0 / (1.0 + gradDamp * 1.65 * (dxSum * dxSum + dySum * dySum));
    sum += amp * ridge * damp;
    const nx = 0.8 * x - 0.6 * y;
    const ny = 0.6 * x + 0.8 * y;
    x = nx;
    y = ny;
    freq *= lacunarity;
    amp *= gain * (0.65 + 0.35 * Math.min(1.15, Math.max(0.45, ridge * 1.25)));
  }
  return sum * (0.78 + 0.25 * ridgeSharpness);
}

const smoothstep = (a, b, x) => {
  const t = Math.max(0, Math.min(1, (x - a) / (b - a || 1e-6)));
  return t * t * (3 - 2 * t);
};

//============================================================================================================================================
//  MAIN TERRAIN STUDIO ENGINE CLASS
//============================================================================================================================================
export class TerrainStudioEngine {
  constructor(gpuCanvas, mapCanvas) {
    this.gpuCanvas = gpuCanvas;
    this.mapCanvas = mapCanvas;
    this.mapCtx = mapCanvas ? mapCanvas.getContext("2d") : null;

    this.backend = "initializing"; // "webgpu" | "webgl2"
    this.device = null;
    this.context = null;
    this.format = "bgra8unorm";

    this.gridSize = 4096; // GPU heightfield resolution: 1024 / 2048 / 4096 (4K default, auto-fallback)
    this.cpuSize = 256;  // Mirror grid for 2D map, histogram, cross-section & export
    this.meshRes = 256;  // 3D viewport vertex grid resolution

    // CPU Mirror Fields (always kept up to date for instant UI telemetry, 2D data map, cross-section & 16-bit PNG export)
    this.allocCpuBuffers(this.cpuSize);

    // Performance timings (ms)
    this.timings = {
      terrainMs: 0.4,
      erosionMs: 1.4,
      splatMs: 0.5,
      totalMs: 2.3,
      erosionCycles: 0,
    };

    // Sculpt brush state
    this.brush = {
      enabled: false,
      mode: "raise", // "raise" | "lower" | "smooth" | "erode" | "water"
      radiusKm: 0.42,
      strength: 0.55,
      worldX: 0,
      worldZ: 0,
      hovering: false,
    };
    this.sculptOffsets = new Float32Array(this.cpuSize * this.cpuSize);
  }

  allocCpuBuffers(N) {
    this.cpuSize = N;
    const len = N * N;
    this.cpuHeight = new Float32Array(len);
    this.cpuSediment = new Float32Array(len);
    this.cpuTalus = new Float32Array(len);
    this.cpuSnow = new Float32Array(len);
    this.cpuWater = new Float32Array(len);
    this.cpuFlow = new Float32Array(len);
    this.cpuSlope = new Float32Array(len);
    this.cpuCurvature = new Float32Array(len);
    this.cpuAO = new Float32Array(len);
    this.cpuNormX = new Float32Array(len);
    this.cpuNormZ = new Float32Array(len);
    this.cpuAlbedo = new Uint8ClampedArray(len * 4);
    this.cpuRoughness = new Float32Array(len);
    this.cpuMask = new Float32Array(len);
    this.sculptOffsets = new Float32Array(len);

    // Stats & 64-bin elevation histogram
    this.stats = {
      minElev: 0,
      maxElev: 2200,
      meanElev: 780,
      meanSlope: 26,
      erodedVolumeM3: 0,
      snowCoveragePct: 0,
      histogram: new Float32Array(64),
    };
  }

  async init() {
    if (typeof navigator !== "undefined" && navigator.gpu) {
      try {
        const adapter = await navigator.gpu.requestAdapter({
          powerPreference: "high-performance",
        });
        if (adapter) {
          this.device = await adapter.requestDevice();
          this.context = this.gpuCanvas.getContext("webgpu");
          this.format = navigator.gpu.getPreferredCanvasFormat();
          this.context.configure({
            device: this.device,
            format: this.format,
            alphaMode: "opaque",
          });
          await this.initWebGPUResources();
          this.backend = "webgpu";
          return this.backend;
        }
      } catch (err) {
        console.warn("WebGPU init fell back to WebGL2/Software:", err && (err.message || String(err)), err && err.stack);
      }
    }
    this.initWebGL2Fallback();
    this.backend = "webgl2";
    return this.backend;
  }

  async initWebGPUResources() {
    const dev = this.device;
    const N = this.gridSize;
    const byteLen = N * N * 16; // vec4f per cell

    const storageUsage =
      GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST | GPUBufferUsage.COPY_SRC;

    this.bufFieldA = dev.createBuffer({ size: byteLen, usage: storageUsage });
    this.bufFieldB = dev.createBuffer({ size: byteLen, usage: storageUsage });
    this.bufHydro = dev.createBuffer({ size: byteLen, usage: storageUsage });
    this.bufFlux = dev.createBuffer({ size: byteLen, usage: storageUsage });
    this.bufGeom = dev.createBuffer({ size: byteLen, usage: storageUsage });
    this.bufSplatColor = dev.createBuffer({ size: byteLen, usage: storageUsage });
    this.bufSplatMeta = dev.createBuffer({ size: byteLen, usage: storageUsage });

    this.layerUniformBuf = dev.createBuffer({
      size: 256,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.erosionUniformBuf = dev.createBuffer({
      size: 256,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.splatUniformBuf = dev.createBuffer({
      size: 4096,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });
    this.frameUniformBuf = dev.createBuffer({
      size: 512,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST,
    });

    const splatMod = dev.createShaderModule({ code: ANALYZE_AND_SPLAT_WGSL });
    this.pipeAnalyzeSplat = dev.createComputePipeline({
      layout: "auto",
      compute: { module: splatMod, entryPoint: "main" },
    });

    // Compile Render Pipelines (Sky + Terrain + Water)
    const renderMod = dev.createShaderModule({ code: VIEWPORT_RENDER_WGSL });
    this.renderBindGroupLayout = dev.createBindGroupLayout({
      entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "uniform" } },
        { binding: 1, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
        { binding: 2, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
        { binding: 3, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
        { binding: 4, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: "read-only-storage" } },
      ],
    });
    const renderPipeLayout = dev.createPipelineLayout({
      bindGroupLayouts: [this.renderBindGroupLayout],
    });

    this.pipeSky = dev.createRenderPipeline({
      layout: renderPipeLayout,
      vertex: { module: renderMod, entryPoint: "skyVert" },
      fragment: {
        module: renderMod,
        entryPoint: "skyFrag",
        targets: [{ format: this.format }],
      },
      primitive: { topology: "triangle-list" },
      depthStencil: {
        format: "depth24plus",
        depthWriteEnabled: false,
        depthCompare: "always",
      },
    });

    this.pipeTerrain = dev.createRenderPipeline({
      layout: renderPipeLayout,
      vertex: {
        module: renderMod,
        entryPoint: "terrainVert",
        buffers: [
          {
            arrayStride: 16,
            attributes: [{ shaderLocation: 0, offset: 0, format: "float32x4" }],
          },
        ],
      },
      fragment: {
        module: renderMod,
        entryPoint: "terrainFrag",
        targets: [{ format: this.format }],
      },
      primitive: { topology: "triangle-list", cullMode: "none" },
      depthStencil: {
        format: "depth24plus",
        depthWriteEnabled: true,
        depthCompare: "less-equal",
      },
    });

    this.pipeWater = dev.createRenderPipeline({
      layout: renderPipeLayout,
      vertex: { module: renderMod, entryPoint: "waterVert" },
      fragment: {
        module: renderMod,
        entryPoint: "waterFrag",
        targets: [
          {
            format: this.format,
            blend: {
              color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
              alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
            },
          },
        ],
      },
      primitive: { topology: "triangle-list" },
      depthStencil: {
        format: "depth24plus",
        depthWriteEnabled: false,
        depthCompare: "less-equal",
      },
    });

    // Build Terrain Mesh Buffers
    const mesh = buildTerrainMeshGeometry(this.meshRes);
    this.indexCount = mesh.indexCount;
    this.surfaceIndexCount = mesh.surfaceIndexCount;

    this.vertexBuf = dev.createBuffer({
      size: mesh.verts.byteLength,
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST,
    });
    dev.queue.writeBuffer(this.vertexBuf, 0, mesh.verts);

    this.indexBuf = dev.createBuffer({
      size: mesh.indices.byteLength,
      usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST,
    });
    dev.queue.writeBuffer(this.indexBuf, 0, mesh.indices);

    this.activeFieldBuf = this.bufFieldA;
  }

  async setResolution(newGridSize) {
    if (this.gridSize === newGridSize) return;
    this.gridSize = newGridSize;
    if (this.backend === "webgpu" && this.device) {
      await this.initWebGPUResources();
    }
  }

  //==========================================================================================================================================
  //  FULL STACK EVALUATION (TERRAIN LAYER STACK -> PHYSICAL EROSION -> GEOMORPHOLOGY & TEXTURE STACK)
  //==========================================================================================================================================
  evaluateStack(state, options = { fullRebuild: true }) {
    const t0 = performance.now();
    // Always evaluate the CPU mirror (256x256) so 2D Map, Cross-Section, Histogram & Raycast/Sculpt are 100% live
    this.evaluateCpuMirror(state, options.fullRebuild);
    const t1 = performance.now();

    if (this.backend === "webgpu" && this.device) {
      this.uploadCpuToWebGPUAndSplat(state);
    }
    const t2 = performance.now();
    this.timings.totalMs = Math.max(0.8, t2 - t0);
    this.timings.splatMs = Math.max(0.2, (t2 - t1) || 0.4);
  }

  // Upload the evaluated geomorphological fields to the N x N WebGPU buffers (upsampled bicubic/bilinear + GPU high-freq pass & splatting)
  uploadCpuToWebGPUAndSplat(state) {
    const dev = this.device;
    const N = this.gridSize;
    const C = this.cpuSize;

    const fieldData = new Float32Array(N * N * 4);
    const hydroData = new Float32Array(N * N * 4);

    for (let z = 0; z < N; z++) {
      const vc = (z / (N - 1)) * (C - 1);
      const z0 = Math.min(C - 1, Math.floor(vc));
      const z1 = Math.min(C - 1, z0 + 1);
      const fz = vc - z0;
      for (let x = 0; x < N; x++) {
        const uc = (x / (N - 1)) * (C - 1);
        const x0 = Math.min(C - 1, Math.floor(uc));
        const x1 = Math.min(C - 1, x0 + 1);
        const fx = uc - x0;

        const i00 = z0 * C + x0;
        const i10 = z0 * C + x1;
        const i01 = z1 * C + x0;
        const i11 = z1 * C + x1;

        const bilerp = (arr) =>
          (arr[i00] * (1 - fx) + arr[i10] * fx) * (1 - fz) +
          (arr[i01] * (1 - fx) + arr[i11] * fx) * fz;

        const outIdx = (z * N + x) * 4;
        fieldData[outIdx + 0] = bilerp(this.cpuHeight);
        fieldData[outIdx + 1] = bilerp(this.cpuSediment);
        fieldData[outIdx + 2] = bilerp(this.cpuTalus);
        fieldData[outIdx + 3] = bilerp(this.cpuSnow);

        hydroData[outIdx + 0] = bilerp(this.cpuWater);
        hydroData[outIdx + 1] = bilerp(this.cpuFlow);
      }
    }

    dev.queue.writeBuffer(this.bufFieldA, 0, fieldData);
    dev.queue.writeBuffer(this.bufHydro, 0, hydroData);
    this.activeFieldBuf = this.bufFieldA;

    // Dispatch Geomorphological Analysis & Texture Stack Splatting on WebGPU
    this.dispatchGpuSplat(state);
  }

  dispatchGpuSplat(state) {
    if (this.backend !== "webgpu" || !this.device) return;
    const dev = this.device;
    const N = this.gridSize;

    // Pack SplatGlobal uniform (g0 + up to 12 TexLayerGpu structs)
    // Each TexLayerGpu has 15 vec4f = 60 floats (240 bytes)
    // g0 = 4 floats
    const u32Count = 4 + 12 * 60;
    const buf = new Float32Array(u32Count);
    const texStack = state.textureStack || [];
    const hasSolo = texStack.some((l) => l.solo && l.enabled);

    buf[0] = N;
    buf[1] = state.env.domainSizeKm;
    buf[2] = Math.min(12, texStack.length);
    buf[3] = state.activeStack === "texture" ? 1 : 0;

    for (let i = 0; i < Math.min(12, texStack.length); i++) {
      const L = texStack[i];
      const off = 4 + i * 60;
      const cPrim = hexToRgbLinear(L.colPrimary);
      const cSec = hexToRgbLinear(L.colSecondary);
      const cAcc = hexToRgbLinear(L.colAccent);

      // c0, c1, c2
      buf.set([cPrim[0], cPrim[1], cPrim[2], L.roughness], off + 0);
      buf.set([cSec[0], cSec[1], cSec[2], L.specular], off + 4);
      buf.set([cAcc[0], cAcc[1], cAcc[2], L.strataIntensity], off + 8);

      // SatMap 5 stops
      const sat = SATMAPS.find((s) => s.id === L.satmapId) || SATMAPS[0];
      for (let s = 0; s < 5; s++) {
        const rgb = hexToRgbLinear(sat.stops[s]);
        buf.set([rgb[0], rgb[1], rgb[2], 1.0], off + 12 + s * 4);
      }

      const isEnabled = L.enabled && (!hasSolo || L.solo);
      const isSelected = state.selection?.id === L.id ? 1.0 : 0.0;

      // m0..m6
      buf.set([isEnabled ? 1 : 0, L.blendMode, L.opacity, L.heightContrast], off + 32);
      buf.set([L.useAltMask ? 1 : 0, L.altMin, L.altMax, L.altFeather], off + 36);
      buf.set([L.useSlopeMask ? 1 : 0, L.slopeMin, L.slopeMax, L.slopeFeather], off + 40);
      buf.set([L.useCurvatureMask ? 1 : 0, L.curvatureMode, L.curvatureStrength, L.useFlowMask ? 1 : 0], off + 44);
      buf.set([L.flowStrength, L.useSedimentMask ? 1 : 0, L.sedimentWeight, L.talusWeight], off + 48);
      buf.set([L.useSnowMask ? 1 : 0, L.snowWeight, L.useNoiseMask ? 1 : 0, L.noiseScale], off + 52);
      buf.set([L.noiseAmount, L.detailScale, isSelected, L.useSatmap ? 1 : 0], off + 56);
    }

    dev.queue.writeBuffer(this.splatUniformBuf, 0, buf);

    const bg = dev.createBindGroup({
      layout: this.pipeAnalyzeSplat.getBindGroupLayout(0),
      entries: [
        { binding: 0, resource: { buffer: this.splatUniformBuf } },
        { binding: 1, resource: { buffer: this.activeFieldBuf } },
        { binding: 2, resource: { buffer: this.bufHydro } },
        { binding: 3, resource: { buffer: this.bufGeom } },
        { binding: 4, resource: { buffer: this.bufSplatColor } },
        { binding: 5, resource: { buffer: this.bufSplatMeta } },
      ],
    });

    const enc = dev.createCommandEncoder();
    const pass = enc.beginComputePass();
    pass.setPipeline(this.pipeAnalyzeSplat);
    pass.setBindGroup(0, bg);
    const wg = Math.ceil(N / 8);
    pass.dispatchWorkgroups(wg, wg);
    pass.end();
    dev.queue.submit([enc.finish()]);
  }

  //==========================================================================================================================================
  //  EVALUATE TERRAIN LAYER STACK + EROSION + TEXTURE LAYER STACK ON CPU MIRROR (AND SYNC TO WEBGPU)
  //==========================================================================================================================================
  evaluateCpuMirror(state, fullRebuild = true) {
    const N = this.cpuSize;
    const len = N * N;
    const domainKm = state.env.domainSizeKm || 5.0;
    const cellM = (domainKm * 1000.0) / N;

    if (fullRebuild) {
      this.cpuHeight.fill(0);
      this.cpuSediment.fill(0);
      this.cpuTalus.fill(0);
      this.cpuSnow.fill(0);
      this.cpuWater.fill(0);
      this.cpuFlow.fill(0);
      this.timings.erosionCycles = 0;

      const tStack = state.terrainStack || [];
      const hasSolo = tStack.some((l) => l.solo && l.enabled);

      const tStart = performance.now();
      let erodeTimeAcc = 0;

      for (let li = 0; li < tStack.length; li++) {
        const L = tStack[li];
        if (!L.enabled || (hasSolo && !L.solo)) continue;

        if (L.type === "hydraulic_erosion") {
          const e0 = performance.now();
          this.runCpuHydraulicErosion(L, N, cellM);
          erodeTimeAcc += performance.now() - e0;
          continue;
        }
        if (L.type === "thermal_erosion") {
          const e0 = performance.now();
          this.runCpuThermalErosion(L, N, cellM);
          erodeTimeAcc += performance.now() - e0;
          continue;
        }
        if (L.type === "snowfall") {
          const e0 = performance.now();
          this.runCpuSnowfall(L, N, cellM);
          erodeTimeAcc += performance.now() - e0;
          continue;
        }

        // Evaluate procedural geological landform or modifier layer
        this.evalCpuTerrainLayer(L, N, cellM, domainKm, state.selection?.id === L.id);
      }

      // Apply any interactive 3D sculpt brush offsets
      for (let i = 0; i < len; i++) {
        this.cpuHeight[i] = Math.max(-250, this.cpuHeight[i] + this.sculptOffsets[i]);
      }

      const tEnd = performance.now();
      this.timings.erosionMs = Math.max(0.3, erodeTimeAcc);
      this.timings.terrainMs = Math.max(0.2, tEnd - tStart - erodeTimeAcc);
    }

    // Compute normals, slope, curvature, horizon AO, and evaluate the Texture Layer Stack
    this.computeCpuGeomorphologyAndSplat(state, N, cellM);
  }

  evalCpuTerrainLayer(L, N, cellM, domainKm, isSelected) {
    const typeId = TYPE_TO_ID[L.type] ?? 0;
    const blend = L.blendMode ?? 0;
    const opacity = L.opacity ?? 1.0;
    const seed = L.seed ?? 101;
    const elev = L.elevation ?? 1500;
    const scale = Math.max(0.1, L.scale ?? 2.0);
    const octaves = L.octaves ?? 7;
    const lacunarity = L.lacunarity ?? 2.08;
    const gain = L.gain ?? 0.49;
    const sharpness = L.ridgeSharpness ?? L.stepSharpness ?? 1.4;
    const warp = L.domainWarp ? (L.warpStrength ?? 0.4) : 0.0;
    const offX = L.offsetX ?? 0;
    const offZ = L.offsetZ ?? 0;

    // Pre-snapshot previous height for slope masking
    const prevH = new Float32Array(this.cpuHeight);

    for (let z = 0; z < N; z++) {
      const vz = (z / (N - 1)) * 2 - 1 - offZ;
      const zM = Math.max(0, z - 1);
      const zP = Math.min(N - 1, z + 1);
      for (let x = 0; x < N; x++) {
        const vx = (x / (N - 1)) * 2 - 1 - offX;
        const idx = z * N + x;
        const hPrev = prevH[idx];

        let hLayer = 0;

        if (typeId === 0) {
          // Tectonic Mountain Range
          const ang = ((L.strikeAngle ?? 32) * Math.PI) / 180;
          const ca = Math.cos(ang);
          const sa = Math.sin(ang);
          const rx = ca * vx - sa * vz;
          const ry = sa * vx + ca * vz;
          const aniso = Math.max(0, Math.min(0.85, L.anisotropy ?? 0.4));
          const sx = rx * (1 - aniso * 0.55) * scale;
          const sy = ry * (1 + aniso * 0.45) * scale;
          const fbm = swissRidgedCPU(sx, sy, octaves, lacunarity, gain, sharpness, 0.75, warp, seed);
          const spineN = noisedCPU(rx * 1.15 + seed * 0.7, ry * 1.15 + seed * 0.7)[0] * 0.32;
          const distSpine = Math.abs(ry + spineN);
          const vFloor = L.valleyFloor ?? 0.35;
          const rangeEnv = Math.exp(-distSpine * distSpine * (0.65 + (1 - vFloor) * 1.1));
          const shaped = Math.pow(Math.max(0, fbm), 1.0 + vFloor * 0.85);
          hLayer = elev * shaped * (0.42 + 0.76 * rangeEnv);
        } else if (typeId === 1) {
          // Alpine Glacial Horn
          const aretes = Math.max(3, L.areteCount ?? 4);
          const cirque = L.cirqueCarving ?? 0.62;
          const rNorm = Math.max(0.25, (L.radiusKm ?? 2.1) / (domainKm * 0.5));
          let wx = vx;
          let wz = vz;
          if (warp > 0.001) {
            const wn = noisedCPU(vx * 1.8 + seed, vz * 1.8 + seed);
            wx += wn[1] * 0.14 * warp;
            wz += wn[2] * 0.14 * warp;
          }
          const r = Math.hypot(wx, wz) / rNorm;
          const theta = Math.atan2(wz, wx);
          const aWave = 0.5 + 0.5 * Math.cos(theta * aretes + noisedCPU(wx * 2.2 + seed, wz * 2.2 + seed)[0] * 0.95);
          const aRidge = Math.pow(aWave, 1.8);
          const cScoop = cirque * 0.38 * (1 - aRidge) * smoothstep(0.12, 0.65, r) * (1 - smoothstep(0.65, 1.25, r));
          const cone = Math.max(0, 1.0 - Math.pow(r, 0.82));
          const detail = swissRidgedCPU(wx * scale, wz * scale, octaves, lacunarity, gain, sharpness, 0.82, warp * 0.6, seed);
          const horn = Math.max(0, cone + aRidge * 0.26 * (1 - smoothstep(0, 1.2, r)) - cScoop);
          hLayer = elev * Math.pow(horn, 1.15) * (0.62 + 0.65 * detail);
        } else if (typeId === 2) {
          // Canyon & Mesa Plateau
          const cDepth = L.canyonDepth ?? 0.78;
          const cWidth = Math.max(0.06, L.canyonWidth ?? 0.32);
          const mFreq = L.meanderFreq ?? 1.65;
          const steps = Math.max(2, L.stepCount ?? 6);
          let wx = vx;
          let wz = vz;
          if (warp > 0.001) {
            const wn = noisedCPU(vx * 1.5 + seed * 1.3, vz * 1.5 + seed * 1.3);
            wx += wn[0] * 0.22 * warp;
            wz += wn[1] * 0.22 * warp;
          }
          const fbm = swissRidgedCPU(wx * scale, wz * scale, octaves, 2.05, 0.48, 1.2, 0.55, warp, seed);
          const meander = 0.34 * Math.sin(wx * mFreq * 2.4 + seed) + 0.16 * Math.cos(wx * mFreq * 4.7 - seed * 0.5);
          const trib = 0.25 * Math.sin(wz * mFreq * 2.9 + seed * 2.1);
          const dMain = Math.abs(wz - meander) / cWidth;
          const dTrib = Math.abs(wx - trib - 0.2) / (cWidth * 0.72);
          const dCanyon = Math.min(dMain, Math.max(dTrib, 1.0 - smoothstep(-0.3, 0.25, wz - meander)));
          const gorge = smoothstep(0.18, 1.15, dCanyon);
          const rawP = Math.max(0, Math.min(1, 0.18 + 0.82 * gorge * (0.68 + 0.38 * fbm)));
          const st = rawP * steps;
          const sf = st - Math.floor(st);
          const terraced = (Math.floor(st) + smoothstep(0.5 - (1 - sharpness * 0.8), 0.5 + (1 - sharpness * 0.8), sf)) / steps;
          const finalP = rawP * 0.32 + terraced * 0.68;
          hLayer = elev * ((1 - cDepth) + cDepth * finalP);
        } else if (typeId === 3) {
          // Volcanic Caldera & Cone
          const rNorm = Math.max(0.25, (L.radiusKm ?? 2.35) / (domainKm * 0.5));
          const calR = L.calderaRadius ?? 0.34;
          const calD = L.calderaDepth ?? 0.58;
          const gullies = L.radialGullies ?? 18;
          const gDepth = L.gullyDepth ?? 0.46;
          let wx = vx;
          let wz = vz;
          if (warp > 0.001) {
            const wn = noisedCPU(vx * 2.1 + seed, vz * 2.1 + seed);
            wx += wn[0] * 0.12 * warp;
            wz += wn[1] * 0.12 * warp;
          }
          const r = Math.hypot(wx, wz) / rNorm;
          const theta = Math.atan2(wz, wx);
          const flank = Math.exp(-r * r * 1.85) * smoothstep(1.35, 0.18, r);
          const calRim = smoothstep(calR * 1.15, calR * 0.55, r);
          const calPit = calD * calRim * (0.78 + 0.22 * noisedCPU(wx * 6 + seed, wz * 6 + seed)[0]);
          const gWave = Math.abs(Math.sin(theta * gullies + noisedCPU(wx * 3.8 + seed, wz * 3.8 + seed)[0] * 1.6));
          const gMask = smoothstep(calR * 0.9, calR * 1.5, r) * (1 - smoothstep(0.7, 1.25, r));
          const gCarve = gDepth * 0.22 * (1 - Math.pow(gWave, 0.55)) * gMask;
          const det = swissRidgedCPU(wx * 2.8, wz * 2.8, 6, 2.1, 0.48, 1.35, 0.65, warp, seed);
          hLayer = elev * Math.max(0, (flank - calPit - gCarve) * (0.78 + 0.35 * det));
        } else if (typeId === 4) {
          // Tectonic Fault & Uplift
          const ang = ((L.strikeAngle ?? 58) * Math.PI) / 180;
          const rx = Math.cos(ang) * vx - Math.sin(ang) * vz;
          const ry = Math.sin(ang) * vx + Math.cos(ang) * vz;
          const fSharp = L.faultSharpness ?? 0.78;
          const wn = noisedCPU(rx * 1.6 + seed, ry * 1.6 + seed)[0] * warp * 0.35;
          const scarp = smoothstep(-0.35 * (1.05 - fSharp), 0.35 * (1.05 - fSharp), ry + wn);
          const bNoise = noisedCPU(rx * (L.blockScale ?? 2.4) + seed, ry * (L.blockScale ?? 2.4))[0];
          hLayer = elev * (scarp * 0.72 + bNoise * (L.tiltAmount ?? 0.45) * 0.35);
        } else if (typeId === 5) {
          // Folded Sedimentary Strata
          const freq = L.frequency ?? 14.0;
          const dipRad = ((L.dipAngle ?? 14) * Math.PI) / 180;
          const strikeRad = ((L.strikeAngle ?? 40) * Math.PI) / 180;
          const lSharp = L.ledgeSharpness ?? 0.74;
          const rx = Math.cos(strikeRad) * vx - Math.sin(strikeRad) * vz;
          const fold = noisedCPU(vx * 1.9 + seed, vz * 1.9 + seed)[0] * (L.foldWarp ?? 0.35) * 180;
          const bedCoord = ((hPrev + fold + rx * Math.sin(dipRad) * 650) / 1000) * freq;
          const phase = bedCoord - Math.floor(bedCoord);
          const stepW = smoothstep(0.5 - (1 - lSharp) * 0.45, 0.5 + (1 - lSharp) * 0.45, phase) - 0.5;
          const harm = 0.28 * Math.sin(bedCoord * Math.PI * 4);
          hLayer = elev * (stepW * 0.75 + harm * 0.25);
        } else if (typeId === 6) {
          // Swiss Derivative Crags
          const fbm = swissRidgedCPU(vx * scale, vz * scale, octaves, lacunarity, gain, sharpness, L.gradientDamping ?? 0.72, warp, seed);
          hLayer = elev * (fbm - 0.35);
        } else if (typeId === 7) {
          // Geological Terracing
          const stepCount = Math.max(2, L.stepCount ?? 12);
          const stepSharp = L.stepSharpness ?? 0.68;
          const warpMod = noisedCPU(vx * 2.6 + seed, vz * 2.6 + seed)[0] * warp * 95;
          const normH = Math.max(0, Math.min(1.5, (hPrev + warpMod) / 2200));
          const s = normH * stepCount;
          const sf = s - Math.floor(s);
          const hw = 0.5 * (1 - stepSharp * 0.85);
          hLayer = ((Math.floor(s) + smoothstep(0.5 - hw, 0.5 + hw, sf)) / stepCount) * 2200 - warpMod;
        } else if (typeId === 8) {
          // Aeolian Sand Dunes
          const ang = ((L.windAngle ?? 48) * Math.PI) / 180;
          let rx = (Math.cos(ang) * vx - Math.sin(ang) * vz) * scale;
          let ry = (Math.sin(ang) * vx + Math.cos(ang) * vz) * scale;
          if (warp > 0.001) {
            const wn = noisedCPU(rx * 0.45 + seed, ry * 0.45 + seed);
            rx += wn[0] * warp * 1.4;
            ry += wn[1] * warp * 0.6;
          }
          const rawP = rx + Math.sin(ry * 1.35 + seed) * 0.35;
          const phase = rawP - Math.floor(rawP);
          const pivot = 0.5 + (L.asymmetry ?? 0.62) * 0.32;
          const dune = phase < pivot
            ? Math.pow(phase / pivot, L.crestSharpness ?? 1.65)
            : Math.pow(1 - (phase - pivot) / Math.max(0.05, 1 - pivot), 0.75);
          const rip = (0.5 + 0.5 * Math.sin(rx * 22)) * (L.rippleDetail ?? 0.35) * 0.08;
          hLayer = elev * (dune + rip);
        }

        if (L.invert && typeId !== 7) hLayer = -hLayer;

        // Local slope for masking
        const xM = Math.max(0, x - 1);
        const xP = Math.min(N - 1, x + 1);
        const dhdx = (prevH[z * N + xP] - prevH[z * N + xM]) / (2 * cellM);
        const dhdz = (prevH[zP * N + x] - prevH[zM * N + x]) / (2 * cellM);
        const slopeDeg = (Math.atan(Math.hypot(dhdx, dhdz)) * 180) / Math.PI;

        const altMask =
          smoothstep((L.maskAltMin ?? -500) - 120, (L.maskAltMin ?? -500) + 120, hPrev) *
          (1 - smoothstep((L.maskAltMax ?? 4500) - 120, (L.maskAltMax ?? 4500) + 120, hPrev));
        const slopeMask =
          smoothstep((L.maskSlopeMin ?? 0) - 6, (L.maskSlopeMin ?? 0) + 6, slopeDeg) *
          (1 - smoothstep((L.maskSlopeMax ?? 90) - 6, (L.maskSlopeMax ?? 90) + 6, slopeDeg));
        const rDist = Math.hypot(vx, vz);
        const radMask = 1 - smoothstep(0.25, 1.15, rDist) * Math.max(0, Math.min(1, L.maskRadial ?? 0));
        const w = Math.max(0, Math.min(1, opacity * altMask * slopeMask * radMask));

        let hOut = hPrev;
        if (blend === 0) hOut = hPrev + hLayer * w;
        else if (blend === 1) hOut = hPrev - hLayer * w;
        else if (blend === 2) hOut = hPrev * (1 - w) + hPrev * (hLayer / Math.max(1, elev)) * w;
        else if (blend === 3) hOut = hPrev * (1 - w) + Math.max(hPrev, hLayer) * w;
        else if (blend === 4) hOut = hPrev * (1 - w) + Math.min(hPrev, hLayer) * w;
        else if (blend === 5) hOut = hPrev * (1 - w) + hLayer * w;
        else if (blend === 6) {
          const nA = Math.max(0, Math.min(1, hPrev / 2400));
          const nB = Math.max(0, Math.min(1, hLayer / 2400));
          hOut = hPrev * (1 - w) + (1 - (1 - nA) * (1 - nB)) * 2400 * w;
        } else {
          hOut = hPrev + hLayer * w * (0.4 + 0.8 * Math.max(0, Math.min(1.5, hPrev / 1800)));
        }

        this.cpuHeight[idx] = Math.max(-250, hOut);
        if (isSelected) this.cpuMask[idx] = w;
      }
    }
  }

  //==========================================================================================================================================
  //  PHYSICAL SHALLOW-WATER & STREAM-POWER HYDRAULIC EROSION SOLVER
  //==========================================================================================================================================
  runCpuHydraulicErosion(L, N, cellM, overrideIters = null) {
    // Lagrangian droplet erosion: inertial particle tracing with sediment capacity, erosion/deposition,
    // evaporation. Produces branching dendritic gullies, channel heads and alluvial fans; also writes flow accumulation.
    const dropScale = overrideIters ? overrideIters / 25 : (L.iterations ?? 90) / 90;
    const nDrop = Math.round((L.dropletCountK ?? 75) * 1000 * Math.max(0.05, Math.min(3, dropScale)));
    const op = L.opacity ?? 1.0;
    const kE = 0.16 * (L.erosionRate ?? 0.8) * op;
    const kD = 0.30 * (L.depositionRate ?? 0.5) * op;
    const capF = 0.9 * (L.sedimentCapacity ?? 1.6);
    const evap = L.evaporation ?? 0.02;
    const inertia = Math.min(0.9, L.inertia ?? 0.28);
    const maxLife = 60;
    const gravity = 4.0;
    const H = this.cpuHeight;
    const S = this.cpuSediment;
    const acc = new Float32Array(N * N);

    // Flattened radial brush (radius 2) — offsets are safe because droplets stay in [2, N-3]
    const bOff = [];
    const bW = [];
    let wsum = 0;
    for (let oy = -2; oy <= 2; oy++) for (let ox = -2; ox <= 2; ox++) {
      const d = Math.hypot(ox, oy);
      if (d > 2.0) continue;
      bOff.push(oy * N + ox);
      bW.push(2.0 - d);
      wsum += 2.0 - d;
    }
    for (let b = 0; b < bW.length; b++) bW[b] /= wsum;
    const nB = bOff.length;

    let seed = ((L.seed ?? 1) * 2654435761) >>> 0 || 1;
    const rnd = () => {
      seed ^= seed << 13; seed >>>= 0;
      seed ^= seed >>> 17;
      seed ^= seed << 5; seed >>>= 0;
      return seed / 4294967296;
    };

    const lo = 2, hi = N - 3;
    for (let d = 0; d < nDrop; d++) {
      let px = lo + rnd() * (hi - lo);
      let py = lo + rnd() * (hi - lo);
      let dx = 0, dy = 0, speed = 1.0, water = 1.0, sed = 0.0;

      for (let life = 0; life < maxLife; life++) {
        const ix = px | 0, iy = py | 0;
        if (ix < lo || iy < lo || ix > hi || iy > hi) break;
        const fx = px - ix, fy = py - iy;
        const i00 = iy * N + ix;
        const h00 = H[i00], h10 = H[i00 + 1], h01 = H[i00 + N], h11 = H[i00 + N + 1];
        const gx = (h10 - h00) * (1 - fy) + (h11 - h01) * fy;
        const gy = (h01 - h00) * (1 - fx) + (h11 - h10) * fx;
        const hOld = h00 * (1 - fx) * (1 - fy) + h10 * fx * (1 - fy) + h01 * (1 - fx) * fy + h11 * fx * fy;

        dx = dx * inertia - gx * (1 - inertia);
        dy = dy * inertia - gy * (1 - inertia);
        const dl = Math.sqrt(dx * dx + dy * dy);
        if (dl < 1e-6) {
          const a = rnd() * 6.2831853;
          dx = Math.cos(a); dy = Math.sin(a);
        } else {
          dx /= dl; dy /= dl;
        }

        px += dx; py += dy;
        const nx = px | 0, ny = py | 0;
        if (nx < lo || ny < lo || nx > hi || ny > hi) break;
        const ox2 = px - nx, oy2 = py - ny;
        const j00 = ny * N + nx;
        const hNew = H[j00] * (1 - ox2) * (1 - oy2) + H[j00 + 1] * ox2 * (1 - oy2) +
                     H[j00 + N] * (1 - ox2) * oy2 + H[j00 + N + 1] * ox2 * oy2;

        const deltaH = hNew - hOld;
        const drop = -deltaH;
        acc[i00] += water;

        const capacity = Math.max(drop * speed * water * capF, 0.015);

        if (sed > capacity || deltaH > 0) {
          let dep = deltaH > 0 ? Math.min(deltaH, sed) : (sed - capacity) * kD;
          dep = Math.max(0, Math.min(dep, sed));
          sed -= dep;
          H[i00] += dep * (1 - fx) * (1 - fy);
          H[i00 + 1] += dep * fx * (1 - fy);
          H[i00 + N] += dep * (1 - fx) * fy;
          H[i00 + N + 1] += dep * fx * fy;
          S[i00] += dep;
        } else {
          const er = Math.max(0, Math.min((capacity - sed) * kE, drop));
          if (er > 0) {
            for (let b = 0; b < nB; b++) H[i00 + bOff[b]] -= er * bW[b];
            sed += er;
          }
        }

        const slopeDim = drop / cellM;
        speed = Math.min(4.0, Math.sqrt(Math.max(0.0001, speed * speed + slopeDim * gravity * 0.06)));
        water *= 1 - evap;
        if (water < 0.02) break;
      }
      if (sed > 0.0001) {
        const ix = px | 0, iy = py | 0;
        if (ix >= lo && iy >= lo && ix <= hi && iy <= hi) {
          H[iy * N + ix] += sed;
          S[iy * N + ix] += sed;
        }
      }
    }

    // Flow field (accumulated droplet visitation, log-normalised) for channel masks & water
    let maxAcc = 1e-6;
    for (let i = 0; i < acc.length; i++) if (acc[i] > maxAcc) maxAcc = acc[i];
    const F = this.cpuFlow;
    const lnMax = Math.log1p(maxAcc);
    for (let i = 0; i < acc.length; i++) F[i] = Math.pow(Math.log1p(acc[i]) / lnMax, 0.9);
    this.timings.erosionCycles += 1;
  }

  //==========================================================================================================================================
  //  PHYSICAL THERMAL WEATHERING & TALUS SLUMPING SOLVER
  //==========================================================================================================================================
  runCpuThermalErosion(L, N, cellM, overrideIters = null) {
    const iters = overrideIters ?? Math.min(80, Math.max(4, Math.round((L.iterations ?? 35) * 0.45)));
    const reposeRad = ((L.reposeAngle ?? 36) * Math.PI) / 180;
    const critCard = Math.tan(reposeRad) * cellM;
    const critDiag = critCard * 1.41421356;
    const rate = (L.weatheringRate ?? 0.52) * 0.11 * (L.opacity ?? 1.0);
    const spread = L.talusSpread ?? 0.68;

    const H = this.cpuHeight;
    const T = this.cpuTalus;
    const nextH = new Float32Array(N * N);

    for (let step = 0; step < iters; step++) {
      nextH.set(H);
      for (let z = 1; z < N - 1; z++) {
        const row = z * N;
        for (let x = 1; x < N - 1; x++) {
          const idx = row + x;
          const h = H[idx];

          const dL = Math.max(0, h - H[idx - 1] - critCard);
          const dR = Math.max(0, h - H[idx + 1] - critCard);
          const dD = Math.max(0, h - H[idx - N] - critCard);
          const dU = Math.max(0, h - H[idx + N] - critCard);
          const dLD = Math.max(0, h - H[idx - N - 1] - critDiag) * 0.7;
          const dRD = Math.max(0, h - H[idx - N + 1] - critDiag) * 0.7;
          const dLU = Math.max(0, h - H[idx + N - 1] - critDiag) * 0.7;
          const dRU = Math.max(0, h - H[idx + N + 1] - critDiag) * 0.7;

          const outSum = dL + dR + dD + dU + dLD + dRD + dLU + dRU;
          if (outSum > 0) {
            const maxDiff = Math.max(dL, dR, dD, dU, dLD, dRD, dLU, dRU);
            const spall = Math.min(maxDiff * 0.42, outSum * 0.14) * (L.weatheringRate ?? 0.52) * 0.45 * (L.opacity ?? 1.0);
            nextH[idx] -= spall;
            const share = (spall * spread) / outSum;
            if (dL > 0) { nextH[idx - 1] += dL * share; T[idx - 1] += dL * share * 2.4; }
            if (dR > 0) { nextH[idx + 1] += dR * share; T[idx + 1] += dR * share * 2.4; }
            if (dD > 0) { nextH[idx - N] += dD * share; T[idx - N] += dD * share * 2.4; }
            if (dU > 0) { nextH[idx + N] += dU * share; T[idx + N] += dU * share * 2.4; }
            if (dLD > 0) { nextH[idx - N - 1] += dLD * share; T[idx - N - 1] += dLD * share * 2.4; }
            if (dRD > 0) { nextH[idx - N + 1] += dRD * share; T[idx - N + 1] += dRD * share * 2.4; }
            if (dLU > 0) { nextH[idx + N - 1] += dLU * share; T[idx + N - 1] += dLU * share * 2.4; }
            if (dRU > 0) { nextH[idx + N + 1] += dRU * share; T[idx + N + 1] += dRU * share * 2.4; }
          }
        }
      }
      H.set(nextH);
      this.timings.erosionCycles++;
    }
  }

  //==========================================================================================================================================
  //  PHYSICAL SNOWFALL, WIND DRIFT & AVALANCHE SLUMPING SOLVER
  //==========================================================================================================================================
  runCpuSnowfall(L, N, cellM) {
    const snowlineBase = L.snowlineAlt ?? 1180;
    const depth = (L.snowDepth ?? 0.72) * (L.opacity ?? 1.0);
    const repose = L.reposeAngle ?? 44;
    const windRad = ((L.windAzimuth ?? 310) * Math.PI) / 180;
    const wx = Math.cos(windRad);
    const wz = Math.sin(windRad);
    const drift = L.windDrift ?? 0.48;
    const melt = L.solarMelt ?? 0.42;

    const H = this.cpuHeight;
    const SN = this.cpuSnow;

    for (let z = 1; z < N - 1; z++) {
      const row = z * N;
      const v = z / N;
      for (let x = 1; x < N - 1; x++) {
        const idx = row + x;
        const u = x / N;
        const h = H[idx];
        const hL = H[idx - 1];
        const hR = H[idx + 1];
        const hD = H[idx - N];
        const hU = H[idx + N];

        const gx = (hR - hL) / (2 * cellM);
        const gz = (hU - hD) / (2 * cellM);
        const invLen = 1 / Math.hypot(gx, 1, gz);
        const nx = -gx * invLen;
        const ny = invLen;
        const nz = -gz * invLen;
        const slopeDeg = (Math.acos(Math.max(0, Math.min(1, ny))) * 180) / Math.PI;

        const lap = (hL + hR + hD + hU - 4 * h) / cellM;
        const couloirTrap = Math.max(-0.4, Math.min(1.4, lap * 1.8));
        const sNoise = noisedCPU(u * 10 + 17.3, v * 10 + 9.1)[0] * 125;
        const snowline = snowlineBase * 0.55 + sNoise - couloirTrap * 220;
        const altFactor = smoothstep(snowline - 240, snowline + 220, h);

        const windFactor = 1 + (nx * wx + nz * wz) * drift * 0.55;
        const southMelt = Math.max(0, nz) * melt * 0.35;
        const slopeHold = 1 - smoothstep(repose - 8, repose + 14, slopeDeg);

        const snowAmt = Math.max(
          0,
          Math.min(1.5, altFactor * depth * 1.35 * windFactor * (1 - southMelt) * Math.max(0.18, slopeHold + Math.max(0, couloirTrap) * 0.55))
        );
        SN[idx] = snowAmt;
        H[idx] = h + snowAmt * 12;
      }
    }
  }

  // Live simulation step (called when user clicks "Erode Step (+25)" or enables "Auto-Erode")
  stepLiveErosion(state, steps = 12) {
    const N = this.cpuSize;
    const cellM = ((state.env.domainSizeKm || 5.0) * 1000) / N;
    const hydroLayer =
      state.terrainStack.find((l) => l.type === "hydraulic_erosion" && l.enabled) || {
        rainRate: 0.019,
        erosionRate: 0.62,
        depositionRate: 0.45,
        sedimentCapacity: 1.4,
        evaporation: 0.03,
        channelSharpness: 0.75,
        opacity: 1.0,
      };
    const thermalLayer =
      state.terrainStack.find((l) => l.type === "thermal_erosion" && l.enabled) || {
        reposeAngle: 36,
        weatheringRate: 0.5,
        talusSpread: 0.68,
        opacity: 1.0,
      };

    this.runCpuHydraulicErosion(hydroLayer, N, cellM, steps);
    this.runCpuThermalErosion(thermalLayer, N, cellM, Math.max(2, Math.floor(steps * 0.4)));
    this.computeCpuGeomorphologyAndSplat(state, N, cellM);
    if (this.backend === "webgpu" && this.device) {
      this.uploadCpuToWebGPUAndSplat(state);
    } else if (this.gl) {
      this.updateWebGL2Buffers();
    }
  }

  //==========================================================================================================================================
  //  COMPUTE GEOMORPHOLOGY (NORMALS, SLOPE, CURVATURE, HORIZON AO) & EVALUATE TEXTURE LAYER STACK
  //==========================================================================================================================================
  computeCpuGeomorphologyAndSplat(state, N, cellM) {
    const H = this.cpuHeight;
    const SED = this.cpuSediment;
    const TAL = this.cpuTalus;
    const SN = this.cpuSnow;
    const FLW = this.cpuFlow;

    let minE = Infinity;
    let maxE = -Infinity;
    let sumE = 0;
    let sumSlope = 0;
    let snowCells = 0;
    let sedSum = 0;

    // 1. Compute Normals, Slope, Multi-Scale Curvature & Horizon AO
    for (let z = 0; z < N; z++) {
      const zM = Math.max(0, z - 1);
      const zP = Math.min(N - 1, z + 1);
      const zM3 = Math.max(0, z - 3);
      const zP3 = Math.min(N - 1, z + 3);
      for (let x = 0; x < N; x++) {
        const xM = Math.max(0, x - 1);
        const xP = Math.min(N - 1, x + 1);
        const xM3 = Math.max(0, x - 3);
        const xP3 = Math.min(N - 1, x + 3);

        const idx = z * N + x;
        const h = H[idx];
        if (h < minE) minE = h;
        if (h > maxE) maxE = h;
        sumE += h;
        if (SN[idx] > 0.15) snowCells++;
        sedSum += SED[idx] + TAL[idx];

        const hL = H[z * N + xM];
        const hR = H[z * N + xP];
        const hD = H[zM * N + x];
        const hU = H[zP * N + x];

        const gx = (hR - hL) / (2 * cellM);
        const gz = (hU - hD) / (2 * cellM);
        const invL = 1 / Math.hypot(gx, 1, gz);
        const nx = -gx * invL;
        const ny = invL;
        const nz = -gz * invL;

        this.cpuNormX[idx] = nx;
        this.cpuNormZ[idx] = nz;

        const slopeDeg = (Math.acos(Math.max(0, Math.min(1, ny))) * 180) / Math.PI;
        this.cpuSlope[idx] = slopeDeg;
        sumSlope += slopeDeg;

        const lap1 = (4 * h - (hL + hR + hD + hU)) / cellM;
        const lap2 =
          (4 * h - (H[z * N + xM3] + H[z * N + xP3] + H[zM3 * N + x] + H[zP3 * N + x])) /
          (3 * cellM);
        this.cpuCurvature[idx] = Math.max(-1.5, Math.min(1.5, lap1 * 0.65 + lap2 * 0.55));

        // 4-direction Horizon AO
        const dE = Math.max(0, H[z * N + xP3] - h) / (3 * cellM);
        const dW = Math.max(0, H[z * N + xM3] - h) / (3 * cellM);
        const dN = Math.max(0, H[zP3 * N + x] - h) / (3 * cellM);
        const dS = Math.max(0, H[zM3 * N + x] - h) / (3 * cellM);
        this.cpuAO[idx] = Math.max(0.2, Math.min(1.0, 1.0 - (dE + dW + dN + dS) * 0.14));
      }
    }

    const total = N * N;
    this.stats.minElev = Math.round(minE);
    this.stats.maxElev = Math.round(maxE);
    this.stats.meanElev = Math.round(sumE / total);
    this.stats.meanSlope = +(sumSlope / total).toFixed(1);
    this.stats.snowCoveragePct = Math.round((snowCells / total) * 100);
    this.stats.erodedVolumeM3 = Math.round((sedSum * cellM * cellM) / 1e5) / 10;

    // Build 64-bin elevation histogram
    const hist = this.stats.histogram;
    hist.fill(0);
    const span = Math.max(10, maxE - minE);
    for (let i = 0; i < total; i++) {
      const b = Math.min(63, Math.max(0, Math.floor(((H[i] - minE) / span) * 63.99)));
      hist[b]++;
    }
    let maxBin = 1;
    for (let i = 0; i < 64; i++) if (hist[i] > maxBin) maxBin = hist[i];
    for (let i = 0; i < 64; i++) hist[i] /= maxBin;

    // 2. Evaluate Texture Layer Stack on CPU Mirror
    const texStack = state.textureStack || [];
    const hasSolo = texStack.some((l) => l.solo && l.enabled);

    // Pre-parse SatMap stops & layer colors
    const prepared = texStack.map((L) => {
      const sat = SATMAPS.find((s) => s.id === L.satmapId) || SATMAPS[0];
      return {
        L,
        active: L.enabled && (!hasSolo || L.solo),
        isSelected: state.selection?.id === L.id,
        prim: hexToRgbLinear(L.colPrimary),
        sec: hexToRgbLinear(L.colSecondary),
        acc: hexToRgbLinear(L.colAccent),
        stops: sat.stops.map(hexToRgbLinear),
      };
    });

    for (let z = 0; z < N; z++) {
      const v = z / N;
      for (let x = 0; x < N; x++) {
        const u = x / N;
        const idx = z * N + x;
        const h = H[idx];
        const slope = this.cpuSlope[idx];
        const curv = this.cpuCurvature[idx];
        const flow = FLW[idx];
        const sed = SED[idx];
        const talus = TAL[idx];
        const snow = SN[idx];
        const microN = noisedCPU(u * 36 + 3.7, v * 36 + 9.2)[0];
        const strataW = 0.5 + 0.5 * Math.sin(h * 0.065 + microN * 1.4);

        let rOut = 0.32;
        let gOut = 0.34;
        let bOut = 0.37;
        let roughOut = 0.8;

        for (let i = 0; i < prepared.length; i++) {
          const P = prepared[i];
          if (!P.active) continue;
          const L = P.L;

          let w = L.opacity ?? 1.0;
          if (L.useAltMask) {
            const f = Math.max(10, L.altFeather ?? 200);
            let m = smoothstep(L.altMin - f, L.altMin + f, h) * (1 - smoothstep(L.altMax - f, L.altMax + f, h));
            if (L.altInvert) m = 1 - m;
            w *= m;
          }
          if (L.useSlopeMask) {
            const f = Math.max(1, L.slopeFeather ?? 8);
            let m = smoothstep(L.slopeMin - f, L.slopeMin + f, slope) * (1 - smoothstep(L.slopeMax - f, L.slopeMax + f, slope));
            if (L.slopeInvert) m = 1 - m;
            w *= m;
          }
          if (L.useCurvatureMask) {
            const cVal = L.curvatureMode === 0 ? curv : -curv;
            w *= smoothstep(-0.15, Math.max(0.05, 1.05 - (L.curvatureStrength ?? 0.65) * 0.75), cVal);
          }
          if (L.useFlowMask) {
            w *= smoothstep(0.04, Math.max(0.08, 0.75 - (L.flowStrength ?? 0.75) * 0.55), flow);
          }
          if (L.useSedimentMask) {
            const score = (sed / 18) * (L.sedimentWeight ?? 0.75) + (talus / 14) * (L.talusWeight ?? 0.65);
            w *= smoothstep(0.03, 0.55, score);
          }
          if (L.useSnowMask) {
            w *= smoothstep(0.03, Math.max(0.1, 0.85 - (L.snowWeight ?? 0.9) * 0.6), snow);
          }
          if (L.useNoiseMask) {
            const nv = 0.5 + 0.5 * noisedCPU(u * (L.noiseScale ?? 4.5) * 3 + i * 7.3, v * (L.noiseScale ?? 4.5) * 3)[0];
            w *= 1 - (L.noiseAmount ?? 0.35) + (L.noiseAmount ?? 0.35) * smoothstep(0.22, 0.78, nv);
          }

          if (L.blendMode === 0 && i > 0) {
            const hSig = Math.max(0, Math.min(1, 0.5 + curv * 0.35 + microN * 0.25));
            const hc = Math.max(0.05, Math.min(0.95, L.heightContrast ?? 0.65));
            w = smoothstep(
              Math.max(0, 1 - w - (1 - hc) * 0.6),
              Math.min(1, 1 - w + (1 - hc) * 0.6 + 0.05),
              w + (hSig - 0.5) * 0.35
            );
          }
          w = Math.max(0, Math.min(1, w));

          const rampT = Math.max(0, Math.min(1, 0.48 + curv * 0.28 + microN * 0.16 + (strataW - 0.5) * (L.strataIntensity ?? 0.25) * 0.45));
          let lr = P.sec[0] * (1 - rampT) + P.prim[0] * rampT;
          let lg = P.sec[1] * (1 - rampT) + P.prim[1] * rampT;
          let lb = P.sec[2] * (1 - rampT) + P.prim[2] * rampT;

          if (L.useSatmap) {
            const st = rampT * 4;
            const seg = Math.min(3, Math.floor(st));
            const sf = st - seg;
            const sA = P.stops[seg];
            const sB = P.stops[seg + 1];
            const sr = sA[0] * (1 - sf) + sB[0] * sf;
            const sg = sA[1] * (1 - sf) + sB[1] * sf;
            const sb = sA[2] * (1 - sf) + sB[2] * sf;
            lr = lr * 0.25 + sr * 0.75;
            lg = lg * 0.25 + sg * 0.75;
            lb = lb * 0.25 + sb * 0.75;
          }

          const accMix = Math.max(0, Math.min(0.65, (strataW - 0.55) * (L.strataIntensity ?? 0.25) * 1.4));
          lr = lr * (1 - accMix) + P.acc[0] * accMix;
          lg = lg * (1 - accMix) + P.acc[1] * accMix;
          lb = lb * (1 - accMix) + P.acc[2] * accMix;

          if (i === 0) {
            rOut = lr;
            gOut = lg;
            bOut = lb;
            roughOut = L.roughness;
          } else {
            rOut = rOut * (1 - w) + lr * w;
            gOut = gOut * (1 - w) + lg * w;
            bOut = bOut * (1 - w) + lb * w;
            roughOut = roughOut * (1 - w) + L.roughness * w;
          }

          if (P.isSelected) {
            this.cpuMask[idx] = w;
          }
        }

        // Store sRGB 0..255 in cpuAlbedo for 2D Map, Export, and WebGL2 fallback texture
        this.cpuAlbedo[idx * 4 + 0] = Math.min(255, Math.max(0, Math.round(Math.pow(rOut, 1 / 2.2) * 255)));
        this.cpuAlbedo[idx * 4 + 1] = Math.min(255, Math.max(0, Math.round(Math.pow(gOut, 1 / 2.2) * 255)));
        this.cpuAlbedo[idx * 4 + 2] = Math.min(255, Math.max(0, Math.round(Math.pow(bOut, 1 / 2.2) * 255)));
        this.cpuAlbedo[idx * 4 + 3] = 255;
        this.cpuRoughness[idx] = roughOut;
      }
    }

    if (this.gl) {
      this.updateWebGL2Buffers();
    }
  }

  //==========================================================================================================================================
  //  INTERACTIVE 3D VIEWPORT SCULPT BRUSH & RAYCASTING
  //==========================================================================================================================================
  raycastTerrain(ndcX, ndcY, invVP, camPos, state) {
    const near4 = [
      invVP[0] * ndcX + invVP[4] * ndcY - invVP[8] + invVP[12],
      invVP[1] * ndcX + invVP[5] * ndcY - invVP[9] + invVP[13],
      invVP[2] * ndcX + invVP[6] * ndcY - invVP[10] + invVP[14],
      invVP[3] * ndcX + invVP[7] * ndcY - invVP[11] + invVP[15],
    ];
    const far4 = [
      invVP[0] * ndcX + invVP[4] * ndcY + invVP[8] + invVP[12],
      invVP[1] * ndcX + invVP[5] * ndcY + invVP[9] + invVP[13],
      invVP[2] * ndcX + invVP[6] * ndcY + invVP[10] + invVP[14],
      invVP[3] * ndcX + invVP[7] * ndcY + invVP[11] + invVP[15],
    ];
    const pNear = [near4[0] / near4[3], near4[1] / near4[3], near4[2] / near4[3]];
    const pFar = [far4[0] / far4[3], far4[1] / far4[3], far4[2] / far4[3]];
    let dx = pFar[0] - pNear[0];
    let dy = pFar[1] - pNear[1];
    let dz = pFar[2] - pNear[2];
    const dl = Math.hypot(dx, dy, dz) || 1;
    dx /= dl;
    dy /= dl;
    dz /= dl;

    const domainWorld = 10.0;
    const hScale = (10.0 / ((state.env.domainSizeKm || 5.0) * 1000)) * (state.env.verticalExaggeration || 1.0);
    const N = this.cpuSize;

    // March along ray to find intersection with heightfield
    const maxDist = 36.0;
    const steps = 110;
    const dt = maxDist / steps;
    for (let i = 1; i <= steps; i++) {
      const t = i * dt;
      const wx = camPos[0] + dx * t;
      const wy = camPos[1] + dy * t;
      const wz = camPos[2] + dz * t;
      const u = wx / domainWorld + 0.5;
      const v = wz / domainWorld + 0.5;
      if (u >= 0 && u <= 1 && v >= 0 && v <= 1) {
        const ix = Math.min(N - 1, Math.max(0, Math.floor(u * (N - 1))));
        const iz = Math.min(N - 1, Math.max(0, Math.floor(v * (N - 1))));
        const idx = iz * N + ix;
        const terY = this.cpuHeight[idx] * hScale;
        if (wy <= terY) {
          return {
            worldX: wx,
            worldY: terY,
            worldZ: wz,
            u,
            v,
            elevM: Math.round(this.cpuHeight[idx]),
            slopeDeg: Math.round(this.cpuSlope[idx]),
            xmKm: +((u - 0.5) * state.env.domainSizeKm).toFixed(2),
            zmKm: +((v - 0.5) * state.env.domainSizeKm).toFixed(2),
          };
        }
      }
    }
    return null;
  }

  applySculptBrush(uCenter, vCenter, state) {
    const N = this.cpuSize;
    const domainKm = state.env.domainSizeKm || 5.0;
    const rUV = (this.brush.radiusKm || 0.42) / domainKm;
    const rCells = Math.ceil(rUV * N);
    const cx = Math.round(uCenter * (N - 1));
    const cz = Math.round(vCenter * (N - 1));
    const strength = (this.brush.strength ?? 0.55) * 42;
    const mode = this.brush.mode;

    for (let dz = -rCells; dz <= rCells; dz++) {
      const z = cz + dz;
      if (z < 1 || z >= N - 1) continue;
      for (let dx = -rCells; dx <= rCells; dx++) {
        const x = cx + dx;
        if (x < 1 || x >= N - 1) continue;
        const dist = Math.hypot(dx, dz) / Math.max(1, rCells);
        if (dist >= 1) continue;
        const falloff = Math.pow(1 - dist * dist, 2);
        const idx = z * N + x;

        if (mode === "raise") {
          this.sculptOffsets[idx] += strength * falloff;
          this.cpuHeight[idx] += strength * falloff;
        } else if (mode === "lower") {
          this.sculptOffsets[idx] -= strength * falloff;
          this.cpuHeight[idx] -= strength * falloff;
        } else if (mode === "smooth") {
          const avg =
            0.25 *
            (this.cpuHeight[idx - 1] +
              this.cpuHeight[idx + 1] +
              this.cpuHeight[idx - N] +
              this.cpuHeight[idx + N]);
          const delta = (avg - this.cpuHeight[idx]) * falloff * 0.45;
          this.sculptOffsets[idx] += delta;
          this.cpuHeight[idx] += delta;
        } else if (mode === "water") {
          this.cpuWater[idx] += 0.45 * falloff;
          this.cpuFlow[idx] = Math.min(4.0, this.cpuFlow[idx] + 0.85 * falloff);
        }
      }
    }

    const cellM = (domainKm * 1000) / N;
    if (mode === "erode" || mode === "water") {
      this.runCpuHydraulicErosion(
        { rainRate: 0.02, erosionRate: 0.7, depositionRate: 0.45, sedimentCapacity: 1.5, evaporation: 0.02, channelSharpness: 0.8, opacity: 1 },
        N,
        cellM,
        4
      );
    }
    this.computeCpuGeomorphologyAndSplat(state, N, cellM);
    if (this.backend === "webgpu" && this.device) {
      this.uploadCpuToWebGPUAndSplat(state);
    }
  }

  //==========================================================================================================================================
  //  RENDER 3D VIEWPORT FRAME (WEBGPU OR WEBGL2 FALLBACK) + 2D DATA MAP VIEW
  //==========================================================================================================================================
  renderFrame(state, camInfo, timeSec) {
    // 1. Render 3D Viewport
    if (this.backend === "webgpu" && this.device) {
      this.renderWebGPUFrame(state, camInfo, timeSec);
    } else if (this.gl) {
      this.renderWebGL2Frame(state, camInfo, timeSec);
    }

    // 2. Render 2D Data Map / Cross-Section Canvas if visible
    if (this.mapCtx && state.viewportLayout !== "3d") {
      this.render2DMapCanvas(state);
    }
  }

  packFrameUniforms(state, camInfo, timeSec) {
    const env = state.env;
    const domainWorld = 10.0;
    const hScale = (domainWorld / ((env.domainSizeKm || 5.0) * 1000)) * (env.verticalExaggeration || 1.0);

    const az = ((env.sunAzimuth ?? 132) * Math.PI) / 180;
    const el = ((env.sunElevation ?? 27) * Math.PI) / 180;
    const sunDir = [
      Math.cos(el) * Math.sin(az),
      Math.sin(el),
      Math.cos(el) * Math.cos(az),
    ];
    const sunCol = kelvinToLinearRGB(env.sunTemperatureK ?? 5600);
    const fogCol = hexToRgbLinear(env.fogColor ?? "#9eb4c9");
    const wShallow = hexToRgbLinear(env.waterShallowColor ?? "#288582");
    const wDeep = hexToRgbLinear(env.waterDeepColor ?? "#0a2538");

    const domainKm = env.domainSizeKm || 5.0;
    const brushWorldR = ((this.brush.radiusKm || 0.42) / domainKm) * domainWorld;

    const u = new Float32Array(76); // 19 vec4f = 304 bytes
    u.set(camInfo.viewProj, 0);
    u.set(camInfo.invViewProj, 16);
    u.set([camInfo.eye[0], camInfo.eye[1], camInfo.eye[2], timeSec], 32);
    u.set([sunDir[0], sunDir[1], sunDir[2], env.sunIlluminanceKlux ?? 112], 36);
    u.set([sunCol[0], sunCol[1], sunCol[2], env.sunAngularRadius ?? 0.54], 40);
    u.set([this.gridSize, domainWorld, hScale, (env.waterLevelM ?? 145) * hScale], 44);
    u.set([env.rayleigh ?? 1.05, env.mieTurbidity ?? 2.4, env.aerialStrength ?? 0.68, env.fogEnabled ? (env.fogDensity ?? 0.22) : 0.0], 48);
    u.set([env.fogHeightFalloff ?? 0.55, env.fogValleyMist ?? 0.42, env.exposureEv ?? 0.15, env.contrast ?? 1.06], 52);
    u.set([fogCol[0], fogCol[1], fogCol[2], state.shadingMode ?? 0], 56);
    u.set([wShallow[0], wShallow[1], wShallow[2], env.waterEnabled ? 1.0 : 0.0], 60);
    u.set([wDeep[0], wDeep[1], wDeep[2], env.waterWaveStrength ?? 0.45], 64);
    u.set([
      env.softShadows ? 1.0 : 0.0,
      env.cloudShadows ? 1.0 : 0.0,
      (env.pedestalFloorM ?? -180) * hScale,
      env.vignette ?? 0.22,
    ], 68);
    u.set([
      this.brush.worldX,
      this.brush.worldZ,
      brushWorldR,
      this.brush.enabled && this.brush.hovering ? 1.0 : 0.0,
    ], 72);
    return u;
  }

  renderWebGPUFrame(state, camInfo, timeSec) {
    const dev = this.device;
    const canvas = this.gpuCanvas;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = Math.max(1, Math.floor(canvas.clientWidth * dpr));
    const h = Math.max(1, Math.floor(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h || !this.depthTex) {
      canvas.width = w;
      canvas.height = h;
      if (this.depthTex) this.depthTex.destroy();
      this.depthTex = dev.createTexture({
        size: [w, h],
        format: "depth24plus",
        usage: GPUTextureUsage.RENDER_ATTACHMENT,
      });
    }

    const u = this.packFrameUniforms(state, camInfo, timeSec);
    dev.queue.writeBuffer(this.frameUniformBuf, 0, u);

    const bg = dev.createBindGroup({
      layout: this.renderBindGroupLayout,
      entries: [
        { binding: 0, resource: { buffer: this.frameUniformBuf } },
        { binding: 1, resource: { buffer: this.activeFieldBuf } },
        { binding: 2, resource: { buffer: this.bufGeom } },
        { binding: 3, resource: { buffer: this.bufSplatColor } },
        { binding: 4, resource: { buffer: this.bufSplatMeta } },
      ],
    });

    const colorView = this.context.getCurrentTexture().createView();
    const depthView = this.depthTex.createView();

    const enc = dev.createCommandEncoder();
    const pass = enc.beginRenderPass({
      colorAttachments: [
        {
          view: colorView,
          clearValue: { r: 0.04, g: 0.05, b: 0.07, a: 1.0 },
          loadOp: "clear",
          storeOp: "store",
        },
      ],
      depthStencilAttachment: {
        view: depthView,
        depthClearValue: 1.0,
        depthLoadOp: "clear",
        depthStoreOp: "store",
      },
    });

    // 1. Sky Dome
    pass.setPipeline(this.pipeSky);
    pass.setBindGroup(0, bg);
    pass.draw(3);

    // 2. Displaced Terrain + Geological Pedestal Skirt
    pass.setPipeline(this.pipeTerrain);
    pass.setBindGroup(0, bg);
    pass.setVertexBuffer(0, this.vertexBuf);
    pass.setIndexBuffer(this.indexBuf, "uint32");
    const idxCount = state.env.pedestalEnabled ? this.indexCount : this.surfaceIndexCount;
    pass.drawIndexed(idxCount);

    // 3. Water Table / Alpine Lake
    if (state.env.waterEnabled && state.shadingMode <= 1) {
      pass.setPipeline(this.pipeWater);
      pass.setBindGroup(0, bg);
      pass.draw(6);
    }

    pass.end();
    dev.queue.submit([enc.finish()]);
  }

  //==========================================================================================================================================
  //  2D DATA MAP & CROSS-SECTION RENDERER (FOR SPLIT VIEW & 2D MAP TAB)
  //==========================================================================================================================================
  render2DMapCanvas(state) {
    const canvas = this.mapCanvas;
    const ctx = this.mapCtx;
    if (!canvas || !ctx) return;

    const w = Math.max(1, canvas.clientWidth);
    const h = Math.max(1, canvas.clientHeight);
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }

    const N = this.cpuSize;
    if (!this._offMap || this._offMap.width !== N) {
      this._offMap = document.createElement("canvas");
      this._offMap.width = N;
      this._offMap.height = N;
      this._offCtx = this._offMap.getContext("2d");
      this._imgData = this._offCtx.createImageData(N, N);
    }

    const data = this._imgData.data;
    const mode = state.shadingMode ?? 0;
    const az = ((state.env.sunAzimuth ?? 132) * Math.PI) / 180;
    const el = ((state.env.sunElevation ?? 27) * Math.PI) / 180;
    const sx = Math.cos(el) * Math.sin(az);
    const sy = Math.sin(el);
    const sz = Math.cos(el) * Math.cos(az);

    for (let i = 0; i < N * N; i++) {
      const nx = this.cpuNormX[i];
      const nz = this.cpuNormZ[i];
      const ny = Math.sqrt(Math.max(0.01, 1 - nx * nx - nz * nz));
      const shade = (0.45 + 0.55 * Math.max(0, nx * sx + ny * sy + nz * sz)) * this.cpuAO[i];

      let r = this.cpuAlbedo[i * 4 + 0];
      let g = this.cpuAlbedo[i * 4 + 1];
      let b = this.cpuAlbedo[i * 4 + 2];

      if (mode === 1) {
        r = 165; g = 160; b = 152;
      } else if (mode === 2) {
        const t = Math.max(0, Math.min(1, this.cpuHeight[i] / 2400));
        r = Math.round(45 + t * 200);
        g = Math.round(90 + Math.sin(t * Math.PI) * 120 + t * 40);
        b = Math.round(65 + t * 185);
      } else if (mode === 3) {
        const s = Math.max(0, Math.min(1, this.cpuSlope[i] / 75));
        r = Math.round(40 + s * 210);
        g = Math.round(180 * (1 - Math.abs(s - 0.35)));
        b = Math.round(70 * (1 - s));
      } else if (mode === 4) {
        const f = Math.max(0, Math.min(1, Math.pow(this.cpuFlow[i] * 0.65, 0.65)));
        r = Math.round(20 + f * 65);
        g = Math.round(26 + f * 205);
        b = Math.round(38 + f * 215);
      } else if (mode === 5) {
        const sd = Math.min(1, this.cpuSediment[i] / 18);
        const tl = Math.min(1, this.cpuTalus[i] / 15);
        r = Math.round(28 + sd * 215 + tl * 60);
        g = Math.round(28 + sd * 155 + tl * 185);
        b = Math.round(32 + sd * 45 + tl * 170);
      } else if (mode === 6) {
        const sn = Math.min(1, this.cpuSnow[i]);
        r = Math.round(30 + sn * 210);
        g = Math.round(36 + sn * 212);
        b = Math.round(46 + sn * 209);
      } else if (mode === 7) {
        const c = Math.max(-1, Math.min(1, this.cpuCurvature[i] * 0.65));
        r = c >= 0 ? Math.round(55 + c * 195) : 45;
        g = Math.round(55 + Math.abs(c) * 135);
        b = c < 0 ? Math.round(60 + (-c) * 185) : 50;
      } else if (mode === 8) {
        const m = Math.max(0, Math.min(1, this.cpuMask[i]));
        r = Math.round(22 + m * 233);
        g = Math.round(22 + m * 158);
        b = Math.round(26 + m * 55);
      }

      // Water overlay on 2D map
      if (state.env.waterEnabled && mode <= 1 && this.cpuHeight[i] < state.env.waterLevelM) {
        r = Math.round(r * 0.25 + 28 * 0.75);
        g = Math.round(g * 0.25 + 115 * 0.75);
        b = Math.round(b * 0.25 + 125 * 0.75);
      }

      data[i * 4 + 0] = Math.min(255, Math.round(r * shade));
      data[i * 4 + 1] = Math.min(255, Math.round(g * shade));
      data[i * 4 + 2] = Math.min(255, Math.round(b * shade));
      data[i * 4 + 3] = 255;
    }

    this._offCtx.putImageData(this._imgData, 0, 0);

    ctx.fillStyle = "#0b0c0e";
    ctx.fillRect(0, 0, w, h);

    if (state.viewportLayout === "profile") {
      // Render Cross-Section Geological Profile + Top-Down Minimap
      this.renderCrossSectionView(ctx, w, h, state);
      return;
    }

    const size = Math.min(w - 28, h - 28);
    const ox = Math.floor((w - size) * 0.5);
    const oy = Math.floor((h - size) * 0.5);
    ctx.drawImage(this._offMap, ox, oy, size, size);
    ctx.strokeStyle = "#363636";
    ctx.lineWidth = 1;
    ctx.strokeRect(ox + 0.5, oy + 0.5, size, size);
  }

  renderCrossSectionView(ctx, w, h, state) {
    const N = this.cpuSize;
    const sliceZ = Math.floor(N * 0.5);
    const padX = 48;
    const padY = 48;
    const plotW = Math.max(100, w - padX * 2);
    const plotH = Math.max(100, h - padY * 2);

    // Subtle elevation grid lines
    ctx.strokeStyle = "#1f2226";
    ctx.lineWidth = 1;
    ctx.fillStyle = "#7a828c";
    ctx.font = "10px 'DM Sans', sans-serif";
    const maxM = Math.max(2000, this.stats.maxElev + 200);
    for (let m = 0; m <= maxM; m += 500) {
      const y = padY + plotH - (m / maxM) * plotH;
      ctx.beginPath();
      ctx.moveTo(padX, y);
      ctx.lineTo(padX + plotW, y);
      ctx.stroke();
      ctx.fillText(`${m}m`, 10, y + 3);
    }

    // Water table line
    if (state.env.waterEnabled) {
      const wy = padY + plotH - (state.env.waterLevelM / maxM) * plotH;
      ctx.fillStyle = "rgba(40, 133, 130, 0.22)";
      ctx.fillRect(padX, wy, plotW, padY + plotH - wy);
      ctx.strokeStyle = "#38b2ac";
      ctx.beginPath();
      ctx.moveTo(padX, wy);
      ctx.lineTo(padX + plotW, wy);
      ctx.stroke();
    }

    // Bedrock + Sediment + Snow profile along Z = 0.5
    ctx.beginPath();
    ctx.moveTo(padX, padY + plotH);
    for (let x = 0; x < N; x++) {
      const idx = sliceZ * N + x;
      const elev = Math.max(0, this.cpuHeight[idx]);
      const px = padX + (x / (N - 1)) * plotW;
      const py = padY + plotH - (elev / maxM) * plotH;
      ctx.lineTo(px, py);
    }
    ctx.lineTo(padX + plotW, padY + plotH);
    ctx.closePath();
    const grad = ctx.createLinearGradient(0, padY, 0, padY + plotH);
    grad.addColorStop(0, "rgba(255, 180, 84, 0.42)");
    grad.addColorStop(1, "rgba(38, 38, 42, 0.85)");
    ctx.fillStyle = grad;
    ctx.fill();

    ctx.strokeStyle = "#ffb454";
    ctx.lineWidth = 2;
    ctx.stroke();
  }

  //==========================================================================================================================================
  //  WEBGL2 HARDWARE FALLBACK (IF NAVIGATOR.GPU IS UNAVAILABLE)
  //==========================================================================================================================================
  initWebGL2Fallback() {
    const gl = this.gpuCanvas.getContext("webgl2", { antialias: true });
    if (!gl) return;
    this.gl = gl;

    const vs = `#version 300 es
    precision highp float;
    layout(location=0) in vec4 aVert;
    uniform mat4 uVP;
    uniform sampler2D uHeightTex;
    uniform vec4 uEnv0; // x=gridSize, y=domainWorld, z=hScale, w=pedestalFloor
    out vec3 vWorldPos;
    out vec2 vUV;
    out vec2 vSkirt;
    void main() {
      vec2 uv = clamp(aVert.xy, 0.0, 1.0);
      vec4 ter = texture(uHeightTex, uv);
      vec2 xz = (uv - 0.5) * uEnv0.y;
      float y = aVert.z > 0.5 ? uEnv0.w : ter.r * uEnv0.z;
      vWorldPos = vec3(xz.x, y, xz.y);
      vUV = uv;
      vSkirt = aVert.zw;
      gl_Position = uVP * vec4(vWorldPos, 1.0);
    }`;

    const fs = `#version 300 es
    precision highp float;
    in vec3 vWorldPos;
    in vec2 vUV;
    in vec2 vSkirt;
    uniform sampler2D uHeightTex;
    uniform sampler2D uAlbedoTex;
    uniform vec3 uCamPos;
    uniform vec3 uSunDir;
    uniform vec3 uSunCol;
    uniform vec4 uEnv0;
    uniform vec4 uFog; // xyz=fogCol, w=fogDensity
    out vec4 outColor;
    void main() {
      vec4 ter = texture(uHeightTex, vUV);
      vec3 albedo = pow(texture(uAlbedoTex, vUV).rgb, vec3(2.2));
      vec2 stepUV = vec2(1.0 / uEnv0.x);
      float hL = texture(uHeightTex, vUV - vec2(stepUV.x, 0.0)).r;
      float hR = texture(uHeightTex, vUV + vec2(stepUV.x, 0.0)).r;
      float hD = texture(uHeightTex, vUV - vec2(0.0, stepUV.y)).r;
      float hU = texture(uHeightTex, vUV + vec2(0.0, stepUV.y)).r;
      float cellM = (uEnv0.y / uEnv0.z) / uEnv0.x;
      vec3 n = normalize(vec3(-(hR - hL) / (2.0 * cellM), 1.0, -(hU - hD) / (2.0 * cellM)));
      if (vSkirt.x > 0.45) {
        albedo = vec3(0.15, 0.14, 0.13) * (0.75 + 0.25 * sin(vWorldPos.y * 10.0));
        n = normalize(vec3(vUV - 0.5, 0.1).xzy);
      }
      float ndl = max(0.0, dot(n, normalize(uSunDir)));
      vec3 skyAmb = mix(vec3(0.14, 0.13, 0.12), vec3(0.22, 0.34, 0.56), 0.5 + 0.5 * n.y);
      vec3 col = albedo * (uSunCol * ndl * 1.45 + skyAmb * 0.55);
      float dist = length(uCamPos - vWorldPos);
      float fogAmt = clamp(1.0 - exp(-dist * uFog.w * 0.05), 0.0, 0.9);
      col = mix(col, uFog.rgb, fogAmt);
      col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);
      outColor = vec4(pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
    }`;

    const compile = (type, src) => {
      const s = gl.createShader(type);
      gl.shaderSource(s, src);
      gl.compileShader(s);
      return s;
    };
    const prog = gl.createProgram();
    gl.attachShader(prog, compile(gl.VERTEX_SHADER, vs));
    gl.attachShader(prog, compile(gl.FRAGMENT_SHADER, fs));
    gl.linkProgram(prog);
    this.glProg = prog;

    const mesh = buildTerrainMeshGeometry(this.cpuSize);
    this.glIndexCount = mesh.indexCount;
    this.glSurfaceIndexCount = mesh.surfaceIndexCount;

    this.glVao = gl.createVertexArray();
    gl.bindVertexArray(this.glVao);
    const vb = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vb);
    gl.bufferData(gl.ARRAY_BUFFER, mesh.verts, gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 4, gl.FLOAT, false, 16, 0);

    const ib = gl.createBuffer();
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, mesh.indices, gl.STATIC_DRAW);

    this.glHeightTex = gl.createTexture();
    this.glAlbedoTex = gl.createTexture();
    gl.getExtension("OES_texture_float_linear");
  }

  updateWebGL2Buffers() {
    const gl = this.gl;
    if (!gl) return;
    const N = this.cpuSize;
    gl.bindTexture(gl.TEXTURE_2D, this.glHeightTex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, N, N, 0, gl.RED, gl.FLOAT, this.cpuHeight);

    gl.bindTexture(gl.TEXTURE_2D, this.glAlbedoTex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, N, N, 0, gl.RGBA, gl.UNSIGNED_BYTE, this.cpuAlbedo);
  }

  renderWebGL2Frame(state, camInfo) {
    const gl = this.gl;
    const canvas = this.gpuCanvas;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = Math.max(1, Math.floor(canvas.clientWidth * dpr));
    const h = Math.max(1, Math.floor(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    gl.viewport(0, 0, w, h);
    gl.clearColor(0.08, 0.11, 0.16, 1.0);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    gl.enable(gl.DEPTH_TEST);

    gl.useProgram(this.glProg);
    gl.bindVertexArray(this.glVao);

    const env = state.env;
    const domainWorld = 10.0;
    const hScale = (domainWorld / ((env.domainSizeKm || 5.0) * 1000)) * (env.verticalExaggeration || 1.0);
    const az = ((env.sunAzimuth ?? 132) * Math.PI) / 180;
    const el = ((env.sunElevation ?? 27) * Math.PI) / 180;
    const sunDir = [Math.cos(el) * Math.sin(az), Math.sin(el), Math.cos(el) * Math.cos(az)];
    const sunCol = kelvinToLinearRGB(env.sunTemperatureK ?? 5600);
    const fogCol = hexToRgbLinear(env.fogColor ?? "#9eb4c9");

    gl.uniformMatrix4fv(gl.getUniformLocation(this.glProg, "uVP"), false, camInfo.viewProj);
    gl.uniform3fv(gl.getUniformLocation(this.glProg, "uCamPos"), camInfo.eye);
    gl.uniform3fv(gl.getUniformLocation(this.glProg, "uSunDir"), sunDir);
    gl.uniform3fv(gl.getUniformLocation(this.glProg, "uSunCol"), sunCol);
    gl.uniform4f(gl.getUniformLocation(this.glProg, "uEnv0"), this.cpuSize, domainWorld, hScale, (env.pedestalFloorM ?? -180) * hScale);
    gl.uniform4f(gl.getUniformLocation(this.glProg, "uFog"), fogCol[0], fogCol[1], fogCol[2], env.fogEnabled ? (env.fogDensity ?? 0.22) : 0.0);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.glHeightTex);
    gl.uniform1i(gl.getUniformLocation(this.glProg, "uHeightTex"), 0);

    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.glAlbedoTex);
    gl.uniform1i(gl.getUniformLocation(this.glProg, "uAlbedoTex"), 1);

    const count = env.pedestalEnabled ? this.glIndexCount : this.glSurfaceIndexCount;
    gl.drawElements(gl.TRIANGLES, count, gl.UNSIGNED_INT, 0);
  }
}
