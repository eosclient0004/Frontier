//============================================================================================================================================
//  SHADERS.JS — WGSL COMPUTE & AAA PROCEDURAL GEOLOGICAL RENDER SHADERS
//  Features:
//  - Multi-scale 3D Sedimentary Stratification (carved cliff ledges + recessed shale seams + mineral color banding)
//  - 3D Cellular Voronoi Rock Outcrop Cleavage & Fracture Cracks
//  - Procedural 3D Boulder Fields & Angular Talus Scree Facets at Cliff Bases
//  - High-Contrast Dendritic Hydraulic Erosion Gullies, Wet Cobble Channels & Braided Alluvial Fans
//  - 28-Step Heightfield Raymarched Soft Shadows + Micro-Cavity Crevice Occlusion
//============================================================================================================================================

export const WGSL_NOISE_LIB = /* wgsl */ `
const PI: f32 = 3.14159265359;

fn hash21(p: vec2f) -> f32 {
  var p3 = fract(vec3f(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

fn hash22(p: vec2f) -> vec2f {
  var p3 = fract(vec3f(p.xyx) * vec3f(0.1031, 0.1030, 0.0973));
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.xx + p3.yz) * p3.zy);
}

// Analytical derivative 2D value/gradient noise: returns vec3f(value, dndx, dndy) in [-1, 1]
fn noised(x: vec2f) -> vec3f {
  let i = floor(x);
  let f = fract(x);
  let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  let du = 30.0 * f * f * (f * (f - 2.0) + 1.0);

  let a = hash21(i + vec2f(0.0, 0.0)) * 2.0 - 1.0;
  let b = hash21(i + vec2f(1.0, 0.0)) * 2.0 - 1.0;
  let c = hash21(i + vec2f(0.0, 1.0)) * 2.0 - 1.0;
  let d = hash21(i + vec2f(1.0, 1.0)) * 2.0 - 1.0;

  let k0 = a;
  let k1 = b - a;
  let k2 = c - a;
  let k3 = a - b - c + d;

  let val = k0 + k1 * u.x + k2 * u.y + k3 * u.x * u.y;
  let deriv = du * vec2f(k1 + k3 * u.y, k2 + k3 * u.x);
  return vec3f(val, deriv.x, deriv.y);
}

// Cellular Voronoi for 3D boulders, rock fracture blocks & talus cobbles
// Returns vec4f(minDist, edgeCrackDist, cellId, angleOfSiteDirection)
fn voronoiCell(p: vec2f) -> vec4f {
  let n = floor(p);
  let f = fract(p);
  var md1 = 8.0;
  var md2 = 8.0;
  var bestId = 0.0;
  var bestVec = vec2f(0.0);

  for (var j = -1; j <= 1; j++) {
    for (var i = -1; i <= 1; i++) {
      let g = vec2f(f32(i), f32(j));
      let o = hash22(n + g);
      let r = g + o - f;
      let d = dot(r, r);
      if (d < md1) {
        md2 = md1;
        md1 = d;
        bestId = o.x;
        bestVec = r;
      } else if (d < md2) {
        md2 = d;
      }
    }
  }
  let d1 = sqrt(md1);
  let d2 = sqrt(md2);
  return vec4f(d1, d2 - d1, bestId, atan2(bestVec.y, bestVec.x));
}
`;

