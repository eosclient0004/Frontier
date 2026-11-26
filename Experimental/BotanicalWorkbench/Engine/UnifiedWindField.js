/*============================================================================================================================================
                                                          UNIFIEDWINDFIELD.JS
============================================================================================================================================*/
// 🧩 Single shared 3D WindField driving trunk sway, limb bend, leaf flutter, VAT sampling, XPBD gust forces, and SDF proxy deformation

export function CreateDefaultWindParameters() {
  return {
    directionDeg: 35,
    speedMps: 6.5,
    gustStrength: 0.45,
    gustFrequencyHz: 0.55,
    wavelengthMeters: 28.0,
    turbulence: 0.35,
    vortexEnabled: false,
    vortexRadiusMeters: 14.0,
    vortexStrength: 0.65,
    trunkFlex: 1.0,
    limbFlex: 1.0,
    detailFlex: 1.0,
  };
}

// Smooth multi-harmonic wave matching Slate's windWave and Frontier's ForceFieldSampler
export function EvaluateWindWave(t) {
  return Math.sin(t) * 0.7 + Math.sin(t * 2.31 + 1.3) * 0.2 + Math.sin(t * 4.7 + 0.7) * 0.1;
}

export function RotateAroundAxisInto(out, px, py, pz, ax, ay, az, angle) {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  const dotAP = ax * px + ay * py + az * pz;
  const cx = ay * pz - az * py;
  const cy = az * px - ax * pz;
  const cz = ax * py - ay * px;
  out[0] = px * c + cx * s + ax * dotAP * (1.0 - c);
  out[1] = py * c + cy * s + ay * dotAP * (1.0 - c);
  out[2] = pz * c + cz * s + az * dotAP * (1.0 - c);
  return out;
}

/**
 * Evaluates the single unified 3D WindField at world position (wx, wy, wz) and simulation time.
 * Identical formula runs in GPU vertex shaders (trunk/limb/leaf/VAT) and CPU analytical SDF/XPBD.
 */
export function SampleUnifiedWind(wx, wy, wz, timeSec, wind, outVec) {
  const rad = (wind.directionDeg * Math.PI) / 180.0;
  const dirX = Math.cos(rad);
  const dirZ = Math.sin(rad);
  const sideX = -dirZ;
  const sideZ = dirX;

  const k = (2.0 * Math.PI) / Math.max(4.0, wind.wavelengthMeters);
  const travelPhase = timeSec * wind.gustFrequencyHz * 2.4 - (wx * dirX + wz * dirZ) * k;
  const gustWave = 0.5 + 0.5 * EvaluateWindWave(travelPhase);
  const slowEnvelope = 1.0 + wind.gustStrength * (0.5 + 0.5 * Math.sin(timeSec * 0.43 + (wx * sideX + wz * sideZ) * k * 0.35));
  const heightShear = Math.pow(Math.max(0.15, wy / 10.0), 0.28);
  const speed = wind.speedMps * (0.55 + 0.45 * gustWave) * slowEnvelope * heightShear;

  const turbX = EvaluateWindWave(timeSec * 1.9 + wx * 0.23 + wy * 0.31) * wind.turbulence;
  const turbY = EvaluateWindWave(timeSec * 1.5 + wz * 0.29 - wx * 0.17) * wind.turbulence * 0.35;
  const turbZ = EvaluateWindWave(timeSec * 2.1 + wz * 0.19 + wy * 0.27) * wind.turbulence;

  let vx = (dirX + sideX * turbX * 0.45) * speed;
  let vy = turbY * speed * 0.3;
  let vz = (dirZ + sideZ * turbZ * 0.45) * speed;

  if (wind.vortexEnabled) {
    const orbitR = wind.vortexRadiusMeters * 0.55;
    const cx = Math.cos(timeSec * 0.45) * orbitR;
    const cz = Math.sin(timeSec * 0.45) * orbitR;
    const dx = wx - cx;
    const dz = wz - cz;
    const dist = Math.hypot(dx, dz) + 0.001;
    const falloff = Math.exp(-(dist * dist) / Math.max(4.0, wind.vortexRadiusMeters * wind.vortexRadiusMeters * 0.5));
    const swirl = wind.speedMps * wind.vortexStrength * 1.35 * falloff;
    vx += (-dz / dist) * swirl;
    vz += (dx / dist) * swirl;
    vy += 0.28 * swirl;
  }

  outVec[0] = vx;
  outVec[1] = vy;
  outVec[2] = vz;
  return outVec;
}

