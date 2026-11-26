// LightingGpuViewport.js — Hardware WebGL2 Forward Rasterizer + Analytic SDF Visibility
// Renders crisp triangle surfaces at 60-144 FPS while keeping SDF soft-cone shadows,
// 128x32 Type-C IES LUTs, Unified Photometric Partition PriorityCut, and Progressive PT.

import { IES_PHI_SAMPLES, IES_PROFILE_COUNT, IES_THETA_SAMPLES } from "./PhotometricGoniometry.js";
import {
  buildSinglePylonGeometry,
  buildStaticEnvironmentGeometry,
  buildVehicleGeometry,
} from "./SceneSurfaceBuilder.js";

const SKY_VERTEX_SRC = `#version 300 es
precision highp float;
precision highp int;

const vec2 QUAD_VERTS[3] = vec2[3](
  vec2(-1.0, -1.0),
  vec2( 3.0, -1.0),
  vec2(-1.0,  3.0)
);

uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;
uniform float uTanHalfFov;
uniform float uAspect;

out vec3 vRayDir;

void main() {
  vec2 ndc = QUAD_VERTS[gl_VertexID];
  vRayDir = uCamForward + uCamRight * (ndc.x * uAspect * uTanHalfFov) + uCamUp * (ndc.y * uTanHalfFov);
  gl_Position = vec4(ndc, 0.9999, 1.0);
}
`;

const SKY_FRAGMENT_SRC = `#version 300 es
precision highp float;
precision highp int;

in vec3 vRayDir;
out vec4 fragColor;

uniform int uEnvPreset;
uniform int uDebugLens;
uniform int uIsPathTracer;
uniform int uAccumFrame;
uniform sampler2D uPrevAccumTex;
uniform vec3 uSunDir;
uniform vec3 uSunColor;

vec3 evalSky(vec3 dir) {
  float y = dir.y;
  float sunDot = max(dot(dir, uSunDir), 1e-4);
  if (uEnvPreset == 0) {
    float t = clamp(y * 1.4, 0.0, 1.0);
    float coveRing = exp(-pow((y - 0.28) / 0.12, 2.0)) * 0.26;
    return mix(vec3(0.025, 0.028, 0.036), vec3(0.05, 0.068, 0.095), t) + vec3(0.9, 0.94, 1.05) * coveRing;
  } else if (uEnvPreset == 1) {
    float up = max(y, 1e-4);
    vec3 sky = mix(vec3(0.32, 0.44, 0.52), vec3(0.11, 0.28, 0.68), pow(up, 0.65));
    float sunGlow = pow(sunDot, 22.0) * 1.4 + pow(sunDot, 420.0) * 14.0;
    return (y < 0.0 ? vec3(0.05, 0.08, 0.045) : sky) + uSunColor * sunGlow;
  } else {
    float up = max(y, 1e-4);
    vec3 sky = mix(vec3(0.55, 0.44, 0.36), vec3(0.15, 0.30, 0.62), pow(up, 0.6));
    float sunGlow = pow(sunDot, 16.0) * 1.6 + pow(sunDot, 360.0) * 16.0;
    return (y < 0.0 ? vec3(0.08, 0.075, 0.07) : sky) + uSunColor * sunGlow;
  }
}

void main() {
  vec3 sky = evalSky(normalize(vRayDir));
  if (uDebugLens == 1 || uDebugLens == 2 || uDebugLens == 4) sky *= 0.25;
  if (uIsPathTracer == 1 && uAccumFrame > 0) {
    vec4 prev = texelFetch(uPrevAccumTex, ivec2(gl_FragCoord.xy), 0);
    fragColor = prev + vec4(min(sky, vec3(32.0)), 1.0);
  } else {
    fragColor = vec4(sky, 1.0);
  }
}
`;

const GEOM_VERTEX_SRC = `#version 300 es
precision highp float;
precision highp int;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aMatId;

uniform vec3 uObjPos;
uniform float uObjYaw;
uniform int uIsPylonInstanced;
uniform int uEmissivePylonCount;

uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;
uniform float uTanHalfFov;
uniform float uAspect;
uniform vec2 uPtJitter;

out vec3 vWorldPos;
out vec3 vWorldNormal;
out vec3 vLocalPos;
out float vMatId;

const float TWO_PI = 6.28318530718;

vec2 rotXZ(vec2 p, float yaw) {
  float c = cos(yaw);
  float s = sin(yaw);
  return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

void main() {
  vec3 localP = aPos;
  vec3 worldP;
  vec3 worldN;
  float outMat = aMatId;

  if (uIsPylonInstanced == 1) {
    int idx = gl_InstanceID;
    int ring = idx / 16;
    int sector = idx - ring * 16;
    float rad = 4.25 + float(ring) * 1.2;
    float phase = float(ring) * 0.19635;
    float a = float(sector) * (TWO_PI / 16.0) + phase;
    vec3 center = vec3(cos(a) * rad, 0.40 + 0.10 * sin(float(idx) * 1.7), sin(a) * rad + 0.5);
    worldP = localP + center;
    worldN = aNormal;
    outMat = 20.0 + float(idx);
  } else {
    worldP = vec3(rotXZ(localP.xz, uObjYaw), localP.y).xzy + uObjPos;
    worldN = normalize(vec3(rotXZ(aNormal.xz, uObjYaw), aNormal.y).xzy);
  }

  vWorldPos = worldP;
  vWorldNormal = worldN;
  vLocalPos = localP;
  vMatId = outMat;

  vec3 rel = worldP - uCamPos;
  float vx = dot(rel, uCamRight);
  float vy = dot(rel, uCamUp);
  float vz = dot(rel, uCamForward);

  const float zNear = 0.15;
  const float zFar = 75.0;
  float clipX = vx / max(uTanHalfFov * uAspect, 1e-4) + uPtJitter.x * vz;
  float clipY = vy / max(uTanHalfFov, 1e-4) + uPtJitter.y * vz;
  float clipZ = (vz * (zFar + zNear) - 2.0 * zFar * zNear) / (zFar - zNear);
  gl_Position = vec4(clipX, clipY, clipZ, vz);
}
`;

const BLIT_FRAGMENT_SRC = `#version 300 es
precision highp float;
precision highp int;

in vec2 vUv;
uniform sampler2D uAccumTex;
uniform float uExposure;
out vec4 fragColor;

vec3 acesFilmicToneMap(vec3 x) {
  float a = 2.51;
  float b = 0.03;
  float c = 2.43;
  float d = 0.59;
  float e = 0.14;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
  vec4 acc = texelFetch(uAccumTex, ivec2(gl_FragCoord.xy), 0);
  vec3 col = max(acc.rgb, vec3(0.0)) / max(acc.a, 1.0);
  col *= uExposure;
  col = acesFilmicToneMap(col);
  col = pow(max(col, vec3(1e-5)), vec3(1.0 / 2.2));
  fragColor = vec4(col, 1.0);
}
`;

const BLIT_VERTEX_SRC = `#version 300 es
precision highp float;
precision highp int;
const vec2 QUAD_VERTS[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
out vec2 vUv;
void main() {
  vec2 p = QUAD_VERTS[gl_VertexID];
  vUv = p * 0.5 + 0.5;
  gl_Position = vec4(p, 0.0, 1.0);
}
`;

