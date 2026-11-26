/*============================================================================================================================================
                                                        WEBERPENNGENERATOR.JS
============================================================================================================================================*/
// 🧩 Weber & Penn (SIGGRAPH 1995) botanical skeleton, cylindrical UV bark builder, foliage frond emitter, and analytical SDF proxy extractor

import { CrownEnvelope } from './SpeciesPresets.js';

const DEG2RAD = Math.PI / 180.0;

class SeededPrng {
  constructor(seed) {
    this.s = (seed >>> 0) || 1;
  }
  next() {
    let t = (this.s += 0x6d2b79f5);
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296.0;
  }
  signed() {
    return this.next() * 2.0 - 1.0;
  }
  vary(base, variance) {
    return base + this.signed() * variance;
  }
}

function v3(x = 0, y = 0, z = 0) {
  return [x, y, z];
}
function addScaled(a, b, s) {
  return [a[0] + b[0] * s, a[1] + b[1] * s, a[2] + b[2] * s];
}
function cross(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}
function dot(a, b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
function norm(a) {
  const l = Math.hypot(a[0], a[1], a[2]);
  return l > 1e-8 ? [a[0] / l, a[1] / l, a[2] / l] : [0, 1, 0];
}
function rotAxis(v, axis, angle) {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  const d = dot(axis, v);
  const cr = cross(axis, v);
  return [
    v[0] * c + cr[0] * s + axis[0] * d * (1.0 - c),
    v[1] * c + cr[1] * s + axis[1] * d * (1.0 - c),
    v[2] * c + cr[2] * s + axis[2] * d * (1.0 - c),
  ];
}

function EvaluateCrownEnvelope(shape, ratio) {
  const r = Math.max(0.0, Math.min(1.0, ratio));
  switch (shape) {
    case CrownEnvelope.Conical:
      return 0.2 + 0.8 * r;
    case CrownEnvelope.Spherical:
      return 0.2 + 0.8 * Math.sin(Math.PI * r);
    case CrownEnvelope.Hemispherical:
      return 0.2 + 0.8 * Math.sin(0.5 * Math.PI * r);
    case CrownEnvelope.Cylindrical:
      return 1.0;
    case CrownEnvelope.TaperedCylindrical:
      return 0.5 + 0.5 * r;
    case CrownEnvelope.Flame:
      return r <= 0.7 ? r / 0.7 : (1.0 - r) / 0.3;
    case CrownEnvelope.InverseConical:
      return 1.0 - 0.8 * r;
    case CrownEnvelope.TendFlame:
      return r <= 0.7 ? 0.5 + 0.5 * (r / 0.7) : 0.5 + 0.5 * ((1.0 - r) / 0.3);
    default:
      return 0.2 + 0.8 * Math.sin(0.5 * Math.PI * r);
  }
}

function EvaluateStemRadius(baseRadius, t, taper, flare, level, nodeSwell, nodeSpacing) {
  let r;
  if (taper <= 1.0) {
    r = baseRadius * (1.0 - taper * t);
  } else if (taper <= 2.0) {
    const z2 = (1.0 - t) * baseRadius;
    const sphere = Math.sqrt(Math.max(0.0, 1.0 - Math.pow((t - (1.0 - (taper - 1.0))) / Math.max(0.05, taper - 1.0), 2.0)));
    r = t < 1.0 - (taper - 1.0) ? baseRadius * (1.0 - 0.15 * t) : Math.max(0.004, baseRadius * sphere);
    if (!Number.isFinite(r)) r = z2;
  } else {
    const k = Math.max(0.1, taper - 2.0);
    r = baseRadius * (1.0 - 0.65 * t) * (1.0 + 0.18 * Math.sin(t * Math.PI * 6.0 * k));
  }
  if (level === 0 && flare > 0.0) {
    const y = Math.max(0.0, 1.0 - 8.0 * t);
    r *= 1.0 + flare * (Math.pow(100.0, y) - 1.0) / 100.0;
  }
  if (level === 0 && nodeSwell > 0.0 && nodeSpacing > 0.005) {
    const phase = (t / nodeSpacing) % 1.0;
    const ridge = Math.exp(-Math.pow((phase - 0.5) * 14.0, 2.0));
    r *= 1.0 + nodeSwell * ridge;
  }
  return Math.max(0.004, r);
}

/**
 * Generates a full botanical specimen:
 * - Hierarchical Weber-Penn stems + buttress roots
 * - Continuous cylindrical-UV bark surface + foliage leaf quads
 * - Per-vertex 3-tier wind weights & limb pivots
 * - Analytical SDF capsules (trunk/boughs) + soft-union canopy spheres
 */
export function GenerateBotanicalSpecimen(preset, options = {}) {
  const tStart = performance.now();
  const botany = preset.botany;
  const roots = preset.roots || { enabled: true, count: 5, length: 0.2, radius: 0.4 };
  const seed = options.seed ?? 1337;
  const lodTier = options.lodTier ?? 0; // 0=Hero, 1=Mid, 2=Far, 3=Forest Cluster
  const uvScale = options.uvScale ?? 1.0;
  const rng = new SeededPrng(seed);

  const specimenScale = Math.max(0.8, rng.vary(botany.scale, botany.scaleV * 0.35));
  const maxLevels = Math.min(lodTier >= 2 ? 2 : lodTier === 1 ? 3 : 4, Math.max(1, Math.round(botany.levels)));
  const stems = [];
  const leafList = [];
  const perLevelCount = [0, 0, 0, 0];

  // Budget caps keep interactive generation fast (< 25ms) even on dense oaks & 4-level conifers
  const maxStemsBudget = lodTier === 0 ? 480 : lodTier === 1 ? 180 : lodTier === 2 ? 55 : 22;
  const maxLeavesBudget = lodTier === 0 ? 4200 : lodTier === 1 ? 1400 : lodTier === 2 ? 420 : 120;

  function growStem(level, origin, dir, right, length, baseRadius, pivot, limbReach, phase, stemRole = 'shoot') {
    if (stems.length >= maxStemsBudget || length < 0.04) return;
    const lv = Math.min(3, level);
    const resBase = Math.max(2, Math.round(botany.curveRes[lv] || 4));
    const segCount = Math.max(2, Math.round(resBase * (lodTier === 0 ? 1.0 : lodTier === 1 ? 0.72 : 0.5)));
    const segLen = length / segCount;

    const curveDeg = botany.curve[lv] || 0;
    const curveBackDeg = botany.curveBack[lv] || 0;
    const curveV = botany.curveV[lv] || 0;
    const bendV = botany.bendV[lv] || 0;
    const taper = botany.taper[lv] ?? 1.0;

    let curPos = [...origin];
    let curDir = norm(dir);
    let curRight = norm(right);
    // Re-orthonormalize right
    curRight = norm(cross(curDir, cross(curRight, curDir)));
    if (Math.hypot(curRight[0], curRight[1], curRight[2]) < 1e-4) {
      curRight = norm(cross(curDir, Math.abs(curDir[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0]));
    }

    const stations = [];
    const stemId = stems.length;
    const stemRecord = {
      id: stemId,
      level,
      role: stemRole,
      length,
      baseRadius,
      pivot: [...pivot],
      limbReach: Math.max(0.25, limbReach),
      phase,
      stations,
    };
    stems.push(stemRecord);
    if (stemRole === 'shoot') perLevelCount[lv]++;

    let splitError = 0;
    const splitRate = stemRole === 'shoot' ? (botany.segSplits[lv] || 0) : 0;

    for (let i = 0; i <= segCount; i++) {
      const t = i / segCount;
      const r = EvaluateStemRadius(
        baseRadius,
        t,
        taper,
        stemRole === 'shoot' ? botany.flare : 0.0,
        stemRole === 'shoot' ? level : 1,
        botany.nodeSwell || 0,
        botany.nodeSpacing || 0.05
      );
      stations.push({ pos: [...curPos], dir: [...curDir], right: [...curRight], radius: r, t, s: t * length });
      if (i === segCount) break;

      // Curvature (with S-curve curveBack support)
      let segPitchDeg = curveBackDeg === 0
        ? curveDeg / segCount
        : (t < 0.5 ? (2.0 * curveDeg) / segCount : (2.0 * curveBackDeg) / segCount);
      segPitchDeg += rng.signed() * (curveV / segCount) * 0.55;
      const segRollDeg = rng.signed() * (bendV / segCount) * 0.65;

      curDir = norm(rotAxis(curDir, curRight, segPitchDeg * DEG2RAD));
      curRight = norm(rotAxis(curRight, curDir, segRollDeg * DEG2RAD));

      // Vertical tropism (attractionUp) + canopy flattening (acacia)
      if (stemRole === 'shoot' && level >= 1) {
        const upBias = (botany.attractionUp || 0) * (level >= 2 ? 0.14 : 0.055);
        if (Math.abs(upBias) > 1e-4) {
          curDir = norm([curDir[0], curDir[1] + upBias, curDir[2]]);
        }
        if ((botany.flatten || 0) > 0.01 && level >= 2) {
          curDir = norm([curDir[0], curDir[1] * (1.0 - 0.35 * botany.flatten), curDir[2]]);
        }
        curRight = norm(cross(curDir, cross(curRight, curDir)));
      }

      // Probabilistic stem split (dichotomous fork)
      if (splitRate > 0 && i > 0 && i < segCount - 1 && stems.length < maxStemsBudget - 8) {
        splitError += splitRate / segCount;
        if (splitError >= 0.65) {
          splitError -= 1.0;
          const splitAng = rng.vary(botany.splitAngle[lv] || 30, botany.splitAngleV[lv] || 6) * DEG2RAD;
          const forkDir = norm(rotAxis(curDir, curRight, splitAng * 0.7));
          curDir = norm(rotAxis(curDir, curRight, -splitAng * 0.45));
          const remLen = length * (1.0 - t) * 0.92;
          const forkRadius = r * 0.78;
          growStem(level, curPos, forkDir, curRight, remLen, forkRadius, level === 0 ? curPos : pivot, limbReach, rng.next(), stemRole);
        }
      }

      curPos = addScaled(curPos, curDir, segLen);
    }

    // Spawn side branches at next recursion level
    if (stemRole === 'shoot' && level + 1 < maxLevels && stems.length < maxStemsBudget) {
      const childLv = level + 1;
      const rawCount = Math.round((botany.branches[childLv] || 0) * (lodTier === 0 ? 0.72 : lodTier === 1 ? 0.45 : 0.3));
      const childCount = Math.min(rawCount, Math.max(2, Math.floor((maxStemsBudget - stems.length) / Math.max(1, 3 - level))));
      const bareBase = Math.max(0.0, Math.min(0.88, botany.baseSize[lv] || 0.05));
      let azimuth = rng.next() * Math.PI * 2.0;

      for (let c = 0; c < childCount; c++) {
        if (stems.length >= maxStemsBudget) break;
        const frac = childCount > 1 ? c / (childCount - 1) : 0.5;
        const offset = Math.max(0.0, Math.min(0.98, bareBase + (1.0 - bareBase) * Math.max(0.0, Math.min(0.98, frac + rng.signed() * 0.04))));
        const stIdx = Math.max(0, Math.min(stations.length - 2, Math.floor(offset * (stations.length - 1))));
        const localFrac = offset * (stations.length - 1) - stIdx;
        const sA = stations[stIdx];
        const sB = stations[stIdx + 1];
        const attachPos = [
          sA.pos[0] + (sB.pos[0] - sA.pos[0]) * localFrac,
          sA.pos[1] + (sB.pos[1] - sA.pos[1]) * localFrac,
          sA.pos[2] + (sB.pos[2] - sA.pos[2]) * localFrac,
        ];
        const parentDir = norm([
          sA.dir[0] + (sB.dir[0] - sA.dir[0]) * localFrac,
          sA.dir[1] + (sB.dir[1] - sA.dir[1]) * localFrac,
          sA.dir[2] + (sB.dir[2] - sA.dir[2]) * localFrac,
        ]);
        const parentRight = norm(sA.right);
        const parentRad = sA.radius + (sB.radius - sA.radius) * localFrac;

        const rotDeg = botany.rotate[childLv] || 137.5;
        const rotStep = rotDeg < 0
          ? (c % 2 === 0 ? 1 : -1) * Math.abs(rotDeg)
          : rng.vary(rotDeg, botany.rotateV[childLv] || 15);
        azimuth += rotStep * DEG2RAD;

        const downBase = botany.downAngle[childLv] || 55;
        const downVar = botany.downAngleV[childLv] || 10;
        const downDeg = downVar < 0
          ? downBase + Math.abs(downVar) * (1.0 - 2.0 * ((offset - bareBase) / Math.max(0.05, 1.0 - bareBase)))
          : rng.vary(downBase, downVar);

        const radialAxis = norm(rotAxis(parentRight, parentDir, azimuth));
        const pitchAxis = norm(cross(parentDir, radialAxis));
        const childDir = norm(rotAxis(parentDir, pitchAxis, downDeg * DEG2RAD));

        const envScale = level === 0
          ? EvaluateCrownEnvelope(botany.shape, (offset - bareBase) / Math.max(0.05, 1.0 - bareBase))
          : 1.0 - 0.55 * offset;
        const relLen = Math.max(0.08, rng.vary(botany.length[childLv] || 0.4, botany.lengthV[childLv] || 0.05));
        const childLen = length * relLen * Math.max(0.2, envScale);
        const childRad = Math.min(
          parentRad * 0.78,
          baseRadius * Math.pow(Math.max(0.05, childLen / Math.max(0.05, length)), botany.ratioPower || 1.25) * (botany.radiusMod[childLv] || 1.0)
        );
        const childPivot = level === 0 ? attachPos : pivot;
        const childReach = level === 0 ? childLen : limbReach;
        growStem(childLv, attachPos, childDir, radialAxis, childLen, childRad, childPivot, childReach, rng.next(), 'shoot');
      }
    }

    // Spawn foliage fronds on terminal (or penultimate) stems
    const isLeafCarrier = stemRole === 'shoot' && (level === maxLevels - 1 || (maxLevels >= 3 && level === maxLevels - 2));
    if (isLeafCarrier && botany.leaves > 0 && leafList.length < maxLeavesBudget) {
      const tuft = Math.max(0.12, Math.min(1.0, botany.leafTuft ?? 1.0));
      const leafDensity = level === maxLevels - 1 ? 1.0 : 0.45;
      const targetLeaves = Math.min(
        Math.round(botany.leaves * leafDensity * (lodTier === 0 ? 0.7 : lodTier === 1 ? 0.42 : 0.25)),
        maxLeavesBudget - leafList.length
      );
      const leafSize = Math.max(0.12, botany.leafScale * specimenScale * 0.19 * (lodTier >= 2 ? 1.65 : 1.15));
      for (let l = 0; l < targetLeaves; l++) {
        const u = Math.max(0.0, Math.min(1.0, 1.0 - tuft + tuft * (targetLeaves > 1 ? l / (targetLeaves - 1) : 1.0)));
        const stIdx = Math.max(0, Math.min(stations.length - 2, Math.floor(u * (stations.length - 1))));
        const frac = u * (stations.length - 1) - stIdx;
        const sA = stations[stIdx];
        const sB = stations[stIdx + 1];
        const pos = [
          sA.pos[0] + (sB.pos[0] - sA.pos[0]) * frac,
          sA.pos[1] + (sB.pos[1] - sA.pos[1]) * frac,
          sA.pos[2] + (sB.pos[2] - sA.pos[2]) * frac,
        ];
        const ang = l * 2.39996 + phase * 6.283;
        const tilt = (rng.next() * 0.8 + 0.25) * Math.PI * 0.5;
        const lDir = norm([
          Math.cos(ang) * Math.sin(tilt),
          Math.cos(tilt) * 0.65 + (botany.attractionUp || 0) * 0.15,
          Math.sin(ang) * Math.sin(tilt),
        ]);
        const lSide = norm(cross(lDir, Math.abs(lDir[1]) < 0.9 ? [0, 1, 0] : [1, 0, 0]));
        const lNorm = norm(cross(lSide, lDir));
        leafList.push({
          pos,
          dir: lDir,
          side: lSide,
          normal: lNorm,
          scaleY: leafSize * rng.vary(1.0, 0.22),
          scaleX: leafSize * (botany.leafScaleX || 0.85) * rng.vary(1.0, 0.2),
          stemId,
          pivot,
          phase: rng.next(),
          limbW: Math.min(1.0, Math.hypot(pos[0] - pivot[0], pos[1] - pivot[1], pos[2] - pivot[2]) / Math.max(0.4, limbReach)),
        });
      }
    }
  }

  // Grow primary trunk(s) (supporting baseSplits for multi-stem palms/trees/desert shrubs)
  const trunkLen = specimenScale * (botany.length[0] || 1.0);
  const trunkRad = trunkLen * (botany.ratio || 0.02) * (botany.radiusMod[0] || 1.0);
  const baseSplits = Math.max(0, Math.round(botany.baseSplits || 0));
  const trunkCount = Math.min(6, baseSplits + 1);
  for (let b = 0; b < trunkCount; b++) {
    const az = (b / trunkCount) * Math.PI * 2.0 + rng.signed() * 0.2;
    const lean = trunkCount > 1 ? 0.18 + rng.next() * 0.15 : 0.03;
    const tDir = norm([Math.cos(az) * lean, 1.0, Math.sin(az) * lean]);
    const tRight = norm([-Math.sin(az), 0.0, Math.cos(az)]);
    const rScale = trunkCount > 1 ? 0.72 : 1.0;
    growStem(0, [0, 0, 0], tDir, tRight, trunkLen * (b === 0 ? 1.0 : rng.vary(0.88, 0.1)), trunkRad * rScale, [0, 0, 0], trunkLen, rng.next(), 'shoot');
  }

  // Grow flared buttress roots around trunk base
  let rootStemCount = 0;
  if (roots.enabled !== false && (roots.count || 0) > 0 && lodTier <= 1) {
    const rCount = Math.min(8, Math.max(3, Math.round(roots.count || 5)));
    for (let r = 0; r < rCount; r++) {
      const az = (r / rCount) * Math.PI * 2.0 + rng.signed() * 0.35;
      const rLen = specimenScale * (roots.length || 0.2) * rng.vary(1.0, 0.18);
      const rRad = trunkRad * (roots.radius || 0.42) * rng.vary(1.0, 0.15);
      const startH = Math.min(trunkLen * 0.08, trunkRad * 1.4);
      const rOrigin = [Math.cos(az) * trunkRad * 0.55, startH, Math.sin(az) * trunkRad * 0.55];
      const rDir = norm([Math.cos(az), -0.42, Math.sin(az)]);
      const rRight = norm([-Math.sin(az), 0, Math.cos(az)]);
      // Build a ground-hugging root polyline
      const stations = [];
      const segs = 5;
      let p = [...rOrigin];
      let d = [...rDir];
      for (let s = 0; s <= segs; s++) {
        const t = s / segs;
        const yFloor = Math.max(-0.18, startH * Math.pow(1.0 - t, 1.8) - 0.14 * t * t);
        p[1] = yFloor;
        stations.push({ pos: [...p], dir: [...d], right: [...rRight], radius: Math.max(0.015, rRad * (1.0 - 0.85 * t)), t, s: t * rLen });
        const step = rLen / segs;
        const wiggle = rng.signed() * 0.22;
        d = norm([d[0] + rRight[0] * wiggle, -0.15, d[2] + rRight[2] * wiggle]);
        p = [p[0] + d[0] * step, yFloor, p[2] + d[2] * step];
      }
      stems.push({
        id: stems.length,
        level: 0,
        role: 'root',
        length: rLen,
        baseRadius: rRad,
        pivot: [0, 0, 0],
        limbReach: 1.0,
        phase: 0,
        stations,
      });
      rootStemCount++;
    }
  }

  // Compute specimen bounding height for normalized wind weight
  let maxY = 1.0;
  for (const st of stems) {
    for (const pt of st.stations) {
      if (pt.pos[1] > maxY) maxY = pt.pos[1];
    }
  }

  // Build Bark Buffer Arrays (Cylindrical UV unwrap + 3-tier wind + stem/cluster tags)
  const barkPositions = [];
  const barkNormals = [];
  const barkUvs = [];
  const barkWind = [];
  const barkPivots = [];
  const barkClusterIds = [];
  const barkStemMeta = []; // [stemId, heightNorm]
  const barkIndices = [];

  for (const st of stems) {
    const radialSegs = st.role === 'root'
      ? 6
      : st.level === 0
        ? (lodTier === 0 ? 12 : lodTier === 1 ? 8 : 6)
        : st.level === 1
          ? (lodTier === 0 ? 8 : 6)
          : (lodTier === 0 ? 5 : 4);
    const baseVertex = barkPositions.length / 3;
    const uWrapRepeat = Math.max(1.0, Math.round((2.0 * Math.PI * st.baseRadius) * 0.8 * uvScale));
    const vLenRepeat = 0.55 * uvScale;

    for (let i = 0; i < st.stations.length; i++) {
      const pt = st.stations[i];
      const forward = pt.dir;
      const sideA = pt.right;
      const sideB = norm(cross(forward, sideA));
      const hNorm = Math.max(0.0, Math.min(1.0, pt.pos[1] / maxY));
      const limbW = st.role === 'root'
        ? 0.0
        : st.level === 0
          ? 0.0
          : Math.min(1.0, Math.hypot(pt.pos[0] - st.pivot[0], pt.pos[1] - st.pivot[1], pt.pos[2] - st.pivot[2]) / st.limbReach);
      const detailW = st.role === 'root' ? 0.0 : st.level >= 2 ? 0.45 * pt.t : 0.0;

      for (let r = 0; r <= radialSegs; r++) {
        const frac = r / radialSegs;
        const theta = frac * Math.PI * 2.0;
        const ct = Math.cos(theta);
        const stSin = Math.sin(theta);
        const nx = sideA[0] * ct + sideB[0] * stSin;
        const ny = sideA[1] * ct + sideB[1] * stSin;
        const nz = sideA[2] * ct + sideB[2] * stSin;
        // Subtle root buttress fluting on lower trunk
        let rad = pt.radius;
        if (st.role === 'shoot' && st.level === 0 && pt.t < 0.25 && (botany.flare || 0) > 0.2) {
          const lobeW = Math.pow(1.0 - pt.t / 0.25, 2.0);
          rad *= 1.0 + 0.16 * lobeW * Math.cos(theta * Math.max(4, Math.round(roots.count || 5)));
        }
        barkPositions.push(pt.pos[0] + nx * rad, pt.pos[1] + ny * rad, pt.pos[2] + nz * rad);
        barkNormals.push(nx, ny, nz);
        barkUvs.push(frac * uWrapRepeat, pt.s * vLenRepeat);
        barkWind.push(hNorm, limbW, st.phase, detailW);
        barkPivots.push(st.pivot[0], st.pivot[1], st.pivot[2]);
        barkClusterIds.push(0); // assigned by 128-tri Cluster Partitioner
        barkStemMeta.push(st.id, hNorm);
      }
    }

    const ringStride = radialSegs + 1;
    for (let i = 0; i < st.stations.length - 1; i++) {
      const rowA = baseVertex + i * ringStride;
      const rowB = baseVertex + (i + 1) * ringStride;
      for (let r = 0; r < radialSegs; r++) {
        barkIndices.push(rowA + r, rowB + r, rowB + r + 1);
        barkIndices.push(rowA + r, rowB + r + 1, rowA + r + 1);
      }
    }
  }

  // Build Foliage Quad Buffer Arrays (clean [0,1]x[0,1] UV sheet + 3-tier wind)
  const leafPositions = [];
  const leafNormals = [];
  const leafUvs = [];
  const leafWind = [];
  const leafPivots = [];
  const leafClusterIds = [];
  const leafTintVar = [];
  const leafIndices = [];

  for (let i = 0; i < leafList.length; i++) {
    const lf = leafList[i];
    const base = leafPositions.length / 3;
    const hw = lf.scaleX * 0.5;
    const len = lf.scaleY;
    const hNorm = Math.max(0.0, Math.min(1.0, lf.pos[1] / maxY));
    const tint = (i * 0.6180339887) % 1.0;

    // 4 quad corners anchored at petiole base (pos) extending along lf.dir
    const c0 = addScaled(lf.pos, lf.side, -hw);
    const c1 = addScaled(lf.pos, lf.side, hw);
    const tipBase = addScaled(lf.pos, lf.dir, len);
    const c2 = addScaled(tipBase, lf.side, hw);
    const c3 = addScaled(tipBase, lf.side, -hw);

    const corners = [c0, c1, c2, c3];
    const uvs = [[0, 0], [1, 0], [1, 1], [0, 1]];
    const detailCorner = [0.25, 0.25, 1.0, 1.0];

    for (let k = 0; k < 4; k++) {
      leafPositions.push(corners[k][0], corners[k][1], corners[k][2]);
      leafNormals.push(lf.normal[0], lf.normal[1], lf.normal[2]);
      leafUvs.push(uvs[k][0], uvs[k][1]);
      leafWind.push(hNorm, lf.limbW, lf.phase, detailCorner[k]);
      leafPivots.push(lf.pivot[0], lf.pivot[1], lf.pivot[2]);
      leafClusterIds.push(0);
      leafTintVar.push(tint);
    }
    leafIndices.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }

  // Extract Analytical SDF Primitives directly from skeleton (ZERO voxelization pass!)
  // 1) Proper Tapered Capsules for Trunk (level 0), Major Boughs (level 1), and Secondary Branches (level 2)
  const sdfCapsules = [];
  for (const st of stems) {
    if (st.role !== 'shoot' || st.level > 2) continue;
    const stride = st.level === 0
      ? Math.max(1, Math.floor(st.stations.length / 6))
      : st.level === 1
        ? Math.max(1, Math.floor(st.stations.length / 3))
        : Math.max(1, st.stations.length - 1);
    for (let i = 0; i < st.stations.length - 1; i += stride) {
      const a = st.stations[i];
      const b = st.stations[Math.min(st.stations.length - 1, i + stride)];
      const hA = Math.max(0.0, Math.min(1.0, a.pos[1] / maxY));
      const hB = Math.max(0.0, Math.min(1.0, b.pos[1] / maxY));
      const lA = st.level === 0 ? 0.0 : Math.min(1.0, Math.hypot(a.pos[0] - st.pivot[0], a.pos[1] - st.pivot[1], a.pos[2] - st.pivot[2]) / st.limbReach);
      const lB = st.level === 0 ? 0.0 : Math.min(1.0, Math.hypot(b.pos[0] - st.pivot[0], b.pos[1] - st.pivot[1], b.pos[2] - st.pivot[2]) / st.limbReach);
      sdfCapsules.push({
        a: [...a.pos],
        b: [...b.pos],
        rA: Math.max(0.025, a.radius * 1.05),
        rB: Math.max(0.02, b.radius * 1.05),
        windA: [hA, lA, st.phase, 0.0],
        windB: [hB, lB, st.phase, 0.0],
        pivot: [...st.pivot],
        level: st.level,
      });
      if (sdfCapsules.length >= 64) break;
    }
    if (sdfCapsules.length >= 64) break;
  }

  // 2) Instanced 1-Leaf Analytical SDF Transforms (1 baked 32x16x32 R16F leaf SDF in memory, instanced N times!)
  const leafSdfInstances = [];
  for (let i = 0; i < leafList.length; i++) {
    const lf = leafList[i];
    const hNorm = Math.max(0.0, Math.min(1.0, lf.pos[1] / maxY));
    leafSdfInstances.push({
      pos: [...lf.pos],
      dir: [...lf.dir],
      side: [...lf.side],
      normal: [...lf.normal],
      scaleX: lf.scaleX,
      scaleY: lf.scaleY,
      windAttr: [hNorm, lf.limbW, lf.phase, 0.65],
      pivot: [...lf.pivot],
    });
  }

  // 3) Legacy Coarse Canopy Bounds (kept for optional comparison vs Instanced 1-Leaf SDF)
  const sdfCanopyLobes = [];
  if (leafList.length > 0) {
    const targetLobes = Math.min(16, Math.max(4, Math.round(leafList.length / 220)));
    const bucketSize = Math.max(1, Math.floor(leafList.length / targetLobes));
    for (let b = 0; b < targetLobes; b++) {
      const start = b * bucketSize;
      const end = Math.min(leafList.length, start + bucketSize);
      if (start >= end) break;
      let cx = 0, cy = 0, cz = 0;
      let px = 0, py = 0, pz = 0;
      let limbSum = 0, phaseSum = 0;
      for (let i = start; i < end; i++) {
        const lf = leafList[i];
        cx += lf.pos[0]; cy += lf.pos[1]; cz += lf.pos[2];
        px += lf.pivot[0]; py += lf.pivot[1]; pz += lf.pivot[2];
        limbSum += lf.limbW;
        phaseSum += lf.phase;
      }
      const n = end - start;
      cx /= n; cy /= n; cz /= n;
      px /= n; py /= n; pz /= n;
      let radAccum = 0;
      for (let i = start; i < end; i++) {
        const lf = leafList[i];
        radAccum += Math.hypot(lf.pos[0] - cx, lf.pos[1] - cy, lf.pos[2] - cz);
      }
      const avgRad = Math.max(0.55, Math.min(maxY * 0.28, (radAccum / n) * 1.25));
      sdfCanopyLobes.push({
        center: [cx, cy, cz],
        radius: avgRad,
        windAttr: [Math.max(0.0, Math.min(1.0, cy / maxY)), limbSum / n, phaseSum / n, 0.55],
        pivot: [px, py, pz],
        leafWeight: n,
      });
    }
  }

  const resolveCanonicalLeafShape = () => {
    const style = (preset.visual?.leafStyle || '').toLowerCase();
    const nm = (preset.name || '').toLowerCase();
    if (style === 'conifer' || /pine|spruce|fir|cedar|cypress|juniper|sequoia|redwood|larch|hemlock/.test(nm)) return 'needle';
    if (style === 'palm' || /palm|banana|bamboo/.test(nm)) return 'palm';
    if (style === 'willow' || /willow|olive|eucalyptus/.test(nm)) return 'willow';
    if (/oak|maple|sycamore|sweetgum|plane/.test(nm)) return 'lobed';
    if (/locust|walnut|ash|rowan|acacia|mimosa/.test(nm)) return 'compound';
    return 'broadleaf';
  };

  return {
    presetName: preset.name,
    leafShape: resolveCanonicalLeafShape(),
    seed,
    lodTier,
    height: maxY,
    trunkRadius: trunkRad,
    stems,
    perLevelCount,
    rootStemCount,
    leafCount: leafList.length,
    bark: {
      positions: new Float32Array(barkPositions),
      normals: new Float32Array(barkNormals),
      uvs: new Float32Array(barkUvs),
      wind: new Float32Array(barkWind),
      pivots: new Float32Array(barkPivots),
      clusterIds: new Float32Array(barkClusterIds),
      stemMeta: new Float32Array(barkStemMeta),
      indices: new Uint32Array(barkIndices),
      triangleCount: barkIndices.length / 3,
    },
    foliage: {
      positions: new Float32Array(leafPositions),
      normals: new Float32Array(leafNormals),
      uvs: new Float32Array(leafUvs),
      wind: new Float32Array(leafWind),
      pivots: new Float32Array(leafPivots),
      clusterIds: new Float32Array(leafClusterIds),
      tintVar: new Float32Array(leafTintVar),
      indices: new Uint32Array(leafIndices),
      triangleCount: leafIndices.length / 3,
    },
    sdfCapsules,
    leafSdfInstances,
    sdfCanopyLobes,
    generationMs: performance.now() - tStart,
  };
}
