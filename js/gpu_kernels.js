//============================================================================================================================================
//  GPU_KERNELS.JS — full-resolution WebGPU compute kernels (texture based, up to 4096 x 4096)
//    fill        : zero-initialise ping-pong fields
//    layer       : every landform / modifier layer (mountain, alpine horn, canyon, volcano, fault, strata, crags, terrace, dunes)
//    fluxMain    : pipe-model hydraulic flux (Mei et al. 2007) with rain, evaporation and flow routing
//    erodeMain   : water update, velocity, stream-power erosion/deposition + semi-Lagrangian sediment advection
//    thermalMain : antisymmetric talus slumping at the angle of repose (mass conserving)
//    fields      : flow accumulation proxy, sediment, talus and physically-driven snowpack
//    splat       : geomorphology analysis + SatMap / PBR texture stack (reuses the shaders.js splat tail)
//    render      : viewport shader with texture-backed sampling (reuses the shaders.js viewport shading)
//============================================================================================================================================
import { WGSL_NOISE_LIB, ANALYZE_AND_SPLAT_WGSL, VIEWPORT_RENDER_WGSL } from "./shaders.js";

export const HEAD = WGSL_NOISE_LIB;

export const FILL_WGSL = /* wgsl */ `
@group(0) @binding(0) var o32: texture_storage_2d<r32float, write>;
@group(0) @binding(1) var o16: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(8, 8)
fn fill32(@builtin(global_invocation_id) gid: vec3u) {
  textureStore(o32, vec2i(i32(gid.x), i32(gid.y)), vec4f(0.0));
}
@compute @workgroup_size(8, 8)
fn fill16(@builtin(global_invocation_id) gid: vec3u) {
  textureStore(o16, vec2i(i32(gid.x), i32(gid.y)), vec4f(0.0));
}
`;