// Ultra-lean Shared Lighting + Analytical Scene SDF Shadow Core (NO primary march, NO calcNormal!)
const SURFACE_GLSL_COMMON = `#version 300 es
precision highp float;
precision highp int;

in vec3 vWorldPos;
in vec3 vWorldNormal;
in vec3 vLocalPos;
in float vMatId;
out vec4 fragColor;

uniform vec2 uResolution;
uniform vec3 uCamPos;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamForward;

uniform sampler2D uLightRegisterTex;
uniform sampler2D uIesAtlasTex;
uniform sampler2D uPrevAccumTex;

uniform int uLuminaireCount;
uniform int uPartitionCellCount;
uniform int uRenderEngine;
uniform int uSchedulerMode;
uniform int uDebugLens;
uniform int uEnvPreset;
uniform int uMatCapLightBlend;
uniform int uSdfVoxelDefectDemo;
uniform int uAccumFrame;
uniform float uTimeSec;

uniform vec3 uCarPos;
uniform float uCarYaw;
uniform vec3 uPrevCarPos;
uniform float uHeadlampIntensity;
uniform float uDrlActive;
uniform float uBrakeActive;
uniform float uTurnSignalActive;
uniform int uEmissivePylonCount;

uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uEnvSh[9];

const float PI = 3.14159265359;
const float TWO_PI = 6.28318530718;

float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

vec2 r2Sequence(int idx, vec2 shift) {
  return fract(shift + vec2(0.754877666, 0.569840291) * float(idx + 1));
}

float sdBox(vec3 p, vec3 b) {
  vec3 q = abs(p) - b;
  return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

vec2 rotXZ(vec2 p, float yaw) {
  float c = cos(yaw);
  float s = sin(yaw);
  return vec2(c * p.x - s * p.y, s * p.x + c * p.y);
}

// Fast analytical scene SDF used strictly for soft SDF cone shadows, AO & PT bounce!
// IMPORTANT: mapCasterSdf intentionally excludes the floor plane (p.y) so grazing shadow rays never self-band!
float mapCasterSdf(vec3 p) {
  float d = 1e6;
  vec3 cp = p - uCarPos;
  cp.xz = rotXZ(cp.xz, -uCarYaw);
  // Inset slightly longitudinally so front headlamps (z=2.16) & rear tail bar (z=-2.18) never self-occlude on floor!
  d = min(d, sdBox(cp - vec3(0.0, 0.50, -0.02), vec3(0.90, 0.28, 1.98)));
  d = min(d, sdBox(cp - vec3(0.0, 0.94, -0.25), vec3(0.66, 0.22, 0.92)));
  d = min(d, length(p - vec3(-2.55, 0.46, 2.4)) - 0.46);
  d = min(d, length(p - vec3(-2.55, 0.46, 1.0)) - 0.46);
  d = min(d, length(p - vec3(-2.55, 0.46, -0.4)) - 0.46);

  if (uEnvPreset == 1) {
    d = min(d, length(p.xz - vec2(5.6, -2.8)) - 0.28);
    d = min(d, length(p.xz - vec2(-5.8, -2.2)) - 0.25);
    d = min(d, length(p.xz - vec2(6.2, 3.2)) - 0.30);
    d = min(d, length(p.xz - vec2(-6.0, 3.6)) - 0.26);
  } else if (uEnvPreset == 2) {
    d = min(d, sdBox(p - vec3(-6.2, 1.35, -3.5), vec3(0.28, 1.35, 2.0)));
    d = min(d, sdBox(p - vec3(-6.2, 1.35, 5.2), vec3(0.28, 1.35, 2.1)));
    d = min(d, sdBox(p - vec3(-6.2, 3.35, 0.5), vec3(0.24, 0.95, 7.5)));
  }
  return d;
}

float traceExactShadow(vec3 ro, vec3 rd, float maxT) {
  float t = 0.06;
  float res = 1.0;
  float ph = 1e20;
  for (int i = 0; i < 24; ++i) {
    float h = mapCasterSdf(ro + rd * t);
    if (h < 0.002) return 0.0;
    float y = (h * h) / (2.0 * ph);
    float d = sqrt(max(0.0, h * h - y * y));
    res = min(res, 14.0 * d / max(0.001, t - y));
    ph = h;
    t += clamp(h, 0.04, 1.25);
    if (t >= maxT) break;
  }
  return clamp(res, 0.0, 1.0);
}

float traceGdfConeShadow(vec3 ro, vec3 rd, float maxT, float coneSoftness) {
  float t = 0.06;
  float res = 1.0;
  float ph = 1e20;
  for (int i = 0; i < 18; ++i) {
    float h = mapCasterSdf(ro + rd * t);
    if (h < 0.002) return 0.0;
    float y = (h * h) / (2.0 * ph);
    float d = sqrt(max(0.0, h * h - y * y));
    res = min(res, coneSoftness * d / max(0.001, t - y));
    ph = h;
    t += clamp(h * 1.15, 0.06, 1.25);
    if (t >= maxT) break;
  }
  res = clamp(res, 0.0, 1.0);
  return res * res * (3.0 - 2.0 * res);
}

float calcSdfAmbientOcclusion(vec3 pos, vec3 nor) {
  float occ = 0.0;
  float sca = 1.0;
  for (int i = 0; i < 4; ++i) {
    float h = 0.05 + 0.18 * float(i);
    vec3 sp = pos + h * nor;
    float d = min(sp.y, mapCasterSdf(sp));
    occ += max(0.0, h - d) * sca;
    sca *= 0.62;
  }
  return clamp(1.0 - 1.35 * occ, 0.25, 1.0);
}

void fetchLuminaire(int idx, out vec4 t0, out vec4 t1, out vec4 t2, out vec4 t3) {
  int base = idx * 4;
  t0 = texelFetch(uLightRegisterTex, ivec2((base + 0) & 255, (base + 0) >> 8), 0);
  t1 = texelFetch(uLightRegisterTex, ivec2((base + 1) & 255, (base + 1) >> 8), 0);
  t2 = texelFetch(uLightRegisterTex, ivec2((base + 2) & 255, (base + 2) >> 8), 0);
  t3 = texelFetch(uLightRegisterTex, ivec2((base + 3) & 255, (base + 3) >> 8), 0);
}

void fetchPartitionCell(int cellIdx, out vec4 n0, out vec4 n1, out vec4 n2, out vec4 n3) {
  int base = 512 + cellIdx * 4;
  n0 = texelFetch(uLightRegisterTex, ivec2((base + 0) & 255, (base + 0) >> 8), 0);
  n1 = texelFetch(uLightRegisterTex, ivec2((base + 1) & 255, (base + 1) >> 8), 0);
  n2 = texelFetch(uLightRegisterTex, ivec2((base + 2) & 255, (base + 2) >> 8), 0);
  n3 = texelFetch(uLightRegisterTex, ivec2((base + 3) & 255, (base + 3) >> 8), 0);
}

float sampleIesAtlas128x32(int profileOrd, vec3 dirFromLight, vec3 beamAxis, vec3 zeroAzimuth) {
  float cosTheta = clamp(dot(dirFromLight, beamAxis), -0.99999, 0.99999);
  float theta = acos(cosTheta);
  float thetaF = (theta / PI) * 127.0;
  int t0 = clamp(int(floor(thetaF)), 0, 126);
  int t1 = t0 + 1;
  float tf = thetaF - float(t0);

  vec3 orthoCand = abs(dot(zeroAzimuth, beamAxis)) < 0.98 ? zeroAzimuth : vec3(0.0, 0.0, 1.0);
  vec3 right = normalize(orthoCand - beamAxis * dot(orthoCand, beamAxis));
  vec3 up = normalize(cross(beamAxis, right));
  float rx = dot(dirFromLight, right);
  float ry = dot(dirFromLight, up);
  float phi = (abs(rx) + abs(ry) < 1e-5) ? 0.0 : atan(ry, rx);
  if (phi < 0.0) phi += TWO_PI;

  float phiF = (phi / TWO_PI) * 32.0;
  int p0 = int(floor(phiF)) & 31;
  int p1 = (p0 + 1) & 31;
  float pf = phiF - float(int(floor(phiF)));

  int rowBase = clamp(profileOrd, 0, 7) * 32;
  float v00 = texelFetch(uIesAtlasTex, ivec2(t0, rowBase + p0), 0).r;
  float v10 = texelFetch(uIesAtlasTex, ivec2(t1, rowBase + p0), 0).r;
  float v01 = texelFetch(uIesAtlasTex, ivec2(t0, rowBase + p1), 0).r;
  float v11 = texelFetch(uIesAtlasTex, ivec2(t1, rowBase + p1), 0).r;
  return mix(mix(v00, v10, tf), mix(v01, v11, tf), pf);
}

float ggxNdf(float nDotH, float rough) {
  float a = max(rough * rough, 0.002);
  float a2 = a * a;
  float d = (nDotH * nDotH) * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d + 1e-6);
}

float smithGeometry(float nDotV, float nDotL, float rough) {
  float r = rough + 1.0;
  float k = (r * r) * 0.125;
  return (nDotV / (nDotV * (1.0 - k) + k)) * (nDotL / (nDotL * (1.0 - k) + k));
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
  return f0 + (1.0 - f0) * pow(max(1.0 - cosTheta, 1e-5), 5.0);
}

vec3 evalOpenPbrBrdf(
  vec3 N, vec3 V, vec3 L,
  vec3 albedo, float metallic, float rough, float clearcoat, float clearcoatRough,
  float specScale, vec3 specDirOverride
) {
  float nDotL = max(dot(N, L), 0.0);
  if (nDotL <= 0.0) return vec3(0.0);
  float nDotV = max(dot(N, V), 1e-4);

  vec3 diffColor = albedo * (1.0 - metallic) / PI;
  vec3 Ls = normalize(specDirOverride);
  float nDotLs = max(dot(N, Ls), 0.0);
  vec3 H = normalize(V + Ls);
  float nDotH = max(dot(N, H), 0.0);
  float vDotH = max(dot(V, H), 0.0);

  vec3 f0 = mix(vec3(0.04), albedo, metallic);
  vec3 F = fresnelSchlick(vDotH, f0);
  float D = ggxNdf(nDotH, rough);
  float G = smithGeometry(nDotV, nDotLs, rough);
  vec3 baseSpec = (D * G * F) / max(4.0 * nDotV * nDotLs, 1e-4);

  vec3 coatSpec = vec3(0.0);
  float coatAtten = 1.0;
  if (clearcoat > 0.01 && uSdfVoxelDefectDemo == 0) {
    float Dc = ggxNdf(nDotH, clearcoatRough);
    float Gc = smithGeometry(nDotV, nDotLs, clearcoatRough);
    float Fc = fresnelSchlick(vDotH, vec3(0.04)).r * clearcoat;
    coatSpec = vec3(Dc * Gc * Fc) / max(4.0 * nDotV * nDotLs, 1e-4);
    coatAtten = 1.0 - Fc * 0.65;
  }

  return diffColor * (1.0 - F) * coatAtten * nDotL + (baseSpec * coatAtten + coatSpec) * nDotLs * specScale;
}

float smoothRangeWindow(float dist, float rangeMeters) {
  float d2 = dist * dist;
  float r2 = max(rangeMeters * rangeMeters, 0.25);
  float ratio = clamp(1.0 - (d2 / r2) * (d2 / r2), 0.0, 1.0);
  return ratio * ratio;
}

vec3 evalUnifiedLuminaireNoShadow(
  int lumIdx, vec3 P, vec3 N, vec3 V,
  vec3 albedo, float metallic, float rough, float clearcoat,
  out vec3 outShadowDir, out float outShadowDist, out float outLuxNoVis
) {
  vec4 t0, t1, t2, t3;
  fetchLuminaire(lumIdx, t0, t1, t2, t3);
  int cat = int(floor(t2.w + 0.5));
  outShadowDir = vec3(0.0, 1.0, 0.0);
  outShadowDist = 0.0;
  outLuxNoVis = 0.0;

  if (cat == 2 || cat == 4) {
    vec3 center = t0.xyz;
    float halfW = t0.w;
    vec3 upAxis = normalize(t1.xyz);
    float halfH = t1.w;
    vec3 rightAxis = normalize(t2.xyz);
    vec3 emitN = normalize(cross(rightAxis, upAxis));
    vec3 col = t3.rgb;
    bool twoSided = t3.w >= 50.0;
    float collimation = mod(t3.w, 50.0);

    vec3 toCenter = center - P;
    if (!twoSided && dot(emitN, -toCenter) <= 0.0) return vec3(0.0);

    vec3 uVec = rightAxis * halfW;
    vec3 vVec = upAxis * halfH;
    vec3 v0 = normalize(center - uVec - vVec - P);
    vec3 v1 = normalize(center + uVec - vVec - P);
    vec3 v2 = normalize(center + uVec + vVec - P);
    vec3 v3 = normalize(center - uVec + vVec - P);

    vec3 c01 = cross(v0, v1); float l01 = length(c01);
    vec3 c12 = cross(v1, v2); float l12 = length(c12);
    vec3 c23 = cross(v2, v3); float l23 = length(c23);
    vec3 c30 = cross(v3, v0); float l30 = length(c30);
    vec3 vecIrr =
      (l01 > 1e-5 ? c01 / l01 : vec3(0.0)) * acos(clamp(dot(v0, v1), -0.9999, 0.9999)) +
      (l12 > 1e-5 ? c12 / l12 : vec3(0.0)) * acos(clamp(dot(v1, v2), -0.9999, 0.9999)) +
      (l23 > 1e-5 ? c23 / l23 : vec3(0.0)) * acos(clamp(dot(v2, v3), -0.9999, 0.9999)) +
      (l30 > 1e-5 ? c30 / l30 : vec3(0.0)) * acos(clamp(dot(v3, v0), -0.9999, 0.9999));

    float irradFactor = abs(dot(vecIrr, N)) * (0.5 / PI);
    float dist = length(toCenter);
    vec3 L = toCenter / max(dist, 1e-4);
    if (collimation > 0.1) irradFactor *= pow(max(dot(emitN, -L), 1e-4), collimation);

    vec3 R = reflect(-V, N);
    float rDotEn = dot(R, emitN);
    vec3 repPos = center;
    if (abs(rDotEn) > 1e-4) {
      float tPlane = dot(center - P, emitN) / rDotEn;
      if (tPlane > 0.0) {
        vec3 delta = (P + R * tPlane) - center;
        repPos = center + rightAxis * clamp(dot(delta, rightAxis), -halfW, halfW)
                        + upAxis * clamp(dot(delta, upAxis), -halfH, halfH);
      }
    }
    vec3 Lspec = normalize(repPos - P);
    float area = 4.0 * halfW * halfH;
    float areaSolid = area / max(dist * dist + area, 0.08);
    outShadowDir = L;
    outShadowDist = dist;
    outLuxNoVis = irradFactor * max(col.r, max(col.g, col.b));

    float specRough = clamp(rough + sqrt(areaSolid) * 0.18, 0.04, 1.0);
    vec3 specOnly = evalOpenPbrBrdf(N, V, Lspec, albedo, metallic, specRough, clearcoat, 0.045, areaSolid * 2.4, Lspec);
    vec3 pureDiff = albedo * (1.0 - metallic) / PI * max(dot(N, Lspec), 0.0);
    vec3 pureSpec = max(specOnly - pureDiff, vec3(0.0));
    if (uDebugLens == 5) return pureSpec * col;
    return ((albedo * (1.0 - metallic)) * irradFactor + pureSpec) * col;
  }

  vec3 L_eval = vec3(0.0);
  vec3 L_spec = vec3(0.0);
  float effRough = rough;
  float specScale = 1.0;
  float totalScale = 0.0;

  if (cat == 3) {
    vec3 segA = t0.xyz;
    float radius = max(t0.w, 0.015);
    vec3 segB = t1.xyz;
    float rangeM = max(t1.w, 1.0);
    vec3 ab = segB - segA;
    float abLen2 = max(dot(ab, ab), 1e-6);
    vec3 closestPt = segA + ab * clamp(dot(P - segA, ab) / abLen2, 0.0, 1.0);
    vec3 toL = closestPt - P;
    float dist = length(toL);
    L_eval = toL / max(dist, 1e-4);

    vec3 R = reflect(-V, N);
    vec3 L0 = segA - P;
    vec3 Ld = (segB - P) - L0;
    float rDotLd = dot(R, Ld);
    float tSpec = clamp((dot(R, L0) * rDotLd - dot(L0, Ld)) / max(dot(Ld, Ld) - rDotLd * rDotLd, 1e-5), 0.0, 1.0);
    L_spec = normalize(mix(segA, segB, tSpec) - P);

    float atten = smoothRangeWindow(dist, rangeM) / max(dist * dist + radius * radius, 0.04);
    float capsuleNorm = min(1.8, 0.45 + sqrt(abLen2) * 0.55);
    totalScale = atten * capsuleNorm;
    effRough = clamp(rough + clamp(radius / max(dist * 0.45, 0.12), 0.04, 0.24), 0.06, 1.0);
    specScale = clamp(radius / max(dist * 0.16 + radius, 0.05), 0.14, 0.65);
    outShadowDir = L_eval;
    outShadowDist = dist;
  } else {
    vec3 lPos = t0.xyz;
    float rangeM = max(t0.w, 1.0);
    vec3 beamAxis = normalize(t1.xyz);
    float radius = max(t1.w, 0.02);
    vec3 zeroAz = t2.xyz;
    int profileOrd = int(floor(t3.w + 0.5));

    vec3 toL = lPos - P;
    float dist = length(toL);
    L_eval = toL / max(dist, 1e-4);
    L_spec = L_eval;
    float iesW = (cat == 1) ? sampleIesAtlas128x32(profileOrd, -L_eval, beamAxis, zeroAz) : 1.0;
    totalScale = iesW * smoothRangeWindow(dist, rangeM) / max(dist * dist + radius * radius * 0.5, 0.04);
    effRough = clamp(rough + radius / max(2.0 * dist, 0.1), 0.04, 1.0);
    outShadowDir = L_eval;
    outShadowDist = dist;
  }

  vec3 col = t3.rgb;
  outLuxNoVis = max(dot(N, L_eval), 0.0) * totalScale * max(col.r, max(col.g, col.b));
  vec3 brdf = evalOpenPbrBrdf(N, V, L_eval, albedo, metallic, effRough, clearcoat, 0.045, specScale, L_spec);
  if (uDebugLens == 5) {
    vec3 pureDiff = albedo * (1.0 - metallic) / PI * max(dot(N, L_eval), 0.0);
    return max(brdf - pureDiff, vec3(0.0)) * col * totalScale;
  }
  return brdf * col * totalScale;
}

float partitionCellImportance(vec3 P, vec3 N, vec4 n0, vec4 n1, vec4 n2, vec4 n3) {
  float power = n0.w;
  if (power <= 1e-5) return 0.0;
  vec3 center = 0.5 * (n0.xyz + n1.xyz);
  float radius = length(0.5 * (n1.xyz - n0.xyz));
  vec3 delta = P - center;
  float dist2 = max(dot(delta, delta), max(radius * radius * 0.25, 0.12));
  float dist = sqrt(dist2);
  vec3 dirFromCluster = delta / dist;

  float thetaU = asin(clamp(radius / max(dist, radius + 1e-4), 0.0, 0.99999));
  float thetaO = acos(clamp(n2.w, -0.99999, 0.99999));
  float cutoffAngle = acos(clamp(n3.w, -0.99999, 0.99999));
  float theta = acos(clamp(dot(n2.xyz, dirFromCluster), -0.99999, 0.99999));
  float effTheta = max(0.0, theta - thetaO - thetaU);
  float coneWeight = (effTheta >= cutoffAngle) ? 0.02 : max(0.08, cos(effTheta));
  float cosNormal = clamp(dot(N, -dirFromCluster) + thetaU, 0.08, 1.0);
  return (power * coneWeight * cosNormal) / dist2;
}

vec3 evalEnvironmentSky(vec3 dir) {
  float y = dir.y;
  float sunDot = max(dot(dir, uSunDir), 1e-4);
  if (uEnvPreset == 0) {
    float t = clamp(y * 1.4, 0.0, 1.0);
    float coveRing = exp(-pow((y - 0.28) / 0.12, 2.0)) * 0.26;
    return mix(vec3(0.025, 0.028, 0.036), vec3(0.05, 0.068, 0.095), t) + vec3(0.9, 0.94, 1.05) * coveRing;
  } else if (uEnvPreset == 1) {
    float up = max(y, 1e-4);
    vec3 sky = mix(vec3(0.32, 0.44, 0.52), vec3(0.11, 0.28, 0.68), pow(up, 0.65));
    float sunGlow = pow(sunDot, 22.0) * 1.4 + pow(sunDot, 420.0) * 14.0;
    return (y < 0.0 ? vec3(0.05, 0.08, 0.045) : sky) + uSunColor * sunGlow;
  } else {
    float up = max(y, 1e-4);
    vec3 sky = mix(vec3(0.55, 0.44, 0.36), vec3(0.15, 0.30, 0.62), pow(up, 0.6));
    float sunGlow = pow(sunDot, 16.0) * 1.6 + pow(sunDot, 360.0) * 16.0;
    return (y < 0.0 ? vec3(0.08, 0.075, 0.07) : sky) + uSunColor * sunGlow;
  }
}

vec3 evalEnvironmentHarmonics(vec3 N) {
  float x = N.x, y = N.y, z = N.z;
  vec3 res =
    uEnvSh[0] * 0.282095 +
    uEnvSh[1] * (0.488603 * y) +
    uEnvSh[2] * (0.488603 * z) +
    uEnvSh[3] * (0.488603 * x) +
    uEnvSh[4] * (1.092548 * x * y) +
    uEnvSh[5] * (1.092548 * y * z) +
    uEnvSh[6] * (0.315392 * (3.0 * z * z - 1.0)) +
    uEnvSh[7] * (1.092548 * x * z) +
    uEnvSh[8] * (0.546274 * (x * x - y * y));
  return max(res, vec3(0.0));
}

void evalMaterial(
  float matId, vec3 worldP, vec3 localCarP, vec3 N,
  out vec3 albedo, out float metallic, out float rough, out float clearcoat, out vec3 emissive
) {
  albedo = vec3(0.5); metallic = 0.0; rough = 0.45; clearcoat = 0.0; emissive = vec3(0.0);
  if (matId < 1.5) {
    vec2 g = abs(fract(worldP.xz * 0.5) - 0.5);
    float seam = smoothstep(0.015, 0.032, min(g.x, g.y));
    float speckle = hash12(floor(worldP.xz * 22.0)) * 0.035;
    if (uEnvPreset == 1) {
      albedo = mix(vec3(0.11, 0.13, 0.10), vec3(0.16, 0.17, 0.14), seam) + speckle;
      rough = mix(0.46, 0.34, seam);
    } else if (uEnvPreset == 2) {
      albedo = mix(vec3(0.12, 0.125, 0.135), vec3(0.18, 0.185, 0.19), seam) + speckle;
      rough = mix(0.38, 0.26, seam);
    } else {
      albedo = mix(vec3(0.09, 0.095, 0.105), vec3(0.14, 0.145, 0.155), seam) + speckle;
      rough = mix(0.36, 0.25, seam);
    }
  } else if (matId < 2.5) {
    albedo = vec3(0.56, 0.57, 0.59);
    if (worldP.z > 7.1 && abs(worldP.x) < 5.0) {
      float tickY = smoothstep(0.012, 0.025, abs(fract(worldP.y * 2.0) - 0.5));
      float centerLine = smoothstep(0.015, 0.03, abs(worldP.x));
      albedo *= mix(0.65, 1.0, min(tickY, centerLine));
      albedo = mix(vec3(0.70, 0.71, 0.73), albedo, 0.4);
    }
    rough = 0.78;
  } else if (matId < 3.5) {
    albedo = vec3(0.92, 0.94, 0.96); metallic = 1.0; rough = 0.04;
  } else if (matId < 4.5) {
    albedo = vec3(0.84, 0.19, 0.05); metallic = 0.18; rough = 0.24; clearcoat = 1.0;
  } else if (matId < 5.5) {
    albedo = vec3(0.76, 0.76, 0.78); rough = 0.90;
  } else if (matId < 6.5) {
    albedo = vec3(0.18); rough = 0.5;
    if (N.y < -0.5) emissive = vec3(11.0, 11.4, 12.2);
  } else if (matId < 7.5) {
    albedo = vec3(0.15, 0.10, 0.07); rough = 0.86;
  } else if (matId < 10.5) {
    albedo = vec3(0.045, 0.145, 0.34); metallic = 0.72; rough = 0.22; clearcoat = 1.0;
  } else if (matId < 11.5) {
    albedo = vec3(0.03, 0.04, 0.055); metallic = 0.1; rough = 0.035; clearcoat = 1.0;
  } else if (matId < 12.5) {
    albedo = vec3(0.045, 0.046, 0.05); rough = 0.76;
  } else if (matId < 13.5) {
    albedo = vec3(0.76, 0.78, 0.82); metallic = 0.94; rough = 0.16;
  } else if (matId < 14.5) {
    albedo = vec3(0.85); rough = 0.08;
    float turnFlash = (abs(localCarP.x) > 0.76 && uTurnSignalActive > 0.5 && fract(uTimeSec * 2.2) < 0.55) ? 1.0 : 0.0;
    emissive = mix(vec3(16.0, 17.5, 20.0) * max(uDrlActive, uHeadlampIntensity), vec3(14.0, 6.2, 0.4) * turnFlash, turnFlash);
  } else if (matId < 15.5) {
    albedo = vec3(0.24, 0.01, 0.01); rough = 0.12;
    emissive = vec3(9.5, 0.32, 0.18) * (1.0 + uBrakeActive * 3.8);
  } else {
    float hue = fract(floor(matId - 20.0 + 0.5) * 0.27);
    vec3 neon = clamp(abs(fract(hue + vec3(0.0, 0.6667, 0.3333)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    albedo = neon * 0.4;
    emissive = (neon * 0.85 + vec3(0.15)) * 7.5;
  }
}

vec3 infernoPalette(float t) {
  t = clamp(t, 0.0, 1.0);
  if (t < 0.33) return mix(vec3(0.04, 0.05, 0.16), vec3(0.32, 0.09, 0.48), t / 0.33);
  if (t < 0.66) return mix(vec3(0.32, 0.09, 0.48), vec3(0.85, 0.25, 0.22), (t - 0.33) / 0.33);
  return mix(vec3(0.85, 0.25, 0.22), vec3(0.98, 0.88, 0.25), (t - 0.66) / 0.34);
}
`;

