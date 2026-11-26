// LightTreePacking.js — Unified GPU Luminaire Register + Conty Estevez & Kulla (2018)
// spatial partition builder + L2 Spherical Harmonics probe packer.

export const MAX_UNIFIED_LUMINAIRES = 128;
export const MAX_PARTITION_CELLS = 256;

export const LUMINAIRE_CATEGORY = {
  POINT_SPHERE: 0,
  PHOTOMETRIC_IES: 1,
  AREA_QUAD_LTC: 2,
  TUBE_CAPSULE: 3,
  SELF_LIT_CLUSTER: 4,
};

function v3(x = 0, y = 0, z = 0) {
  return [x, y, z];
}

function add3(a, b) {
  return [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
}

function sub3(a, b) {
  return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}

function scale3(a, s) {
  return [a[0] * s, a[1] * s, a[2] * s];
}

function dot3(a, b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

function cross3(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

function len3(a) {
  return Math.hypot(a[0], a[1], a[2]);
}

function norm3(a) {
  const l = len3(a);
  return l > 1e-6 ? [a[0] / l, a[1] / l, a[2] / l] : [0, -1, 0];
}

export function kelvinToLinearRgb(kelvin) {
  const t = Math.max(1000, Math.min(20000, kelvin)) / 100.0;
  let r, g, b;
  if (t <= 66) {
    r = 255;
    g = 99.4708025861 * Math.log(t) - 161.1195681661;
    b = t <= 19 ? 0 : 138.5177312231 * Math.log(t - 10) - 305.0447927307;
  } else {
    r = 329.698727446 * Math.pow(t - 60, -0.1332047592);
    g = 288.1221695283 * Math.pow(t - 60, -0.0755148492);
    b = 255;
  }
  const srgbToLinear = (c) => {
    const u = Math.max(0, Math.min(255, c)) / 255.0;
    return u <= 0.04045 ? u / 12.92 : Math.pow((u + 0.055) / 1.055, 2.4);
  };
  return [srgbToLinear(r), srgbToLinear(g), srgbToLinear(b)];
}

export function createLuminaireBounds(lum) {
  const cat = lum.category;
  let minB, maxB, axis, cosSpread, cosCutoff, cutoffAngle, power, rgbPower;
  const color = lum.rgbIntensity;
  const luma = Math.max(0.01, 0.2126 * color[0] + 0.7152 * color[1] + 0.0722 * color[2]);

  if (cat === LUMINAIRE_CATEGORY.POINT_SPHERE) {
    const r = Math.min(lum.rangeMeters, 14.0) * 0.12 + lum.radiusMeters + 0.2;
    minB = [lum.pos[0] - r, lum.pos[1] - r, lum.pos[2] - r];
    maxB = [lum.pos[0] + r, lum.pos[1] + r, lum.pos[2] + r];
    axis = [0, -1, 0];
    cosSpread = -1.0;
    cosCutoff = -1.0;
    cutoffAngle = Math.PI;
    // Direct irradiance scale at 1m is rgbIntensity / d^2 -> exact cd-equivalent power = luma
    power = luma;
    const tailScale = 4.0 * Math.PI;
    rgbPower = [color[0] * tailScale, color[1] * tailScale, color[2] * tailScale];
  } else if (cat === LUMINAIRE_CATEGORY.PHOTOMETRIC_IES) {
    const r = lum.radiusMeters + 0.18;
    minB = [lum.pos[0] - r, lum.pos[1] - r, lum.pos[2] - r];
    maxB = [lum.pos[0] + r, lum.pos[1] + r, lum.pos[2] + r];
    axis = norm3(lum.beamAxis);
    const halfSpread = (lum.spreadDeg || 48) * (Math.PI / 180.0);
    cosSpread = Math.cos(halfSpread * 0.5);
    cutoffAngle = Math.min(Math.PI, Math.max(halfSpread * 1.45, 1.05));
    cosCutoff = Math.cos(cutoffAngle);
    power = luma * 1.15;
    const tailScale = 4.0 * Math.PI * 0.35;
    rgbPower = [color[0] * tailScale, color[1] * tailScale, color[2] * tailScale];
  } else if (cat === LUMINAIRE_CATEGORY.AREA_QUAD_LTC || cat === LUMINAIRE_CATEGORY.SELF_LIT_CLUSTER) {
    const extX = Math.abs(lum.rightAxis[0]) * lum.halfW + Math.abs(lum.upAxis[0]) * lum.halfH + 0.15;
    const extY = Math.abs(lum.rightAxis[1]) * lum.halfW + Math.abs(lum.upAxis[1]) * lum.halfH + 0.15;
    const extZ = Math.abs(lum.rightAxis[2]) * lum.halfW + Math.abs(lum.upAxis[2]) * lum.halfH + 0.15;
    minB = [lum.pos[0] - extX, lum.pos[1] - extY, lum.pos[2] - extZ];
    maxB = [lum.pos[0] + extX, lum.pos[1] + extY, lum.pos[2] + extZ];
    axis = norm3(cross3(lum.rightAxis, lum.upAxis));
    const twoSided = lum.twoSided > 0.5;
    cosSpread = twoSided ? -1.0 : 0.7;
    cutoffAngle = twoSided ? Math.PI : Math.PI * 0.52;
    cosCutoff = Math.cos(cutoffAngle);
    const area = Math.max(0.05, 4.0 * lum.halfW * lum.halfH);
    // Irradiance scale at 1m is color * area / PI
    const effScale = area / Math.PI;
    power = luma * effScale;
    const tailScale = 4.0 * area;
    rgbPower = [color[0] * tailScale, color[1] * tailScale, color[2] * tailScale];
  } else {
    // TUBE_CAPSULE
    const r = lum.radiusMeters + 0.12;
    minB = [
      Math.min(lum.segA[0], lum.segB[0]) - r,
      Math.min(lum.segA[1], lum.segB[1]) - r,
      Math.min(lum.segA[2], lum.segB[2]) - r,
    ];
    maxB = [
      Math.max(lum.segA[0], lum.segB[0]) + r,
      Math.max(lum.segA[1], lum.segB[1]) + r,
      Math.max(lum.segA[2], lum.segB[2]) + r,
    ];
    axis = [0, -1, 0];
    cosSpread = -1.0;
    cosCutoff = -1.0;
    cutoffAngle = Math.PI;
    const segLen = Math.max(0.12, len3(sub3(lum.segB, lum.segA)));
    const capNorm = Math.min(1.8, 0.45 + segLen * 0.55);
    power = luma * capNorm;
    const tailScale = 4.0 * Math.PI * capNorm;
    rgbPower = [color[0] * tailScale, color[1] * tailScale, color[2] * tailScale];
  }

  return {
    minB,
    maxB,
    centroid: [
      0.5 * (minB[0] + maxB[0]),
      0.5 * (minB[1] + maxB[1]),
      0.5 * (minB[2] + maxB[2]),
    ],
    axis,
    cosSpread,
    sinSpread: Math.sqrt(Math.max(0, 1 - cosSpread * cosSpread)),
    cosCutoff,
    cutoffAngle,
    power,
    rgbPower,
  };
}

function mergeBounds(a, b) {
  const minB = [
    Math.min(a.minB[0], b.minB[0]),
    Math.min(a.minB[1], b.minB[1]),
    Math.min(a.minB[2], b.minB[2]),
  ];
  const maxB = [
    Math.max(a.maxB[0], b.maxB[0]),
    Math.max(a.maxB[1], b.maxB[1]),
    Math.max(a.maxB[2], b.maxB[2]),
  ];

  // Spherical cone union (Conty Estevez & Kulla 2018)
  const totalPower = a.power + b.power;
  let axis = a.power >= b.power ? a.axis : b.axis;
  let thetaO = 0;
  let thetaE = Math.max(a.cutoffAngle, b.cutoffAngle);

  const cosA = Math.acos(Math.max(-1, Math.min(1, a.cosSpread)));
  const cosB = Math.acos(Math.max(-1, Math.min(1, b.cosSpread)));
  const maxS = Math.max(cosA, cosB);
  const minS = Math.min(cosA, cosB);
  const cosD = Math.max(-1, Math.min(1, dot3(a.axis, b.axis)));
  const d = Math.acos(cosD);

  if (Math.min(d + minS, Math.PI) <= maxS) {
    axis = cosA >= cosB ? a.axis : b.axis;
    thetaO = maxS;
  } else {
    thetaO = (maxS + d + minS) * 0.5;
    if (thetaO >= Math.PI) {
      thetaO = Math.PI;
      axis = [0, 1, 0];
    } else if (d > 1e-4) {
      const rAngle = thetaO - maxS;
      const baseAxis = cosA >= cosB ? a.axis : b.axis;
      const otherAxis = cosA >= cosB ? b.axis : a.axis;
      const ortho = norm3(sub3(otherAxis, scale3(baseAxis, dot3(baseAxis, otherAxis))));
      axis = norm3(add3(scale3(baseAxis, Math.cos(rAngle)), scale3(ortho, Math.sin(rAngle))));
    }
  }

  return {
    minB,
    maxB,
    centroid: [
      0.5 * (minB[0] + maxB[0]),
      0.5 * (minB[1] + maxB[1]),
      0.5 * (minB[2] + maxB[2]),
    ],
    axis,
    cosSpread: Math.cos(thetaO),
    sinSpread: Math.sin(thetaO),
    cosCutoff: Math.cos(Math.min(Math.PI, thetaE)),
    cutoffAngle: Math.min(Math.PI, thetaE),
    power: totalPower,
    rgbPower: [
      a.rgbPower[0] + b.rgbPower[0],
      a.rgbPower[1] + b.rgbPower[1],
      a.rgbPower[2] + b.rgbPower[2],
    ],
  };
}

export function buildUnifiedPartitionAndPack(luminaires) {
  const count = Math.min(luminaires.length, MAX_UNIFIED_LUMINAIRES);
  // 256 wide x 16 tall RGBA32F buffer -> 4096 vec4 texels
  // Texels 0..511   : 128 Luminaires * 4 vec4s
  // Texels 512..1535: 256 Partition entries * 4 vec4s
  const texData = new Float32Array(256 * 16 * 4);

  const leaves = [];
  for (let i = 0; i < count; ++i) {
    const lum = luminaires[i];
    const bounds = createLuminaireBounds(lum);
    leaves.push({ lumIndex: i, bounds });

    const base = i * 4 * 4;
    // T0: Pos/SegA, ParamA (Range or HalfW or TubeRadius)
    if (lum.category === LUMINAIRE_CATEGORY.TUBE_CAPSULE) {
      texData[base + 0] = lum.segA[0];
      texData[base + 1] = lum.segA[1];
      texData[base + 2] = lum.segA[2];
      texData[base + 3] = lum.radiusMeters;
      // T1: SegB, Range
      texData[base + 4] = lum.segB[0];
      texData[base + 5] = lum.segB[1];
      texData[base + 6] = lum.segB[2];
      texData[base + 7] = lum.rangeMeters;
      // T2: Aux, Category
      texData[base + 8] = 0;
      texData[base + 9] = 0;
      texData[base + 10] = 1;
      texData[base + 11] = lum.category;
      // T3: ColorIntensity, Extra
      texData[base + 12] = lum.rgbIntensity[0];
      texData[base + 13] = lum.rgbIntensity[1];
      texData[base + 14] = lum.rgbIntensity[2];
      texData[base + 15] = 0.0;
    } else if (
      lum.category === LUMINAIRE_CATEGORY.AREA_QUAD_LTC ||
      lum.category === LUMINAIRE_CATEGORY.SELF_LIT_CLUSTER
    ) {
      texData[base + 0] = lum.pos[0];
      texData[base + 1] = lum.pos[1];
      texData[base + 2] = lum.pos[2];
      texData[base + 3] = lum.halfW;
      // T1: UpAxis, HalfH
      texData[base + 4] = lum.upAxis[0];
      texData[base + 5] = lum.upAxis[1];
      texData[base + 6] = lum.upAxis[2];
      texData[base + 7] = lum.halfH;
      // T2: RightAxis, Category
      texData[base + 8] = lum.rightAxis[0];
      texData[base + 9] = lum.rightAxis[1];
      texData[base + 10] = lum.rightAxis[2];
      texData[base + 11] = lum.category;
      // T3: ColorIntensity, Extra (packed twoSided + collimation)
      texData[base + 12] = lum.rgbIntensity[0];
      texData[base + 13] = lum.rgbIntensity[1];
      texData[base + 14] = lum.rgbIntensity[2];
      texData[base + 15] = (lum.twoSided > 0.5 ? 100.0 : 0.0) + (lum.collimation || 0.0);
    } else {
      // POINT_SPHERE (0) or PHOTOMETRIC_IES (1)
      texData[base + 0] = lum.pos[0];
      texData[base + 1] = lum.pos[1];
      texData[base + 2] = lum.pos[2];
      texData[base + 3] = lum.rangeMeters;
      // T1: BeamAxis, RadiusMeters
      const axis = lum.beamAxis || [0, -1, 0];
      texData[base + 4] = axis[0];
      texData[base + 5] = axis[1];
      texData[base + 6] = axis[2];
      texData[base + 7] = lum.radiusMeters;
      // T2: ZeroAzimuth, Category
      const zeroAz = lum.zeroAzimuth || [1, 0, 0];
      texData[base + 8] = zeroAz[0];
      texData[base + 9] = zeroAz[1];
      texData[base + 10] = zeroAz[2];
      texData[base + 11] = lum.category;
      // T3: ColorIntensity, ProfileOrdinal
      texData[base + 12] = lum.rgbIntensity[0];
      texData[base + 13] = lum.rgbIntensity[1];
      texData[base + 14] = lum.rgbIntensity[2];
      texData[base + 15] = lum.profileOrdinal || 0.0;
    }
  }

  // Build binary spatial partition recursively (SAOH-style largest-axis centroid median split)
  const cells = new Array(MAX_PARTITION_CELLS);
  let nextSlot = 0;

  function allocSlot() {
    if (nextSlot >= MAX_PARTITION_CELLS) return MAX_PARTITION_CELLS - 1;
    return nextSlot++;
  }

  function buildSubPartition(items, slot) {
    if (items.length === 0) {
      cells[slot] = {
        minB: [0, 0, 0],
        maxB: [0, 0, 0],
        power: 0,
        rgbPower: [0, 0, 0],
        firstNestedOrLeaf: -1,
        axis: [0, -1, 0],
        cosSpread: -1,
        sinSpread: 0,
        cosCutoff: -1,
        cutoffAngle: Math.PI,
      };
      return cells[slot];
    }
    if (items.length === 1) {
      const b = items[0].bounds;
      cells[slot] = {
        ...b,
        firstNestedOrLeaf: -(items[0].lumIndex + 1),
      };
      return b;
    }

    // Compute centroid spread axis + directional cone split heuristic
    let cMin = [...items[0].bounds.centroid];
    let cMax = [...items[0].bounds.centroid];
    for (let i = 1; i < items.length; ++i) {
      const c = items[i].bounds.centroid;
      cMin = [Math.min(cMin[0], c[0]), Math.min(cMin[1], c[1]), Math.min(cMin[2], c[2])];
      cMax = [Math.max(cMax[0], c[0]), Math.max(cMax[1], c[1]), Math.max(cMax[2], c[2])];
    }
    const span = [cMax[0] - cMin[0], cMax[1] - cMin[1], cMax[2] - cMin[2]];
    let splitAxis = 0;
    if (span[1] > span[splitAxis]) splitAxis = 1;
    if (span[2] > span[splitAxis]) splitAxis = 2;

    const sorted = items.slice().sort((a, b) => a.bounds.centroid[splitAxis] - b.bounds.centroid[splitAxis]);
    const mid = Math.max(1, Math.floor(sorted.length * 0.5));
    const leftItems = sorted.slice(0, mid);
    const rightItems = sorted.slice(mid);

    // Reserve adjacent pair sequentially: Left = firstNested, Right = firstNested + 1
    const leftSlot = allocSlot();
    const rightSlot = allocSlot();
    const leftBounds = buildSubPartition(leftItems, leftSlot);
    const rightBounds = buildSubPartition(rightItems, rightSlot);
    const combined = mergeBounds(leftBounds, rightBounds);

    cells[slot] = {
      ...combined,
      firstNestedOrLeaf: leftSlot,
    };
    return combined;
  }

  if (leaves.length > 0) {
    const rootSlot = allocSlot(); // 0
    buildSubPartition(leaves, rootSlot);
  }

  // Write partition entries starting at texel offset 512
  for (let i = 0; i < nextSlot; ++i) {
    const cell = cells[i];
    const base = (512 + i * 4) * 4;
    // N0: minB.xyz, totalPower
    texData[base + 0] = cell.minB[0];
    texData[base + 1] = cell.minB[1];
    texData[base + 2] = cell.minB[2];
    texData[base + 3] = cell.power;
    // N1: maxB.xyz, firstNestedOrLeaf
    texData[base + 4] = cell.maxB[0];
    texData[base + 5] = cell.maxB[1];
    texData[base + 6] = cell.maxB[2];
    texData[base + 7] = cell.firstNestedOrLeaf;
    // N2: axis.xyz, cosSpread
    texData[base + 8] = cell.axis[0];
    texData[base + 9] = cell.axis[1];
    texData[base + 10] = cell.axis[2];
    texData[base + 11] = cell.cosSpread;
    // N3: rgbPower.rgb, cosCutoff
    texData[base + 12] = cell.rgbPower[0];
    texData[base + 13] = cell.rgbPower[1];
    texData[base + 14] = cell.rgbPower[2];
    texData[base + 15] = cell.cosCutoff;
  }

  return {
    texData,
    luminaireCount: count,
    partitionCellCount: nextSlot,
    cells: cells.slice(0, nextSlot),
  };
}

// Compute L2 Spherical Harmonics (9 RGB coefficients) for Environment preset (MatCap + Diffuse IBL)
export function bakeEnvironmentHarmonics(envPresetIndex, sunDir) {
  const sh = new Float32Array(9 * 3);
  // Sample 128 Fibonacci sphere directions analytically
  const samples = 128;
  const goldenAngle = Math.PI * (3 - Math.sqrt(5));
  const weight = (4.0 * Math.PI) / samples;

  const evalEnv = (d) => {
    const y = d[1];
    const sunDot = Math.max(0, dot3(d, sunDir));
    if (envPresetIndex === 0) {
      // Lobby 1: Dark Cyclorama Cove
      const zenith = [0.05, 0.065, 0.095];
      const horizon = [0.025, 0.028, 0.036];
      const floor = [0.012, 0.013, 0.016];
      const t = Math.max(0, y);
      const coveRing = Math.exp(-Math.pow((y - 0.34) / 0.14, 2)) * 0.22;
      return [
        (y >= 0 ? horizon[0] * (1 - t) + zenith[0] * t : floor[0]) + coveRing * 0.9,
        (y >= 0 ? horizon[1] * (1 - t) + zenith[1] * t : floor[1]) + coveRing * 0.95,
        (y >= 0 ? horizon[2] * (1 - t) + zenith[2] * t : floor[2]) + coveRing * 1.05,
      ];
    } else if (envPresetIndex === 1) {
      // Lobby 2: Sunlit Forest Showcase
      const up = y >= 0 ? y : 0;
      const skyR = 0.24 * (1 - up) + 0.10 * up + 0.75 * Math.pow(sunDot, 10);
      const skyG = 0.36 * (1 - up) + 0.28 * up + 0.68 * Math.pow(sunDot, 10);
      const skyB = 0.44 * (1 - up) + 0.68 * up + 0.52 * Math.pow(sunDot, 10);
      const canopy = y < 0.15 && y > -0.25 ? 0.14 : 0.0;
      return y >= 0
        ? [skyR * (1 - canopy * 0.5) + 0.06 * canopy, skyG + 0.18 * canopy, skyB * (1 - canopy * 0.4)]
        : [0.07, 0.10, 0.055];
    } else {
      // Lobby 3: Afternoon Racing Pit & Stadium Sky
      const up = Math.max(0, y);
      const warmSun = Math.pow(sunDot, 8);
      const sky = [
        0.48 * (1 - up) + 0.16 * up + 0.85 * warmSun,
        0.42 * (1 - up) + 0.32 * up + 0.68 * warmSun,
        0.38 * (1 - up) + 0.62 * up + 0.42 * warmSun,
      ];
      return y >= 0 ? sky : [0.09, 0.085, 0.08];
    }
  };

  for (let i = 0; i < samples; ++i) {
    const y = 1 - (i / (samples - 1)) * 2;
    const rad = Math.sqrt(Math.max(0, 1 - y * y));
    const theta = goldenAngle * i;
    const d = [Math.cos(theta) * rad, y, Math.sin(theta) * rad];
    const col = evalEnv(d);

    const b = [
      0.282095,
      0.488603 * d[1],
      0.488603 * d[2],
      0.488603 * d[0],
      1.092548 * d[0] * d[1],
      1.092548 * d[1] * d[2],
      0.315392 * (3 * d[2] * d[2] - 1),
      1.092548 * d[0] * d[2],
      0.546274 * (d[0] * d[0] - d[1] * d[1]),
    ];

    for (let k = 0; k < 9; ++k) {
      sh[k * 3 + 0] += col[0] * b[k] * weight;
      sh[k * 3 + 1] += col[1] * b[k] * weight;
      sh[k * 3 + 2] += col[2] * b[k] * weight;
    }
  }
  return sh;
}