export const LAYER_WGSL = /* wgsl */ `
struct Glob { n: vec4f, };  // x=N, y=cellM, z=unused, w=domainKm
@group(0) @binding(0) var<uniform> G: Glob;
@group(0) @binding(1) var<uniform> Lp: array<vec4f, 8>;
@group(0) @binding(2) var prevH: texture_2d<f32>;
@group(0) @binding(3) var outH: texture_storage_2d<r32float, write>;

fn prm(i: i32) -> f32 { return Lp[i >> 2][i & 3]; }

fn ld(p: vec2i) -> f32 {
  let N = i32(G.n.x);
  return textureLoad(prevH, clamp(p, vec2i(0), vec2i(N - 1)), 0).x;
}

fn ridged(p0: vec2f, oct: i32, lac: f32, gain: f32, sharp: f32, damp: f32) -> f32 {
  var sum = 0.0;
  var norm = 0.0;
  var amp = 0.5;
  var f = 1.0;
  var grad = vec2f(0.0);
  for (var i = 0; i < 12; i++) {
    if (i >= oct) { break; }
    let n = noised(p0 * f);
    grad += n.yz * f;
    let r = clamp(1.0 - abs(n.x), 0.0, 1.0);
    sum += amp * pow(r, sharp) / (1.0 + damp * dot(grad, grad));
    norm += amp;
    amp *= gain;
    f *= lac;
  }
  return sum / max(norm, 1e-4);
}

fn warp2(p: vec2f, amt: f32, seed: f32) -> vec2f {
  let w = vec2f(
    noised(p * 0.7 + vec2f(seed * 0.73, seed * 0.19)).x,
    noised(p * 0.7 + vec2f(seed * 0.41 + 11.3, 3.7)).x
  );
  return p + w * amt;
}

fn layerH(t: i32, vx: f32, vz: f32, prev: f32) -> f32 {
  let elev = prm(4);
  let scale = prm(5);
  let oct = i32(prm(6));
  let lac = prm(7);
  let gain = prm(8);
  let sharp = prm(9);
  let warp = prm(10);
  let seed = prm(11);
  let strike = prm(12);
  let aniso = prm(13);
  let vfl = prm(14);
  let cliff = prm(15);
  let rKm = prm(16);
  let p1 = prm(17);
  let p2 = prm(18);
  let p3 = prm(19);
  let damp = prm(27);
  let p4 = prm(28);
  let p5 = prm(29);
  let radN = max(rKm / max(0.5 * G.n.w, 1e-3), 1e-3);
  let sd = vec2f(seed * 0.731, seed * 0.519);
  let pw = warp2(vec2f(vx, vz), warp * 0.55, seed);
  let ca = cos(strike);
  let sa = sin(strike);

  if (t == 0) {
    // Tectonic mountain range: anisotropic ridged multifractal + spine envelope
    let rx = ca * pw.x - sa * pw.y;
    let ry = sa * pw.x + ca * pw.y;
    let sx = rx * (1.0 - aniso * 0.55) * scale;
    let sy = ry * (1.0 + aniso * 0.45) * scale;
    let f = ridged(vec2f(sx, sy) + sd, oct, lac, gain, sharp, damp);
    let spine = noised(vec2f(rx * 1.15 + seed * 0.7, ry * 1.15 + seed * 0.7)).x * 0.32;
    let ds = abs(ry + spine);
    let env = exp(-ds * ds * (0.65 + (1.0 - vfl) * 1.1));
    let shaped = pow(max(f, 0.0), 1.0 + vfl * 0.85);
    return elev * shaped * (0.42 + 0.76 * env);
  }
  if (t == 1) {
    // Alpine glacial horn: pyramidal profile, arete ridges, cirque bowls
    let r = length(pw) / radN;
    let f = ridged(pw * scale + sd, oct, lac, gain, sharp, damp);
    let base = pow(clamp(1.0 - r, 0.0, 1.0), 1.4 + cliff * 0.9);
    let ang = atan2(pw.y, pw.x);
    let cirq = p2 * exp(-pow((r - 0.5) / 0.12, 2.0)) * pow(0.5 + 0.5 * cos(ang * p1 + seed), 4.0);
    let h = elev * base * (0.55 + 0.7 * f);
    return h * (1.0 - 0.35 * clamp(cirq, 0.0, 1.0));
  }
  if (t == 2) {
    // Canyon & mesa plateau: stepped caprock benches + meandering gorge
    let plateau = elev * (0.6 + 0.4 * ridged(pw * scale + sd, oct, lac, gain, 1.0, 0.0));
    let nrm = clamp(plateau / max(elev, 1.0), 0.0, 1.0);
    let sc = nrm * p4;
    let hw = 0.5 * (1.0 - p5 * 0.85);
    let stepped = (floor(sc) + smoothstep(0.5 - hw, 0.5 + hw, fract(sc))) / max(p4, 1.0) * elev;
    let zc = 0.35 * sin(pw.x * p3 * 3.14159 + seed);
    let d = abs(pw.y - zc);
    let gorge = smoothstep(p2 * 0.5, 0.0, d);
    return mix(stepped, stepped * 0.25, gorge * p1);
  }
  if (t == 3) {
    // Volcanic cone with caldera and radial lahar gullies
    let r = length(pw) / radN;
    let cone = pow(clamp(1.0 - r, 0.0, 1.0), 1.7);
    let inside = smoothstep(p1 * 1.05, p1 * 0.6, r);
    var h = elev * cone * (1.0 - p2 * inside);
    let a = atan2(pw.y, pw.x);
    let gul = pow(abs(sin(a * p3 * 0.5 + seed)), 3.0);
    h = h * (1.0 - p4 * gul * smoothstep(p1 * 1.2, 0.8, r));
    h = h + elev * 0.04 * ridged(pw * scale + sd, oct, lac, gain, sharp, damp);
    return h;
  }
  if (t == 4) {
    // Tectonic fault escarpments: tilted fault blocks with sharp scarps
    let rx = ca * pw.x - sa * pw.y;
    let ry = sa * pw.x + ca * pw.y;
    let u = ry * p2 + 0.25 * noised(vec2f(rx * 2.0 + seed, ry * 2.0)).x;
    let f = fract(u);
    let st = smoothstep(0.5 - 0.45 * p1, 0.5 + 0.45 * p1, f);
    return elev * (0.55 * st + 0.45 * f - 0.25 * p3 * (f - 0.5));
  }
  if (t == 5) {
    // Folded sedimentary strata: resistant ledges that follow the existing topography
    let rx = ca * pw.x - sa * pw.y;
    let fold = noised(pw * 1.9 + vec2f(seed, seed)).x * p3 * 180.0;
    let bed = ((prev + fold + rx * sin(p1) * 650.0) / 1000.0) * scale;
    let ph = fract(bed);
    let stepW = smoothstep(0.5 - (1.0 - p2) * 0.45, 0.5 + (1.0 - p2) * 0.45, ph) - 0.5;
    let harm = 0.28 * sin(bed * 12.566);
    return elev * (stepW * 0.75 + harm * 0.25);
  }
  if (t == 6) {
    // Rugged outcrops & crags: derivative-damped ridged noise
    let f = ridged(pw * scale + sd, oct, lac, gain, sharp, damp);
    return elev * (f - 0.35);
  }
  if (t == 7) {
    // Stepped cliff terracing
    let wm = noised(pw * 2.6 + vec2f(seed, seed)).x * warp * 95.0;
    let nrm = clamp((prev + wm) / 2200.0, 0.0, 1.5);
    let s = nrm * p1;
    let hw = 0.5 * (1.0 - p2 * 0.85);
    return ((floor(s) + smoothstep(0.5 - hw, 0.5 + hw, fract(s))) / max(p1, 1.0)) * 2200.0 - wm;
  }
  // t == 8: aeolian dunes (barchan / transverse with asymmetric slip faces)
  let rx = (ca * pw.x - sa * pw.y) * scale;
  let ry = (sa * pw.x + ca * pw.y) * scale;
  let rawP = rx + sin(ry * 1.35 + seed) * 0.35;
  let ph = fract(rawP);
  let pivot = 0.5 + p2 * 0.32;
  var dune = 0.0;
  if (ph < pivot) {
    dune = pow(ph / pivot, p1);
  } else {
    dune = pow(1.0 - (ph - pivot) / max(0.05, 1.0 - pivot), 0.75);
  }
  let rip = (0.5 + 0.5 * sin(rx * 22.0)) * p3 * 0.08;
  return elev * (dune + rip);
}

@compute @workgroup_size(8, 8)
fn layerMain(@builtin(global_invocation_id) gid: vec3u) {
  let N = i32(G.n.x);
  let p = vec2i(i32(gid.x), i32(gid.y));
  if (p.x >= N || p.y >= N) { return; }
  let cm = G.n.y;
  let prev = ld(p);
  let uv = (vec2f(f32(p.x), f32(p.y)) + 0.5) / f32(N);
  let t = i32(prm(0));
  var vx = uv.x * 2.0 - 1.0 - prm(25);
  var vz = uv.y * 2.0 - 1.0 - prm(26);

  var h = layerH(t, vx, vz, prev);
  if (prm(3) > 0.5 && t != 7) { h = -h; }

  let gx = (ld(p + vec2i(1, 0)) - ld(p + vec2i(-1, 0))) / (2.0 * cm);
  let gz = (ld(p + vec2i(0, 1)) - ld(p + vec2i(0, -1))) / (2.0 * cm);
  let slopeDeg = degrees(atan(length(vec2f(gx, gz))));

  let altMin = prm(20);
  let altMax = prm(21);
  let feather = 0.06 * (altMax - altMin) + 10.0;
  let mAlt = smoothstep(altMin - feather, altMin + feather, prev) * (1.0 - smoothstep(altMax - feather, altMax + feather, prev));
  let slMin = prm(22);
  let slMax = prm(23);
  let mSl = smoothstep(slMin - 3.0, slMin + 3.0, slopeDeg) * (1.0 - smoothstep(slMax - 3.0, slMax + 3.0, slopeDeg));
  let rr = length(vec2f(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0));
  let mRad = mix(1.0, 1.0 - smoothstep(0.0, 1.2, rr), prm(24));
  let w = clamp(mAlt * mSl * mRad * prm(2), 0.0, 1.0);

  let mode = i32(prm(1) + 0.5);
  var outV = prev;
  if (mode == 0) { outV = prev + h * w; }
  else if (mode == 1) { outV = prev - h * w; }
  else if (mode == 2) { outV = prev + prev * (h / max(prm(4), 1.0)) * w; }
  else if (mode == 3) { outV = mix(prev, max(prev, h), w); }
  else if (mode == 4) { outV = mix(prev, min(prev, h), w); }
  else if (mode == 5) { outV = mix(prev, h, w); }
  else if (mode == 6) { outV = prev + h * w - prev * h * w / max(prm(4) * 3.0, 1.0); }
  else { outV = prev + h * w * 1.5; }
  textureStore(outH, p, vec4f(outV, 0.0, 0.0, 1.0));
}
`;

