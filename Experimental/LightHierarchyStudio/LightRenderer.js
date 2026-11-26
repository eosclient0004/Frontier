/*============================================================================================================================================
                                                              LIGHTRENDERER.JS
============================================================================================================================================*/
// 🧩 Unified 3-System WebGL PBR Renderer:
//    - 2-Cascade Toroidal Clipmap (Fine + Coarse) for STATIC lights
//    - Cube-Grid / Pod Directional Atlas + Nanite Light BVH for DYNAMIC lights
//    - Project Zero Header Debug Views (Lit | Error x8 | Toroidal Clipmap | Static vs Dynamic | Cube-Grid Pods)
//    - HW GPU Timer Support (EXT_disjoint_timer_query / gl.finish() fallback)

import { SCENE_BOXES, WORLD_HALF_EXTENT } from './LightEngine.js';

const MAX_LIGHTS = 32;

const VERT_SRC = `
attribute vec2 aPos;
varying vec2 vUv;
void main() {
  vUv = aPos * 0.5 + 0.5;
  gl_Position = vec4(aPos, 0.0, 1.0);
}
`;

function BuildGlslBoxesBlock() {
  let code = `const int NUM_BOXES = ${SCENE_BOXES.length};\n`;
  code += `void getBox(int i, out vec3 bMin, out vec3 bMax, out vec3 bCol, out float bRough) {\n`;
  for (let i = 0; i < SCENE_BOXES.length; i++) {
    const b = SCENE_BOXES[i];
    const kw = i === 0 ? 'if' : 'else if';
    code += `  ${kw} (i == ${i}) { bMin = vec3(${b.min.map((v) => v.toFixed(3)).join(',')}); bMax = vec3(${b.max.map((v) => v.toFixed(3)).join(',')}); bCol = vec3(${b.color.map((v) => v.toFixed(3)).join(',')}); bRough = ${b.rough.toFixed(3)}; }\n`;
  }
  code += `  else { bMin = vec3(0.0); bMax = vec3(0.0); bCol = vec3(0.5); bRough = 0.5; }\n}\n`;
  return code;
}