const REALTIME_FRAGMENT_SRC =
  SURFACE_GLSL_COMMON +
  `
vec3 evalClusterTailApprox(
  vec4 n0, vec4 n1, vec4 n2, vec4 n3, vec3 P, vec3 N, vec3 albedo, float metallic
) {
  if (uDebugLens == 5) return vec3(0.0);
  vec3 center = 0.5 * (n0.xyz + n1.xyz);
  vec3 delta = P - center;
  float d2 = max(dot(delta, delta), 0.75);
  float dist = sqrt(d2);
  vec3 dirFromCluster = delta / dist;
  float nDotC = clamp(dot(N, -dirFromCluster), 0.0, 1.0);

  // Respect directional beam/emission cone (n2.xyz = axis, n2.w = cosSpread, n3.w = cosCutoff)
  // so spotlights/softboxes fading into the tail never emit fake backward/sideways light onto walls!
  float thetaO = acos(clamp(n2.w, -0.99999, 0.99999));
  float thetaCut = acos(clamp(n3.w, -0.99999, 0.99999));
  float theta = acos(clamp(dot(n2.xyz, dirFromCluster), -0.99999, 0.99999));
  float coneAtten = (thetaCut >= 3.05)
    ? 1.0
    : (1.0 - smoothstep(max(0.0, thetaO * 0.4), min(PI, thetaCut + thetaO + 0.15), theta));
  float rangeRoll = 1.0 / (1.0 + d2 * 0.025);

  return (albedo * (1.0 - metallic) / PI) * (n3.rgb / (4.0 * PI * d2)) * nDotC * coneAtten * rangeRoll * 0.55;
}

vec3 shadePointRealTime(
  vec3 P, vec3 N, vec3 V,
  vec3 albedo, float metallic, float rough, float clearcoat,
  bool doShadows, bool fastSdfShadow, vec2 fragCoord,
  out int outCutEvals, out float outLuxTotal
) {
  outCutEvals = 0;
  outLuxTotal = 0.0;
  vec3 evalP = (uRenderEngine == 1 && uSdfVoxelDefectDemo == 1) ? (floor(P / 0.45) + 0.5) * 0.45 : P;
  vec3 total = vec3(0.0);

  if (uEnvPreset >= 1) {
    float sunVis = 1.0;
    if (doShadows && dot(N, uSunDir) > 0.0) {
      sunVis = fastSdfShadow ? traceGdfConeShadow(P + N * 0.025, uSunDir, 18.0, 10.0)
                             : traceExactShadow(P + N * 0.025, uSunDir, 20.0);
    }
    total += evalOpenPbrBrdf(N, V, uSunDir, albedo, metallic, rough, clearcoat, 0.045, 1.0, uSunDir) * uSunColor * sunVis;
    outLuxTotal += max(dot(N, uSunDir), 0.0) * dot(uSunColor, vec3(0.3, 0.6, 0.1)) * sunVis;
  }

  if (uLuminaireCount <= 0) return total;

  const int MAX_CAND = 16;
  int candIds[16] = int[16](0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  int candCells[16] = int[16](0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  float candScores[16] = float[16](0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  float candWeights[16] = float[16](1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0);
  int candCount = 0;
  int evalCount = 0;
  float candMult = 1.0;
  vec3 tailApprox = vec3(0.0);

  if (uSchedulerMode == 2) {
    // Direct Brute-Force evaluates all luminaires up to 64
    evalCount = min(uLuminaireCount, 64);
  } else if (uRenderEngine == 0) {
    int cap = min(uLuminaireCount, 4);
    for (int i = 0; i < 4; ++i) {
      if (i >= cap) break;
      candIds[i] = i;
      candWeights[i] = 1.0;
      candCount++;
    }
    evalCount = candCount;
  } else if (uSchedulerMode == 3) {
    for (int i = 0; i < 48; ++i) {
      if (i >= uLuminaireCount || candCount >= 8) break;
      vec4 t0, t1, t2, t3;
      fetchLuminaire(i, t0, t1, t2, t3);
      float lViewZ = dot(t0.xyz - uCamPos, uCamForward);
      vec3 toL = normalize(t0.xyz - uCamPos);
      if (lViewZ <= 0.2 || abs(dot(toL, uCamRight)) >= 0.78) continue;
      candIds[candCount] = i;
      candWeights[candCount] = 1.0;
      candCount++;
    }
    evalCount = candCount;
  } else if (uSchedulerMode == 4) {
    float r = hash12(fragCoord + vec2(uTimeSec * 60.0, 17.3));
    candIds[0] = clamp(int(floor(r * float(uLuminaireCount))), 0, uLuminaireCount - 1);
    candWeights[0] = 1.0;
    candCount = 1;
    evalCount = 1;
    candMult = float(uLuminaireCount) * 0.52;
    vec3 ghostDelta = P - uPrevCarPos;
    total += vec3(1.6, 0.14, 0.08) * exp(-dot(ghostDelta, ghostDelta) * 0.55) * length(uCarPos - uPrevCarPos) * 14.0;
  } else {
    int targetBudget = (uSchedulerMode == 1 || uRenderEngine == 1) ? 6 : 10;
    int collectGoal = min(targetBudget + 4, MAX_CAND);
    const int QMAX = 24;
    int qCells[24] = int[24](0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    float qScores[24] = float[24](0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    int qSize = 0;

    if (uPartitionCellCount > 0) {
      vec4 rn0, rn1, rn2, rn3;
      fetchPartitionCell(0, rn0, rn1, rn2, rn3);
      qCells[0] = 0;
      qScores[0] = partitionCellImportance(evalP, N, rn0, rn1, rn2, rn3);
      qSize = 1;
    }

    for (int iter = 0; iter < 48; ++iter) {
      if (qSize <= 0 || candCount >= collectGoal) break;
      int bestSlot = 0;
      float bestScore = qScores[0];
      for (int k = 1; k < QMAX; ++k) {
        if (k >= qSize) break;
        if (qScores[k] > bestScore) { bestScore = qScores[k]; bestSlot = k; }
      }
      int cellIdx = qCells[bestSlot];
      qSize--;
      qCells[bestSlot] = qCells[qSize];
      qScores[bestSlot] = qScores[qSize];

      vec4 n0, n1, n2, n3;
      fetchPartitionCell(cellIdx, n0, n1, n2, n3);
      int link = int(floor(n1.w + 0.5));
      if (link < 0) {
        int lumIdx = -link - 1;
        if (lumIdx >= 0 && lumIdx < uLuminaireCount) {
          candIds[candCount] = lumIdx;
          candCells[candCount] = cellIdx;
          candScores[candCount] = bestScore;
          candCount++;
        }
      } else if (link >= 0 && link + 1 < uPartitionCellCount && qSize + 2 <= QMAX) {
        vec4 ln0, ln1, ln2, ln3, rn0, rn1, rn2, rn3;
        fetchPartitionCell(link, ln0, ln1, ln2, ln3);
        qCells[qSize] = link;
        qScores[qSize] = partitionCellImportance(evalP, N, ln0, ln1, ln2, ln3);
        qSize++;
        fetchPartitionCell(link + 1, rn0, rn1, rn2, rn3);
        qCells[qSize] = link + 1;
        qScores[qSize] = partitionCellImportance(evalP, N, rn0, rn1, rn2, rn3);
        qSize++;
      } else {
        tailApprox += evalClusterTailApprox(n0, n1, n2, n3, evalP, N, albedo, metallic);
      }
    }

    // Sort collected candidates descending so rank cutoff is exact
    for (int i = 0; i < MAX_CAND - 1; ++i) {
      if (i >= candCount - 1) break;
      for (int j = i + 1; j < MAX_CAND; ++j) {
        if (j >= candCount) break;
        if (candScores[j] > candScores[i]) {
          int tmpId = candIds[i]; candIds[i] = candIds[j]; candIds[j] = tmpId;
          int tmpCell = candCells[i]; candCells[i] = candCells[j]; candCells[j] = tmpCell;
          float tmpSc = candScores[i]; candScores[i] = candScores[j]; candScores[j] = tmpSc;
        }
      }
    }

    float maxQScore = 0.0;
    for (int r = 0; r < QMAX; ++r) {
      if (r >= qSize) break;
      maxQScore = max(maxQScore, qScores[r]);
      vec4 n0, n1, n2, n3;
      fetchPartitionCell(qCells[r], n0, n1, n2, n3);
      tailApprox += evalClusterTailApprox(n0, n1, n2, n3, evalP, N, albedo, metallic);
    }

    evalCount = min(candCount, targetBudget);
    float cutFloor = (candCount > targetBudget) ? max(candScores[targetBudget], maxQScore) : maxQScore;

    // Extra candidates beyond targetBudget fold smoothly into tailApprox
    for (int extra = 0; extra < MAX_CAND; ++extra) {
      if (extra < evalCount || extra >= candCount) continue;
      vec4 cn0, cn1, cn2, cn3;
      fetchPartitionCell(candCells[extra], cn0, cn1, cn2, cn3);
      tailApprox += evalClusterTailApprox(cn0, cn1, cn2, cn3, evalP, N, albedo, metallic);
    }

    // Narrow C0/C1 feather ONLY near the cutoff boundary so active high-priority lights retain 100% weight!
    for (int i = 0; i < MAX_CAND; ++i) {
      if (i >= evalCount) break;
      float w = (cutFloor > 1e-6)
        ? smoothstep(cutFloor * 0.96, cutFloor * 1.14, candScores[i])
        : 1.0;
      candWeights[i] = w;
      if (w < 0.999) {
        vec4 cn0, cn1, cn2, cn3;
        fetchPartitionCell(candCells[i], cn0, cn1, cn2, cn3);
        tailApprox += (1.0 - w) * evalClusterTailApprox(cn0, cn1, cn2, cn3, evalP, N, albedo, metallic);
      }
    }
  }

  vec3 shDir0 = vec3(0.0, 1.0, 0.0); float shDist0 = 0.0; vec3 shRad0 = vec3(0.0);
  vec3 shDir1 = vec3(0.0, 1.0, 0.0); float shDist1 = 0.0; vec3 shRad1 = vec3(0.0);
  vec3 shDir2 = vec3(0.0, 1.0, 0.0); float shDist2 = 0.0; vec3 shRad2 = vec3(0.0);
  vec3 shDir3 = vec3(0.0, 1.0, 0.0); float shDist3 = 0.0; vec3 shRad3 = vec3(0.0);
  int shCaptured = 0;

  for (int e = 0; e < 64; ++e) {
    if (e >= evalCount) break;
    int lumIdx = (uSchedulerMode == 2) ? e : candIds[e < MAX_CAND ? e : 0];
    float lumWeight = (uSchedulerMode == 2) ? 1.0 : candWeights[e < MAX_CAND ? e : 0];
    if (lumWeight <= 1e-4) continue;

    vec3 shDir;
    float shDist, luxVal;
    vec3 rad = evalUnifiedLuminaireNoShadow(lumIdx, evalP, N, V, albedo, metallic, rough, clearcoat, shDir, shDist, luxVal) * (candMult * lumWeight);
    outLuxTotal += luxVal * candMult * lumWeight;

    // Cast shadows deterministically on key studio/automotive rig lights (lumIdx < 8) that are >0.35m away
    if (doShadows && lumIdx < 8 && shCaptured < 4 && shDist > 0.35 && max(rad.r, max(rad.g, rad.b)) > 0.012) {
      if (shCaptured == 0) { shDir0 = shDir; shDist0 = shDist; shRad0 = rad; }
      else if (shCaptured == 1) { shDir1 = shDir; shDist1 = shDist; shRad1 = rad; }
      else if (shCaptured == 2) { shDir2 = shDir; shDist2 = shDist; shRad2 = rad; }
      else { shDir3 = shDir; shDist3 = shDist; shRad3 = rad; }
      shCaptured++;
    } else {
      total += rad;
    }
  }

  if (shCaptured > 0) {
    float v0 = fastSdfShadow
      ? traceGdfConeShadow(P + N * 0.025, shDir0, min(shDist0 - 0.32, 14.0), 10.0)
      : traceExactShadow(P + N * 0.025, shDir0, min(shDist0 - 0.32, 15.0));
    total += shRad0 * v0;
  }
  if (shCaptured > 1) {
    float v1 = fastSdfShadow
      ? traceGdfConeShadow(P + N * 0.025, shDir1, min(shDist1 - 0.32, 14.0), 10.0)
      : traceExactShadow(P + N * 0.025, shDir1, min(shDist1 - 0.32, 15.0));
    total += shRad1 * v1;
  }
  if (shCaptured > 2) {
    float v2 = fastSdfShadow
      ? traceGdfConeShadow(P + N * 0.025, shDir2, min(shDist2 - 0.32, 14.0), 10.0)
      : traceExactShadow(P + N * 0.025, shDir2, min(shDist2 - 0.32, 15.0));
    total += shRad2 * v2;
  }
  if (shCaptured > 3) {
    float v3 = fastSdfShadow
      ? traceGdfConeShadow(P + N * 0.025, shDir3, min(shDist3 - 0.32, 14.0), 10.0)
      : traceExactShadow(P + N * 0.025, shDir3, min(shDist3 - 0.32, 15.0));
    total += shRad3 * v3;
  }

  outCutEvals = evalCount;
  return total + tailApprox;
}

void main() {
  vec3 P = vWorldPos;
  vec3 N = normalize(vWorldNormal);
  if (!gl_FrontFacing) N = -N;
  vec3 V = normalize(uCamPos - P);

  vec3 albedo, emissive;
  float metallic, rough, clearcoat;
  evalMaterial(vMatId, P, vLocalPos, N, albedo, metallic, rough, clearcoat, emissive);

  bool isMatCap = (uRenderEngine == 0);
  bool fastSdf = (uRenderEngine == 1);
  bool doSh = !isMatCap;

  int cutEvals;
  float luxTotal;
  vec3 direct = (isMatCap && uMatCapLightBlend == 0)
    ? vec3(0.0)
    : shadePointRealTime(P, N, V, albedo, metallic, rough, clearcoat, doSh, fastSdf, gl_FragCoord.xy, cutEvals, luxTotal);

  vec3 finalRgb = vec3(0.0);
  if (isMatCap) {
    vec2 nv = vec2(dot(N, uCamRight), dot(N, uCamUp));
    float nz = sqrt(max(0.0, 1.0 - min(dot(nv, nv), 1.0)));
    float keyCurve = clamp(dot(normalize(vec3(nv, nz)), normalize(vec3(-0.35, 0.72, 0.6))), 1e-4, 1.0);
    vec3 baseClay = (vMatId > 9.5 && vMatId < 10.5) ? vec3(0.22, 0.34, 0.56) : albedo * 0.68 + vec3(0.14);
    vec3 matcap = baseClay * (0.32 + 0.68 * pow(keyCurve, 1.35))
      + vec3(0.28, 0.32, 0.38) * pow(max(1.0 - nz, 1e-4), 2.6)
      + vec3(pow(keyCurve, 26.0) * 0.45);
    finalRgb = (uMatCapLightBlend == 1)
      ? matcap * 0.68 + evalEnvironmentHarmonics(N) * 0.35 + direct * 0.55 + emissive * 0.5
      : matcap + emissive * 0.5;
  } else {
    float ao = (uRenderEngine == 1 || uRenderEngine == 3) ? calcSdfAmbientOcclusion(P, N) : 1.0;
    float effClearcoat = (uSdfVoxelDefectDemo == 1 && uRenderEngine == 1) ? 0.0 : clearcoat;
    float effRough = (uSdfVoxelDefectDemo == 1 && uRenderEngine == 1) ? max(rough, 0.65) : rough;

    vec3 diffuseIbl = evalEnvironmentHarmonics(N) * (albedo * (1.0 - metallic)) * ao;
    vec3 R = reflect(-V, N);
    vec3 envSpec = mix(evalEnvironmentSky(R), evalEnvironmentHarmonics(R), clamp(effRough * 1.15, 0.0, 1.0));
    float nDotV = max(dot(N, V), 1e-4);
    vec3 specIbl = envSpec * fresnelSchlick(nDotV, mix(vec3(0.04), albedo, metallic)) * ao;
    if (effClearcoat > 0.01) {
      float Fc = fresnelSchlick(nDotV, vec3(0.04)).r * effClearcoat;
      specIbl = specIbl * (1.0 - Fc * 0.65) + evalEnvironmentSky(R) * Fc;
    }

    vec3 radBleed = vec3(0.0);
    if (uRenderEngine == 1 || uRenderEngine == 3) {
      float giStr = (uRenderEngine == 3) ? 1.0 : 0.62;
      float floorFacing = clamp(-N.y * 0.5 + 0.5, 0.0, 1.0);
      float underMask = clamp(length(P.xz - uCarPos.xz) * 0.45, 0.35, 1.0);
      vec3 warmBounce = vec3(0.11, 0.12, 0.13) * exp(-dot(P.xz, P.xz) * 0.08) * floorFacing * underMask;
      if (uEnvPreset >= 1) warmBounce += uSunColor * 0.085 * floorFacing * underMask;
      vec3 carF = vec3(sin(uCarYaw), 0.0, cos(uCarYaw));
      vec3 tailPos = uCarPos - carF * 2.2;
      vec3 nosePos = uCarPos + carF * 2.2;
      vec3 rubyB = vec3(0.85, 0.04, 0.02) * (1.0 + uBrakeActive * 2.2) / (dot(P - tailPos, P - tailPos) + 0.45) * 0.52;
      vec3 drlB = vec3(0.35, 0.42, 0.55) * max(uDrlActive, uHeadlampIntensity) / (dot(P - nosePos, P - nosePos) + 0.45) * 0.45;
      radBleed = (warmBounce + rubyB + drlB) * (albedo * (1.0 - metallic)) * giStr * ao;
    }

    vec3 giOnly = diffuseIbl + radBleed;
    vec3 envGi = (uDebugLens == 5) ? specIbl : (giOnly + specIbl);

    if (uDebugLens == 1) finalRgb = infernoPalette(float(cutEvals) / 10.0);
    else if (uDebugLens == 2) finalRgb = infernoPalette(log2(1.0 + luxTotal * 1.4) / 5.5);
    else if (uDebugLens == 3) finalRgb = vec3(traceGdfConeShadow(P + N * 0.025, normalize(vec3(0.45, 0.85, 0.3)), 14.0, fastSdf ? 8.0 : 26.0) * ao);
    else if (uDebugLens == 4) finalRgb = giOnly * 2.2;
    else if (uDebugLens == 5) finalRgb = direct + envGi + emissive * 0.1;
    else finalRgb = direct + envGi + emissive;
  }

  fragColor = vec4(max(finalRgb, vec3(0.0)), 1.0);
}
`;

