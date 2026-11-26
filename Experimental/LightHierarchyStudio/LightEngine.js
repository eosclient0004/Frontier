/*============================================================================================================================================
                                                               LIGHTENGINE.JS
============================================================================================================================================*/
// 🧩 Unified 3-System Lighting Engine:
//    (1) Static Auto-Baked 2-Cascade Toroidal Clipmap (Fine + Coarse)
//    (2) Dynamic Cube-Grid / Pod Directional Atlas (95.1 KB / pod)
//    (3) Nanite-for-Lights Agglomerative Distance BVH + Error-Guided Unified Hybrid

export const WORLD_HALF_EXTENT = 12.0; // Base physical plaza geometry spans [-12, +12] m (24m x 24m)
export const ONE_CUBE_GRID_POD_KB = 95.1; // 16x16x6 R11G11B10/RGBA16F directional pod atlas + mip chain

export const SCENE_SCALES = {
  small: {
    id: 'small',
    label: 'Small Room (16m)',
    worldSpanM: 16,
    fineHalfSpan: 4.5,
    coarseHalfSpan: 12.0,
    fineRes: 16,
    coarseRes: 12,
    defaultLodBudget: 0.16,
    defaultHybridGate: 0.12,
    podBudget: 8,
    desc: 'Tight interior — high-res 0.56m fine toroidal cells & strict close-up headlight split.',
  },
  medium: {
    id: 'medium',
    label: 'Medium Plaza (36m)',
    worldSpanM: 36,
    fineHalfSpan: 6.0,
    coarseHalfSpan: 12.0,
    fineRes: 16,
    coarseRes: 16,
    defaultLodBudget: 0.24,
    defaultHybridGate: 0.18,
    podBudget: 12,
    desc: 'Balanced exterior — toroidal fine clipmap tracks probe while coarse covers full plaza.',
  },
  large: {
    id: 'large',
    label: 'Super Large World (160m)',
    worldSpanM: 160,
    fineHalfSpan: 7.5,
    coarseHalfSpan: 12.0,
    fineRes: 12,
    coarseRes: 12,
    defaultLodBudget: 0.38,
    defaultHybridGate: 0.28,
    podBudget: 20,
    desc: 'Open-world streaming — aggressive toroidal scroll & distant Nanite light cluster merging.',
  },
};

export const SCENE_BOXES = [
  { name: 'CarChassis',  min: [-1.15, 0.16, -6.2], max: [1.15, 0.96, -2.2], color: [0.16, 0.18, 0.22], rough: 0.28 },
  { name: 'CarCabin',    min: [-0.95, 0.96, -5.5], max: [0.95, 1.52, -3.5], color: [0.11, 0.13, 0.17], rough: 0.18 },
  { name: 'PillarLeft',  min: [-1.45, 0.0,  1.2],  max: [-0.95, 1.65, 1.7], color: [0.42, 0.44, 0.48], rough: 0.65 },
  { name: 'PillarRight', min: [ 0.95, 0.0,  1.2],  max: [ 1.45, 1.65, 1.7], color: [0.42, 0.44, 0.48], rough: 0.65 },
  { name: 'NeonDivider', min: [ 5.35, 0.0, -3.6],  max: [ 5.65, 2.2, -0.4], color: [0.30, 0.32, 0.36], rough: 0.55 },
  { name: 'LanternPost', min: [-5.68, 0.0, -2.68], max: [-5.32, 1.55, -2.32], color: [0.34, 0.33, 0.30], rough: 0.72 },
];

//------------------------------------------------------------------------------------------------------------------------
//                          PROJECT ZERO LIGHT COMPONENTS (WITH STATIC / DYNAMIC MOBILITY TOGGLE)
//------------------------------------------------------------------------------------------------------------------------