//============================================================================================================================================
//  GEOMORPHOLOGICAL ANALYSIS & TEXTURE LAYER STACK SPLATTING COMPUTE SHADER
//============================================================================================================================================
export const ANALYZE_AND_SPLAT_WGSL = /* wgsl */ `
${WGSL_NOISE_LIB}

struct TexLayerGpu {
  c0: vec4f, // primaryRGB, roughness
  c1: vec4f, // secondaryRGB, specular
  c2: vec4f, // accentRGB, strataIntensity
  s0: vec4f,
  s1: vec4f,
  s2: vec4f,
  s3: vec4f,
  s4: vec4f,
  m0: vec4f, // x=enabled, y=blendMode, z=opacity, w=heightContrast
  m1: vec4f, // x=useAltMask, y=altMin, z=altMax, w=altFeather
  m2: vec4f, // x=useSlopeMask, y=slopeMin, z=slopeMax, w=slopeFeather
  m3: vec4f, // x=useCurvMask, y=curvMode, z=curvStrength, w=useFlowMask
  m4: vec4f, // x=flowStrength, y=useSedMask, z=sedWeight, w=talusWeight
  m5: vec4f, // x=useSnowMask, y=snowWeight, z=useNoiseMask, w=noiseScale
  m6: vec4f, // x=noiseAmount, y=boulderDetail, z=isSelected, w=useSatmap
};

struct SplatGlobal {
  g0: vec4f, // x=gridSize, y=domainSizeKm, z=numTexLayers, w=activeStackMode
  layers: array<TexLayerGpu, 12>,
};

@group(0) @binding(0) var<uniform> S: SplatGlobal;
@group(0) @binding(1) var<storage, read> Terrain: array<vec4f>;
@group(0) @binding(2) var<storage, read> Hydro: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> Geom: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> SplatColor: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> SplatMeta: array<vec4f>;

fn hAt(x: i32, z: i32, N: i32) -> f32 {
  return Terrain[clamp(z, 0, N - 1) * N + clamp(x, 0, N - 1)].x;
}

fn evalSatmap(L: TexLayerGpu, tIn: f32) -> vec3f {
  let t = clamp(tIn, 0.0, 1.0) * 4.0;
  let seg = i32(floor(t));
  let f = fract(t);
  if (seg <= 0) { return mix(L.s0.xyz, L.s1.xyz, f); }
  if (seg == 1) { return mix(L.s1.xyz, L.s2.xyz, f); }
  if (seg == 2) { return mix(L.s2.xyz, L.s3.xyz, f); }
  return mix(L.s3.xyz, L.s4.xyz, clamp(t - 3.0, 0.0, 1.0));
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) gid: vec3u) {
  let N = i32(S.g0.x);
  let x = i32(gid.x);
  let z = i32(gid.y);
  if (x >= N || z >= N) { return; }

  let idx = z * N + x;
  let ter = Terrain[idx];
  let hyd = Hydro[idx];
  let h = ter.x;
  let sed = ter.y;
  let talus = ter.z;
  let snow = ter.w;
  let flow = hyd.y;

  let cellM = (S.g0.y * 1000.0) / f32(N);
  let hL = hAt(x - 1, z, N);
  let hR = hAt(x + 1, z, N);
  let hD = hAt(x, z - 1, N);
  let hU = hAt(x, z + 1, N);
  let hL2 = hAt(x - 3, z, N);
  let hR2 = hAt(x + 3, z, N);
  let hD2 = hAt(x, z - 3, N);
  let hU2 = hAt(x, z + 3, N);

  let gx = (hR - hL) / (2.0 * cellM);
  let gz = (hU - hD) / (2.0 * cellM);
  let normal = normalize(vec3f(-gx, 1.0, -gz));
  let slopeDeg = acos(clamp(normal.y, 0.0, 1.0)) * 180.0 / PI;

  // Multi-scale curvature: positive on convex ridges/outcrops, negative in concave gullies/crevices
  let lap1 = (4.0 * h - (hL + hR + hD + hU)) / cellM;
  let lap2 = (4.0 * h - (hL2 + hR2 + hD2 + hU2)) / (3.0 * cellM);
  let curvature = clamp(lap1 * 0.72 + lap2 * 0.58, -1.8, 1.8);

  // Horizon Ambient Occlusion (8-tap multi-radius)
  var aoSum = 0.0;
  let dirs = array<vec2i, 8>(
    vec2i(1, 0), vec2i(-1, 0), vec2i(0, 1), vec2i(0, -1),
    vec2i(1, 1), vec2i(-1, 1), vec2i(1, -1), vec2i(-1, -1)
  );
  for (var d = 0; d < 8; d++) {
    let dir = dirs[d];
    let s1 = hAt(x + dir.x * 2, z + dir.y * 2, N) - h;
    let s2 = hAt(x + dir.x * 5, z + dir.y * 5, N) - h;
    let elevAngle = max(atan2(s1, cellM * 2.0), atan2(s2, cellM * 5.0));
    aoSum += clamp(1.0 - max(0.0, elevAngle) / (PI * 0.42), 0.12, 1.0);
  }
  let ao = aoSum / 8.0;

  Geom[idx] = vec4f(normal.x, normal.z, curvature, ao);

  // Evaluate Texture Layer Stack bottom-to-top
  let uv = vec2f(f32(x), f32(z)) / f32(N);
  let microN = noised(uv * 48.0 + vec2f(3.7, 9.2)).x;
  let foldN = noised(uv * 6.5 + vec2f(11.4, 5.1)).x * 38.0;
  // Multi-frequency geological bedding signal
  let bedPhase = (h + foldN) * 0.085;
  let strataWave = 0.5 + 0.35 * sin(bedPhase) + 0.15 * sin(bedPhase * 2.7 + microN);

  var outCol = vec3f(0.28, 0.30, 0.33);
  var outRough = 0.8;
  var outStrata = 0.45;
  var selectedMask = SplatMeta[idx].w;

  let count = clamp(i32(S.g0.z + 0.5), 0, 12);
  for (var i = 0; i < 12; i++) {
    if (i >= count) { break; }
    let L = S.layers[i];
    if (L.m0.x < 0.5) { continue; }

    var w = L.m0.z;

    if (L.m1.x > 0.5) {
      let feather = max(10.0, L.m1.w);
      let mAlt = smoothstep(L.m1.y - feather, L.m1.y + feather, h) *
                 (1.0 - smoothstep(L.m1.z - feather, L.m1.z + feather, h));
      w *= mAlt;
    }

    if (L.m2.x > 0.5) {
      let feather = max(1.0, L.m2.w);
      let mSlope = smoothstep(L.m2.y - feather, L.m2.y + feather, slopeDeg) *
                   (1.0 - smoothstep(L.m2.z - feather, L.m2.z + feather, slopeDeg));
      w *= mSlope;
    }

    if (L.m3.x > 0.5) {
      let cVal = select(-curvature, curvature, L.m3.y < 0.5);
      let mCurv = smoothstep(-0.12, max(0.05, 0.95 - L.m3.z * 0.72), cVal);
      w *= mCurv;
    }

    if (L.m3.w > 0.5) {
      let mFlow = smoothstep(0.03, max(0.06, 0.65 - L.m4.x * 0.5), flow);
      w *= mFlow;
    }

    if (L.m4.y > 0.5) {
      let sedScore = (sed / 14.0) * L.m4.z + (talus / 11.0) * L.m4.w;
      let mSed = clamp(smoothstep(0.02, 0.48, sedScore), 0.0, 1.0);
      w *= mSed;
    }

    if (L.m5.x > 0.5) {
      let mSnow = clamp(smoothstep(0.03, max(0.08, 0.78 - L.m5.y * 0.55), snow), 0.0, 1.0);
      w *= mSnow;
    }

    if (L.m5.z > 0.5) {
      let nVal = 0.5 + 0.5 * noised(uv * L.m5.w * 3.5 + vec2f(f32(i) * 7.3)).x;
      w *= mix(1.0, smoothstep(0.20, 0.80, nVal), L.m6.x);
    }

    // Height-Mask Rock Protrusion Blending
    let blendMode = i32(L.m0.y + 0.5);
    if (blendMode == 0 && i > 0) {
      // Underlying convex rock outcrops resist being covered by snow/sediment/moss
      let outcropResist = max(0.0, curvature * 0.42) + microN * 0.22;
      let contrast = clamp(L.m0.w, 0.1, 0.96);
      let edge = (1.0 - contrast) * 0.48;
      w = smoothstep(max(0.0, (1.0 - w) - edge), min(1.0, (1.0 - w) + edge + 0.04), w - outcropResist * 0.32);
    }

    w = clamp(w, 0.0, 1.0);

    let rampT = clamp(
      0.46 + curvature * 0.34 + microN * 0.18 + (strataWave - 0.5) * L.c2.w * 0.65,
      0.0,
      1.0
    );
    var layerCol = mix(L.c1.xyz, L.c0.xyz, rampT);
    if (L.m6.w > 0.5) {
      let satCol = evalSatmap(L, rampT);
      layerCol = mix(layerCol, satCol, 0.82);
    }
    layerCol = mix(layerCol, L.c2.xyz, clamp((strataWave - 0.52) * L.c2.w * 1.65, 0.0, 0.72));

    if (i == 0) {
      outCol = layerCol;
      outRough = L.c0.w;
      outStrata = L.c2.w;
    } else if (blendMode == 2) {
      outCol = mix(outCol, outCol * layerCol * 1.6, w);
      outRough = mix(outRough, L.c0.w, w);
    } else if (blendMode == 3) {
      let ovl = select(1.0 - 2.0 * (1.0 - outCol) * (1.0 - layerCol), 2.0 * outCol * layerCol, outCol < vec3f(0.5));
      outCol = mix(outCol, ovl, w);
      outRough = mix(outRough, L.c0.w, w);
    } else {
      outCol = mix(outCol, layerCol, w);
      outRough = mix(outRough, L.c0.w, w);
      outStrata = mix(outStrata, L.c2.w, w);
    }

    if (L.m6.z > 0.5) {
      selectedMask = w;
    }
  }

  SplatColor[idx] = vec4f(outCol, outRough);
  SplatMeta[idx] = vec4f(hyd.x, flow, outStrata, selectedMask);
}
`;