const ERODE_COMMON = /* wgsl */ `
struct Glob { n: vec4f, };
@group(0) @binding(0) var<uniform> G: Glob;
@group(0) @binding(1) var<uniform> ER: array<vec4f, 3>;
@group(0) @binding(2) var hTex: texture_2d<f32>;
@group(0) @binding(3) var wTex: texture_2d<f32>;
@group(0) @binding(4) var fPrev: texture_2d<f32>;

fn inb(p: vec2i) -> bool {
  let N = i32(G.n.x);
  return p.x >= 0 && p.y >= 0 && p.x < N && p.y < N;
}
fn ldH(p: vec2i) -> f32 {
  let N = i32(G.n.x);
  return textureLoad(hTex, clamp(p, vec2i(0), vec2i(N - 1)), 0).x;
}
fn ldW(p: vec2i) -> f32 {
  let N = i32(G.n.x);
  return textureLoad(wTex, clamp(p, vec2i(0), vec2i(N - 1)), 0).x;
}
`;

export const FLUX_WGSL = /* wgsl */ `
${ERODE_COMMON}
@group(0) @binding(5) var fOut: texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn fluxMain(@builtin(global_invocation_id) gid: vec3u) {
  let N = i32(G.n.x);
  let p = vec2i(i32(gid.x), i32(gid.y));
  if (p.x >= N || p.y >= N) { return; }
  let dt = ER[0].x;
  let g = ER[0].y;
  let A = ER[0].z;
  let l = G.n.y;
  let hC = ldH(p) + ldW(p);
  let fp = textureLoad(fPrev, p, 0);
  var fL = 0.0;
  var fR = 0.0;
  var fT = 0.0;
  var fB = 0.0;
  if (p.x > 0) {
    let hn = ldH(p + vec2i(-1, 0)) + ldW(p + vec2i(-1, 0));
    fL = max(0.0, fp.x + dt * A * g * (hC - hn) / l);
  }
  if (p.x < N - 1) {
    let hn = ldH(p + vec2i(1, 0)) + ldW(p + vec2i(1, 0));
    fR = max(0.0, fp.y + dt * A * g * (hC - hn) / l);
  }
  if (p.y > 0) {
    let hn = ldH(p + vec2i(0, -1)) + ldW(p + vec2i(0, -1));
    fT = max(0.0, fp.z + dt * A * g * (hC - hn) / l);
  }
  if (p.y < N - 1) {
    let hn = ldH(p + vec2i(0, 1)) + ldW(p + vec2i(0, 1));
    fB = max(0.0, fp.w + dt * A * g * (hC - hn) / l);
  }
  let sum = fL + fR + fT + fB;
  let W = ldW(p);
  var K = 1.0;
  if (sum > 0.0) { K = min(1.0, W * l * l / (sum * dt)); }
  textureStore(fOut, p, vec4f(fL, fR, fT, fB) * K);
}
`;