export function CreateDefaultLightComponents() {
  return [
    {
      id: 'comp-hl-left',
      name: 'ECE Low Beam · Left',
      kind: 'Spot light',
      mobility: 'dynamic', // Car headlights default DYNAMIC -> Cube-Grid Pod + Nanite BVH
      enabled: true,
      subCount: 5,
      fluxPerSub: 3.4,
      dimmer: 1.0,
      coneDeg: 58,
      temperatureK: 4800,
      color: [1.0, 0.94, 0.80],
      group: 'Headlights',
      subGroup: 'LeftHeadlight',
      anchor: [-0.82, 0.64, -2.10],
    },
    {
      id: 'comp-hl-right',
      name: 'ECE Low Beam · Right',
      kind: 'Spot light',
      mobility: 'dynamic',
      enabled: true,
      subCount: 5,
      fluxPerSub: 3.4,
      dimmer: 1.0,
      coneDeg: 58,
      temperatureK: 4800,
      color: [1.0, 0.94, 0.80],
      group: 'Headlights',
      subGroup: 'RightHeadlight',
      anchor: [0.82, 0.64, -2.10],
    },
    {
      id: 'comp-lantern5',
      name: 'Lantern Cluster (5×)',
      kind: 'LED emitter',
      mobility: 'static', // Static architectural cluster -> Auto-Baked Toroidal Clipmap
      enabled: true,
      subCount: 5,
      fluxPerSub: 3.2,
      dimmer: 1.0,
      coneDeg: 180,
      temperatureK: 2700,
      color: [1.0, 0.66, 0.24],
      group: 'LanternCluster5',
      subGroup: 'LanternCluster5',
      anchor: [-5.5, 1.95, -2.5],
    },
    {
      id: 'comp-neon-west',
      name: 'Neon Strip · West (-X)',
      kind: 'LED strip',
      mobility: 'static',
      enabled: true,
      subCount: 4,
      fluxPerSub: 3.6,
      dimmer: 1.0,
      coneDeg: 82,
      temperatureK: 6500,
      color: [1.0, 0.20, 0.68],
      group: 'OpposingNeon',
      subGroup: 'MagentaWest',
      anchor: [5.15, 1.35, -2.0],
    },
    {
      id: 'comp-neon-east',
      name: 'Neon Strip · East (+X)',
      kind: 'LED strip',
      mobility: 'static',
      enabled: true,
      subCount: 4,
      fluxPerSub: 3.6,
      dimmer: 1.0,
      coneDeg: 82,
      temperatureK: 9000,
      color: [0.16, 0.88, 1.0],
      group: 'OpposingNeon',
      subGroup: 'CyanEast',
      anchor: [5.85, 1.35, -2.0],
    },
    {
      id: 'comp-plaza',
      name: 'Plaza IES Downlights (6×)',
      kind: 'IES photometric',
      mobility: 'static',
      enabled: true,
      subCount: 6,
      fluxPerSub: 3.0,
      dimmer: 1.0,
      coneDeg: 180,
      temperatureK: 4000,
      color: [0.75, 0.88, 0.56],
      group: 'PlazaLamps',
      subGroup: 'PlazaLamps',
      anchor: [0.0, 2.45, 5.9],
    },
    {
      id: 'comp-drone',
      name: 'Hover Rover Drone (3×)',
      kind: 'Point light',
      mobility: 'dynamic',
      enabled: true,
      subCount: 3,
      fluxPerSub: 3.1,
      dimmer: 1.0,
      coneDeg: 180,
      temperatureK: 5600,
      color: [0.28, 1.0, 0.58],
      group: 'RoverCluster',
      subGroup: 'RoverCluster',
      anchor: [0.0, 1.25, 4.6],
    },
  ];
}

export function KelvinToTintRgb(kelvin) {
  const k = Math.max(1800, Math.min(12000, kelvin)) / 100.0;
  let r, g, b;
  if (k <= 66) {
    r = 255;
    g = Math.max(0, Math.min(255, 99.47 * Math.log(k) - 161.12));
    b = k <= 19 ? 0 : Math.max(0, Math.min(255, 138.52 * Math.log(k - 10) - 305.04));
  } else {
    r = Math.max(0, Math.min(255, 329.7 * Math.pow(k - 60, -0.1332)));
    g = Math.max(0, Math.min(255, 288.12 * Math.pow(k - 60, -0.0755)));
    b = 255;
  }
  return [r / 255, g / 255, b / 255];
}

/**
 * Expands the Project Zero Light Component Registry into all active physical emitters (up to 32).
 * Each emitter inherits its parent component's `mobility` ('static' | 'dynamic'), `dimmer`, `coneDeg`, and `color`!
 */
