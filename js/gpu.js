//============================================================================================================================================
//  GPU.JS — full-resolution WebGPU terrain core (up to 4096 x 4096 texture-resident fields)
//  Everything that defines the 3D terrain runs on the GPU at the simulation resolution:
//    terrain layer stack (9 landforms + modifiers) -> pipe-model hydraulic erosion -> thermal talus -> snowpack/flow
//    -> geomorphology (normals, curvature, AO) -> texture layer stack (SatMap / PBR splat) -> textured viewport render.
//  The 256^2 CPU mirror is kept only for the 2D map, statistics and sculpt picking; it is filled by a GPU box-downsample readback.
//============================================================================================================================================
import {
  HEAD,
  FILL_WGSL,
  LAYER_WGSL,
  FLUX_WGSL,
  ERODE_WGSL,
  THERMAL_WGSL,
  FIELDS_WGSL,
  buildSplatWGSL,
  buildRenderWGSL,
} from "./gpu_kernels.js";
import { SATMAPS } from "./presets.js";
import { hexToRgbLinear, buildTerrainMeshGeometry } from "./engine.js";

const SHADER_VERT = GPUShaderStage.VERTEX;
const SHADER_FRAG = GPUShaderStage.FRAGMENT;
const TEX_USAGE =
  GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.STORAGE_BINDING | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST;