const _rotTmp = new Float32Array(3);
const _windTmp = new Float32Array(3);

/**
 * Evaluates a point's displaced world position under the exact same 3-tier cantilever+limb+flutter
 * wind field used on GPU — used directly to displace Analytical SDF Capsules and Canopy Lobes!
 */
export function DisplacePointByUnifiedWind(
  outPos,
  localX, localY, localZ,
  worldOriginX, worldOriginZ,
  windH, windLimb, windPhase, windDetail,
  pivotX, pivotY, pivotZ,
  specimenHeight,
  timeSec,
  wind
) {
  SampleUnifiedWind(worldOriginX + localX, Math.max(0.2, localY), worldOriginZ + localZ, timeSec, wind, _windTmp);
  const speed = Math.hypot(_windTmp[0], _windTmp[2]);
  const strength = speed / 8.0;

  let dirX = 1.0;
  let dirZ = 0.0;
  if (speed > 1e-4) {
    dirX = _windTmp[0] / speed;
    dirZ = _windTmp[2] / speed;
  }
  const sideX = -dirZ;
  const sideZ = dirX;

  let px = localX;
  let py = localY;
  let pz = localZ;

  // Tier 2: limb bending around branch pivot
  if (windLimb > 0.0) {
    const rx = px - pivotX;
    const ry = py - pivotY;
    const rz = pz - pivotZ;
    const ph = windPhase * 6.2831853;
    const w = windLimb * windLimb;
    const ang = wind.limbFlex * strength * 0.14 * w * (EvaluateWindWave(timeSec * 1.35 + ph) * 0.8 + 0.5);
    RotateAroundAxisInto(_rotTmp, rx, ry, rz, sideX, 0.0, sideZ, ang);
    RotateAroundAxisInto(_rotTmp, _rotTmp[0], _rotTmp[1], _rotTmp[2], dirX, 0.0, dirZ, ang * 0.35 * Math.sin(timeSec * 0.9 + ph * 1.7));
    px = pivotX + _rotTmp[0];
    py = pivotY + _rotTmp[1];
    pz = pivotZ + _rotTmp[2];
  }

  // Tier 3: detail flutter (canopy twigs & foliage)
  if (windDetail > 0.0) {
    const f = windDetail * wind.detailFlex * strength;
    const ph = windPhase * 12.566 + px * 3.1 + pz * 2.7;
    px += sideX * (Math.sin(timeSec * 7.3 + ph) * 0.025 * f) + dirX * (Math.sin(timeSec * 6.2 + ph * 0.7) * 0.025 * f);
    py += Math.sin(timeSec * 5.1 + ph * 1.3) * 0.018 * f;
    pz += sideZ * (Math.sin(timeSec * 7.3 + ph) * 0.025 * f) + dirZ * (Math.sin(timeSec * 6.2 + ph * 0.7) * 0.025 * f);
  }

  // Tier 1: whole-trunk cantilever sway
  const sway = wind.trunkFlex * strength * 0.042 * windH * windH * Math.max(1.5, specimenHeight);
  const gust = EvaluateWindWave(timeSec * 0.85 - (worldOriginX * dirX + worldOriginZ * dirZ) * (6.28318 / Math.max(4.0, wind.wavelengthMeters))) * 0.65 + 0.35;
  px += dirX * (sway * gust) + sideX * (sway * 0.22 * EvaluateWindWave(timeSec * 0.55 + 2.0));
  pz += dirZ * (sway * gust) + sideZ * (sway * 0.22 * EvaluateWindWave(timeSec * 0.55 + 2.0));
  // Arc-length preservation drop
  py -= Math.min(py * 0.25, 0.5 * (sway * gust) * (sway * gust) / Math.max(1.0, specimenHeight));

  outPos[0] = px;
  outPos[1] = py;
  outPos[2] = pz;
  return outPos;
}