export const ERODE_WGSL = /* wgsl */ `
${ERODE_COMMON}
@group(0) @binding(5) var sTex: texture_2d<f32>;
@group(0) @binding(6) var fNew: texture_2d<f32>;
@group(0) @binding(7) var hOut: texture_storage_2d<r32float, write>;
@group(0) @binding(8) var wOut: texture_storage_2d<r32float, write>;
@group(0) @binding(9) var sOut: texture_storage_2d<r32float, write>;

fn ldF(p: vec2i) -> vec4f {
  if (!inb(p)) { return vec4f(0.0); }
  return textureLoad(fNew, p, 0);
}
fn ldS(p: vec2i) -> f32 {
  let N = i32(G.n.x);
  return textureLoad(sTex, clamp(p, vec2i(0), vec2i(N - 1)), 0).x;
}
fn bilerpS(q: vec2f) -> f32 {
  let N = G.n.x;
  let c = clamp(q, vec2f(0.0), vec2f(N - 1.0));
  let i0 = vec2i(floor(c));
  let f = c - floor(c);
  let a = ldS(i0);
  let b = ldS(i0 + vec2i(1, 0));
  let cc = ldS(i0 + vec2i(0, 1));
  let d = ldS(i0 + vec2i(1, 1));
  return mix(mix(a, b, f.x), mix(cc, d, f.x), f.y);
}

@compute @workgroup_size(8, 8)
fn erodeMain(@builtin(global_invocation_id) gid: vec3u) {
  let N = i32(G.n.x);
  let p = vec2i(i32(gid.x), i32(gid.y));
  if (p.x >= N || p.y >= N) { return; }
  let dt = ER[0].x;
  let l = G.n.y;
  let rain = ER[0].w;
  let Kc = ER[1].x;
  let Ks = ER[1].y;
  let Kd = ER[1].z;
  let evap = ER[1].w;

  let hC = ldH(p);
  let W = ldW(p);
  let f = ldF(p);
  let outFlow = f.x + f.y + f.z + f.w;
  // inflows: left neighbour's right-flux, right neighbour's left-flux, up neighbour's down-flux, down neighbour's up-flux
  let inL = ldF(p + vec2i(-1, 0)).y;
  let inR = ldF(p + vec2i(1, 0)).x;
  let inT = ldF(p + vec2i(0, -1)).w;
  let inB = ldF(p + vec2i(0, 1)).z;
  let inflow = inL + inR + inT + inB;

  let oro = 1.0 + 0.6 * smoothstep(600.0, 3200.0, hC);
  var Wn = W + dt * (inflow - outFlow) / (l * l) + rain * oro;
  Wn = max(0.0, Wn * (1.0 - evap));
  let Wm = max(0.5 * (W + Wn), 1e-4);

  let vx = ((inL + f.y) - (inR + f.x)) * 0.5 / (Wm * l);
  let vz = ((inB + f.w) - (inT + f.z)) * 0.5 / (Wm * l);
  let speed = length(vec2f(vx, vz));

  let hl = ldH(p + vec2i(-1, 0));
  let hr = ldH(p + vec2i(1, 0));
  let hd = ldH(p + vec2i(0, -1));
  let hu = ldH(p + vec2i(0, 1));
  let slope = length(vec2f(hr - hl, hu - hd)) / (2.0 * l);
  let sinA = slope / sqrt(1.0 + slope * slope);

  let C = Kc * max(sinA, 0.02) * speed * (0.3 + Wm);
  let S0 = ldS(p);
  var dH = 0.0;
  if (S0 < C) {
    dH = -min(Ks * (C - S0) * dt, 0.5);
  } else {
    dH = min(Kd * (S0 - C) * dt, 0.5);
  }
  let hNew = hC + dH;
  let S1 = max(0.0, S0 - dH);
  let src = vec2f(f32(p.x), f32(p.y)) - vec2f(vx, vz) * dt / l;
  let sAdv = bilerpS(src);
  let sNew = max(0.0, sAdv + (S1 - S0));

  textureStore(hOut, p, vec4f(hNew, 0.0, 0.0, 1.0));
  textureStore(wOut, p, vec4f(Wn, 0.0, 0.0, 1.0));
  textureStore(sOut, p, vec4f(sNew, 0.0, 0.0, 1.0));
}
`;