const PATHTRACER_FRAGMENT_SRC =
  SURFACE_GLSL_COMMON +
  `
vec3 traceStochasticNee(vec3 P, vec3 N, vec3 V, vec3 albedo, float metallic, float rough, float clearcoat, vec2 xi, out int outEvals) {
  outEvals = 0;
  vec3 total = vec3(0.0);

  if (uEnvPreset >= 1 && dot(N, uSunDir) > 0.0) {
    float vis = traceExactShadow(P + N * 0.025, uSunDir, 18.0);
    total += evalOpenPbrBrdf(N, V, uSunDir, albedo, metallic, rough, clearcoat, 0.045, 1.0, uSunDir) * uSunColor * vis;
  }
  if (uLuminaireCount <= 0) return total;

  for (int c = 0; c < 3; ++c) {
    int cellIdx = 0;
    float pdf = 1.0;
    float u = fract(xi.x + float(c) * 0.33333);
    int pickedLum = -1;

    for (int lvl = 0; lvl < 10; ++lvl) {
      vec4 n0, n1, n2, n3;
      fetchPartitionCell(cellIdx, n0, n1, n2, n3);
      int link = int(floor(n1.w + 0.5));
      if (link < 0) { pickedLum = -link - 1; break; }
      if (link + 1 >= uPartitionCellCount) break;
      vec4 ln0, ln1, ln2, ln3, rn0, rn1, rn2, rn3;
      fetchPartitionCell(link, ln0, ln1, ln2, ln3);
      fetchPartitionCell(link + 1, rn0, rn1, rn2, rn3);
      float wL = max(partitionCellImportance(P, N, ln0, ln1, ln2, ln3), 1e-6);
      float wR = max(partitionCellImportance(P, N, rn0, rn1, rn2, rn3), 1e-6);
      float pL = wL / (wL + wR);
      if (u < pL) { cellIdx = link; pdf *= pL; u /= max(pL, 1e-4); }
      else { cellIdx = link + 1; pdf *= (1.0 - pL); u = (u - pL) / max(1.0 - pL, 1e-4); }
    }

    if (pickedLum >= 0 && pickedLum < uLuminaireCount && pdf > 1e-4) {
      vec3 shDir;
      float shDist, luxVal;
      vec3 rad = evalUnifiedLuminaireNoShadow(pickedLum, P, N, V, albedo, metallic, rough, clearcoat, shDir, shDist, luxVal);
      float vis = (shDist > 0.08 && max(rad.r, max(rad.g, rad.b)) > 0.002)
        ? traceExactShadow(P + N * 0.025, shDir, min(shDist - 0.14, 15.0))
        : 1.0;
      total += (rad * vis) / (pdf * 3.0);
      outEvals++;
    }
  }
  return total;
}

void main() {
  vec3 P = vWorldPos;
  vec3 N = normalize(vWorldNormal);
  if (!gl_FrontFacing) N = -N;
  vec3 V = normalize(uCamPos - P);

  vec3 albedo, emissive;
  float metallic, rough, clearcoat;
  evalMaterial(vMatId, P, vLocalPos, N, albedo, metallic, rough, clearcoat, emissive);

  vec2 shift = vec2(hash12(gl_FragCoord.xy), hash12(gl_FragCoord.xy + 19.7));
  vec2 xi = r2Sequence(uAccumFrame * 3 + 7, shift);
  int evals;
  vec3 rad = traceStochasticNee(P, N, V, albedo, metallic, rough, clearcoat, xi, evals) + emissive;

  vec3 ortho1 = normalize(abs(N.y) < 0.99 ? cross(N, vec3(0.0, 1.0, 0.0)) : cross(N, vec3(1.0, 0.0, 0.0)));
  vec3 ortho2 = cross(N, ortho1);
  float specProb = clamp(mix(0.25, 0.92, max(metallic, clearcoat)), 0.15, 0.92);
  vec2 bXi = r2Sequence(uAccumFrame * 5 + 11, shift);
  vec3 bounceDir, bounceW;
  if (bXi.x < specProb) {
    vec3 R = reflect(-V, N);
    float effR = clearcoat > 0.5 && bXi.y < 0.5 ? 0.045 : rough;
    float phi = bXi.y * TWO_PI;
    bounceDir = normalize(R + (ortho1 * cos(phi) + ortho2 * sin(phi)) * (effR * effR * sqrt(bXi.x / specProb)));
    bounceW = fresnelSchlick(max(dot(N, V), 1e-4), mix(vec3(0.04), albedo, max(metallic, clearcoat * 0.25))) / specProb;
  } else {
    float phi = bXi.y * TWO_PI;
    float u = (bXi.x - specProb) / max(1.0 - specProb, 1e-4);
    bounceDir = normalize(ortho1 * cos(phi) * sqrt(u) + ortho2 * sin(phi) * sqrt(u) + N * sqrt(1.0 - u));
    bounceW = (albedo * (1.0 - metallic)) / (1.0 - specProb);
  }

  float t2 = 0.05;
  bool hit2 = false;
  vec3 ro2 = P + N * 0.025;
  for (int s = 0; s < 20; ++s) {
    vec3 bp = ro2 + bounceDir * t2;
    float d2 = min(max(bp.y, 0.0), mapCasterSdf(bp));
    if (d2 < 0.01) { hit2 = true; break; }
    t2 += clamp(d2, 0.05, 1.2);
    if (t2 > 18.0) break;
  }
  if (!hit2) {
    rad += bounceW * evalEnvironmentSky(bounceDir);
  } else {
    vec3 P2 = ro2 + bounceDir * t2;
    vec3 bAlb = (P2.y < 0.04) ? vec3(0.14) : vec3(0.42);
    rad += bounceW * evalEnvironmentHarmonics(vec3(0.0, 1.0, 0.0)) * bAlb;
  }

  vec3 finalRgb = (uDebugLens == 1) ? infernoPalette(float(evals) / 4.0) : rad;
  vec3 clampedRgb = clamp(finalRgb, vec3(0.0), vec3(32.0));

  if (uAccumFrame > 0) {
    vec4 prev = texelFetch(uPrevAccumTex, ivec2(gl_FragCoord.xy), 0);
    fragColor = prev + vec4(clampedRgb, 1.0);
  } else {
    fragColor = vec4(clampedRgb, 1.0);
  }
}
`;