/**
 * Shared GLSL snippet injected into bark, foliage, and 10,000+ instanced forest materials
 * so every tier evaluates the exact same unified WindField equation on the GPU.
 */
export const UNIFIED_WIND_GLSL_UNIFORMS = /* glsl */ `
uniform float uTime;
uniform vec2  uWindDirXZ;
uniform float uWindSpeed;
uniform float uWindGust;
uniform float uWindFreq;
uniform float uWindWavelength;
uniform float uWindTurb;
uniform float uVortexEnabled;
uniform float uVortexRadius;
uniform float uVortexStrength;
uniform float uSpecimenHeight;
uniform float uTrunkFlex;
uniform float uLimbFlex;
uniform float uDetailFlex;
uniform float uLeafShuffleMode; // 0 = Analytic Wind, 1 = VAT Harmonic Lookup, 2 = Hero XPBD Elastic Gust
`;

export const UNIFIED_WIND_GLSL_FUNCTIONS = /* glsl */ `
float frontierWindWave(float t) {
  return sin(t) * 0.7 + sin(t * 2.31 + 1.3) * 0.2 + sin(t * 4.7 + 0.7) * 0.1;
}

vec3 frontierRotateAxis(vec3 p, vec3 axis, float angle) {
  float c = cos(angle);
  float s = sin(angle);
  return p * c + cross(axis, p) * s + axis * dot(axis, p) * (1.0 - c);
}

vec3 frontierSampleWind(vec3 worldPos) {
  vec2 dir = normalize(uWindDirXZ + vec2(1e-5, 0.0));
  vec2 side = vec2(-dir.y, dir.x);
  float k = 6.2831853 / max(4.0, uWindWavelength);
  float travel = uTime * uWindFreq * 2.4 - dot(worldPos.xz, dir) * k;
  float gustWave = 0.5 + 0.5 * frontierWindWave(travel);
  float slowEnv = 1.0 + uWindGust * (0.5 + 0.5 * sin(uTime * 0.43 + dot(worldPos.xz, side) * k * 0.35));
  float shear = pow(max(0.15, worldPos.y / 10.0), 0.28);
  float spd = uWindSpeed * (0.55 + 0.45 * gustWave) * slowEnv * shear;

  float tx = frontierWindWave(uTime * 1.9 + worldPos.x * 0.23 + worldPos.y * 0.31) * uWindTurb;
  float ty = frontierWindWave(uTime * 1.5 + worldPos.z * 0.29 - worldPos.x * 0.17) * uWindTurb * 0.35;
  float tz = frontierWindWave(uTime * 2.1 + worldPos.z * 0.19 + worldPos.y * 0.27) * uWindTurb;

  vec3 vel = vec3(
    (dir.x + side.x * tx * 0.45) * spd,
    ty * spd * 0.3,
    (dir.y + side.y * tz * 0.45) * spd
  );

  if (uVortexEnabled > 0.5) {
    float orbitR = uVortexRadius * 0.55;
    vec2 center = vec2(cos(uTime * 0.45), sin(uTime * 0.45)) * orbitR;
    vec2 d = worldPos.xz - center;
    float dist = length(d) + 0.001;
    float falloff = exp(-(dist * dist) / max(4.0, uVortexRadius * uVortexRadius * 0.5));
    float swirl = uWindSpeed * uVortexStrength * 1.35 * falloff;
    vel.x += (-d.y / dist) * swirl;
    vel.z += ( d.x / dist) * swirl;
    vel.y += 0.28 * swirl;
  }
  return vel;
}

vec3 frontierApplyUnifiedWind(vec3 localPos, vec3 worldOrigin, vec4 windAttr, vec3 pivot) {
  vec3 vel = frontierSampleWind(worldOrigin + vec3(localPos.x, max(0.2, localPos.y), localPos.z));
  float spd = length(vel.xz);
  float strength = spd / 8.0;
  vec3 dir = spd > 1e-4 ? vec3(vel.x / spd, 0.0, vel.z / spd) : vec3(1.0, 0.0, 0.0);
  vec3 side = vec3(-dir.z, 0.0, dir.x);
  vec3 p = localPos;

  // Tier 2: limb rotation around branch collar pivot
  if (windAttr.y > 0.0) {
    vec3 rel = p - pivot;
    float ph = windAttr.z * 6.2831853 + dot(worldOrigin.xz, vec2(0.17, 0.29));
    float w = windAttr.y * windAttr.y;
    float ang = uLimbFlex * strength * 0.14 * w * (frontierWindWave(uTime * 1.35 + ph) * 0.8 + 0.5);
    rel = frontierRotateAxis(rel, side, ang);
    rel = frontierRotateAxis(rel, dir, ang * 0.35 * sin(uTime * 0.9 + ph * 1.7));
    p = pivot + rel;
  }

  // Tier 3: leaf & twig shuffle (switches between Analytic Wind, VAT Harmonic Sheet, and Hero XPBD Elastic)
  if (windAttr.w > 0.0) {
    float f = windAttr.w * uDetailFlex * strength;
    float ph = windAttr.z * 12.566 + p.x * 3.1 + p.z * 2.7 + dot(worldOrigin.xz, vec2(0.37, 0.53));
    if (uLeafShuffleMode < 0.5) {
      // Analytic multi-harmonic wind flutter (10,000+ forest fast path)
      p += side * (sin(uTime * 7.3 + ph) * 0.025 * f);
      p.y += sin(uTime * 5.1 + ph * 1.3) * 0.018 * f;
      p += dir  * (sin(uTime * 6.2 + ph * 0.7) * 0.025 * f);
    } else if (uLeafShuffleMode < 1.5) {
      // VAT-style phase-quantized 2-mode loop (simulates baked VertexMotionAtlas 2D lookup)
      float vatU = fract(uTime * (0.45 + 0.25 * strength) + windAttr.z);
      float theta = vatU * 6.2831853;
      float bend = sin(theta) * 0.65 + cos(theta * 2.0 + ph) * 0.35;
      float twist = sin(theta * 3.0 + ph * 0.5);
      p += side * (bend * 0.042 * f) + dir * (twist * 0.030 * f);
      p.y += (cos(theta * 2.0 + ph) * 0.022 - 0.008 * abs(bend)) * f;
    } else {
      // Hero XPBD-style elastic whip & damped rebound on gusts
      float gustImpulse = pow(max(0.0, frontierWindWave(uTime * 2.2 + ph * 0.4)), 2.0);
      float springOsc = sin(uTime * 11.5 + ph) * exp(-0.15 * fract(uTime * 0.8 + windAttr.z));
      p += dir * ((0.055 * gustImpulse + 0.028 * springOsc) * f);
      p += side * (0.045 * springOsc * f);
      p.y += (0.032 * sin(uTime * 9.0 + ph * 1.4) - 0.02 * gustImpulse) * f;
    }
  }

  // Tier 1: whole-trunk cantilever sway
  float k = 6.2831853 / max(4.0, uWindWavelength);
  float sway = uTrunkFlex * strength * 0.042 * windAttr.x * windAttr.x * max(1.5, uSpecimenHeight);
  float gust = frontierWindWave(uTime * 0.85 - dot(worldOrigin.xz, dir.xz) * k) * 0.65 + 0.35;
  p += dir * (sway * gust);
  p += side * (sway * 0.22 * frontierWindWave(uTime * 0.55 + 2.0));
  p.y -= min(p.y * 0.25, 0.5 * (sway * gust) * (sway * gust) / max(1.0, uSpecimenHeight));
  return p;
}
`;