export const THERMAL_WGSL = /* wgsl */ `
${ERODE_COMMON}
@group(0) @binding(5) var hOut: texture_storage_2d<r32float, write>;

@compute @workgroup_size(8, 8)
fn thermalMain(@builtin(global_invocation_id) gid: vec3u) {
  let N = i32(G.n.x);
  let p = vec2i(i32(gid.x), i32(gid.y));
  if (p.x >= N || p.y >= N) { return; }
  let rate = ER[2].x;
  let tanR = ER[2].y;
  let l = G.n.y;
  let h = ldH(p);
  var acc = 0.0;
  for (var j = -1; j <= 1; j++) {
    for (var i = -1; i <= 1; i++) {
      if (i == 0 && j == 0) { continue; }
      let q = p + vec2i(i, j);
      if (!inb(q)) { continue; }
      let dist = select(l, l * 1.41421, i != 0 && j != 0);
      let dh = ldH(q) - h;
      let ex = max(0.0, abs(dh) - tanR * dist);
      acc += sign(dh) * ex * 0.5 * rate;
    }
  }
  textureStore(hOut, p, vec4f(h + acc, 0.0, 0.0, 1.0));
}
`;

export const FIELDS_WGSL = /* wgsl */ `
struct Glob { n: vec4f, };
@group(0) @binding(0) var<uniform> G: Glob;
@group(0) @binding(1) var<uniform> SN: array<vec4f, 2>;
@group(0) @binding(2) var hTex: texture_2d<f32>;
@group(0) @binding(3) var fTex: texture_2d<f32>;
@group(0) @binding(4) var sTex: texture_2d<f32>;
@group(0) @binding(5) var outB: texture_storage_2d<rgba16float, write>;

fn ldH(p: vec2i) -> f32 {
  let N = i32(G.n.x);
  return textureLoad(hTex, clamp(p, vec2i(0), vec2i(N - 1)), 0).x;
}
fn flowAt(p: vec2i) -> f32 {
  let N = i32(G.n.x);
  let q = clamp(p, vec2i(0), vec2i(N - 1));
  let f = textureLoad(fTex, q, 0);
  return f.x + f.y + f.z + f.w;
}

@compute @workgroup_size(8, 8)
fn fieldsMain(@builtin(global_invocation_id) gid: vec3u) {
  let N = i32(G.n.x);
  let p = vec2i(i32(gid.x), i32(gid.y));
  if (p.x >= N || p.y >= N) { return; }
  let cm = G.n.y;
  let hC = ldH(p);
  let gx = (ldH(p + vec2i(1, 0)) - ldH(p + vec2i(-1, 0))) / (2.0 * cm);
  let gz = (ldH(p + vec2i(0, 1)) - ldH(p + vec2i(0, -1))) / (2.0 * cm);
  let slopeDeg = degrees(atan(length(vec2f(gx, gz))));
  let curv = (ldH(p + vec2i(1, 0)) + ldH(p + vec2i(-1, 0)) + ldH(p + vec2i(0, 1)) + ldH(p + vec2i(0, -1)) - 4.0 * hC) / (cm * cm);

  // Flow accumulation proxy: 5-tap smoothed pipe-model outflow, saturating response
  let fl = (flowAt(p) * 2.0 + flowAt(p + vec2i(1, 0)) + flowAt(p + vec2i(-1, 0)) + flowAt(p + vec2i(0, 1)) + flowAt(p + vec2i(0, -1))) / 6.0;
  let flow = 1.0 - exp(-fl / max(SN[1].z, 1e-6));

  let sed = textureLoad(sTex, p, 0).x;
  let talus = sed * smoothstep(8.0, 20.0, slopeDeg) * (1.0 - smoothstep(34.0, 46.0, slopeDeg));

  var snow = 0.0;
  if (SN[1].w > 0.5) {
    let snowline = SN[0].x;
    let depth = SN[0].y;
    let wind = vec2f(sin(SN[0].z), cos(SN[0].z));
    let nrm = normalize(vec3f(-gx, 1.0, -gz));
    let altF = smoothstep(snowline - 240.0, snowline + 220.0, hC);
    let windF = 1.0 + dot(nrm.xz, wind) * SN[0].w * 0.55;
    let south = max(0.0, nrm.z) * SN[1].x * 0.35;
    let hold = 1.0 - smoothstep(SN[1].y - 8.0, SN[1].y + 14.0, slopeDeg);
    snow = clamp(altF * depth * 1.35 * windF * (1.0 - south) * max(0.18, hold + max(0.0, curv * cm) * 0.55), 0.0, 1.5);
  }
  textureStore(outB, p, vec4f(flow, sed, talus, snow));
}
`;