function compileShader(gl, type, source) {
  const shader = gl.createShader(type);
  if (!shader) throw new Error("createShader failed (WebGL context lost)");
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const info = gl.getShaderInfoLog(shader) || "(empty shader log)";
    gl.deleteShader(shader);
    throw new Error(`GLSL compile failed: ${info}`);
  }
  return shader;
}

function linkProgram(gl, vsSource, fsSource) {
  const vs = compileShader(gl, gl.VERTEX_SHADER, vsSource);
  const fs = compileShader(gl, gl.FRAGMENT_SHADER, fsSource);
  const prog = gl.createProgram();
  gl.attachShader(prog, vs);
  gl.attachShader(prog, fs);
  gl.linkProgram(prog);
  gl.deleteShader(vs);
  gl.deleteShader(fs);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) {
    const log = gl.getProgramInfoLog(prog) || (gl.isContextLost() ? "WebGL context lost during link" : "(empty link log)");
    throw new Error(`Program link failed: ${log}`);
  }
  return prog;
}

function createSurfaceGpuBuffers(gl, geom) {
  const vao = gl.createVertexArray();
  gl.bindVertexArray(vao);

  const posBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);
  gl.bufferData(gl.ARRAY_BUFFER, geom.positions, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 0, 0);

  const norBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, norBuf);
  gl.bufferData(gl.ARRAY_BUFFER, geom.normals, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(1);
  gl.vertexAttribPointer(1, 3, gl.FLOAT, false, 0, 0);

  const matBuf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, matBuf);
  gl.bufferData(gl.ARRAY_BUFFER, geom.matIds, gl.STATIC_DRAW);
  gl.enableVertexAttribArray(2);
  gl.vertexAttribPointer(2, 1, gl.FLOAT, false, 0, 0);

  const idxBuf = gl.createBuffer();
  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, idxBuf);
  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, geom.indices, gl.STATIC_DRAW);

  gl.bindVertexArray(null);
  return { vao, indexCount: geom.indices.length, buffers: [posBuf, norBuf, matBuf, idxBuf] };
}