const TYPE_IDS = {
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

const CPU_MIRROR = 256;
const MESH_MAX = 2048;
const DOWN_CH = 8; // H, W, flow, sediment, talus, snow, (2 spare)

const DOWN_WGSL = /* wgsl */ `
struct Down { n: vec4f, };  // x=N, y=block size, z=mirror size
@group(0) @binding(0) var<uniform> D: Down;
@group(0) @binding(1) var hTex: texture_2d<f32>;
@group(0) @binding(2) var bTex: texture_2d<f32>;
@group(0) @binding(3) var wTex: texture_2d<f32>;
@group(0) @binding(4) var<storage, read_write> outBuf: array<f32>;
@compute @workgroup_size(8, 8)
fn downMain(@builtin(global_invocation_id) gid: vec3u) {
  let C = i32(D.n.z);
  let x = i32(gid.x);
  let z = i32(gid.y);
  if (x >= C || z >= C) { return; }
  let k = i32(D.n.y);
  var h = 0.0;
  var w = 0.0;
  var fl = 0.0;
  var sd = 0.0;
  var tl = 0.0;
  var sn = 0.0;
  for (var j = 0; j < k; j++) {
    for (var i = 0; i < k; i++) {
      let q = vec2i(x * k + i, z * k + j);
      h += textureLoad(hTex, q, 0).x;
      w += textureLoad(wTex, q, 0).x;
      let b = textureLoad(bTex, q, 0);
      fl += b.x;
      sd += b.y;
      tl += b.z;
      sn += b.w;
    }
  }
  let inv = 1.0 / f32(k * k);
  let o = (z * C + x) * ${DOWN_CH};
  outBuf[o + 0] = h * inv;
  outBuf[o + 1] = w * inv;
  outBuf[o + 2] = fl * inv;
  outBuf[o + 3] = sd * inv;
  outBuf[o + 4] = tl * inv;
  outBuf[o + 5] = sn * inv;
  outBuf[o + 6] = 0.0;
  outBuf[o + 7] = 0.0;
}
`;

// Per-layer parameter block (32 floats) — index map is documented in LAYER_WGSL (prm(i))
function packLayerGPU(L, typeId, opts = {}) {
  const a = new Float32Array(32);
  const num = (v, d) => (typeof v === "number" && Number.isFinite(v) ? v : d);
  const rad = (deg) => (num(deg, 0) * Math.PI) / 180;
  a[0] = typeId;
  a[1] = num(L.blendMode, 0);
  a[2] = num(L.opacity, 1);
  a[3] = L.invert ? 1 : 0;
  a[4] = num(L.elevation, 1500);
  a[5] = num(L.scale, 2);
  a[6] = Math.max(1, Math.min(12, Math.round(num(L.octaves, 7))));
  a[7] = num(L.lacunarity, 2.08);
  a[8] = num(L.gain, 0.49);
  a[9] = num(L.ridgeSharpness, 1.4);
  a[10] = L.domainWarp ? num(L.warpStrength, 0.4) : 0;
  a[11] = num(L.seed, 101);
  a[12] = rad(L.strikeAngle ?? L.windAngle ?? 32);
  a[13] = num(L.anisotropy, 0.4);
  a[14] = num(L.valleyFloor, 0.35);
  a[15] = num(L.cliffSteepness, 0.6);
  a[16] = num(L.radiusKm, 2.2);
  a[17] = num(L.areteCount ?? L.stepCount ?? L.radialGullies, 4);
  a[18] = num(L.cirqueCarving ?? L.canyonWidth ?? L.ledgeSharpness ?? L.calderaDepth, 0.7);
  a[19] = num(L.meanderFreq ?? L.foldWarp ?? L.tiltAmount ?? L.slopePreservation, 1.75);
  a[20] = num(opts.altMin, -1e6);
  a[21] = num(opts.altMax, 1e6);
  a[22] = num(opts.slopeMin, 0);
  a[23] = num(opts.slopeMax, 90);
  a[24] = num(opts.radial, 0);
  a[25] = num(L.offsetX, 0);
  a[26] = num(L.offsetZ, 0);
  a[27] = num(L.gradientDamping, 0.72);
  a[28] = num(L.gullyDepth ?? L.stepCount, 0.58);
  a[29] = num(L.stepSharpness ?? L.calderaRadius, 0.5);
  // Type-specific overrides
  switch (typeId) {
    case 1: // alpine: arete count, cirque carving
      a[17] = num(L.areteCount, 4);
      a[18] = num(L.cirqueCarving, 0.7);
      break;
    case 2: // canyon/mesa: depth, width, meander, steps, sharpness
      a[17] = num(L.canyonDepth, 0.85);
      a[18] = num(L.canyonWidth, 0.3);
      a[19] = num(L.meanderFreq, 1.75);
      a[28] = Math.max(1, num(L.stepCount, 7));
      a[29] = num(L.stepSharpness, 0.6);
      break;
    case 3: // volcano: caldera radius, depth, radial gullies, gully depth
      a[17] = num(L.calderaRadius, 0.36);
      a[18] = num(L.calderaDepth, 0.68);
      a[19] = num(L.radialGullies, 22);
      a[28] = num(L.gullyDepth, 0.58);
      break;
    case 4: // fault: sharpness, block scale, tilt
      a[17] = num(L.faultSharpness, 0.86);
      a[18] = num(L.blockScale, 3.2);
      a[19] = num(L.tiltAmount, 0.55);
      break;
    case 5: // strata: frequency (scale), dip (rad), ledge sharpness, fold warp
      a[5] = num(L.frequency, 16);
      a[17] = rad(L.dipAngle ?? 12);
      a[18] = num(L.ledgeSharpness, 0.6);
      a[19] = num(L.foldWarp, 0.4);
      break;
    case 7: // terrace: steps, sharpness, slope preservation
      a[17] = num(L.stepCount, 9);
      a[18] = num(L.stepSharpness, 0.6);
      a[19] = num(L.slopePreservation, 0.4);
      break;
    case 8: // dunes: crest sharpness, asymmetry, ripple detail
      a[17] = num(L.crestSharpness, 1.6);
      a[18] = num(L.asymmetry, 0.5);
      a[19] = num(L.rippleDetail, 0.5);
      break;
    default:
      break;
  }
  return a;
}

// Hydraulic erosion (pipe model) parameters — tuned to the same user controls as the CPU solver
function packErodeGPU(L) {
  const a = new Float32Array(12);
  const num = (v, d) => (typeof v === "number" && Number.isFinite(v) ? v : d);
  const dt = 0.02;
  a[0] = dt; // dt
  a[1] = 9.81; // gravity
  a[2] = 1.0; // pipe cross-section
  a[3] = num(L.rainRate, 0.019) * 0.6; // rain per step (m)
  a[4] = num(L.sedimentCapacity, 1.4) * 1.0; // Kc
  a[5] = num(L.erosionRate, 0.62) * 3.0; // Ks
  a[6] = num(L.depositionRate, 0.45) * 2.0; // Kd
  a[7] = num(L.evaporation, 0.03) * 0.2; // evap
  return a;
}

function packThermalGPU(L) {
  const a = new Float32Array(12);
  const num = (v, d) => (typeof v === "number" && Number.isFinite(v) ? v : d);
  a[8] = Math.min(0.125, num(L.weatheringRate, 0.5) * 0.2); // rate (<= 0.125 for stability of the 8-neighbour slump)
  a[9] = Math.tan((num(L.reposeAngle, 36) * Math.PI) / 180); // tan(repose)
  return a;
}

function packSplatGPU(state, N, domainKm) {
  // Mirrors the SplatGlobal layout used by the texture stack (g0 + 12 x TexLayerGpu, 60 floats each)
  const buf = new Float32Array(4 + 12 * 60);
  const texStack = state.textureStack || [];
  const hasSolo = texStack.some((l) => l.solo && l.enabled);
  buf[0] = N;
  buf[1] = domainKm;
  buf[2] = Math.min(12, texStack.length);
  buf[3] = state.activeStack === "texture" ? 1 : 0;
  for (let i = 0; i < Math.min(12, texStack.length); i++) {
    const L = texStack[i];
    const off = 4 + i * 60;
    const cPrim = hexToRgbLinear(L.colPrimary);
    const cSec = hexToRgbLinear(L.colSecondary);
    const cAcc = hexToRgbLinear(L.colAccent);
    buf.set([cPrim[0], cPrim[1], cPrim[2], L.roughness], off + 0);
    buf.set([cSec[0], cSec[1], cSec[2], L.specular], off + 4);
    buf.set([cAcc[0], cAcc[1], cAcc[2], L.strataIntensity], off + 8);
    const sat = SATMAPS.find((s) => s.id === L.satmapId) || SATMAPS[0];
    for (let s = 0; s < 5; s++) {
      const rgb = hexToRgbLinear(sat.stops[s]);
      buf.set([rgb[0], rgb[1], rgb[2], 1.0], off + 12 + s * 4);
    }
    const isEnabled = L.enabled && (!hasSolo || L.solo);
    const isSelected = state.selection?.id === L.id ? 1.0 : 0.0;
    buf.set([isEnabled ? 1 : 0, L.blendMode, L.opacity, L.heightContrast], off + 32);
    buf.set([L.useAltMask ? 1 : 0, L.altMin, L.altMax, L.altFeather], off + 36);
    buf.set([L.useSlopeMask ? 1 : 0, L.slopeMin, L.slopeMax, L.slopeFeather], off + 40);
    buf.set([L.useCurvatureMask ? 1 : 0, L.curvatureMode, L.curvatureStrength, L.useFlowMask ? 1 : 0], off + 44);
    buf.set([L.flowStrength, L.useSedimentMask ? 1 : 0, L.sedimentWeight, L.talusWeight], off + 48);
    buf.set([L.useSnowMask ? 1 : 0, L.snowWeight, L.useNoiseMask ? 1 : 0, L.noiseScale], off + 52);
    buf.set([L.noiseAmount, L.detailScale, isSelected, L.useSatmap ? 1 : 0], off + 56);
  }
  return buf;
}

// Mass-balanced GPU field pipeline with ping-pong textures
export class GpuTerrainCore {
  constructor(engine) {
    this.e = engine;
    this.dev = engine.device;
    this.ready = false;
    this.N = 0;
    this.seq = 0;
    this.lastState = null;
    this.snowParams = null;
    this.layerBufs = [];
    this.erBufs = [];
    this.buildPipelines();
  }

  buildPipelines() {
    const dev = this.dev;
    const mod = (code, label) => dev.createShaderModule({ code, label });
    const comp = (module, entryPoint) => dev.createComputePipeline({ layout: "auto", compute: { module, entryPoint } });

    const fillMod = mod(FILL_WGSL, "gpu-fill");
    this.pFill32 = comp(fillMod, "fill32");
    this.pFill16 = comp(fillMod, "fill16");
    this.pLayer = comp(mod(HEAD + LAYER_WGSL, "gpu-layer"), "layerMain");
    this.pFlux = comp(mod(FLUX_WGSL, "gpu-flux"), "fluxMain");
    this.pErode = comp(mod(ERODE_WGSL, "gpu-erode"), "erodeMain");
    this.pThermal = comp(mod(THERMAL_WGSL, "gpu-thermal"), "thermalMain");
    this.pFields = comp(mod(FIELDS_WGSL, "gpu-fields"), "fieldsMain");
    this.pSplat = comp(mod(buildSplatWGSL(), "gpu-splat"), "main");
    this.pDown = comp(mod(DOWN_WGSL, "gpu-downsample"), "downMain");

    const renderMod = mod(buildRenderWGSL(), "gpu-viewport");
    this.rBgl = dev.createBindGroupLayout({
      entries: [
        { binding: 0, visibility: SHADER_VERT | SHADER_FRAG, buffer: { type: "uniform" } },
        ...[1, 2, 3, 4, 5, 6].map((b) => ({
          binding: b,
          visibility: SHADER_VERT | SHADER_FRAG,
          texture: { sampleType: "unfilterable-float", viewDimension: "2d" },
        })),
      ],
    });
    const rLayout = dev.createPipelineLayout({ bindGroupLayouts: [this.rBgl] });
    const fmt = this.e.format;
    this.pipeSky = dev.createRenderPipeline({
      layout: rLayout,
      vertex: { module: renderMod, entryPoint: "skyVert" },
      fragment: { module: renderMod, entryPoint: "skyFrag", targets: [{ format: fmt }] },
      primitive: { topology: "triangle-list" },
      depthStencil: { format: "depth24plus", depthWriteEnabled: false, depthCompare: "always" },
    });
    this.pipeTerrain = dev.createRenderPipeline({
      layout: rLayout,
      vertex: {
        module: renderMod,
        entryPoint: "terrainVert",
        buffers: [{ arrayStride: 16, attributes: [{ shaderLocation: 0, offset: 0, format: "float32x4" }] }],
      },
      fragment: { module: renderMod, entryPoint: "terrainFrag", targets: [{ format: fmt }] },
      primitive: { topology: "triangle-list", cullMode: "none" },
      depthStencil: { format: "depth24plus", depthWriteEnabled: true, depthCompare: "less-equal" },
    });
    this.pipeWater = dev.createRenderPipeline({
      layout: rLayout,
      vertex: { module: renderMod, entryPoint: "waterVert" },
      fragment: {
        module: renderMod,
        entryPoint: "waterFrag",
        targets: [
          {
            format: fmt,
            blend: {
              color: { srcFactor: "src-alpha", dstFactor: "one-minus-src-alpha", operation: "add" },
              alpha: { srcFactor: "one", dstFactor: "one-minus-src-alpha", operation: "add" },
            },
          },
        ],
      },
      primitive: { topology: "triangle-list" },
      depthStencil: { format: "depth24plus", depthWriteEnabled: false, depthCompare: "less-equal" },
    });

    // Small uniform blocks (one per layer slot so every write is queued before the single submit)
    for (let i = 0; i < 16; i++) {
      this.layerBufs.push(dev.createBuffer({ size: 128, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }));
      this.erBufs.push(dev.createBuffer({ size: 48, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST }));
    }
    const U = GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST;
    this.gBuf = dev.createBuffer({ size: 16, usage: U });
    this.snBuf = dev.createBuffer({ size: 32, usage: U });
    this.splatU = dev.createBuffer({ size: (4 + 12 * 60) * 4, usage: U });
    this.downU = dev.createBuffer({ size: 16, usage: U });
    this.frameBuf = dev.createBuffer({ size: 320, usage: U });
    this.sculptTex = dev.createTexture({
      size: [CPU_MIRROR, CPU_MIRROR],
      format: "r32float",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });
    this.sampler = null;
    this.depthTex = null;
  }

  destroyFields() {
    const ds = [...(this.H || []), ...(this.W || []), ...(this.S || []), ...(this.F || []), this.attrB, this.geom, this.splat, this.meta, this.downBuf];
    for (const t of ds) if (t && t.destroy) t.destroy();
  }

  // Allocate ping-pong fields at N x N. Returns false if the device cannot hold the set (caller falls back).
  allocate(N) {
    const dev = this.dev;
    this.destroyFields();
    this.ready = false;
    dev.pushErrorScope("out-of-memory");
    dev.pushErrorScope("validation");
    const mk = (fmt) => dev.createTexture({ size: [N, N], format: fmt, usage: TEX_USAGE });
    this.H = [mk("r32float"), mk("r32float")];
    this.W = [mk("r32float"), mk("r32float")];
    this.S = [mk("r32float"), mk("r32float")];
    this.F = [mk("rgba16float"), mk("rgba16float")];
    this.attrB = mk("rgba16float");
    this.geom = mk("rgba16float");
    this.splat = mk("rgba16float");
    this.meta = mk("rgba16float");
    this.downBuf = dev.createBuffer({
      size: CPU_MIRROR * CPU_MIRROR * DOWN_CH * 4,
      usage: GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC,
    });
    this.N = N;
    this.ci = 0;
    this.cf = 0;
    return dev.popErrorScope().then(async (valV) => {
      const valOOM = await dev.popErrorScope();
      const err = valOOM || valV;
      if (err) throw new Error(err.message || "GPU allocation failed");
      this.buildMesh(Math.min(MESH_MAX, N));
      this.fillAll();
      this.ready = true;
      return true;
    });
  }

  buildMesh(M) {
    if (this.meshM === M && this.meshVB) return;
    const dev = this.dev;
    if (this.meshVB) this.meshVB.destroy();
    if (this.meshIB) this.meshIB.destroy();
    const mesh = buildTerrainMeshGeometry(M);
    this.meshVB = dev.createBuffer({ size: mesh.verts.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    this.meshIB = dev.createBuffer({ size: mesh.indices.byteLength, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST });
    dev.queue.writeBuffer(this.meshVB, 0, mesh.verts);
    dev.queue.writeBuffer(this.meshIB, 0, mesh.indices);
    this.meshIndexCount = mesh.indexCount;
    this.meshSurfaceCount = mesh.surfaceIndexCount;
    this.meshM = M;
  }

  fillAll() {
    const enc = this.dev.createCommandEncoder();
    this.fillTextures(enc);
    this.dev.queue.submit([enc.finish()]);
  }

  fillTextures(enc) {
    const N = this.N;
    const wg = N / 8;
    const fill = (pipe, tex) => {
      const pass = enc.beginComputePass();
      pass.setPipeline(pipe);
      pass.setBindGroup(0, this.dev.createBindGroup({ layout: pipe.getBindGroupLayout(0), entries: [{ binding: pipe === this.pFill32 ? 0 : 1, resource: tex.createView() }] }));
      pass.dispatchWorkgroups(wg, wg);
      pass.end();
    };
    for (const t of [...this.H, ...this.W, ...this.S]) fill(this.pFill32, t);
    for (const t of this.F) fill(this.pFill16, t);
  }

  dispatch(enc, pipe, pairs) {
    const dev = this.dev;
    const bg = dev.createBindGroup({
      layout: pipe.getBindGroupLayout(0),
      entries: pairs.map(([binding, resource]) => ({
        binding,
        resource: resource instanceof GPUTexture ? resource.createView() : { buffer: resource },
      })),
    });
    const pass = enc.beginComputePass();
    pass.setPipeline(pipe);
    pass.setBindGroup(0, bg);
    const wg = this.N / 8;
    pass.dispatchWorkgroups(wg, wg);
    pass.end();
  }

  // ---------- stack passes ----------
  terrainLayer(enc, L, li, state) {
    const typeId = TYPE_IDS[L.type] ?? 0;
    const buf = this.layerBufs[li];
    const sel = state.selection?.id === L.id;
    const opts = {};
    if (L.useAltMask) {
      opts.altMin = L.altMin;
      opts.altMax = L.altMax;
    }
    this.dev.queue.writeBuffer(buf, 0, packLayerGPU(L, typeId, opts));
    const src = this.H[this.ci];
    const dst = this.H[1 - this.ci];
    this.dispatch(enc, this.pLayer, [
      [0, this.gBuf],
      [1, buf],
      [2, src],
      [3, dst],
    ]);
    this.ci = 1 - this.ci;
    return sel;
  }

  erodeLayer(enc, L, li) {
    const iters = Math.max(0, Math.min(400, Math.round(L.iterations ?? 90)));
    const buf = this.erBufs[li];
    this.dev.queue.writeBuffer(buf, 0, packErodeGPU(L));
    this.activeErBuf = buf;
    this.erodeSteps(enc, buf, iters);
  }

  erodeSteps(enc, erBuf, iters) {
    for (let k = 0; k < iters; k++) {
      const c = this.ci;
      const cf = this.cf;
      const fn = 1 - cf;
      this.dispatch(enc, this.pFlux, [
        [0, this.gBuf],
        [1, erBuf],
        [2, this.H[c]],
        [3, this.W[c]],
        [4, this.F[cf]],
        [5, this.F[fn]],
      ]);
      this.cf = fn;
      this.dispatch(enc, this.pErode, [
        [0, this.gBuf],
        [1, erBuf],
        [2, this.H[c]],
        [3, this.W[c]],
        [5, this.S[c]],
        [6, this.F[fn]],
        [7, this.H[1 - c]],
        [8, this.W[1 - c]],
        [9, this.S[1 - c]],
      ]);
      this.ci = 1 - c;
    }
  }

  thermalLayer(enc, L, li) {
    const iters = Math.max(0, Math.min(200, Math.round(L.iterations ?? 40)));
    const buf = this.erBufs[li];
    const th = packThermalGPU(L);
    const erParams = new Float32Array(12);
    erParams.set(th.subarray(8, 10), 8);
    this.dev.queue.writeBuffer(buf, 0, erParams);
    for (let k = 0; k < iters; k++) {
      const c = this.ci;
      this.dispatch(enc, this.pThermal, [
        [0, this.gBuf],
        [1, buf],
        [2, this.H[c]],
        [5, this.H[1 - c]],
      ]);
      this.ci = 1 - c;
    }
  }

  snowFromLayer(L) {
    const deg2rad = (d) => ((d ?? 0) * Math.PI) / 180;
    return {
      snowline: L.snowlineAlt ?? 2400,
      depth: L.snowDepth ?? 0.6,
      windAz: deg2rad(L.windAzimuth ?? 225),
      drift: L.windDrift ?? 0.5,
      melt: L.solarMelt ?? 0.5,
      repose: L.reposeAngle ?? 38,
    };
  }

  // Fields, geomorphology and texture splat (+ mirror readback)
  fieldsAndSplat(enc, state) {
    const dev = this.dev;
    const N = this.N;
    const sn = this.snowParams;
    const snowOn = sn ? 1 : 0;
    const rain = (state.terrainStack || []).find((l) => l.type === "hydraulic_erosion" && l.enabled)?.rainRate ?? 0.019;
    // SN[0] = (snowline, depth, windAz, drift); SN[1] = (melt, repose, flowScale, enabled)
    const flowScale = 0.6 * rain * 60 + 0.01; // normalises the pipe-model outflow into a 0..1 flow visual
    dev.queue.writeBuffer(
      this.snBuf,
      0,
      new Float32Array([
        sn?.snowline ?? 0,
        sn?.depth ?? 0,
        sn?.windAz ?? 0,
        sn?.drift ?? 0,
        sn?.melt ?? 0,
        sn?.repose ?? 38,
        flowScale,
        snowOn,
      ])
    );
    const domainKm = state.env.domainSizeKm || 5.0;
    dev.queue.writeBuffer(this.splatU, 0, packSplatGPU(state, N, domainKm));

    const c = this.ci;
    const cf = this.cf;
    this.dispatch(enc, this.pFields, [
      [0, this.gBuf],
      [1, this.snBuf],
      [2, this.H[c]],
      [3, this.F[cf]],
      [4, this.S[c]],
      [5, this.attrB],
    ]);
    this.dispatch(enc, this.pSplat, [
      [0, this.splatU],
      [1, this.H[c]],
      [2, this.attrB],
      [3, this.geom],
      [4, this.splat],
      [5, this.meta],
    ]);
    // Box-downsample to the CPU mirror resolution for 2D map, picking and statistics
    const k = N / CPU_MIRROR;
    dev.queue.writeBuffer(this.downU, 0, new Float32Array([N, k, CPU_MIRROR, 0]));
    const pass = enc.beginComputePass();
    pass.setPipeline(this.pDown);
    pass.setBindGroup(
      0,
      dev.createBindGroup({
        layout: this.pDown.getBindGroupLayout(0),
        entries: [
          { binding: 0, resource: { buffer: this.downU } },
          { binding: 1, resource: this.H[c].createView() },
          { binding: 2, resource: this.attrB.createView() },
          { binding: 3, resource: this.W[c].createView() },
          { binding: 4, resource: { buffer: this.downBuf } },
        ],
      })
    );
    pass.dispatchWorkgroups(CPU_MIRROR / 8, CPU_MIRROR / 8);
    pass.end();
  }

  // ---------- evaluation ----------
  evaluate(state, rebuildStack = true) {
    const dev = this.dev;
    const N = this.N;
    this.lastState = state;
    const domainKm = state.env.domainSizeKm || 5.0;
    const cm = (domainKm * 1000) / N;
    dev.queue.writeBuffer(this.gBuf, 0, new Float32Array([N, cm, 0, domainKm]));
    const enc = dev.createCommandEncoder();
    if (rebuildStack) {
      this.fillTextures(enc);
      this.ci = 0;
      this.cf = 0;
      this.snowParams = null;
      const tStack = state.terrainStack || [];
      const hasSolo = tStack.some((l) => l.solo && l.enabled);
      for (let li = 0; li < tStack.length && li < 16; li++) {
        const L = tStack[li];
        if (!L.enabled || (hasSolo && !L.solo)) continue;
        if (L.type === "hydraulic_erosion") this.erodeLayer(enc, L, li);
        else if (L.type === "thermal_erosion") this.thermalLayer(enc, L, li);
        else if (L.type === "snowfall") this.snowParams = this.snowFromLayer(L);
        else this.terrainLayer(enc, L, li, state);
      }
    }
    this.fieldsAndSplat(enc, state);
    dev.queue.submit([enc.finish()]);
    this.scheduleMirrorReadback();
  }

  // Continue hydraulic erosion on the current field (interactive "+ erosion" step)
  continueErosion(state, steps) {
    const dev = this.dev;
    const hydro =
      (state.terrainStack || []).find((l) => l.type === "hydraulic_erosion" && l.enabled) ||
      { rainRate: 0.019, erosionRate: 0.62, depositionRate: 0.45, sedimentCapacity: 1.4, evaporation: 0.03 };
    const thermal =
      (state.terrainStack || []).find((l) => l.type === "thermal_erosion" && l.enabled) ||
      { reposeAngle: 36, weatheringRate: 0.5, talusSpread: 0.68 };
    const cm = ((state.env.domainSizeKm || 5.0) * 1000) / this.N;
    dev.queue.writeBuffer(this.gBuf, 0, new Float32Array([this.N, cm, 0, state.env.domainSizeKm || 5.0]));
    const erBuf = this.erBufs[15];
    dev.queue.writeBuffer(erBuf, 0, packErodeGPU(hydro));
    const thermBuf = this.erBufs[14];
    const th = packThermalGPU(thermal);
    const tp = new Float32Array(12);
    tp.set(th.subarray(8, 10), 8);
    dev.queue.writeBuffer(thermBuf, 0, tp);
    const enc = dev.createCommandEncoder();
    this.erodeSteps(enc, erBuf, Math.max(1, Math.round(steps)));
    for (let k = 0; k < Math.max(2, Math.floor(steps * 0.4)); k++) {
      const c = this.ci;
      this.dispatch(enc, this.pThermal, [
        [0, this.gBuf],
        [1, thermBuf],
        [2, this.H[c]],
        [5, this.H[1 - c]],
      ]);
      this.ci = 1 - c;
    }
    this.fieldsAndSplat(enc, state);
    dev.queue.submit([enc.finish()]);
    this.scheduleMirrorReadback();
  }

  scheduleMirrorReadback() {
    const dev = this.dev;
    const seq = ++this.seq;
    const size = CPU_MIRROR * CPU_MIRROR * DOWN_CH * 4;
    const staging = dev.createBuffer({ size, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST });
    const enc = dev.createCommandEncoder();
    enc.copyBufferToBuffer(this.downBuf, 0, staging, 0, size);
    dev.queue.submit([enc.finish()]);
    staging
      .mapAsync(GPUMapMode.READ)
      .then(() => {
        const data = new Float32Array(staging.getMappedRange().slice(0));
        staging.unmap();
        staging.destroy();
        if (seq !== this.seq) return; // a newer evaluation superseded this one
        this.applyMirror(data);
      })
      .catch((err) => console.warn("GPU mirror readback failed:", err));
  }

  applyMirror(data) {
    const e = this.e;
    const C = CPU_MIRROR;
    const n = C * C;
    for (let i = 0; i < n; i++) {
      const o = i * DOWN_CH;
      e.cpuHeight[i] = Math.max(-250, data[o + 0] + e.sculptOffsets[i]);
      e.cpuWater[i] = data[o + 1];
      e.cpuFlow[i] = data[o + 2];
      e.cpuSediment[i] = data[o + 3];
      e.cpuTalus[i] = data[o + 4];
      e.cpuSnow[i] = data[o + 5];
    }
    const domainKm = this.lastState?.env?.domainSizeKm || 5.0;
    if (this.lastState) e.computeCpuGeomorphologyAndSplat(this.lastState, C, (domainKm * 1000) / C);
    if (typeof e.onMirrorReady === "function") e.onMirrorReady();
  }

  // ---------- render ----------
  renderFrame(state, camInfo, timeSec) {
    const dev = this.dev;
    const canvas = this.e.gpuCanvas;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    const w = Math.max(1, Math.floor(canvas.clientWidth * dpr));
    const h = Math.max(1, Math.floor(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h || !this.depthTex) {
      canvas.width = w;
      canvas.height = h;
      if (this.depthTex) this.depthTex.destroy();
      this.depthTex = dev.createTexture({ size: [w, h], format: "depth24plus", usage: GPUTextureUsage.RENDER_ATTACHMENT });
    }
    const u = this.e.packFrameUniforms(state, camInfo, timeSec);
    dev.queue.writeBuffer(this.frameBuf, 0, u);

    // Sculpt offsets (256^2) are live: upload every frame (256 KB)
    const sculpt = new Float32Array(this.e.sculptOffsets);
    dev.queue.writeTexture({ texture: this.sculptTex }, sculpt, { bytesPerRow: CPU_MIRROR * 4, rowsPerImage: CPU_MIRROR }, [CPU_MIRROR, CPU_MIRROR]);

    const bg = dev.createBindGroup({
      layout: this.rBgl,
      entries: [
        { binding: 0, resource: { buffer: this.frameBuf } },
        { binding: 1, resource: this.H[this.ci].createView() },
        { binding: 2, resource: this.geom.createView() },
        { binding: 3, resource: this.attrB.createView() },
        { binding: 4, resource: this.splat.createView() },
        { binding: 5, resource: this.meta.createView() },
        { binding: 6, resource: this.sculptTex.createView() },
      ],
    });
    const enc = dev.createCommandEncoder();
    const pass = enc.beginRenderPass({
      colorAttachments: [
        { view: this.e.context.getCurrentTexture().createView(), clearValue: { r: 0.04, g: 0.05, b: 0.07, a: 1 }, loadOp: "clear", storeOp: "store" },
      ],
      depthStencilAttachment: {
        view: this.depthTex.createView(),
        depthClearValue: 1.0,
        depthLoadOp: "clear",
        depthStoreOp: "store",
      },
    });
    pass.setBindGroup(0, bg);
    pass.setPipeline(this.pipeSky);
    pass.draw(3);
    pass.setPipeline(this.pipeTerrain);
    pass.setVertexBuffer(0, this.meshVB);
    pass.setIndexBuffer(this.meshIB, "uint32");
    pass.drawIndexed(state.env.pedestalEnabled ? this.meshIndexCount : this.meshSurfaceCount);
    if (state.env.waterEnabled && state.shadingMode <= 1) {
      pass.setPipeline(this.pipeWater);
      pass.draw(6);
    }
    pass.end();
    dev.queue.submit([enc.finish()]);
  }
}

// Route the engine's WebGPU paths through the full-resolution GPU core. WebGL2/CPU fallbacks are untouched.
export function installGpuPath(Engine) {
  const P = Engine.prototype;
  const origEval = P.evaluateStack;
  const origStep = P.stepLiveErosion;

  P.initWebGPUResources = async function () {
    if (!this.device) return;
    if (!this.gpu) this.gpu = new GpuTerrainCore(this);
    const tryList = [this.gridSize, 2048, 1024].filter((v, i, a) => v && a.indexOf(v) === i && v <= this.gridSize);
    let lastErr = null;
    for (const N of tryList) {
      try {
        await this.gpu.allocate(N);
        this.gridSize = N;
        this.gpuResolution = N;
        return;
      } catch (err) {
        lastErr = err;
        console.warn(`GPU field allocation at ${N}^2 failed, falling back:`, err && err.message);
      }
    }
    throw lastErr || new Error("GPU allocation failed");
  };

  P.evaluateStack = function (state, options = { fullRebuild: true }) {
    if (this.backend !== "webgpu" || !this.gpu || !this.gpu.ready) return origEval.call(this, state, options);
    const t0 = performance.now();
    this.gpu.evaluate(state, options.fullRebuild !== false);
    const dt = Math.max(0.5, performance.now() - t0);
    this.timings.totalMs = dt;
    this.timings.splatMs = dt;
    this.timings.erosionMs = dt;
    this.timings.terrainMs = dt;
  };

  P.stepLiveErosion = function (state, steps = 12) {
    if (this.backend !== "webgpu" || !this.gpu || !this.gpu.ready) return origStep.call(this, state, steps);
    this.gpu.continueErosion(state, steps);
  };

  P.uploadCpuToWebGPUAndSplat = function () {
    // Superseded by GPU evaluation (fields never leave the GPU except the 256^2 mirror readback)
  };

  P.renderWebGPUFrame = function (state, camInfo, timeSec) {
    if (!this.gpu || !this.gpu.ready) return;
    this.gpu.renderFrame(state, camInfo, timeSec);
  };
  return Engine;
}