//============================================================================================================================================
//  AAA 3D VIEWPORT RENDER SHADERS (WITH CLIFF STRATIFICATION, BOULDERS, RUGGED OUTCROPS & DEEP EROSION CHANNELS)
//============================================================================================================================================
export const VIEWPORT_RENDER_WGSL = /* wgsl */ `
${WGSL_NOISE_LIB}

struct FrameUniforms {
  viewProj: mat4x4f,
  invViewProj: mat4x4f,
  camPos: vec4f,
  sunDir: vec4f,
  sunCol: vec4f,
  env0: vec4f, // x=gridSize, y=domainWorldSize, z=heightScale, w=waterLevelWorld
  env1: vec4f, // x=rayleigh, y=mieTurbidity, z=aerialStrength, w=fogDensity
  env2: vec4f, // x=fogHeightFalloff, y=fogValleyMist, z=exposure, w=contrast
  env3: vec4f, // xyz=fogColor, w=shadingMode
  water0: vec4f,
  water1: vec4f,
  flags: vec4f, // x=softShadows, y=cloudShadows, z=pedestalFloorWorld, w=vignette
  brush: vec4f,
};

@group(0) @binding(0) var<uniform> F: FrameUniforms;
@group(0) @binding(1) var<storage, read> TerrainBuf: array<vec4f>;
@group(0) @binding(2) var<storage, read> GeomBuf: array<vec4f>;
@group(0) @binding(3) var<storage, read> SplatBuf: array<vec4f>;
@group(0) @binding(4) var<storage, read> HydroMetaBuf: array<vec4f>;

fn sampleTerrainBilerp(uv: vec2f) -> vec4f {
  let N = F.env0.x;
  let p = clamp(uv, vec2f(0.0), vec2f(1.0)) * (N - 1.0);
  let i0 = vec2i(floor(p));
  let i1 = min(i0 + vec2i(1, 1), vec2i(i32(N) - 1));
  let f = fract(p);
  let Ni = i32(N);
  let v00 = TerrainBuf[i0.y * Ni + i0.x];
  let v10 = TerrainBuf[i0.y * Ni + i1.x];
  let v01 = TerrainBuf[i1.y * Ni + i0.x];
  let v11 = TerrainBuf[i1.y * Ni + i1.x];
  return mix(mix(v00, v10, f.x), mix(v01, v11, f.x), f.y);
}

fn sampleGeomBilerp(uv: vec2f) -> vec4f {
  let N = F.env0.x;
  let p = clamp(uv, vec2f(0.0), vec2f(1.0)) * (N - 1.0);
  let i0 = vec2i(floor(p));
  let i1 = min(i0 + vec2i(1, 1), vec2i(i32(N) - 1));
  let f = fract(p);
  let Ni = i32(N);
  let v00 = GeomBuf[i0.y * Ni + i0.x];
  let v10 = GeomBuf[i0.y * Ni + i1.x];
  let v01 = GeomBuf[i1.y * Ni + i0.x];
  let v11 = GeomBuf[i1.y * Ni + i1.x];
  return mix(mix(v00, v10, f.x), mix(v01, v11, f.x), f.y);
}

fn sampleSplatBilerp(uv: vec2f) -> vec4f {
  let N = F.env0.x;
  let p = clamp(uv, vec2f(0.0), vec2f(1.0)) * (N - 1.0);
  let i0 = vec2i(floor(p));
  let i1 = min(i0 + vec2i(1, 1), vec2i(i32(N) - 1));
  let f = fract(p);
  let Ni = i32(N);
  let v00 = SplatBuf[i0.y * Ni + i0.x];
  let v10 = SplatBuf[i0.y * Ni + i1.x];
  let v01 = SplatBuf[i1.y * Ni + i0.x];
  let v11 = SplatBuf[i1.y * Ni + i1.x];
  return mix(mix(v00, v10, f.x), mix(v01, v11, f.x), f.y);
}

fn sampleHydroBilerp(uv: vec2f) -> vec4f {
  let N = F.env0.x;
  let p = clamp(uv, vec2f(0.0), vec2f(1.0)) * (N - 1.0);
  let i0 = vec2i(floor(p));
  let i1 = min(i0 + vec2i(1, 1), vec2i(i32(N) - 1));
  let f = fract(p);
  let Ni = i32(N);
  let v00 = HydroMetaBuf[i0.y * Ni + i0.x];
  let v10 = HydroMetaBuf[i0.y * Ni + i1.x];
  let v01 = HydroMetaBuf[i1.y * Ni + i0.x];
  let v11 = HydroMetaBuf[i1.y * Ni + i1.x];
  return mix(mix(v00, v10, f.x), mix(v01, v11, f.x), f.y);
}

fn computeSkyColor(rayDir: vec3f) -> vec3f {
  let sunDir = normalize(F.sunDir.xyz);
  let mu = dot(rayDir, sunDir);
  let sunElev = clamp(sunDir.y, -0.1, 1.0);
  let zenith = clamp(rayDir.y, 0.0, 1.0);

  let rayleighStrength = F.env1.x;
  let mieTurb = F.env1.y;

  let dayFactor = smoothstep(-0.04, 0.28, sunElev);
  let zenithCol = mix(vec3f(0.05, 0.09, 0.20), vec3f(0.10, 0.23, 0.52) * rayleighStrength, dayFactor);
  let horizonCol = mix(
    vec3f(0.68, 0.34, 0.18),
    mix(vec3f(0.54, 0.67, 0.82), F.env3.xyz, 0.35),
    smoothstep(0.0, 0.38, sunElev)
  );

  let hCurve = pow(1.0 - zenith, 2.6);
  var sky = mix(zenithCol, horizonCol, hCurve);

  let g = 0.78;
  let hg = (1.0 - g * g) / (4.0 * PI * pow(max(0.02, 1.0 + g * g - 2.0 * g * mu), 1.5));
  let aureole = F.sunCol.xyz * hg * 0.045 * mieTurb * (0.4 + 0.6 * hCurve);
  sky += aureole;

  let sunDisc = smoothstep(0.9994, 0.99985, mu) * step(0.0, rayDir.y);
  sky += F.sunCol.xyz * sunDisc * 4.5;

  if (rayDir.y < 0.0) {
    let groundBounce = vec3f(0.07, 0.08, 0.10) * (0.3 + 0.7 * dayFactor);
    sky = mix(groundBounce, horizonCol * 0.75, smoothstep(-0.35, 0.0, rayDir.y));
  }
  return sky;
}

fn acesTonemap(x: vec3f) -> vec3f {
  let a = 2.51;
  let b = 0.03;
  let c = 2.43;
  let d = 0.59;
  let e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3f(0.0), vec3f(1.0));
}

fn applyPost(linearCol: vec3f, uvScreen: vec2f) -> vec4f {
  var c = linearCol * exp2(F.env2.z);
  c = acesTonemap(c);
  c = pow(max(vec3f(0.0), c), vec3f(1.0 / 2.2));
  c = clamp((c - 0.5) * F.env2.w + 0.5, vec3f(0.0), vec3f(1.0));
  let d = length(uvScreen - vec2f(0.5));
  c *= 1.0 - smoothstep(0.35, 0.85, d) * F.flags.w;
  return vec4f(c, 1.0);
}

//--------------------------------------------------------------------------------------------------------------------------------------------
// SKY FULLSCREEN PASS
//--------------------------------------------------------------------------------------------------------------------------------------------
struct SkyOut {
  @builtin(position) pos: vec4f,
  @location(0) ndc: vec2f,
};

@vertex
fn skyVert(@builtin(vertex_index) vid: u32) -> SkyOut {
  var pos = array<vec2f, 3>(vec2f(-1.0, -1.0), vec2f(3.0, -1.0), vec2f(-1.0, 3.0));
  var out: SkyOut;
  out.pos = vec4f(pos[vid], 0.9999, 1.0);
  out.ndc = pos[vid];
  return out;
}

@fragment
fn skyFrag(in: SkyOut) -> @location(0) vec4f {
  let farClip = F.invViewProj * vec4f(in.ndc, 1.0, 1.0);
  let worldPos = farClip.xyz / farClip.w;
  let rayDir = normalize(worldPos - F.camPos.xyz);
  let sky = computeSkyColor(rayDir);
  let uvScreen = in.ndc * 0.5 + 0.5;
  return applyPost(sky, uvScreen);
}

//--------------------------------------------------------------------------------------------------------------------------------------------
// TERRAIN MESH & GEOLOGICAL PEDESTAL PASS
//--------------------------------------------------------------------------------------------------------------------------------------------
struct TerrainVSIn {
  @location(0) uvAndSkirt: vec4f,
};

struct TerrainVSOut {
  @builtin(position) clipPos: vec4f,
  @location(0) worldPos: vec3f,
  @location(1) uv: vec2f,
  @location(2) skirtInfo: vec2f,
};

@vertex
fn terrainVert(in: TerrainVSIn) -> TerrainVSOut {
  let uv = clamp(in.uvAndSkirt.xy, vec2f(0.0), vec2f(1.0));
  let ter = sampleTerrainBilerp(uv);
  let domainSize = F.env0.y;
  let hScale = F.env0.z;

  let worldXZ = (uv - 0.5) * domainSize;
  var worldY = ter.x * hScale;
  if (in.uvAndSkirt.z > 0.5) {
    worldY = F.flags.z;
  }

  let worldPos = vec3f(worldXZ.x, worldY, worldXZ.y);
  var out: TerrainVSOut;
  out.clipPos = F.viewProj * vec4f(worldPos, 1.0);
  out.worldPos = worldPos;
  out.uv = uv;
  out.skirtInfo = in.uvAndSkirt.zw;
  return out;
}

// 28-Step Heightfield Raymarched Soft Shadows
fn raymarchHeightShadow(uvStart: vec2f, hStartM: f32, sunDir: vec3f) -> f32 {
  if (F.flags.x < 0.5) { return 1.0; }
  let domainMeters = F.env0.y / F.env0.z;
  let horizSun = max(0.04, length(vec2f(sunDir.x, sunDir.z)));
  let stepUV = (vec2f(sunDir.x, sunDir.z) / horizSun) / F.env0.x * 1.65;
  let horizDistPerStep = length(stepUV) * domainMeters;
  let dzPerStep = (sunDir.y / horizSun) * horizDistPerStep;

  var shadow = 1.0;
  var t = 0.85;
  for (var i = 0; i < 28; i++) {
    let sampleUV = uvStart + stepUV * t;
    if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) {
      break;
    }
    let rayH = hStartM + dzPerStep * t;
    let terH = sampleTerrainBilerp(sampleUV).x;
    let diff = terH - rayH;
    if (diff > -2.5) {
      let penumbra = clamp(1.0 - (diff + 2.5) / (6.0 + t * 2.0), 0.0, 1.0);
      shadow = min(shadow, penumbra);
      if (shadow < 0.03) { break; }
    }
    t += 0.9 + f32(i) * 0.22;
  }
  return mix(0.11, 1.0, shadow);
}

// Multi-Scale Procedural Geological Micro-Structure Synthesizer:
// Computes per-pixel Stratification Ledges, Rugged Voronoi Outcrop Cracks, and 3D Boulder/Talus Scree!
struct GeoMicroDetail {
  normal: vec3f,
  albedoMod: f32,
  cavityShadow: f32,
  roughnessMod: f32,
  strataBand: f32,
};

// All detail is authored in METRES: 1 world unit = 500 m horizontally, height = worldY / hScale metres.
fn synthesizeGeologicalDetail(
  worldPos: vec3f,
  baseNormal: vec3f,
  slopeDeg: f32,
  curvature: f32,
  talusM: f32,
  sedM: f32,
  flow: f32,
  snowM: f32,
  strataIntensity: f32
) -> GeoMicroDetail {
  var res: GeoMicroDetail;
  let K = F.env0.z * 500.0;                 // world slope per metre-slope (VE included)
  let M = worldPos.xz * 500.0;              // horizontal metres
  let Y = worldPos.y / max(F.env0.z, 1e-6); // height metres
  let bare = 1.0 - clamp(snowM * 1.25, 0.0, 1.0);
  let cliffMask = smoothstep(26.0, 44.0, slopeDeg) * bare;
  let outcropMask = clamp(smoothstep(0.10, 0.60, curvature) + cliffMask * 0.65, 0.0, 1.0) * bare;
  let boulderZone = clamp(talusM / 6.0 + sedM / 16.0, 0.0, 1.0) *
                    smoothstep(4.0, 12.0, slopeDeg) * (1.0 - smoothstep(40.0, 54.0, slopeDeg)) * bare;

  // 1. Sedimentary bedding: resistant ledges (24 m beds) with recessed shale seams and laminae
  let fold = noised(M / 420.0 + vec2f(4.2, 8.7)).x;
  let bedCoord = Y / 24.0 + fold * 1.7 + M.x / 1100.0;
  let ph = fract(bedCoord);
  let ledgeTop = smoothstep(0.26, 0.34, ph) * (1.0 - smoothstep(0.66, 0.74, ph));
  let recess = smoothstep(0.74, 0.84, ph) * (1.0 - smoothstep(0.96, 1.0, ph));
  let laminae = noised(vec2f(bedCoord * 9.0, M.x / 22.0 + M.y / 22.0)).x;
  let strataSignal = (ledgeTop - recess * 0.8) * strataIntensity + laminae * 0.18 * strataIntensity;

  // 2. Triplanar joint network (34 m blocks) with sharp fissures and facet tilt
  let wTri = pow(abs(baseNormal), vec3f(4.0));
  let wts = wTri / max(0.001, wTri.x + wTri.y + wTri.z);
  let vXZ = voronoiCell(M / 34.0);
  let vZY = voronoiCell(vec2f(M.y, Y) / 34.0);
  let vXY = voronoiCell(vec2f(M.x, Y) / 34.0);
  let crackDist = vXZ.y * wts.y + vZY.y * wts.x + vXY.y * wts.z;
  let blockId = vXZ.z * wts.y + vZY.z * wts.x + vXY.z * wts.z;
  let fissure = (1.0 - smoothstep(0.0, 0.055, crackDist)) * outcropMask;
  let facetA = (vXZ.z - 0.5) * 0.30;
  let facetB = (vXY.z - 0.5) * 0.30;

  // 3. Boulders as true round domes: medium (9 m cells, ~8 m across) and large (26 m cells, ~24 m across)
  let bM = voronoiCell(M / 9.0 + vec2f(29.4, 19.8));
  let hasM = step(0.52, bM.z);
  let qM = clamp(bM.x / 0.45, 0.0, 0.999);
  let domeM = sqrt(max(0.0, 1.0 - qM * qM)) * hasM;
  let slopeM = (2.4 / 4.05) * qM / max(0.08, sqrt(1.0 - qM * qM)) * hasM;

  let bL = voronoiCell(M / 26.0 + vec2f(13.1, 7.4));
  let hasL = step(0.70, bL.z);
  let qL = clamp(bL.x / 0.46, 0.0, 0.999);
  let domeL = sqrt(max(0.0, 1.0 - qL * qL)) * hasL;
  let slopeL = (4.5 / 12.0) * qL / max(0.08, sqrt(1.0 - qL * qL)) * hasL;

  let bzM = boulderZone;
  let gxB = -(cos(bM.w) * slopeM * bzM + cos(bL.w) * slopeL * bzM) * K;
  let gzB = -(sin(bM.w) * slopeM * bzM + sin(bL.w) * slopeL * bzM) * K;
  let boulderCrevice = (smoothstep(0.78, 0.98, qM) * hasM + smoothstep(0.78, 0.98, qL) * hasL * 0.8) * bzM;
  let boulderTone = mix(0.80, 1.22, bL.z * 0.5 + bM.z * 0.5) * max(hasM, hasL) * bzM + (1.0 - max(hasM, hasL)) * 0.0;

  // 4. Gully micro-chutes & rill striations (dendritic channel floor texture)
  let rill = noised(M / 6.5 + vec2f(3.0, 1.0)).x;
  let gullyCut = clamp(flow * 1.25, 0.0, 1.0);

  // 5. Ledge-top tilt & cliff face breakup (outcrop normals)
  let cliffBreak = noised(M / 13.0 + vec2f(7.0, 2.0)).x;
  let dY = (ledgeTop * 0.28 - recess * 0.10) * cliffMask + outcropMask * 0.06 * cliffBreak;
  let dX = (facetA * outcropMask) + gxB + (laminae * 0.02) * (0.25 + gullyCut);
  let dZ = (facetB * outcropMask) + gzB + (cliffBreak * 0.07) * outcropMask + (rill * 0.035) * gullyCut;

  res.normal = normalize(vec3f(baseNormal.x + dX, baseNormal.y + dY, baseNormal.z + dZ));

  // Albedo: bedding bands, fissure staining, boulder mineral variation, wet channel darkening
  res.albedoMod = 1.0 + strataSignal * 0.30 * (0.45 + 0.55 * cliffMask)
                 + (blockId - 0.5) * 0.12 * outcropMask
                 + (boulderTone - 1.0) * 0.8
                 + (rill - 0.5) * 0.08
                 - fissure * 0.42
                 - gullyCut * 0.22;

  // Cavity: fissures, boulder bases, bedding overhangs (recess undercut), gully floors
  let overhang = clamp(recess * cliffMask * 0.6, 0.0, 0.6);
  res.cavityShadow = clamp(1.0 - fissure * 0.55 - boulderCrevice * 0.55 - overhang - gullyCut * 0.25, 0.14, 1.0);
  res.roughnessMod = -gullyCut * 0.34 + bzM * 0.06 + fissure * 0.05;
  res.strataBand = strataSignal;
  return res;
}

@fragment
fn terrainFrag(in: TerrainVSOut) -> @location(0) vec4f {
  let uv = clamp(in.uv, vec2f(0.0), vec2f(1.0));
  let ter = sampleTerrainBilerp(uv);
  let geom = sampleGeomBilerp(uv);
  let splat = sampleSplatBilerp(uv);
  let hydro = sampleHydroBilerp(uv);

  let hM = ter.x;
  let sedM = ter.y;
  let talusM = ter.z;
  let snowM = ter.w;
  let flow = hydro.y;
  let strataInt = hydro.z;
  let selMask = hydro.w;

  // High-precision sub-texel normal reconstruction in fragment shader
  let eps = 1.0 / F.env0.x;
  let hL = sampleTerrainBilerp(uv - vec2f(eps, 0.0)).x;
  let hR = sampleTerrainBilerp(uv + vec2f(eps, 0.0)).x;
  let hD = sampleTerrainBilerp(uv - vec2f(0.0, eps)).x;
  let hU = sampleTerrainBilerp(uv + vec2f(0.0, eps)).x;
  // Central differences in WORLD units: height (m * hScale) over horizontal span (units)
  let dxU = 2.0 * eps * F.env0.y;
  var normal = normalize(vec3f(-(hR - hL) * F.env0.z / dxU, 1.0, -(hU - hD) * F.env0.z / dxU));

  let curvature = geom.z;
  let ao = geom.w;
  let slopeDeg = acos(clamp(normal.y, 0.0, 1.0)) * 180.0 / PI;

  let sunDir = normalize(F.sunDir.xyz);
  let viewVec = F.camPos.xyz - in.worldPos;
  let distCam = length(viewVec);
  let viewDir = viewVec / max(0.001, distCam);

  // Geological Pedestal Cross-Section Wall (with visible sedimentary rock strata & fault lines!)
  if (in.skirtInfo.x > 0.45) {
    let side = i32(in.skirtInfo.y + 0.5);
    var wallN = vec3f(0.0, 0.0, 1.0);
    if (side == 0) { wallN = vec3f(0.0, 0.0, -1.0); }
    else if (side == 1) { wallN = vec3f(1.0, 0.0, 0.0); }
    else if (side == 2) { wallN = vec3f(0.0, 0.0, 1.0); }
    else { wallN = vec3f(-1.0, 0.0, 0.0); }

    let fold = noised(in.worldPos.xz * 1.1).x * 0.12;
    let yBand = (in.worldPos.y + fold) * 14.0;
    let band = 0.5 + 0.35 * sin(yBand) + 0.15 * sin(yBand * 3.1);
    let topEdge = smoothstep(ter.x * F.env0.z - 0.07, ter.x * F.env0.z, in.worldPos.y);
    let baseRock = mix(vec3f(0.09, 0.085, 0.08), vec3f(0.24, 0.21, 0.18), band);
    let wallAlbedo = mix(baseRock, splat.xyz * 0.78, topEdge);
    let ndl = max(0.0, dot(wallN, sunDir));
    let wallLit = wallAlbedo * (vec3f(0.14, 0.16, 0.20) + F.sunCol.xyz * ndl * 0.68);
    return applyPost(wallLit, vec2f(0.5));
  }

  // Synthesize Multi-Scale Cliffs, Outcrops, Boulders, Stratification & Erosion Gully Detail
  let micro = synthesizeGeologicalDetail(
    in.worldPos,
    normal,
    slopeDeg,
    curvature,
    talusM,
    sedM,
    flow,
    snowM,
    max(0.35, strataInt)
  );
  normal = micro.normal;

  let mode = i32(F.env3.w + 0.5);

  // Diagnostic & Data Map Shading Modes
  if (mode >= 2) {
    var mapCol = splat.xyz;
    if (mode == 2) {
      let t = clamp(hM / 2400.0, 0.0, 1.0);
      let c0 = vec3f(0.10, 0.26, 0.22);
      let c1 = vec3f(0.28, 0.52, 0.28);
      let c2 = vec3f(0.72, 0.62, 0.36);
      let c3 = vec3f(0.58, 0.38, 0.26);
      let c4 = vec3f(0.95, 0.97, 1.00);
      if (t < 0.25) { mapCol = mix(c0, c1, t * 4.0); }
      else if (t < 0.5) { mapCol = mix(c1, c2, (t - 0.25) * 4.0); }
      else if (t < 0.75) { mapCol = mix(c2, c3, (t - 0.5) * 4.0); }
      else { mapCol = mix(c3, c4, (t - 0.75) * 4.0); }
      let contour = abs(fract(hM / 100.0) - 0.5);
      mapCol *= mix(0.62, 1.0, smoothstep(0.0, 0.04, contour));
    } else if (mode == 3) {
      let s = clamp(slopeDeg / 75.0, 0.0, 1.0);
      mapCol = mix(
        mix(vec3f(0.14, 0.55, 0.32), vec3f(0.95, 0.68, 0.22), clamp(s * 2.0, 0.0, 1.0)),
        vec3f(0.88, 0.18, 0.20),
        clamp((s - 0.5) * 2.0, 0.0, 1.0)
      );
    } else if (mode == 4) {
      let f = clamp(pow(flow * 0.75, 0.58), 0.0, 1.0);
      mapCol = mix(vec3f(0.07, 0.09, 0.13), vec3f(0.16, 0.78, 1.0), f);
      mapCol += vec3f(0.55, 0.95, 1.0) * smoothstep(0.45, 1.0, f);
    } else if (mode == 5) {
      let s = clamp(sedM / 15.0, 0.0, 1.0);
      let t = clamp(talusM / 12.0, 0.0, 1.0);
      mapCol = vec3f(0.09, 0.09, 0.11) + vec3f(0.94, 0.64, 0.20) * s + vec3f(0.24, 0.80, 0.72) * t;
    } else if (mode == 6) {
      let sn = clamp(snowM, 0.0, 1.0);
      mapCol = mix(vec3f(0.12, 0.14, 0.18), vec3f(0.90, 0.96, 1.0), sn);
    } else if (mode == 7) {
      let c = clamp(curvature * 0.75, -1.0, 1.0);
      mapCol = select(
        mix(vec3f(0.20, 0.20, 0.22), vec3f(0.12, 0.72, 0.84), -c),
        mix(vec3f(0.20, 0.20, 0.22), vec3f(0.98, 0.66, 0.22), c),
        c >= 0.0
      );
    } else if (mode == 8) {
      let m = clamp(selMask, 0.0, 1.0);
      mapCol = mix(vec3f(0.08, 0.08, 0.10), vec3f(1.0, 0.70, 0.28), m);
    } else {
      mapCol = splat.xyz * micro.albedoMod;
    }

    let hillshade = (0.48 + 0.52 * max(0.0, dot(normal, sunDir))) * ao * micro.cavityShadow;
    return applyPost(mapCol * hillshade, vec2f(0.5));
  }

  var albedo = clamp(splat.xyz * micro.albedoMod, vec3f(0.01), vec3f(0.98));
  var roughness = clamp(splat.w + micro.roughnessMod, 0.06, 0.98);
  if (mode == 1) {
    // Gaea Studio Clay Relief Mode (shows every erosion gully, boulder, outcrop & stratum with extreme clarity!)
    albedo = vec3f(0.64, 0.61, 0.58) * (0.88 + 0.24 * micro.albedoMod);
    roughness = 0.68;
  }

  // Heightfield Raymarched Soft Shadow + Volumetric Cloud Shadow + Micro-Cavity Crevice Shadow
  let sunShadow = raymarchHeightShadow(uv, hM, sunDir);
  var cloudShadow = 1.0;
  if (F.flags.y > 0.5) {
    let cloudUV = uv * 2.5 + vec2f(F.camPos.w * 0.012, F.camPos.w * 0.007);
    let cn = 0.5 + 0.5 * noised(cloudUV).x + 0.25 * noised(cloudUV * 2.3).x;
    cloudShadow = mix(1.0, 0.45, smoothstep(0.54, 0.82, cn));
  }
  let shadow = sunShadow * cloudShadow;

  // PBR Direct + Sky Hemisphere + Warm Valley Bounce GI Lighting
  let ndl = max(0.0, dot(normal, sunDir));
  let halfVec = normalize(sunDir + viewDir);
  let ndh = max(0.0, dot(normal, halfVec));

  let alpha = roughness * roughness;
  let alpha2 = alpha * alpha;
  let denom = ndh * ndh * (alpha2 - 1.0) + 1.0;
  let D = alpha2 / max(0.0001, PI * denom * denom);
  let fresnel = 0.04 + 0.96 * pow(1.0 - max(0.0, dot(halfVec, viewDir)), 5.0);
  let spec = D * fresnel * (1.0 - roughness * 0.72) * 0.28;

  let sunIntensity = (F.sunDir.w / 100.0) * 1.65;
  let directLight = F.sunCol.xyz * sunIntensity * ndl * shadow * micro.cavityShadow;

  let skyZenith = vec3f(0.21, 0.35, 0.60) * F.env1.x;
  let groundBounce = albedo * vec3f(0.30, 0.25, 0.20) * (0.4 + 0.6 * max(0.0, sunDir.y));
  let ambientIrr = mix(groundBounce, skyZenith, 0.5 + 0.5 * normal.y) * ao * micro.cavityShadow * 0.54;

  var color = albedo * (directLight + ambientIrr) + F.sunCol.xyz * spec * shadow * ndl;

  // Interactive 3D Sculpt Brush Ring Gizmo
  if (F.brush.w > 0.5) {
    let dBrush = length(in.worldPos.xz - F.brush.xy);
    let rB = F.brush.z;
    let ring = smoothstep(rB * 0.92, rB * 0.98, dBrush) * (1.0 - smoothstep(rB * 0.98, rB * 1.05, dBrush));
    let fill = (1.0 - smoothstep(0.0, rB, dBrush)) * 0.14;
    color = mix(color, vec3f(1.0, 0.71, 0.33), clamp(ring * 0.9 + fill, 0.0, 1.0));
  }

  // Exponential Height Fog + Valley Mist + Aerial Perspective
  let heightAboveWater = max(0.0, in.worldPos.y - F.env0.w);
  let valleyFactor = exp(-heightAboveWater * F.env2.x * 2.2) * (1.0 + F.env2.y * 1.35);
  let fogOpticalDepth = distCam * F.env1.w * 0.052 * (0.32 + 0.68 * valleyFactor);
  let fogAmount = clamp(1.0 - exp(-fogOpticalDepth), 0.0, 0.88);

  let skyRay = computeSkyColor(-viewDir);
  let fogCol = mix(F.env3.xyz * (0.45 + 0.55 * max(0.1, sunDir.y)), skyRay, F.env1.z * 0.68);
  color = mix(color, fogCol, fogAmount);

  return applyPost(color, vec2f(0.5));
}

//--------------------------------------------------------------------------------------------------------------------------------------------
// WATER TABLE / OCEAN / ALPINE LAKE RENDER PASS
//--------------------------------------------------------------------------------------------------------------------------------------------
struct WaterVSOut {
  @builtin(position) clipPos: vec4f,
  @location(0) worldPos: vec3f,
  @location(1) uv: vec2f,
};

@vertex
fn waterVert(@builtin(vertex_index) vid: u32) -> WaterVSOut {
  var quad = array<vec2f, 6>(
    vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),
    vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0)
  );
  let uv = quad[vid];
  let worldXZ = (uv - 0.5) * F.env0.y;
  let worldPos = vec3f(worldXZ.x, F.env0.w, worldXZ.y);
  var out: WaterVSOut;
  out.clipPos = F.viewProj * vec4f(worldPos, 1.0);
  out.worldPos = worldPos;
  out.uv = uv;
  return out;
}

@fragment
fn waterFrag(in: WaterVSOut) -> @location(0) vec4f {
  if (F.water0.w < 0.5) { discard; }
  let ter = sampleTerrainBilerp(in.uv);
  let bedWorldY = ter.x * F.env0.z;
  let depthWorld = F.env0.w - bedWorldY;
  if (depthWorld <= 0.0) { discard; }

  let depthMeters = depthWorld / F.env0.z;
  let t = F.camPos.w;
  let waveStr = F.water1.w;

  let p = in.worldPos.xz * 7.5;
  let w1 = noised(p + vec2f(t * 0.55, t * 0.32));
  let w2 = noised(p * 2.3 - vec2f(t * 0.42, -t * 0.48));
  let nWater = normalize(vec3f(
    (w1.y + w2.y * 0.5) * 0.08 * waveStr,
    1.0,
    (w1.z + w2.z * 0.5) * 0.08 * waveStr
  ));

  let viewVec = F.camPos.xyz - in.worldPos;
  let distCam = length(viewVec);
  let viewDir = viewVec / max(0.001, distCam);
  let sunDir = normalize(F.sunDir.xyz);

  let absorb = 1.0 - exp(-depthMeters * 0.022);
  let bedCol = sampleSplatBilerp(in.uv).xyz * 0.55;
  let waterBody = mix(
    mix(bedCol, F.water0.xyz, clamp(depthMeters * 0.045, 0.15, 1.0)),
    F.water1.xyz,
    absorb
  );

  let reflDir = reflect(-viewDir, nWater);
  let skyRefl = computeSkyColor(vec3f(reflDir.x, abs(reflDir.y), reflDir.z));
  let fresnel = 0.03 + 0.97 * pow(1.0 - max(0.0, dot(nWater, viewDir)), 4.5);

  let halfVec = normalize(sunDir + viewDir);
  let sunGlint = pow(max(0.0, dot(nWater, halfVec)), 180.0) * 2.6;

  let foamNoise = 0.5 + 0.5 * noised(in.worldPos.xz * 18.0 + vec2f(t * 0.4)).x;
  let shoreFoam = (1.0 - smoothstep(0.0, 14.0, depthMeters)) * smoothstep(0.32, 0.72, foamNoise) * 0.65;

  var color = mix(waterBody, skyRefl, fresnel * 0.78) + F.sunCol.xyz * sunGlint + vec3f(0.92, 0.96, 1.0) * shoreFoam;

  let fogOpticalDepth = distCam * F.env1.w * 0.052;
  let fogAmount = clamp(1.0 - exp(-fogOpticalDepth), 0.0, 0.88);
  let fogCol = mix(F.env3.xyz * 0.75, computeSkyColor(-viewDir), F.env1.z * 0.68);
  color = mix(color, fogCol, fogAmount);

  return applyPost(color, vec2f(0.5));
}
`;