const SURFACE_UNIFORM_NAMES = [
  "uObjPos", "uObjYaw", "uIsPylonInstanced", "uEmissivePylonCount",
  "uCamPos", "uCamRight", "uCamUp", "uCamForward", "uTanHalfFov", "uAspect", "uPtJitter",
  "uResolution", "uLightRegisterTex", "uIesAtlasTex", "uPrevAccumTex",
  "uLuminaireCount", "uPartitionCellCount", "uRenderEngine", "uSchedulerMode",
  "uDebugLens", "uEnvPreset", "uMatCapLightBlend", "uSdfVoxelDefectDemo",
  "uAccumFrame", "uTimeSec", "uCarPos", "uCarYaw", "uPrevCarPos",
  "uHeadlampIntensity", "uDrlActive", "uBrakeActive", "uTurnSignalActive",
  "uSunDir", "uSunColor", "uEnvSh[0]",
];

export class LightingGpuViewport {
  constructor(canvas) {
    this.canvas = canvas;
    canvas.addEventListener("webglcontextlost", (e) => {
      e.preventDefault();
      this.contextLost = true;
    });
    const gl = canvas.getContext("webgl2", {
      antialias: false,
      alpha: false,
      powerPreference: "high-performance",
    });
    if (!gl) throw new Error("WebGL2 is required for LightingArchitectureLab.");

    this.gl = gl;
    this.contextLost = false;
    this.floatFboExt = gl.getExtension("EXT_color_buffer_float");
    this.halfFloatFboExt = gl.getExtension("EXT_color_buffer_half_float");

    this.skyProg = linkProgram(gl, SKY_VERTEX_SRC, SKY_FRAGMENT_SRC);
    this.rtProg = linkProgram(gl, GEOM_VERTEX_SRC, REALTIME_FRAGMENT_SRC);
    this.ptProg = null;
    this.blitProg = linkProgram(gl, BLIT_VERTEX_SRC, BLIT_FRAGMENT_SRC);
    this.emptyVao = gl.createVertexArray();

    // Build GPU triangle geometry buffers
    this.vehicleSurf = createSurfaceGpuBuffers(gl, buildVehicleGeometry());
    this.pylonSurf = createSurfaceGpuBuffers(gl, buildSinglePylonGeometry());
    this.envSurfs = [
      createSurfaceGpuBuffers(gl, buildStaticEnvironmentGeometry(0)),
      createSurfaceGpuBuffers(gl, buildStaticEnvironmentGeometry(1)),
      createSurfaceGpuBuffers(gl, buildStaticEnvironmentGeometry(2)),
    ];

    this.lightRegisterTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.lightRegisterTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, 256, 16, 0, gl.RGBA, gl.FLOAT, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

    this.iesAtlasTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.iesAtlasTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, IES_THETA_SAMPLES, IES_PHI_SAMPLES * IES_PROFILE_COUNT, 0, gl.RED, gl.FLOAT, null);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