// Splat pass: swap the storage-buffer bindings of the shaders.js splat kernel for textures (logic is unchanged)
export function buildSplatWGSL() {
  let s = ANALYZE_AND_SPLAT_WGSL;
  const rep = (a, b) => {
    if (!s.includes(a)) throw new Error("splat patch anchor missing: " + a.slice(0, 60));
    s = s.replace(a, b);
  };
  rep(
    `@group(0) @binding(1) var<storage, read> Terrain: array<vec4f>;
@group(0) @binding(2) var<storage, read> Hydro: array<vec4f>;
@group(0) @binding(3) var<storage, read_write> Geom: array<vec4f>;
@group(0) @binding(4) var<storage, read_write> SplatColor: array<vec4f>;
@group(0) @binding(5) var<storage, read_write> SplatMeta: array<vec4f>;`,
    `@group(0) @binding(1) var hTex: texture_2d<f32>;
@group(0) @binding(2) var bTex: texture_2d<f32>;
@group(0) @binding(3) var outGeom: texture_storage_2d<rgba16float, write>;
@group(0) @binding(4) var outSplat: texture_storage_2d<rgba16float, write>;
@group(0) @binding(5) var outMeta: texture_storage_2d<rgba16float, write>;
fn TerrainAt(idx: i32) -> vec4f {
  let N = i32(S.g0.x);
  let p = vec2i(idx % N, idx / N);
  let b = textureLoad(bTex, p, 0);
  return vec4f(textureLoad(hTex, p, 0).x, b.y, b.z, b.w);
}
fn HydroAt(idx: i32) -> vec4f {
  let N = i32(S.g0.x);
  let p = vec2i(idx % N, idx / N);
  return vec4f(0.0, textureLoad(bTex, p, 0).x, 0.0, 0.0);
}`
  );
  rep("  return Terrain[clamp(z, 0, N - 1) * N + clamp(x, 0, N - 1)].x;", "  return textureLoad(hTex, vec2i(clamp(x, 0, N - 1), clamp(z, 0, N - 1)), 0).x;");
  rep("  let ter = Terrain[idx];", "  let ter = TerrainAt(idx);");
  rep("  let hyd = Hydro[idx];", "  let hyd = HydroAt(idx);");
  rep("  Geom[idx] = vec4f(normal.x, normal.z, curvature, ao);", "  textureStore(outGeom, vec2i(x, z), vec4f(normal.x, normal.z, curvature, ao));");
  rep("  var selectedMask = SplatMeta[idx].w;", "  var selectedMask = 0.0;");
  rep("  SplatColor[idx] = vec4f(outCol, outRough);", "  textureStore(outSplat, vec2i(x, z), vec4f(outCol, outRough));");
  rep("  SplatMeta[idx] = vec4f(hyd.x, flow, outStrata, selectedMask);", "  textureStore(outMeta, vec2i(x, z), vec4f(hyd.x, flow, outStrata, selectedMask));");
  return s;
}