const FRAG_SRC = `
precision highp float;
const int MAX_LIGHTS = ${MAX_LIGHTS};
varying vec2 vUv;

uniform vec2  uResolution;
uniform vec3  uCamPos;
uniform vec3  uCamRight;
uniform vec3  uCamUp;
uniform vec3  uCamFwd;
uniform float uTanHalfFov;

// Lighting Mode:
//   0 = Exact (LOD 0 Reference, all 32 raw emitters)
//   1 = Naive Point Merge (averaged groups -> big error!)
//   2 = 1 Cube-Grid / Pod (95.1 KB/pod directional pods)
//   3 = Toroidal Clipmap (Static 2-Cascade Clipmap + Dynamic Pods)
//   4 = Unified All-3 (Static Toroidal Clipmap + Dynamic Nanite BVH/Pods + Error-Guided Promotion)
uniform int   uLightMode;

// Debug View Mode (from Project Zero Viewport Header):
//   0 = Lit PBR
//   1 = Error x8 Heatmap |Exact - uLightMode|
//   2 = Toroidal Clipmap Debug (Fine vs Coarse cascades + cell boundaries)
//   3 = Static (Cyan) vs Dynamic (Gold) Split Visualization
//   4 = Cube-Grid Pods & Nanite LOD Heatmap
uniform int   uDebugView;
uniform float uErrorGain;
uniform float uFloorRoughness;
uniform float uHybridErrorGate;
uniform float uShowGridLines;

// Raw 32 Ground Truth lights
uniform int   uRawCount;
uniform vec4  uRawPosFlux[${MAX_LIGHTS}];
uniform vec4  uRawDirCone[${MAX_LIGHTS}];
uniform vec4  uRawColor[${MAX_LIGHTS}];

// Primary Approx Array (Naive lights / All Cube-Grid Pods / Dynamic Nanite Cut)
uniform int   uApproxCount;
uniform vec4  uApproxPosFlux[${MAX_LIGHTS}];
uniform vec4  uApproxDirCone[${MAX_LIGHTS}];
uniform vec4  uApproxColor[${MAX_LIGHTS}];

// Secondary Array (Static Pods for high-error cell promotion in Unified All-3)
uniform int   uStaticPromoCount;
uniform vec4  uStaticPromoPosFlux[${MAX_LIGHTS}];
uniform vec4  uStaticPromoDirCone[${MAX_LIGHTS}];
uniform vec4  uStaticPromoColor[${MAX_LIGHTS}];

// 2-Cascade Toroidal Clipmap (Fine follows probe/focus, Coarse covers full world)
uniform sampler2D uFineIrrTex;
uniform sampler2D uFineDirTex;
uniform sampler2D uCoarseIrrTex;
uniform sampler2D uCoarseDirTex;
uniform vec4      uFineBounds;    // xy = snapped centerXZ, z = halfSpan, w = res
uniform vec4      uCoarseBounds;  // xy = centerXZ (0,0), z = halfSpan, w = res
uniform vec4      uClipScales;    // x = fineIrrScale, y = fineErrScale, z = coarseIrrScale, w = coarseErrScale

${BuildGlslBoxesBlock()}

vec2 intersectBox(vec3 ro, vec3 rd, vec3 bMin, vec3 bMax, out vec3 outN) {
  vec3 invD = 1.0 / max(abs(rd), vec3(1e-5)) * sign(rd + vec3(1e-9));
  vec3 t0s = (bMin - ro) * invD;
  vec3 t1s = (bMax - ro) * invD;
  vec3 tSm = min(t0s, t1s);
  vec3 tBg = max(t0s, t1s);
  float tNear = max(max(tSm.x, tSm.y), tSm.z);
  float tFar  = min(min(tBg.x, tBg.y), tBg.z);
  outN = vec3(0.0, 1.0, 0.0);
  if (tNear > tFar || tFar < 0.0) return vec2(-1.0);
  if (tSm.x > tSm.y && tSm.x > tSm.z) outN = vec3(-sign(rd.x), 0.0, 0.0);
  else if (tSm.y > tSm.z) outN = vec3(0.0, -sign(rd.y), 0.0);
  else outN = vec3(0.0, 0.0, -sign(rd.z));
  return vec2(tNear, tFar);
}

float shadowRay(vec3 p, vec3 lightPos) {
  vec3 toL = lightPos - p;
  float dist = length(toL);
  if (dist < 0.02) return 1.0;
  vec3 rd = toL / dist;
  vec3 ro = p + rd * 0.02;
  for (int i = 0; i < NUM_BOXES; i++) {
    vec3 bMin, bMax, bCol; float bR;
    getBox(i, bMin, bMax, bCol, bR);
    vec3 tmpN;
    vec2 hit = intersectBox(ro, rd, bMin, bMax, tmpN);
    if (hit.x > 0.0 && hit.x < dist - 0.04) return 0.0;
  }
  return 1.0;
}

vec3 evalPbrSingleLight(vec3 P, vec3 N, vec3 V, vec3 albedo, float rough, vec4 posFlux, vec4 dirCone, vec3 col) {
  vec3 toL = posFlux.xyz - P;
  float distSq = dot(toL, toL) + 0.35;
  float dist = sqrt(distSq);
  vec3 L = toL / dist;
  float ndl = max(dot(N, L), 0.0);
  if (ndl <= 0.0) return vec3(0.0);

  float spot = 1.0;
  if (dirCone.w > -0.95) {
    float cosAng = dot(-L, normalize(dirCone.xyz));
    spot = smoothstep(dirCone.w, min(0.99, dirCone.w + 0.30), cosAng);
    if (spot <= 0.001) return vec3(0.0);
  }

  float vis = shadowRay(P + N * 0.015, posFlux.xyz);
  if (vis <= 0.0) return vec3(0.0);

  vec3 H = normalize(L + V);
  float ndh = max(dot(N, H), 0.0);
  float ndv = max(dot(N, V), 0.05);
  float a = max(0.06, rough * rough);
  float a2 = a * a;
  float denom = ndh * ndh * (a2 - 1.0) + 1.0;
  float D = a2 / (3.14159265 * denom * denom + 1e-5);
  float F = 0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
  float spec = (D * F) / (4.0 * ndv * ndl + 0.15);

  float radiance = (posFlux.w * spot * vis) / (distSq * 3.14159265);
  return col * radiance * (albedo * ndl + vec3(spec * ndl * 0.85));
}

vec3 evalRawExact(vec3 P, vec3 N, vec3 V, vec3 albedo, float rough) {
  vec3 sum = albedo * vec3(0.016, 0.020, 0.028);
  for (int i = 0; i < MAX_LIGHTS; i++) {
    if (i >= uRawCount) break;
    sum += evalPbrSingleLight(P, N, V, albedo, rough, uRawPosFlux[i], uRawDirCone[i], uRawColor[i].rgb);
  }
  return sum;
}

vec3 evalApproxArray(vec3 P, vec3 N, vec3 V, vec3 albedo, float rough) {
  vec3 sum = vec3(0.0);
  for (int i = 0; i < MAX_LIGHTS; i++) {
    if (i >= uApproxCount) break;
    sum += evalPbrSingleLight(P, N, V, albedo, rough, uApproxPosFlux[i], uApproxDirCone[i], uApproxColor[i].rgb);
  }
  return sum;
}

vec3 evalStaticPromoArray(vec3 P, vec3 N, vec3 V, vec3 albedo, float rough) {
  vec3 sum = vec3(0.0);
  for (int i = 0; i < MAX_LIGHTS; i++) {
    if (i >= uStaticPromoCount) break;
    sum += evalPbrSingleLight(P, N, V, albedo, rough, uStaticPromoPosFlux[i], uStaticPromoDirCone[i], uStaticPromoColor[i].rgb);
  }
  return sum;
}

// Samples the 2-Cascade Toroidal Clipmap (Fine around focus/probe -> smooth border blend -> Coarse full world)
vec3 evalToroidalClipmap(vec3 P, vec3 N, vec3 V, vec3 albedo, float rough, out float outErr, out float outFineWeight) {
  vec2 fUv = (P.xz - (uFineBounds.xy - vec2(uFineBounds.z))) / (2.0 * uFineBounds.z);
  vec2 cUv = clamp((P.xz - (uCoarseBounds.xy - vec2(uCoarseBounds.z))) / (2.0 * uCoarseBounds.z), 0.001, 0.999);

  float edgeDist = min(min(fUv.x, 1.0 - fUv.x), min(fUv.y, 1.0 - fUv.y));
  outFineWeight = smoothstep(0.0, 0.14, edgeDist);

  vec4 cIrr = texture2D(uCoarseIrrTex, cUv);
  vec4 cDir = texture2D(uCoarseDirTex, cUv);
  vec3 L0 = cIrr.rgb * uClipScales.z;
  float dirR = cIrr.a;
  vec3 domDir = normalize(cDir.rgb * 2.0 - 1.0);
  outErr = cDir.a * uClipScales.w;

  if (outFineWeight > 0.001) {
    vec2 cfUv = clamp(fUv, 0.001, 0.999);
    vec4 fIrr = texture2D(uFineIrrTex, cfUv);
    vec4 fDir = texture2D(uFineDirTex, cfUv);
    vec3 fL0 = fIrr.rgb * uClipScales.x;
    vec3 fDom = normalize(fDir.rgb * 2.0 - 1.0);
    L0 = mix(L0, fL0, outFineWeight);
    dirR = mix(dirR, fIrr.a, outFineWeight);
    domDir = normalize(mix(domDir, fDom, outFineWeight));
    outErr = mix(outErr, fDir.a * uClipScales.y, outFineWeight);
  }

  float ndl = max(dot(N, domDir), 0.0);
  vec3 H = normalize(domDir + V);
  float ndh = max(dot(N, H), 0.0);
  float ndv = max(dot(N, V), 0.05);
  float a = max(0.08, rough * rough);
  float a2 = a * a;
  float denom = ndh * ndh * (a2 - 1.0) + 1.0;
  float D = a2 / (3.14159265 * denom * denom + 1e-5);
  float F = 0.04 + 0.96 * pow(1.0 - max(dot(H, V), 0.0), 5.0);
  float spec = (D * F) / (4.0 * ndv * max(ndl, 0.15) + 0.15);

  float normalMod = mix(1.0, max(0.16, ndl / max(0.25, domDir.y)), dirR * 0.85);
  return albedo * L0 * normalMod + L0 * (dirR * spec * 0.72);
}

vec3 evalPipeline(int mode, vec3 P, vec3 N, vec3 V, vec3 albedo, float rough, out vec3 outStaticCol, out vec3 outDynamicCol, out float outFineW, out float outPromoW) {
  vec3 ambient = albedo * vec3(0.016, 0.020, 0.028);
  outStaticCol = vec3(0.0);
  outDynamicCol = vec3(0.0);
  outFineW = 0.0;
  outPromoW = 0.0;

  if (mode == 0) {
    return evalRawExact(P, N, V, albedo, rough);
  }
  if (mode == 1 || mode == 2) {
    // Mode 1: Naive Point Merge | Mode 2: 1 Cube-Grid/Pod
    outDynamicCol = evalApproxArray(P, N, V, albedo, rough);
    return ambient + outDynamicCol;
  }

  // Modes 3 & 4: Separate STATIC Toroidal Clipmap + DYNAMIC Cube-Grid Pod / Nanite BVH!
  float cellErr = 0.0;
  vec3 clipStatic = evalToroidalClipmap(P, N, V, albedo, rough, cellErr, outFineW);
  vec3 dynLights = evalApproxArray(P, N, V, albedo, rough);
  outDynamicCol = dynLights;

  if (mode == 3) {
    outStaticCol = clipStatic;
    return ambient + clipStatic + dynLights;
  }

  // Mode 4: Unified All-3! Uses Error-Guided Promotion ONLY where Static Toroidal cell error is high!
  float wPromo = smoothstep(uHybridErrorGate * 0.75, uHybridErrorGate * 1.35, cellErr);
  outPromoW = wPromo;
  vec3 finalStatic = clipStatic;
  if (wPromo > 0.01) {
    vec3 promoStatic = evalStaticPromoArray(P, N, V, albedo, rough);
    finalStatic = mix(clipStatic, promoStatic, wPromo);
  }
  outStaticCol = finalStatic;
  return ambient + finalStatic + dynLights;
}

vec3 toneMap(vec3 c) {
  c = c / (vec3(1.0) + c);
  return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 infernoPalette(float t) {
  t = clamp(t, 0.0, 1.0);
  vec3 c0 = vec3(0.03, 0.03, 0.07);
  vec3 c1 = vec3(0.36, 0.07, 0.43);
  vec3 c2 = vec3(0.84, 0.28, 0.15);
  vec3 c3 = vec3(0.99, 0.88, 0.25);
  if (t < 0.33) return mix(c0, c1, t / 0.33);
  if (t < 0.66) return mix(c1, c2, (t - 0.33) / 0.33);
  return mix(c2, c3, (t - 0.66) / 0.34);
}

void main() {
  vec2 ndc = vUv * 2.0 - 1.0;
  float aspect = uResolution.x / max(1.0, uResolution.y);
  vec3 rd = normalize(uCamFwd + uCamRight * (ndc.x * aspect * uTanHalfFov) + uCamUp * (ndc.y * uTanHalfFov));
  vec3 ro = uCamPos;

  float bestT = 1e6;
  vec3 hitN = vec3(0.0, 1.0, 0.0);
  vec3 hitCol = vec3(0.35);
  float hitRough = uFloorRoughness;
  bool hitFloor = false;

  if (rd.y < -1e-4) {
    float tf = -ro.y / rd.y;
    if (tf > 0.0 && tf < 180.0) {
      vec3 fp = ro + rd * tf;
      if (abs(fp.x) <= ${WORLD_HALF_EXTENT.toFixed(1)} && abs(fp.z) <= ${WORLD_HALF_EXTENT.toFixed(1)}) {
        bestT = tf;
        hitFloor = true;
        vec2 tile = floor(fp.xz * 0.5);
        float chk = mod(tile.x + tile.y, 2.0);
        hitCol = mix(vec3(0.24, 0.26, 0.29), vec3(0.32, 0.34, 0.38), chk);
        hitRough = clamp(uFloorRoughness + (chk - 0.5) * 0.14, 0.08, 0.95);
      }
    }
  }

  for (int i = 0; i < NUM_BOXES; i++) {
    vec3 bMin, bMax, bCol; float bR;
    getBox(i, bMin, bMax, bCol, bR);
    vec3 bn;
    vec2 h = intersectBox(ro, rd, bMin, bMax, bn);
    if (h.x > 0.0 && h.x < bestT) {
      bestT = h.x; hitN = bn; hitCol = bCol; hitRough = bR; hitFloor = false;
    }
  }

  if (bestT > 1e5) {
    float h = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    gl_FragColor = vec4(mix(vec3(0.04, 0.05, 0.07), vec3(0.08, 0.10, 0.14), h), 1.0);
    return;
  }

  vec3 P = ro + rd * bestT;
  vec3 V = -rd;
  vec3 staticCol, dynamicCol;
  float fineW, promoW;
  vec3 outRgb;

  if (uDebugView == 1) {
    // Error x8 Heatmap |Exact - uLightMode|
    vec3 truth = toneMap(evalRawExact(P, hitN, V, hitCol, hitRough));
    vec3 approx = toneMap(evalPipeline(uLightMode, P, hitN, V, hitCol, hitRough, staticCol, dynamicCol, fineW, promoW));
    float err = length(truth - approx) * 0.57735;
    outRgb = infernoPalette(err * uErrorGain);
  } else {
    vec3 hdr = evalPipeline(uLightMode, P, hitN, V, hitCol, hitRough, staticCol, dynamicCol, fineW, promoW);
    outRgb = toneMap(hdr);

    if (uDebugView == 2) {
      // Toroidal Clipmap Cascade Visualization: Fine Cascade (Cyan) vs Coarse Cascade (Amber) + Error Promotion (Magenta)
      vec3 cascCol = mix(vec3(0.88, 0.55, 0.18), vec3(0.18, 0.82, 0.94), fineW);
      if (promoW > 0.15) cascCol = mix(cascCol, vec3(0.95, 0.25, 0.65), promoW * 0.7);
      outRgb = mix(outRgb * 0.65, cascCol, 0.42);
    } else if (uDebugView == 3) {
      // Static vs Dynamic System Split: Static Clipmap = Cyan-Teal, Dynamic Pods/Nanite BVH = Warm Gold
      float sLum = dot(toneMap(staticCol), vec3(0.3, 0.6, 0.1));
      float dLum = dot(toneMap(dynamicCol), vec3(0.3, 0.6, 0.1));
      outRgb = vec3(0.10, 0.85, 0.95) * (sLum * 1.45) + vec3(1.0, 0.68, 0.18) * (dLum * 1.45) + vec3(0.03);
    } else if (uDebugView == 4) {
      // Cube-Grid Pods & Error-Promotion Overlay
      outRgb = mix(outRgb, mix(vec3(0.18, 0.72, 0.56), vec3(1.0, 0.52, 0.18), promoW), 0.30);
    }
  }

  // Toroidal Clipmap Cell Grid Lines (Fine = Cyan, Coarse = Dim Slate)
  if (hitFloor && (uShowGridLines > 0.5 || uDebugView == 2)) {
    vec2 fUv = (P.xz - (uFineBounds.xy - vec2(uFineBounds.z))) / (2.0 * uFineBounds.z);
    bool inFine = fUv.x >= 0.0 && fUv.x <= 1.0 && fUv.y >= 0.0 && fUv.y <= 1.0;
    if (inFine) {
      vec2 cellUv = fract(fUv * uFineBounds.w);
      float edge = min(min(cellUv.x, 1.0 - cellUv.x), min(cellUv.y, 1.0 - cellUv.y));
      float line = 1.0 - smoothstep(0.0, 0.05, edge);
      float border = 1.0 - smoothstep(0.0, 0.012, min(min(fUv.x, 1.0 - fUv.x), min(fUv.y, 1.0 - fUv.y)));
      outRgb = mix(outRgb, vec3(0.28, 0.90, 1.0), max(line * 0.32, border * 0.85));
    } else {
      vec2 cUv = (P.xz - (uCoarseBounds.xy - vec2(uCoarseBounds.z))) / (2.0 * uCoarseBounds.z);
      vec2 cellUv = fract(cUv * uCoarseBounds.w);
      float edge = min(min(cellUv.x, 1.0 - cellUv.x), min(cellUv.y, 1.0 - cellUv.y));
      float line = 1.0 - smoothstep(0.0, 0.045, edge);
      outRgb = mix(outRgb, vec3(0.85, 0.62, 0.28), line * 0.25);
    }
  }

  gl_FragColor = vec4(outRgb, 1.0);
}
`;