    this.accumWidth = 0;
    this.accumHeight = 0;
    this.accumTextures = [null, null];
    this.accumDepths = [null, null];
    this.accumFbos = [null, null];
    this.accumIndex = 0;
    this.accumFrame = 0;

    this.skyUniforms = this._collectUniforms(this.skyProg, [
      "uCamRight", "uCamUp", "uCamForward", "uTanHalfFov", "uAspect",
      "uEnvPreset", "uDebugLens", "uIsPathTracer", "uAccumFrame", "uPrevAccumTex",
      "uSunDir", "uSunColor",
    ]);
    this.rtUniforms = this._collectUniforms(this.rtProg, SURFACE_UNIFORM_NAMES);
    this.ptUniforms = null;
    this.blitUniforms = {
      uAccumTex: gl.getUniformLocation(this.blitProg, "uAccumTex"),
      uExposure: gl.getUniformLocation(this.blitProg, "uExposure"),
    };
  }

  _collectUniforms(prog, names) {
    const out = {};
    for (const n of names) out[n] = this.gl.getUniformLocation(prog, n);
    return out;
  }

  uploadIesAtlas(floatArray) {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.iesAtlasTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, IES_THETA_SAMPLES, IES_PHI_SAMPLES * IES_PROFILE_COUNT, gl.RED, gl.FLOAT, floatArray);
  }

  uploadLightRegister(texData) {
    const gl = this.gl;
    gl.bindTexture(gl.TEXTURE_2D, this.lightRegisterTex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, 256, 16, gl.RGBA, gl.FLOAT, texData);
  }

  resetAccumulation() {
    this.accumFrame = 0;
  }

  _ensureAccumTargets(w, h) {
    const gl = this.gl;
    if (this.accumWidth === w && this.accumHeight === h) return;

    this.accumWidth = w;
    this.accumHeight = h;
    this.accumFrame = 0;

    const useFloat = Boolean(this.floatFboExt);
    const useHalf = !useFloat && Boolean(this.halfFloatFboExt);
    const internalFmt = useFloat ? gl.RGBA32F : useHalf ? gl.RGBA16F : gl.RGBA8;
    const texType = useFloat ? gl.FLOAT : useHalf ? gl.HALF_FLOAT : gl.UNSIGNED_BYTE;

    for (let i = 0; i < 2; ++i) {
      if (this.accumTextures[i]) gl.deleteTexture(this.accumTextures[i]);
      if (this.accumDepths[i]) gl.deleteRenderbuffer(this.accumDepths[i]);
      if (this.accumFbos[i]) gl.deleteFramebuffer(this.accumFbos[i]);

      const tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, internalFmt, w, h, 0, gl.RGBA, texType, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

      const depthRb = gl.createRenderbuffer();
      gl.bindRenderbuffer(gl.RENDERBUFFER, depthRb);
      gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, w, h);

      const fbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depthRb);

      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE && useFloat && this.halfFloatFboExt) {
        gl.bindTexture(gl.TEXTURE_2D, tex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA16F, w, h, 0, gl.RGBA, gl.HALF_FLOAT, null);
      }
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) {
        gl.bindTexture(gl.TEXTURE_2D, tex);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA8, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      }

      this.accumTextures[i] = tex;
      this.accumDepths[i] = depthRb;
      this.accumFbos[i] = fbo;
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  }

  renderViewport(params) {
    const gl = this.gl;
    if (this.contextLost || gl.isContextLost()) return;

    const renderScale = params.renderScale || 1.0;
    const w = Math.max(240, Math.floor(this.canvas.clientWidth * renderScale));
    const h = Math.max(160, Math.floor(this.canvas.clientHeight * renderScale));
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = w;
      this.canvas.height = h;
    }

    const isPt = params.renderEngine === 4;
    if (isPt && !this.ptProg) {
      this.ptProg = linkProgram(gl, GEOM_VERTEX_SRC, PATHTRACER_FRAGMENT_SRC);
      this.ptUniforms = this._collectUniforms(this.ptProg, SURFACE_UNIFORM_NAMES);
    }

    this._ensureAccumTargets(w, h);
    const readIdx = this.accumIndex;
    const writeIdx = 1 - this.accumIndex;
    const aspect = w / Math.max(h, 1);

    gl.bindFramebuffer(gl.FRAMEBUFFER, this.accumFbos[writeIdx]);
    gl.viewport(0, 0, w, h);

    // 1. Background Sky / Studio Cove Pass (always writes background, disables depth write)
    gl.disable(gl.DEPTH_TEST);
    gl.depthMask(false);
    gl.useProgram(this.skyProg);
    gl.bindVertexArray(this.emptyVao);
    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_2D, this.accumTextures[readIdx]);
    gl.uniform1i(this.skyUniforms.uPrevAccumTex, 2);
    gl.uniform3fv(this.skyUniforms.uCamRight, params.camRight);
    gl.uniform3fv(this.skyUniforms.uCamUp, params.camUp);
    gl.uniform3fv(this.skyUniforms.uCamForward, params.camForward);
    gl.uniform1f(this.skyUniforms.uTanHalfFov, params.tanHalfFov);
    gl.uniform1f(this.skyUniforms.uAspect, aspect);
    gl.uniform1i(this.skyUniforms.uEnvPreset, params.envPreset);
    gl.uniform1i(this.skyUniforms.uDebugLens, params.debugLens);
    gl.uniform1i(this.skyUniforms.uIsPathTracer, isPt ? 1 : 0);
    gl.uniform1i(this.skyUniforms.uAccumFrame, this.accumFrame);
    gl.uniform3fv(this.skyUniforms.uSunDir, params.sunDir);
    gl.uniform3fv(this.skyUniforms.uSunColor, params.sunColor);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    // 2. Forward Surface Geometry Pass with Hardware Depth Test
    gl.enable(gl.DEPTH_TEST);
    gl.depthFunc(gl.LESS);
    gl.depthMask(true);
    gl.clear(gl.DEPTH_BUFFER_BIT);

    const prog = isPt ? this.ptProg : this.rtProg;
    const u = isPt ? this.ptUniforms : this.rtUniforms;
    gl.useProgram(prog);

    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.lightRegisterTex);
    gl.uniform1i(u.uLightRegisterTex, 0);

    gl.activeTexture(gl.TEXTURE1);
    gl.bindTexture(gl.TEXTURE_2D, this.iesAtlasTex);
    gl.uniform1i(u.uIesAtlasTex, 1);

    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_2D, this.accumTextures[readIdx]);
    gl.uniform1i(u.uPrevAccumTex, 2);

    let jx = 0.0, jy = 0.0;
    if (isPt && this.accumFrame > 0) {
      const a1 = 0.754877666, a2 = 0.569840291;
      jx = (((this.accumFrame * a1) % 1) - 0.5) / w;
      jy = (((this.accumFrame * a2) % 1) - 0.5) / h;
    }

    gl.uniform2f(u.uResolution, w, h);
    gl.uniform3fv(u.uCamPos, params.camPos);
    gl.uniform3fv(u.uCamRight, params.camRight);
    gl.uniform3fv(u.uCamUp, params.camUp);
    gl.uniform3fv(u.uCamForward, params.camForward);
    gl.uniform1f(u.uTanHalfFov, params.tanHalfFov);
    gl.uniform1f(u.uAspect, aspect);
    gl.uniform2f(u.uPtJitter, jx, jy);

    gl.uniform1i(u.uLuminaireCount, params.luminaireCount);
    gl.uniform1i(u.uPartitionCellCount, params.partitionCellCount);
    gl.uniform1i(u.uRenderEngine, params.renderEngine);
    gl.uniform1i(u.uSchedulerMode, params.schedulerMode);
    gl.uniform1i(u.uDebugLens, params.debugLens);
    gl.uniform1i(u.uEnvPreset, params.envPreset);
    gl.uniform1i(u.uMatCapLightBlend, params.matCapLightBlend ? 1 : 0);
    gl.uniform1i(u.uSdfVoxelDefectDemo, params.sdfVoxelDefectDemo ? 1 : 0);
    gl.uniform1i(u.uAccumFrame, this.accumFrame);
    gl.uniform1f(u.uTimeSec, params.timeSec);

    gl.uniform3fv(u.uCarPos, params.carPos);
    gl.uniform1f(u.uCarYaw, params.carYaw);
    gl.uniform3fv(u.uPrevCarPos, params.prevCarPos);
    gl.uniform1f(u.uHeadlampIntensity, params.headlampIntensity);
    gl.uniform1f(u.uDrlActive, params.drlActive ? 1.0 : 0.0);
    gl.uniform1f(u.uBrakeActive, params.brakeActive ? 1.0 : 0.0);
    gl.uniform1f(u.uTurnSignalActive, params.turnSignalActive ? 1.0 : 0.0);
    gl.uniform1i(u.uEmissivePylonCount, params.emissivePylonCount);

    gl.uniform3fv(u.uSunDir, params.sunDir);
    gl.uniform3fv(u.uSunColor, params.sunColor);
    gl.uniform3fv(u["uEnvSh[0]"], params.envSh);

    // Draw 2a: Static Lobby Stage (floor, walls, spheres, softbox)
    const envSurf = this.envSurfs[params.envPreset] || this.envSurfs[0];
    gl.uniform3f(u.uObjPos, 0.0, 0.0, 0.0);
    gl.uniform1f(u.uObjYaw, 0.0);
    gl.uniform1i(u.uIsPylonInstanced, 0);
    gl.bindVertexArray(envSurf.vao);
    gl.drawElements(gl.TRIANGLES, envSurf.indexCount, gl.UNSIGNED_SHORT, 0);

    // Draw 2b: Hero GT Coupe
    gl.uniform3fv(u.uObjPos, params.carPos);
    gl.uniform1f(u.uObjYaw, params.carYaw);
    gl.uniform1i(u.uIsPylonInstanced, 0);
    gl.bindVertexArray(this.vehicleSurf.vao);
    gl.drawElements(gl.TRIANGLES, this.vehicleSurf.indexCount, gl.UNSIGNED_SHORT, 0);

    // Draw 2c: Instanced Emissive Light Pylons (0..48 in 1 GPU call!)
    if (params.emissivePylonCount > 0) {
      gl.uniform3f(u.uObjPos, 0.0, 0.0, 0.0);
      gl.uniform1f(u.uObjYaw, 0.0);
      gl.uniform1i(u.uIsPylonInstanced, 1);
      gl.bindVertexArray(this.pylonSurf.vao);
      gl.drawElementsInstanced(gl.TRIANGLES, this.pylonSurf.indexCount, gl.UNSIGNED_SHORT, 0, Math.min(48, params.emissivePylonCount));
    }

    // 3. Tone-mapped ACES + Gamma Blit to Canvas
    gl.disable(gl.DEPTH_TEST);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.viewport(0, 0, w, h);
    gl.useProgram(this.blitProg);
    gl.bindVertexArray(this.emptyVao);
    gl.activeTexture(gl.TEXTURE2);
    gl.bindTexture(gl.TEXTURE_2D, null);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, this.accumTextures[writeIdx]);
    gl.uniform1i(this.blitUniforms.uAccumTex, 0);
    gl.uniform1f(this.blitUniforms.uExposure, params.exposure);
    gl.drawArrays(gl.TRIANGLES, 0, 3);

    if (!this._checkedFirstFrameError) {
      this._checkedFirstFrameError = true;
      const err = gl.getError();
      if (err !== gl.NO_ERROR) {
        console.error("WebGL2 error in LightingGpuViewport:", err);
      }
    }

    this.accumIndex = writeIdx;
    this.accumFrame = isPt ? Math.min(this.accumFrame + 1, 256) : 0;
  }
}