export function ExpandComponentsToRawLights(components, timeSec = 0, animateDrone = true) {
  const lights = [];
  let id = 0;

  const byId = new Map(components.map((c) => [c.id, c]));
  const hlOffsets = [
    [0.0, 0.0, 0.0], [-0.11, 0.04, 0.0], [0.11, 0.04, 0.0], [-0.07, -0.05, 0.0], [0.07, -0.05, 0.0],
  ];

  // 1) Left Headlight
  const hlL = byId.get('comp-hl-left');
  if (hlL && hlL.enabled) {
    const cosCone = hlL.coneDeg >= 170 ? -1.0 : Math.cos((hlL.coneDeg * Math.PI) / 180);
    for (const o of hlOffsets) {
      lights.push({
        id: id++, componentId: hlL.id, mobility: hlL.mobility,
        group: hlL.group, subGroup: hlL.subGroup,
        pos: [-0.82 + o[0], 0.64 + o[1], -2.10 + o[2]],
        dir: [0.0, -0.10, 0.995],
        cosCone, color: [...hlL.color], flux: hlL.fluxPerSub * hlL.dimmer,
      });
    }
  }

  // 2) Right Headlight
  const hlR = byId.get('comp-hl-right');
  if (hlR && hlR.enabled) {
    const cosCone = hlR.coneDeg >= 170 ? -1.0 : Math.cos((hlR.coneDeg * Math.PI) / 180);
    for (const o of hlOffsets) {
      lights.push({
        id: id++, componentId: hlR.id, mobility: hlR.mobility,
        group: hlR.group, subGroup: hlR.subGroup,
        pos: [0.82 + o[0], 0.64 + o[1], -2.10 + o[2]],
        dir: [0.0, -0.10, 0.995],
        cosCone, color: [...hlR.color], flux: hlR.fluxPerSub * hlR.dimmer,
      });
    }
  }

  // 3) 5-Bulb Lantern Cluster
  const lan = byId.get('comp-lantern5');
  if (lan && lan.enabled) {
    const cosCone = lan.coneDeg >= 170 ? -1.0 : Math.cos((lan.coneDeg * Math.PI) / 180);
    for (let i = 0; i < 5; i++) {
      const ang = (i / 5) * Math.PI * 2.0;
      lights.push({
        id: id++, componentId: lan.id, mobility: lan.mobility,
        group: lan.group, subGroup: lan.subGroup,
        pos: [-5.5 + Math.cos(ang) * 0.22, 1.95 + (i % 2) * 0.09, -2.5 + Math.sin(ang) * 0.22],
        dir: [0.0, -1.0, 0.0],
        cosCone, color: [...lan.color], flux: lan.fluxPerSub * lan.dimmer,
      });
    }
  }

  // 4) Opposing Neon West (-X)
  const nw = byId.get('comp-neon-west');
  if (nw && nw.enabled) {
    const cosCone = nw.coneDeg >= 170 ? -1.0 : Math.cos((nw.coneDeg * Math.PI) / 180);
    for (let i = 0; i < 4; i++) {
      lights.push({
        id: id++, componentId: nw.id, mobility: nw.mobility,
        group: nw.group, subGroup: nw.subGroup,
        pos: [5.15, 1.35, -3.1 + i * 0.72],
        dir: [-0.95, -0.31, 0.0],
        cosCone, color: [...nw.color], flux: nw.fluxPerSub * nw.dimmer,
      });
    }
  }

  // 5) Opposing Neon East (+X)
  const ne = byId.get('comp-neon-east');
  if (ne && ne.enabled) {
    const cosCone = ne.coneDeg >= 170 ? -1.0 : Math.cos((ne.coneDeg * Math.PI) / 180);
    for (let i = 0; i < 4; i++) {
      lights.push({
        id: id++, componentId: ne.id, mobility: ne.mobility,
        group: ne.group, subGroup: ne.subGroup,
        pos: [5.85, 1.35, -3.1 + i * 0.72],
        dir: [0.95, -0.31, 0.0],
        cosCone, color: [...ne.color], flux: ne.fluxPerSub * ne.dimmer,
      });
    }
  }

  // 6) Plaza IES Downlights
  const pl = byId.get('comp-plaza');
  if (pl && pl.enabled) {
    const cosCone = pl.coneDeg >= 170 ? -1.0 : Math.cos((pl.coneDeg * Math.PI) / 180);
    const plazaCoords = [
      [-7.8, 2.4, 5.8, [0.52, 0.92, 0.62]],
      [-7.4, 2.4, 6.2, [0.52, 0.92, 0.62]],
      [-7.6, 2.5, 5.5, [0.52, 0.92, 0.62]],
      [ 7.5, 2.4, 6.0, [0.98, 0.78, 0.48]],
      [ 7.9, 2.4, 5.6, [0.98, 0.78, 0.48]],
      [ 7.6, 2.5, 6.4, [0.98, 0.78, 0.48]],
    ];
    for (let i = 0; i < plazaCoords.length; i++) {
      const c = plazaCoords[i];
      lights.push({
        id: id++, componentId: pl.id, mobility: pl.mobility,
        group: i < 3 ? 'PlazaWest' : 'PlazaEast',
        subGroup: i < 3 ? 'PlazaWest' : 'PlazaEast',
        pos: [c[0], c[1], c[2]],
        dir: [0.0, -1.0, 0.0],
        cosCone, color: c[3], flux: pl.fluxPerSub * pl.dimmer,
      });
    }
  }

  // 7) Hover Rover Drone
  const dr = byId.get('comp-drone');
  if (dr && dr.enabled) {
    const cosCone = dr.coneDeg >= 170 ? -1.0 : Math.cos((dr.coneDeg * Math.PI) / 180);
    const t = (animateDrone && dr.mobility === 'dynamic') ? timeSec * 0.55 : 0.7;
    const rx = Math.sin(t) * 4.4;
    const rz = 4.6 + Math.cos(t * 1.3) * 2.2;
    dr.anchor = [rx, 1.25, rz];
    for (let i = 0; i < 3; i++) {
      const a = t * 2.5 + (i * Math.PI * 2) / 3;
      lights.push({
        id: id++, componentId: dr.id, mobility: dr.mobility,
        group: dr.group, subGroup: dr.subGroup,
        pos: [rx + Math.cos(a) * 0.16, 1.25, rz + Math.sin(a) * 0.16],
        dir: [0.0, -1.0, 0.0],
        cosCone, color: [...dr.color], flux: dr.fluxPerSub * dr.dimmer,
      });
    }
  }

  return lights;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  ANALYTIC RAY-BOX SHADOW & PHYSICAL LIGHT EVALUATION
//------------------------------------------------------------------------------------------------------------------------

function RayIntersectsBox(ox, oy, oz, dx, dy, dz, maxT, box) {
  let tmin = 0.02;
  let tmax = maxT - 0.03;
  const dirs = [dx, dy, dz];
  const orig = [ox, oy, oz];
  for (let axis = 0; axis < 3; axis++) {
    const inv = Math.abs(dirs[axis]) > 1e-6 ? 1.0 / dirs[axis] : 1e9;
    let t0 = (box.min[axis] - orig[axis]) * inv;
    let t1 = (box.max[axis] - orig[axis]) * inv;
    if (inv < 0) { const tmp = t0; t0 = t1; t1 = tmp; }
    tmin = Math.max(tmin, t0);
    tmax = Math.min(tmax, t1);
    if (tmax < tmin) return false;
  }
  return true;
}

export function ComputeFloorVisibility(px, py, pz, lx, ly, lz) {
  const vx = lx - px, vy = ly - py, vz = lz - pz;
  const dist = Math.hypot(vx, vy, vz);
  if (dist < 1e-4) return 1.0;
  const invD = 1.0 / dist;
  const dx = vx * invD, dy = vy * invD, dz = vz * invD;
  for (let i = 0; i < SCENE_BOXES.length; i++) {
    if (RayIntersectsBox(px, py, pz, dx, dy, dz, dist, SCENE_BOXES[i])) return 0.0;
  }
  return 1.0;
}

export function EvaluateSingleLightAtFloor(px, pz, light, useShadows = true) {
  const py = 0.02;
  const lx = light.pos[0] - px;
  const ly = light.pos[1] - py;
  const lz = light.pos[2] - pz;
  const distSq = lx * lx + ly * ly + lz * lz + 0.35;
  const dist = Math.sqrt(distSq);
  const invD = 1.0 / dist;
  const wx = lx * invD, wy = ly * invD, wz = lz * invD;
  if (wy <= 0.0) return [0, 0, 0, 0, 0, 0];

  let spot = 1.0;
  if (light.cosCone > -0.95) {
    const cosAngle = -(wx * light.dir[0] + wy * light.dir[1] + wz * light.dir[2]);
    const edge0 = light.cosCone;
    const edge1 = Math.min(0.99, light.cosCone + 0.30);
    const t = Math.max(0.0, Math.min(1.0, (cosAngle - edge0) / Math.max(1e-4, edge1 - edge0)));
    spot = t * t * (3.0 - 2.0 * t);
    if (spot <= 1e-4) return [0, 0, 0, 0, 0, 0];
  }

  const vis = useShadows ? ComputeFloorVisibility(px, py, pz, light.pos[0], light.pos[1], light.pos[2]) : 1.0;
  if (vis <= 0.0) return [0, 0, 0, 0, 0, 0];

  const atten = (light.flux * wy * spot * vis) / (distSq * Math.PI);
  return [light.color[0] * atten, light.color[1] * atten, light.color[2] * atten, wx, wy, wz];
}

//------------------------------------------------------------------------------------------------------------------------
//                           1. NAIVE POINT MERGE (THE "AVERAGED HEADLIGHTS" BAD BASELINE)
//------------------------------------------------------------------------------------------------------------------------

function ComputeClusterRadius(list, center) {
  let maxR = 0.05;
  for (const l of list) {
    const d = Math.hypot(l.pos[0] - center[0], l.pos[1] - center[1], l.pos[2] - center[2]);
    if (d > maxR) maxR = d;
  }
  return maxR;
}

export function BuildNaiveMergedLights(rawLights) {
  const byGroup = new Map();
  for (const l of rawLights) {
    if (!byGroup.has(l.group)) byGroup.set(l.group, []);
    byGroup.get(l.group).push(l);
  }
  const merged = [];
  let idx = 0;
  for (const [groupName, list] of byGroup.entries()) {
    let px = 0, py = 0, pz = 0, dx = 0, dy = 0, dz = 0, cr = 0, cg = 0, cb = 0, fluxSum = 0;
    let minCos = 1.0;
    for (const l of list) {
      px += l.pos[0]; py += l.pos[1]; pz += l.pos[2];
      dx += l.dir[0]; dy += l.dir[1]; dz += l.dir[2];
      cr += l.color[0] * l.flux; cg += l.color[1] * l.flux; cb += l.color[2] * l.flux;
      fluxSum += l.flux;
      minCos = Math.min(minCos, l.cosCone);
    }
    const n = list.length;
    const dLen = Math.hypot(dx, dy, dz);
    merged.push({
      id: idx++,
      group: groupName,
      subGroup: groupName,
      mobility: list[0].mobility,
      memberCount: n,
      members: list,
      pos: [px / n, py / n, pz / n],
      dir: dLen > 0.05 ? [dx / dLen, dy / dLen, dz / dLen] : [0, -1, 0],
      cosCone: dLen > 0.05 ? minCos : -1.0,
      color: [cr / Math.max(1e-5, fluxSum), cg / Math.max(1e-5, fluxSum), cb / Math.max(1e-5, fluxSum)],
      flux: fluxSum,
      radius: ComputeClusterRadius(list, [px / n, py / n, pz / n]),
      isNaiveWarning: groupName === 'Headlights' || groupName === 'OpposingNeon',
    });
  }
  return merged;
}

//------------------------------------------------------------------------------------------------------------------------
//                           2. 1 CUBE-GRID / POD ATLAS (95.1 KB / POD — PRESERVES DIRECTIONAL BEAMS!)
//------------------------------------------------------------------------------------------------------------------------

/**
 * Merges each tight photometric pod (subGroup: e.g. Left Headlight 5 LEDs -> 1 Pod, Right Headlight 5 LEDs -> 1 Pod,
 * Lantern 5 -> 1 Pod, Neon West 4 -> 1 Pod, Neon East 4 -> 1 Pod, Drone 3 -> 1 Pod) into 1 Directional Cube-Grid Pod!
 * Unlike Naive Point Merge, each Cube-Grid Pod preserves tight sub-fixture separation & exact cone directionality!
 */
export function BuildCubeGridPods(lights) {
  const byPod = new Map();
  for (const l of lights) {
    const key = `${l.mobility}:${l.subGroup}`;
    if (!byPod.has(key)) byPod.set(key, []);
    byPod.get(key).push(l);
  }
  const pods = [];
  let idx = 0;
  for (const [, list] of byPod.entries()) {
    let fluxSum = 0, px = 0, py = 0, pz = 0, dx = 0, dy = 0, dz = 0, cr = 0, cg = 0, cb = 0;
    let minCos = 1.0;
    for (const l of list) {
      fluxSum += l.flux;
      px += l.pos[0] * l.flux; py += l.pos[1] * l.flux; pz += l.pos[2] * l.flux;
      dx += l.dir[0] * l.flux; dy += l.dir[1] * l.flux; dz += l.dir[2] * l.flux;
      cr += l.color[0] * l.flux; cg += l.color[1] * l.flux; cb += l.color[2] * l.flux;
      minCos = Math.min(minCos, l.cosCone);
    }
    const invF = 1.0 / Math.max(1e-5, fluxSum);
    const pos = [px * invF, py * invF, pz * invF];
    const dLen = Math.hypot(dx, dy, dz);
    pods.push({
      id: idx++,
      podLabel: `${list[0].subGroup} (${list.length}→1 Pod)`,
      group: list[0].group,
      subGroup: list[0].subGroup,
      mobility: list[0].mobility,
      isCubePod: true,
      vramKb: ONE_CUBE_GRID_POD_KB,
      memberCount: list.length,
      members: list,
      pos,
      dir: dLen > 1e-4 ? [dx / dLen, dy / dLen, dz / dLen] : [0, -1, 0],
      cosCone: minCos,
      color: [cr * invF, cg * invF, cb * invF],
      flux: fluxSum,
      radius: ComputeClusterRadius(list, pos),
    });
  }
  return pods;
}

//------------------------------------------------------------------------------------------------------------------------
//                           3. NANITE-FOR-LIGHTS AGGLOMERATIVE LIGHT BVH (DISTANCE-ERROR HIERARCHY)
//------------------------------------------------------------------------------------------------------------------------

function CreateBvhNodeStats(list) {
  let fluxSum = 0, px = 0, py = 0, pz = 0, cr = 0, cg = 0, cb = 0, dx = 0, dy = 0, dz = 0;
  let minCos = 1.0;
  for (const l of list) {
    fluxSum += l.flux;
    px += l.pos[0] * l.flux; py += l.pos[1] * l.flux; pz += l.pos[2] * l.flux;
    cr += l.color[0] * l.flux; cg += l.color[1] * l.flux; cb += l.color[2] * l.flux;
    dx += l.dir[0] * l.flux; dy += l.dir[1] * l.flux; dz += l.dir[2] * l.flux;
    minCos = Math.min(minCos, l.cosCone);
  }
  const invF = 1.0 / Math.max(1e-6, fluxSum);
  const pos = [px * invF, py * invF, pz * invF];
  const color = [cr * invF, cg * invF, cb * invF];
  const dLen = Math.hypot(dx, dy, dz);
  const dir = dLen > 1e-4 ? [dx / dLen, dy / dLen, dz / dLen] : [0, -1, 0];
  const dirCoherence = dLen * invF;
  const radius = ComputeClusterRadius(list, pos);
  let colorSpread = 0;
  for (const l of list) {
    colorSpread = Math.max(colorSpread, Math.hypot(l.color[0] - color[0], l.color[1] - color[1], l.color[2] - color[2]));
  }
  const allStatic = list.every((l) => l.mobility === 'static');
  const allDynamic = list.every((l) => l.mobility === 'dynamic');
  return {
    pos, dir,
    cosCone: dirCoherence > 0.72 ? minCos : -1.0,
    color,
    flux: fluxSum,
    radius,
    dirCoherence,
    colorSpread,
    mobility: allStatic ? 'static' : allDynamic ? 'dynamic' : 'mixed',
    memberCount: list.length,
    members: list,
  };
}

export function BuildLightBvh(rawLights) {
  if (!rawLights || rawLights.length === 0) return null;
  let nextNodeId = 0;
  const active = rawLights.map((l) => ({
    id: nextNodeId++,
    depth: 0,
    ...CreateBvhNodeStats([l]),
    left: null,
    right: null,
  }));

  while (active.length > 1) {
    let bestI = 0, bestJ = 1, bestCost = Infinity;
    for (let i = 0; i < active.length; i++) {
      for (let j = i + 1; j < active.length; j++) {
        const a = active[i], b = active[j];
        const dist = Math.hypot(a.pos[0] - b.pos[0], a.pos[1] - b.pos[1], a.pos[2] - b.pos[2]);
        const dirDot = a.dir[0] * b.dir[0] + a.dir[1] * b.dir[1] + a.dir[2] * b.dir[2];
        const dirPenalty = (1.0 - Math.max(-1.0, Math.min(1.0, dirDot))) * 3.5;
        const colDist = Math.hypot(a.color[0] - b.color[0], a.color[1] - b.color[1], a.color[2] - b.color[2]) * 3.0;
        const conePenalty = Math.abs(a.cosCone - b.cosCone) * 1.4;
        const mobPenalty = a.mobility !== b.mobility ? 2.8 : 0.0; // Keeps Static & Dynamic subtrees cleanly separated!
        const cost = Math.max(a.radius, b.radius) + dist * 0.5 + dirPenalty + colDist + conePenalty + mobPenalty;
        if (cost < bestCost) { bestCost = cost; bestI = i; bestJ = j; }
      }
    }
    const left = active[bestI], right = active[bestJ];
    const parent = {
      id: nextNodeId++,
      depth: Math.max(left.depth, right.depth) + 1,
      ...CreateBvhNodeStats([...left.members, ...right.members]),
      left, right,
    };
    active.splice(bestJ, 1);
    active.splice(bestI, 1);
    active.push(parent);
  }
  return active[0];
}

export function ComputeLightCutForReceiver(bvhRoot, rx, ry, rz, errorThreshold = 0.24, maxActiveLights = 12) {
  if (!bvhRoot) return [];
  function scoreNode(n) {
    if (!n.left || !n.right) return 0.0;
    const dist = Math.max(0.5, Math.hypot(n.pos[0] - rx, n.pos[1] - ry, n.pos[2] - rz));
    const fluxScale = Math.sqrt(Math.max(1.0, n.flux) / 14.0);
    return ((n.radius / dist) + (1.0 - n.dirCoherence) * (2.6 / dist) + n.colorSpread * (2.2 / dist)) * fluxScale;
  }

  const active = [{ node: bvhRoot, err: scoreNode(bvhRoot) }];
  while (active.length < maxActiveLights) {
    active.sort((a, b) => b.err - a.err);
    const top = active[0];
    if (!top || !top.node.left || top.err <= errorThreshold) break;
    active.shift();
    active.push({ node: top.node.left, err: scoreNode(top.node.left) });
    active.push({ node: top.node.right, err: scoreNode(top.node.right) });
  }
  return active.map((it) => ({
    ...it.node,
    nodeError: it.err,
    lodLevel: it.node.memberCount === 1 ? 0 : it.node.radius < 0.45 ? 1 : 2,
  }));
}

//------------------------------------------------------------------------------------------------------------------------
//                  4. AUTO-BAKED 2-CASCADE TOROIDAL CLIPMAP (FINE FOLLOWS PROBE + COARSE COVERS WORLD)
//------------------------------------------------------------------------------------------------------------------------

function BakeSingleCascade(lights, res, centerX, centerZ, halfSpan, toroidalScroll = true) {
  const cellSize = (halfSpan * 2.0) / res;
  // Toroidal snapping: lock grid origin to integer cell multiples so voxels NEVER swim as focus moves!
  const snapX = toroidalScroll ? Math.round(centerX / cellSize) * cellSize : 0.0;
  const snapZ = toroidalScroll ? Math.round(centerZ / cellSize) * cellSize : 0.0;
  const originCellX = Math.round(snapX / cellSize);
  const originCellZ = Math.round(snapZ / cellSize);

  const cells = new Array(res * res);
  const texIrr = new Float32Array(res * res * 4);
  const texDir = new Float32Array(res * res * 4);
  let maxErr = 1e-4;

  for (let gz = 0; gz < res; gz++) {
    for (let gx = 0; gx < res; gx++) {
      const cx = snapX - halfSpan + (gx + 0.5) * cellSize;
      const cz = snapZ - halfSpan + (gz + 0.5) * cellSize;

      let sumR = 0, sumG = 0, sumB = 0;
      let vx = 0, vy = 0, vz = 0, lumSum = 0;

      for (let i = 0; i < lights.length; i++) {
        const resL = EvaluateSingleLightAtFloor(cx, cz, lights[i], true);
        const lum = resL[0] * 0.2126 + resL[1] * 0.7152 + resL[2] * 0.0722;
        if (lum > 1e-5) {
          sumR += resL[0]; sumG += resL[1]; sumB += resL[2];
          vx += resL[3] * lum; vy += resL[4] * lum; vz += resL[5] * lum;
          lumSum += lum;
        }
      }

      const vLen = Math.hypot(vx, vy, vz);
      const directionality = lumSum > 1e-5 ? Math.min(1.0, vLen / lumSum) : 0.0;
      const domDir = vLen > 1e-5 ? [vx / vLen, vy / vLen, vz / vLen] : [0, 1, 0];

      // Stratified 4-corner residual error check inside this toroidal cell
      const off = cellSize * 0.34;
      let subErr = 0;
      const corners = [[cx - off, cz - off], [cx + off, cz - off], [cx - off, cz + off], [cx + off, cz + off]];
      for (const pr of corners) {
        let tr = 0, tg = 0, tb = 0;
        for (let i = 0; i < lights.length; i++) {
          const r2 = EvaluateSingleLightAtFloor(pr[0], pr[1], lights[i], true);
          tr += r2[0]; tg += r2[1]; tb += r2[2];
        }
        const dr = tr - sumR, dg = tg - sumG, db = tb - sumB;
        subErr += Math.sqrt((dr * dr + dg * dg + db * db) / 3.0);
      }
      const cellErr = subErr * 0.25 + (1.0 - directionality) * Math.min(2.5, lumSum) * 0.65;
      if (cellErr > maxErr) maxErr = cellErr;

      // Toroidal ring-buffer physical address (gx + originCellX) mod res
      const physX = ((gx + originCellX) % res + res) % res;
      const physZ = ((gz + originCellZ) % res + res) % res;
      const idx = gz * res + gx;

      cells[idx] = {
        gx, gz, physX, physZ, cx, cz,
        irradiance: [sumR, sumG, sumB],
        domDir, directionality, cellError: cellErr,
      };
      texIrr[idx * 4 + 0] = sumR; texIrr[idx * 4 + 1] = sumG; texIrr[idx * 4 + 2] = sumB; texIrr[idx * 4 + 3] = directionality;
      texDir[idx * 4 + 0] = domDir[0]; texDir[idx * 4 + 1] = domDir[1]; texDir[idx * 4 + 2] = domDir[2]; texDir[idx * 4 + 3] = cellErr;
    }
  }

  return {
    res, halfSpan, cellSize,
    centerX: snapX, centerZ: snapZ,
    originCellX, originCellZ,
    cells, texIrr, texDir, maxErr,
    vramBytes: res * res * 16, // 2 x RGBA16F (8B + 8B) per toroidal cell
  };
}

/**
 * Automatically bakes a 2-Cascade Toroidal Clipmap (Fine cascade centered around focus/probe + Coarse cascade
 * covering full world extent) for STATIC lights (or ALL lights when comparing pure clipmap mode).
 */
export function BakeToroidalClipmap(lights, focusX, focusZ, scaleCfg, fineResOverride = null) {
  const t0 = performance.now();
  const fineRes = fineResOverride || scaleCfg.fineRes;
  const coarseRes = scaleCfg.coarseRes;
  const fine = BakeSingleCascade(lights, fineRes, focusX, focusZ, scaleCfg.fineHalfSpan, true);
  const coarse = BakeSingleCascade(lights, coarseRes, 0.0, 0.0, scaleCfg.coarseHalfSpan, false);
  return {
    fine,
    coarse,
    bakeMs: performance.now() - t0,
    totalVramKb: (fine.vramBytes + coarse.vramBytes) / 1024.0,
    lightCountBaked: lights.length,
  };
}

function SampleCascadeCpu(casc, px, pz) {
  const lx = (px - (casc.centerX - casc.halfSpan)) / (casc.halfSpan * 2.0);
  const lz = (pz - (casc.centerZ - casc.halfSpan)) / (casc.halfSpan * 2.0);
  if (lx < 0.02 || lx > 0.98 || lz < 0.02 || lz > 0.98) return null;
  const gx = Math.min(casc.res - 1, Math.max(0, Math.floor(lx * casc.res)));
  const gz = Math.min(casc.res - 1, Math.max(0, Math.floor(lz * casc.res)));
  return casc.cells[gz * casc.res + gx];
}

export function SampleToroidalClipmapCpu(clipmap, px, pz) {
  const fineCell = SampleCascadeCpu(clipmap.fine, px, pz);
  if (fineCell) return { cell: fineCell, cascade: 0 };
  const coarseCell = SampleCascadeCpu(clipmap.coarse, px, pz) || clipmap.coarse.cells[0];
  return { cell: coarseCell, cascade: 1 };
}

//------------------------------------------------------------------------------------------------------------------------
//                  5. EXACT VRAM CALCULATOR + QUANTITATIVE RMSE % EVALUATION ACROSS ALL 5 MODES
//------------------------------------------------------------------------------------------------------------------------

export function ComputeModeVramMb(modeKey, viewportW, viewportH, rawCount, podCount, clipmap) {
  // Base G-Buffer + HDR + Depth footprint (matches reference benchmark layout: ~16-20 MB at ~780x590)
  const baseRtMb = Math.max(4.0, (viewportW * viewportH * 42) / (1024 * 1024));
  let lightAtlasKb = 0;

  if (modeKey === 'exact') {
    lightAtlasKb = (rawCount * 64) / 1024; // 64B GPU struct per light
  } else if (modeKey === 'naive') {
    lightAtlasKb = (6 * 64) / 1024;
  } else if (modeKey === 'pods') {
    lightAtlasKb = podCount * ONE_CUBE_GRID_POD_KB; // 95.1 KB per Cube-Grid Pod!
  } else if (modeKey === 'clipmap') {
    lightAtlasKb = clipmap ? clipmap.totalVramKb * 16.0 : 512; // 3D toroidal Fine+Coarse clipmap budget
  } else {
    // Unified All-3: Static Toroidal Clipmap + Dynamic Cube-Grid Pods + Nanite BVH table
    const dynPods = Math.max(1, podCount);
    const clipKb = clipmap ? clipmap.totalVramKb * 16.0 : 512;
    lightAtlasKb = clipKb + dynPods * ONE_CUBE_GRID_POD_KB + 48.0;
  }

  return {
    baseRtMb,
    lightAtlasKb,
    totalVramMb: baseRtMb + lightAtlasKb / 1024.0,
  };
}

/**
 * Evaluates exact quantitative RMSE (expressed as % relative to Reference, matching reference benchmark),
 * PSNR (dB), and average light evaluations / pixel across the scene floor for any of the 5 pipelines!
 */
export function EvaluateModeErrorStats(modeKey, ctx) {
  const { rawLights, naiveLights, allPods, dynPods, dynBvh, staticBvh, staticClipmap, opts } = ctx;
  const samples = 18;
  const step = (WORLD_HALF_EXTENT * 2.0) / samples;
  let sqErrSum = 0, sqRefSum = 0, evalsSum = 0;
  const nTotal = samples * samples;
  const tm = (v) => v / (1.0 + v);

  for (let sz = 0; sz < samples; sz++) {
    for (let sx = 0; sx < samples; sx++) {
      const px = -WORLD_HALF_EXTENT + (sx + 0.5) * step;
      const pz = -WORLD_HALF_EXTENT + (sz + 0.5) * step;

      let tr = 0, tg = 0, tb = 0;
      for (let i = 0; i < rawLights.length; i++) {
        const res = EvaluateSingleLightAtFloor(px, pz, rawLights[i], true);
        tr += res[0]; tg += res[1]; tb += res[2];
      }

      let ar = 0, ag = 0, ab = 0, evals = 0;
      if (modeKey === 'exact') {
        ar = tr; ag = tg; ab = tb; evals = rawLights.length;
      } else if (modeKey === 'naive') {
        evals = naiveLights.length;
        for (const l of naiveLights) {
          const r = EvaluateSingleLightAtFloor(px, pz, l, true);
          ar += r[0]; ag += r[1]; ab += r[2];
        }
      } else if (modeKey === 'pods') {
        evals = allPods.length;
        for (const p of allPods) {
          const r = EvaluateSingleLightAtFloor(px, pz, p, true);
          ar += r[0]; ag += r[1]; ab += r[2];
        }
      } else if (modeKey === 'clipmap') {
        // Static lights from 2-Cascade Toroidal Clipmap (1 lookup) + Dynamic lights from Cube-Grid Pods!
        const sSample = SampleToroidalClipmapCpu(staticClipmap, px, pz);
        ar += sSample.cell.irradiance[0]; ag += sSample.cell.irradiance[1]; ab += sSample.cell.irradiance[2];
        evals = 1 + dynPods.length;
        for (const p of dynPods) {
          const r = EvaluateSingleLightAtFloor(px, pz, p, true);
          ar += r[0]; ag += r[1]; ab += r[2];
        }
      } else {
        // Unified All-3: Static Toroidal Clipmap (1 O(1) sample where smooth) + Nanite BVH promotion in high-error cells + Dynamic Nanite BVH!
        const sSample = SampleToroidalClipmapCpu(staticClipmap, px, pz);
        if (sSample.cell.cellError <= opts.hybridErrorGate) {
          ar += sSample.cell.irradiance[0]; ag += sSample.cell.irradiance[1]; ab += sSample.cell.irradiance[2];
          evals += 1;
        } else if (staticBvh) {
          const sCut = ComputeLightCutForReceiver(staticBvh, px, 0.02, pz, opts.errorThreshold * 0.85, opts.maxActiveLights);
          evals += sCut.length;
          for (const sNode of sCut) {
            const r = EvaluateSingleLightAtFloor(px, pz, sNode, true);
            ar += r[0]; ag += r[1]; ab += r[2];
          }
        }
        // Dynamic System: Nanite Distance-Error BVH over dynamic lights!
        const dynCut = dynBvh
          ? ComputeLightCutForReceiver(dynBvh, px, 0.02, pz, opts.errorThreshold * 0.75, opts.maxActiveLights)
          : [];
        evals += dynCut.length;
        for (const d of dynCut) {
          const r = EvaluateSingleLightAtFloor(px, pz, d, true);
          ar += r[0]; ag += r[1]; ab += r[2];
        }
      }

      const rT = tm(tr), gT = tm(tg), bT = tm(tb);
      const rA = tm(ar), gA = tm(ag), bA = tm(ab);
      const dr = rT - rA, dg = gT - gA, db = bT - bA;
      sqErrSum += (dr * dr + dg * dg + db * db) / 3.0;
      sqRefSum += (rT * rT + gT * gT + bT * bT) / 3.0;
      evalsSum += evals;
    }
  }

  const rmse = Math.sqrt(sqErrSum / nTotal);
  const refRms = Math.sqrt(Math.max(1e-6, sqRefSum / nTotal));
  const rmsePct = modeKey === 'exact' ? 0.0 : (rmse / refRms) * 100.0;
  const psnr = rmse > 1e-6 ? 20.0 * Math.log10(1.0 / rmse) : 99.9;
  const avgEvals = evalsSum / nTotal;
  return {
    rmse,
    rmsePct,
    psnr,
    avgEvals,
    speedup: rawLights.length / Math.max(1.0, avgEvals),
  };
}