export class LightViewportRenderer {
  constructor(glCanvas, overlayCanvas) {
    this.glCanvas = glCanvas;
    this.overlayCanvas = overlayCanvas;
    this.overlayCtx = overlayCanvas.getContext('2d');
    const gl = glCanvas.getContext('webgl', { antialias: false, preserveDrawingBuffer: false });
    if (!gl) throw new Error('WebGL not available');
    this.gl = gl;
    // Detect HW GPU Timer extension (matches reference benchmark: EXT_disjoint_timer_query)
    this.timerExt = gl.getExtension('EXT_disjoint_timer_query') || null;
    this.timerLabel = this.timerExt ? 'HW GPU Timer (EXT_disjoint_timer_query)' : 'Isolated GPU Pass Timer (gl.finish)';

    const vs = this._compile(gl.VERTEX_SHADER, VERT_SRC);
    const fs = this._compile(gl.FRAGMENT_SHADER, FRAG_SRC);
    const prog = gl.createProgram();
    gl.attachShader(prog, vs);
    gl.attachShader(prog, fs);
    gl.linkProgram(prog);
    if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      throw new Error('Shader link failed: ' + gl.getProgramInfoLog(prog));
    }
    this.prog = prog;
    gl.useProgram(prog);

    const vbo = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
    const loc = gl.getAttribLocation(prog, 'aPos');
    gl.enableVertexAttribArray(loc);
    gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);

    this.loc = {};
    const names = [
      'uResolution', 'uCamPos', 'uCamRight', 'uCamUp', 'uCamFwd', 'uTanHalfFov',
      'uLightMode', 'uDebugView', 'uErrorGain', 'uFloorRoughness', 'uHybridErrorGate', 'uShowGridLines',
      'uRawCount', 'uRawPosFlux', 'uRawDirCone', 'uRawColor',
      'uApproxCount', 'uApproxPosFlux', 'uApproxDirCone', 'uApproxColor',
      'uStaticPromoCount', 'uStaticPromoPosFlux', 'uStaticPromoDirCone', 'uStaticPromoColor',
      'uFineIrrTex', 'uFineDirTex', 'uCoarseIrrTex', 'uCoarseDirTex',
      'uFineBounds', 'uCoarseBounds', 'uClipScales',
    ];
    for (const n of names) this.loc[n] = gl.getUniformLocation(prog, n);

    this.fineIrrTex = this._createTex(0);
    this.fineDirTex = this._createTex(1);
    this.coarseIrrTex = this._createTex(2);
    this.coarseDirTex = this._createTex(3);
    this.clipScales = new Float32Array([4, 2, 4, 2]);
    this.fineBounds = new Float32Array([0, 0, 6, 16]);
    this.coarseBounds = new Float32Array([0, 0, 12, 16]);

    this._rawPos = new Float32Array(MAX_LIGHTS * 4);
    this._rawDir = new Float32Array(MAX_LIGHTS * 4);
    this._rawCol = new Float32Array(MAX_LIGHTS * 4);
    this._appPos = new Float32Array(MAX_LIGHTS * 4);
    this._appDir = new Float32Array(MAX_LIGHTS * 4);
    this._appCol = new Float32Array(MAX_LIGHTS * 4);
    this._proPos = new Float32Array(MAX_LIGHTS * 4);
    this._proDir = new Float32Array(MAX_LIGHTS * 4);
    this._proCol = new Float32Array(MAX_LIGHTS * 4);
  }

  _compile(type, src) {
    const gl = this.gl;
    const sh = gl.createShader(type);
    gl.shaderSource(sh, src);
    gl.compileShader(sh);
    if (!gl.getShaderParameter(sh, gl.COMPILE_STATUS)) {
      throw new Error('Shader compile error: ' + gl.getShaderInfoLog(sh));
    }
    return sh;
  }

  _createTex(unit) {
    const gl = this.gl;
    const tex = gl.createTexture();
    gl.activeTexture(gl.TEXTURE0 + unit);
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return tex;
  }

  _uploadCascade(casc, irrUnit, irrTex, dirUnit, dirTex) {
    const gl = this.gl;
    const res = casc.res;
    const count = res * res;
    let maxIrr = 0.25, maxErr = 0.20;
    for (let i = 0; i < count; i++) {
      maxIrr = Math.max(maxIrr, casc.texIrr[i * 4], casc.texIrr[i * 4 + 1], casc.texIrr[i * 4 + 2]);
      maxErr = Math.max(maxErr, casc.texDir[i * 4 + 3]);
    }
    const bIrr = new Uint8Array(count * 4);
    const bDir = new Uint8Array(count * 4);
    for (let i = 0; i < count; i++) {
      bIrr[i * 4 + 0] = Math.min(255, Math.round((casc.texIrr[i * 4 + 0] / maxIrr) * 255));
      bIrr[i * 4 + 1] = Math.min(255, Math.round((casc.texIrr[i * 4 + 1] / maxIrr) * 255));
      bIrr[i * 4 + 2] = Math.min(255, Math.round((casc.texIrr[i * 4 + 2] / maxIrr) * 255));
      bIrr[i * 4 + 3] = Math.min(255, Math.round(casc.texIrr[i * 4 + 3] * 255));

      bDir[i * 4 + 0] = Math.min(255, Math.max(0, Math.round((casc.texDir[i * 4 + 0] * 0.5 + 0.5) * 255)));
      bDir[i * 4 + 1] = Math.min(255, Math.max(0, Math.round((casc.texDir[i * 4 + 1] * 0.5 + 0.5) * 255)));
      bDir[i * 4 + 2] = Math.min(255, Math.max(0, Math.round((casc.texDir[i * 4 + 2] * 0.5 + 0.5) * 255)));
      bDir[i * 4 + 3] = Math.min(255, Math.round((casc.texDir[i * 4 + 3] / maxErr) * 255));
    }
    gl.activeTexture(gl.TEXTURE0 + irrUnit);
    gl.bindTexture(gl.TEXTURE_2D, irrTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, res, res, 0, gl.RGBA, gl.UNSIGNED_BYTE, bIrr);

    gl.activeTexture(gl.TEXTURE0 + dirUnit);
    gl.bindTexture(gl.TEXTURE_2D, dirTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, res, res, 0, gl.RGBA, gl.UNSIGNED_BYTE, bDir);
    return [maxIrr, maxErr];
  }

  uploadToroidalClipmap(clipmap) {
    const [fIrr, fErr] = this._uploadCascade(clipmap.fine, 0, this.fineIrrTex, 1, this.fineDirTex);
    const [cIrr, cErr] = this._uploadCascade(clipmap.coarse, 2, this.coarseIrrTex, 3, this.coarseDirTex);
    this.clipScales.set([fIrr, fErr, cIrr, cErr]);
    this.fineBounds.set([clipmap.fine.centerX, clipmap.fine.centerZ, clipmap.fine.halfSpan, clipmap.fine.res]);
    this.coarseBounds.set([clipmap.coarse.centerX, clipmap.coarse.centerZ, clipmap.coarse.halfSpan, clipmap.coarse.res]);
  }

  resize() {
    const rect = this.glCanvas.parentElement.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 1.5);
    const w = Math.max(2, Math.floor(rect.width * dpr));
    const h = Math.max(2, Math.floor(rect.height * dpr));
    if (this.glCanvas.width !== w || this.glCanvas.height !== h) {
      this.glCanvas.width = w;
      this.glCanvas.height = h;
      this.overlayCanvas.width = Math.max(2, Math.floor(rect.width));
      this.overlayCanvas.height = Math.max(2, Math.floor(rect.height));
    }
    this.gl.viewport(0, 0, w, h);
  }

  render(cam, rawLights, approxLights, staticPromoLights, params) {
    const gl = this.gl;
    this.resize();
    gl.useProgram(this.prog);

    const pack = (list, posArr, dirArr, colArr) => {
      const n = Math.min(MAX_LIGHTS, list ? list.length : 0);
      for (let i = 0; i < n; i++) {
        const l = list[i];
        posArr[i * 4 + 0] = l.pos[0]; posArr[i * 4 + 1] = l.pos[1]; posArr[i * 4 + 2] = l.pos[2]; posArr[i * 4 + 3] = l.flux;
        dirArr[i * 4 + 0] = l.dir[0]; dirArr[i * 4 + 1] = l.dir[1]; dirArr[i * 4 + 2] = l.dir[2]; dirArr[i * 4 + 3] = l.cosCone;
        colArr[i * 4 + 0] = l.color[0]; colArr[i * 4 + 1] = l.color[1]; colArr[i * 4 + 2] = l.color[2]; colArr[i * 4 + 3] = 1.0;
      }
      return n;
    };

    gl.uniform2f(this.loc.uResolution, this.glCanvas.width, this.glCanvas.height);
    gl.uniform3fv(this.loc.uCamPos, cam.pos);
    gl.uniform3fv(this.loc.uCamRight, cam.right);
    gl.uniform3fv(this.loc.uCamUp, cam.up);
    gl.uniform3fv(this.loc.uCamFwd, cam.fwd);
    gl.uniform1f(this.loc.uTanHalfFov, cam.tanHalfFov);

    gl.uniform1i(this.loc.uLightMode, params.lightMode);
    gl.uniform1i(this.loc.uDebugView, params.debugView);
    gl.uniform1f(this.loc.uErrorGain, params.errorGain);
    gl.uniform1f(this.loc.uFloorRoughness, params.floorRoughness);
    gl.uniform1f(this.loc.uHybridErrorGate, params.hybridErrorGate);
    gl.uniform1f(this.loc.uShowGridLines, params.showGrid ? 1.0 : 0.0);

    gl.uniform1i(this.loc.uRawCount, pack(rawLights, this._rawPos, this._rawDir, this._rawCol));
    gl.uniform4fv(this.loc.uRawPosFlux, this._rawPos);
    gl.uniform4fv(this.loc.uRawDirCone, this._rawDir);
    gl.uniform4fv(this.loc.uRawColor, this._rawCol);

    gl.uniform1i(this.loc.uApproxCount, pack(approxLights, this._appPos, this._appDir, this._appCol));
    gl.uniform4fv(this.loc.uApproxPosFlux, this._appPos);
    gl.uniform4fv(this.loc.uApproxDirCone, this._appDir);
    gl.uniform4fv(this.loc.uApproxColor, this._appCol);

    gl.uniform1i(this.loc.uStaticPromoCount, pack(staticPromoLights, this._proPos, this._proDir, this._proCol));
    gl.uniform4fv(this.loc.uStaticPromoPosFlux, this._proPos);
    gl.uniform4fv(this.loc.uStaticPromoDirCone, this._proDir);
    gl.uniform4fv(this.loc.uStaticPromoColor, this._proCol);

    gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, this.fineIrrTex);   gl.uniform1i(this.loc.uFineIrrTex, 0);
    gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, this.fineDirTex);   gl.uniform1i(this.loc.uFineDirTex, 1);
    gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, this.coarseIrrTex); gl.uniform1i(this.loc.uCoarseIrrTex, 2);
    gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, this.coarseDirTex); gl.uniform1i(this.loc.uCoarseDirTex, 3);

    gl.uniform4fv(this.loc.uFineBounds, this.fineBounds);
    gl.uniform4fv(this.loc.uCoarseBounds, this.coarseBounds);
    gl.uniform4fv(this.loc.uClipScales, this.clipScales);

    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
  }

  project(cam, wx, wy, wz) {
    const w = this.overlayCanvas.width;
    const h = this.overlayCanvas.height;
    const aspect = w / Math.max(1, h);
    const rx = wx - cam.pos[0], ry = wy - cam.pos[1], rz = wz - cam.pos[2];
    const z = rx * cam.fwd[0] + ry * cam.fwd[1] + rz * cam.fwd[2];
    if (z <= 0.1) return null;
    const x = rx * cam.right[0] + ry * cam.right[1] + rz * cam.right[2];
    const y = rx * cam.up[0] + ry * cam.up[1] + rz * cam.up[2];
    return [((x / (z * aspect * cam.tanHalfFov)) * 0.5 + 0.5) * w, (1.0 - ((y / (z * cam.tanHalfFov)) * 0.5 + 0.5)) * h, z];
  }

  unprojectToFloor(cam, clientX, clientY) {
    const rect = this.overlayCanvas.getBoundingClientRect();
    const ndcX = ((clientX - rect.left) / Math.max(1, rect.width)) * 2.0 - 1.0;
    const ndcY = (1.0 - (clientY - rect.top) / Math.max(1, rect.height)) * 2.0 - 1.0;
    const aspect = rect.width / Math.max(1, rect.height);
    const rdx = cam.fwd[0] + cam.right[0] * ndcX * aspect * cam.tanHalfFov + cam.up[0] * ndcY * cam.tanHalfFov;
    const rdy = cam.fwd[1] + cam.right[1] * ndcX * aspect * cam.tanHalfFov + cam.up[1] * ndcY * cam.tanHalfFov;
    const rdz = cam.fwd[2] + cam.right[2] * ndcX * aspect * cam.tanHalfFov + cam.up[2] * ndcY * cam.tanHalfFov;
    if (rdy >= -1e-4) return null;
    const t = -cam.pos[1] / rdy;
    const wx = cam.pos[0] + rdx * t;
    const wz = cam.pos[2] + rdz * t;
    if (Math.abs(wx) > WORLD_HALF_EXTENT || Math.abs(wz) > WORLD_HALF_EXTENT) return null;
    return [wx, wz];
  }

  drawDebugOverlay(cam, opts) {
    const ctx = this.overlayCtx;
    ctx.clearRect(0, 0, this.overlayCanvas.width, this.overlayCanvas.height);

    // 1) Toroidal Clipmap Fine-Cascade Dominant Direction Arrows
    if ((opts.showGrid || opts.debugView === 2) && opts.clipmap) {
      const fine = opts.clipmap.fine;
      const step = fine.res > 14 ? 2 : 1;
      for (let gz = 0; gz < fine.res; gz += step) {
        for (let gx = 0; gx < fine.res; gx += step) {
          const cell = fine.cells[gz * fine.res + gx];
          const p0 = this.project(cam, cell.cx, 0.08, cell.cz);
          if (!p0) continue;
          const errNorm = Math.min(1.0, cell.cellError / Math.max(0.05, fine.maxErr));
          const len = (0.30 + cell.directionality * 0.65) * (fine.cellSize * 0.45);
          const p1 = this.project(cam, cell.cx + cell.domDir[0] * len, 0.08 + cell.domDir[1] * len * 0.5, cell.cz + cell.domDir[2] * len);
          ctx.strokeStyle = `rgba(${Math.round(55 + errNorm * 195)},${Math.round(220 - errNorm * 135)},${Math.round(235 - errNorm * 165)},0.75)`;
          ctx.lineWidth = 1.25;
          if (p1) {
            ctx.beginPath(); ctx.moveTo(p0[0], p0[1]); ctx.lineTo(p1[0], p1[1]); ctx.stroke();
          }
        }
      }
    }

    // 2) Light Positions, Static(Cyan) vs Dynamic(Gold) Badges, Cube-Grid Pods & Merge Lines
    if (opts.showLights) {
      if (opts.panelRole === 'truth') {
        for (const l of opts.rawLights) {
          const p = this.project(cam, l.pos[0], l.pos[1], l.pos[2]);
          if (!p) continue;
          const isStatic = l.mobility === 'static';
          ctx.fillStyle = isStatic ? '#38d9f5' : '#ffb454';
          ctx.beginPath(); ctx.arc(p[0], p[1], 3.0, 0, Math.PI * 2); ctx.fill();
          ctx.strokeStyle = '#000'; ctx.lineWidth = 0.8; ctx.stroke();
        }
      } else if (opts.panelRole === 'approx') {
        for (const node of opts.approxLights) {
          const pc = this.project(cam, node.pos[0], node.pos[1], node.pos[2]);
          if (!pc) continue;
          const isBad = Boolean(node.isNaiveWarning);
          const isPod = Boolean(node.isCubePod);
          const ringCol = isBad ? '#ff5f56' : isPod ? '#38d9f5' : node.mobility === 'static' ? '#5bdb87' : '#ffb454';

          if (node.members && node.members.length > 1) {
            ctx.save();
            ctx.setLineDash([3, 3]);
            ctx.strokeStyle = isBad ? 'rgba(255,95,86,0.72)' : 'rgba(91,219,135,0.52)';
            ctx.lineWidth = 1.1;
            for (const m of node.members) {
              const pm = this.project(cam, m.pos[0], m.pos[1], m.pos[2]);
              if (!pm) continue;
              ctx.beginPath(); ctx.moveTo(pm[0], pm[1]); ctx.lineTo(pc[0], pc[1]); ctx.stroke();
            }
            ctx.restore();
          }

          ctx.strokeStyle = ringCol;
          ctx.lineWidth = 1.8;
          ctx.beginPath(); ctx.arc(pc[0], pc[1], isPod ? 7.5 : 6.0, 0, Math.PI * 2); ctx.stroke();
          ctx.fillStyle = `rgb(${Math.round(node.color[0] * 255)},${Math.round(node.color[1] * 255)},${Math.round(node.color[2] * 255)})`;
          ctx.beginPath(); ctx.arc(pc[0], pc[1], 3.4, 0, Math.PI * 2); ctx.fill();

          if (node.memberCount > 1) {
            const tag = isBad
              ? `${node.memberCount}→1 CENTER!`
              : isPod
                ? `POD ${node.memberCount}→1`
                : `${node.mobility === 'dynamic' ? 'DYN' : 'STC'} ${node.memberCount}→1`;
            ctx.font = '600 9px "DM Sans", sans-serif';
            const tw = ctx.measureText(tag).width + 8;
            ctx.fillStyle = isBad ? 'rgba(128,22,22,0.88)' : 'rgba(14,18,22,0.86)';
            ctx.fillRect(pc[0] - tw * 0.5, pc[1] - 21, tw, 13);
            ctx.fillStyle = isBad ? '#ffb4ae' : ringCol;
            ctx.fillText(tag, pc[0] - tw * 0.5 + 4, pc[1] - 11.5);
          }
        }
      }
    }

    // 3) Interactive Receiver & Toroidal Clipmap Focus Probe
    if (opts.probePos) {
      const pp = this.project(cam, opts.probePos[0], 0.05, opts.probePos[1]);
      if (pp) {
        ctx.strokeStyle = '#ffb454';
        ctx.lineWidth = 2.0;
        ctx.beginPath(); ctx.arc(pp[0], pp[1], 8.0, 0, Math.PI * 2); ctx.stroke();
        ctx.fillStyle = '#ffb454';
        ctx.beginPath(); ctx.arc(pp[0], pp[1], 2.5, 0, Math.PI * 2); ctx.fill();
        ctx.font = '600 9px "DM Sans", sans-serif';
        ctx.fillStyle = '#ffd699';
        ctx.fillText(`CLIPMAP FOCUS (${opts.probePos[0].toFixed(1)}, ${opts.probePos[1].toFixed(1)})`, pp[0] + 10, pp[1] + 3);
      }
    }
  }
}