// Viewport: swap the storage-buffer sampling for texture sampling (bilinear via textureLoad), keep all shading.
export function buildRenderWGSL() {
  let s = VIEWPORT_RENDER_WGSL;
  const rep = (a, b) => {
    if (!s.includes(a)) throw new Error("render patch anchor missing: " + a.slice(0, 60));
    s = s.replace(a, b);
  };
  rep(
    `@group(0) @binding(1) var<storage, read> TerrainBuf: array<vec4f>;
@group(0) @binding(2) var<storage, read> GeomBuf: array<vec4f>;
@group(0) @binding(3) var<storage, read> SplatBuf: array<vec4f>;
@group(0) @binding(4) var<storage, read> HydroMetaBuf: array<vec4f>;`,
    `@group(0) @binding(1) var hTex: texture_2d<f32>;
@group(0) @binding(2) var geomTex: texture_2d<f32>;
@group(0) @binding(3) var attrBTex: texture_2d<f32>;
@group(0) @binding(4) var splatTex: texture_2d<f32>;
@group(0) @binding(5) var metaTex: texture_2d<f32>;
@group(0) @binding(6) var sculptTex: texture_2d<f32>;`
  );
  const a = s.indexOf("fn sampleTerrainBilerp(uv: vec2f) -> vec4f {");
  const b = s.indexOf("fn computeSkyColor");
  if (a < 0 || b < 0) throw new Error("render sampler block not found");
  s =
    s.slice(0, a) +
    `fn bil(t: texture_2d<f32>, uv: vec2f) -> vec4f {
  let dim = vec2f(textureDimensions(t));
  let p = clamp(uv, vec2f(0.0), vec2f(1.0)) * (dim - 1.0);
  let i0 = vec2i(floor(p));
  let lim = vec2i(dim) - vec2i(1, 1);
  let i1 = min(i0 + vec2i(1, 1), lim);
  let f = fract(p);
  let v00 = textureLoad(t, i0, 0);
  let v10 = textureLoad(t, vec2i(i1.x, i0.y), 0);
  let v01 = textureLoad(t, vec2i(i0.x, i1.y), 0);
  let v11 = textureLoad(t, i1, 0);
  return mix(mix(v00, v10, f.x), mix(v01, v11, f.x), f.y);
}
fn sampleTerrainBilerp(uv: vec2f) -> vec4f {
  let hh = bil(hTex, uv);
  let bb = bil(attrBTex, uv);
  let sc = bil(sculptTex, uv).x;
  return vec4f(hh.x + sc, bb.y, bb.z, bb.w);
}
fn sampleGeomBilerp(uv: vec2f) -> vec4f { return bil(geomTex, uv); }
fn sampleSplatBilerp(uv: vec2f) -> vec4f { return bil(splatTex, uv); }
fn sampleHydroBilerp(uv: vec2f) -> vec4f { return bil(metaTex, uv); }

` +
    s.slice(b);
  return s;
}
