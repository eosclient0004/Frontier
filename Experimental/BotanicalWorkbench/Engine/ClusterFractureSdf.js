/*============================================================================================================================================
                                                        CLUSTERFRACTURESDF.JS
============================================================================================================================================*/
// 🧩 128-triangle LOD cluster partitioner, procedural bark/foliage/UV textures, trunk fracture splitter, and live wind-deformed SDF/GDF section

import { DisplacePointByUnifiedWind } from './UnifiedWindField.js';

export const MAX_TRIANGLES_PER_CLUSTER = 128;

function HashClusterRgb(clusterId) {
  // Deterministic perceptually separated HSV -> RGB for 128-tri cluster visualization
  const hue = (clusterId * 0.618033988749895) % 1.0;
  const sat = 0.68 + 0.22 * ((clusterId * 37) % 5) / 4.0;
  const val = 0.78 + 0.18 * ((clusterId * 17) % 3) / 2.0;
  const i = Math.floor(hue * 6.0);
  const f = hue * 6.0 - i;
  const p = val * (1.0 - sat);
  const q = val * (1.0 - f * sat);
  const t = val * (1.0 - (1.0 - f) * sat);
  switch (i % 6) {
    case 0: return [val, t, p];
    case 1: return [q, val, p];
    case 2: return [p, val, t];
    case 3: return [p, q, val];
    case 4: return [t, p, val];
    default: return [val, p, q];
  }
}

/**
 * Partitions an indexed surface into 128-triangle clusters matching Frontier's ClusterPyramid.h.
 * Emits non-indexed cluster-tagged vertex attributes so each 128-tri cluster renders cleanly in Cluster mode.
 */
export function BuildClusteredBuffers(surfaceData, clusterIdOffset = 0, includeTint = false) {
  const idx = surfaceData.indices;
  const triCount = Math.floor(idx.length / 3);
  const outVerts = triCount * 3;
  const pos = new Float32Array(outVerts * 3);
  const nrm = new Float32Array(outVerts * 3);
  const uv = new Float32Array(outVerts * 2);
  const wind = new Float32Array(outVerts * 4);
  const piv = new Float32Array(outVerts * 3);
  const col = new Float32Array(outVerts * 3);
  const tint = includeTint ? new Float32Array(outVerts) : null;

  const clusterCount = Math.ceil(triCount / MAX_TRIANGLES_PER_CLUSTER);
  const clusterSpheres = [];

  for (let c = 0; c < clusterCount; c++) {
    const startTri = c * MAX_TRIANGLES_PER_CLUSTER;
    const endTri = Math.min(triCount, startTri + MAX_TRIANGLES_PER_CLUSTER);
    const rgb = HashClusterRgb(clusterIdOffset + c);
    let cx = 0, cy = 0, cz = 0;
    let countV = 0;

    for (let t = startTri; t < endTri; t++) {
      for (let k = 0; k < 3; k++) {
        const srcV = idx[t * 3 + k];
        const dstV = t * 3 + k;
        const px = surfaceData.positions[srcV * 3];
        const py = surfaceData.positions[srcV * 3 + 1];
        const pz = surfaceData.positions[srcV * 3 + 2];
        pos[dstV * 3] = px;
        pos[dstV * 3 + 1] = py;
        pos[dstV * 3 + 2] = pz;
        cx += px; cy += py; cz += pz;
        countV++;

        nrm[dstV * 3] = surfaceData.normals[srcV * 3];
        nrm[dstV * 3 + 1] = surfaceData.normals[srcV * 3 + 1];
        nrm[dstV * 3 + 2] = surfaceData.normals[srcV * 3 + 2];

        uv[dstV * 2] = surfaceData.uvs[srcV * 2];
        uv[dstV * 2 + 1] = surfaceData.uvs[srcV * 2 + 1];

        wind[dstV * 4] = surfaceData.wind[srcV * 4];
        wind[dstV * 4 + 1] = surfaceData.wind[srcV * 4 + 1];
        wind[dstV * 4 + 2] = surfaceData.wind[srcV * 4 + 2];
        wind[dstV * 4 + 3] = surfaceData.wind[srcV * 4 + 3];

        piv[dstV * 3] = surfaceData.pivots[srcV * 3];
        piv[dstV * 3 + 1] = surfaceData.pivots[srcV * 3 + 1];
        piv[dstV * 3 + 2] = surfaceData.pivots[srcV * 3 + 2];

        col[dstV * 3] = rgb[0];
        col[dstV * 3 + 1] = rgb[1];
        col[dstV * 3 + 2] = rgb[2];

        if (includeTint && surfaceData.tintVar) {
          tint[dstV] = surfaceData.tintVar[srcV];
        }
      }
    }

    if (countV > 0) {
      cx /= countV; cy /= countV; cz /= countV;
      let maxR = 0.05;
      for (let t = startTri; t < endTri; t++) {
        for (let k = 0; k < 3; k++) {
          const dstV = t * 3 + k;
          const d = Math.hypot(pos[dstV * 3] - cx, pos[dstV * 3 + 1] - cy, pos[dstV * 3 + 2] - cz);
          if (d > maxR) maxR = d;
        }
      }
      clusterSpheres.push({ id: clusterIdOffset + c, center: [cx, cy, cz], radius: maxR, triangles: endTri - startTri });
    }
  }

  return { positions: pos, normals: nrm, uvs: uv, wind, pivots: piv, clusterColors: col, tint, clusterCount, clusterSpheres, triangleCount: triCount };
}

/**
 * Splits a botanical specimen into a rooted lower stump and a fractured upper crown piece
 * along a Voronoi-jagged horizontal break seam at normalized height `cutHeightNorm`.
 */
export function SplitSpecimenForFracture(clusteredBark, clusteredFoliage, specimenHeight, cutHeightNorm = 0.28) {
  const cutY = Math.max(0.4, Math.min(specimenHeight * 0.78, specimenHeight * cutHeightNorm));

  function jaggedThreshold(x, z) {
    const ang = Math.atan2(z, x);
    return cutY + Math.sin(ang * 5.0) * 0.14 + Math.cos(ang * 11.0 + 0.8) * 0.07;
  }

  function partitionSurface(src, includeTint) {
    const lower = { positions: [], normals: [], uvs: [], wind: [], pivots: [], clusterColors: [], tint: [] };
    const upper = { positions: [], normals: [], uvs: [], wind: [], pivots: [], clusterColors: [], tint: [] };
    const tris = src.triangleCount;
    for (let t = 0; t < tris; t++) {
      const i0 = t * 3;
      const avgX = (src.positions[i0 * 3] + src.positions[(i0 + 1) * 3] + src.positions[(i0 + 2) * 3]) / 3.0;
      const avgY = (src.positions[i0 * 3 + 1] + src.positions[(i0 + 1) * 3 + 1] + src.positions[(i0 + 2) * 3 + 1]) / 3.0;
      const avgZ = (src.positions[i0 * 3 + 2] + src.positions[(i0 + 1) * 3 + 2] + src.positions[(i0 + 2) * 3 + 2]) / 3.0;
      const dst = avgY >= jaggedThreshold(avgX, avgZ) ? upper : lower;
      for (let k = 0; k < 3; k++) {
        const v = i0 + k;
        dst.positions.push(src.positions[v * 3], src.positions[v * 3 + 1], src.positions[v * 3 + 2]);
        dst.normals.push(src.normals[v * 3], src.normals[v * 3 + 1], src.normals[v * 3 + 2]);
        dst.uvs.push(src.uvs[v * 2], src.uvs[v * 2 + 1]);
        dst.wind.push(src.wind[v * 4], src.wind[v * 4 + 1], src.wind[v * 4 + 2], src.wind[v * 4 + 3]);
        dst.pivots.push(src.pivots[v * 3], src.pivots[v * 3 + 1], src.pivots[v * 3 + 2]);
        dst.clusterColors.push(src.clusterColors[v * 3], src.clusterColors[v * 3 + 1], src.clusterColors[v * 3 + 2]);
        if (includeTint && src.tint) dst.tint.push(src.tint[v]);
      }
    }
    const finalize = (bag) => ({
      positions: new Float32Array(bag.positions),
      normals: new Float32Array(bag.normals),
      uvs: new Float32Array(bag.uvs),
      wind: new Float32Array(bag.wind),
      pivots: new Float32Array(bag.pivots),
      clusterColors: new Float32Array(bag.clusterColors),
      tint: includeTint ? new Float32Array(bag.tint) : null,
      triangleCount: bag.positions.length / 9,
    });
    return { lower: finalize(lower), upper: finalize(upper) };
  }

  return {
    cutY,
    bark: partitionSurface(clusteredBark, false),
    foliage: partitionSurface(clusteredFoliage, true),
  };
}

/**
 * Generates procedural tileable PBR Bark Albedo canvas (fissures, plates, birch lenticels, etc.)
 */
export function ConstructBarkTextureCanvas(visual) {
  const size = 256;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d');
  ctx.fillStyle = visual.bark;
  ctx.fillRect(0, 0, size, size);

  const isBirch = visual.bark.toLowerCase() === '#dedad2' || visual.bark.toLowerCase() === '#cfcac0';

  if (isBirch) {
    // Horizontal dark lenticels characteristic of Birch & Aspen
    for (let i = 0; i < 140; i++) {
      const x = (i * 73) % size;
      const y = (i * 41) % size;
      const w = 14 + (i * 19) % 38;
      const h = 1.5 + (i % 3);
      ctx.fillStyle = visual.barkDark;
      ctx.globalAlpha = 0.45 + (i % 5) * 0.1;
      ctx.fillRect(x, y, w, h);
    }
  } else {
    // Vertical fibrous bark furrows & ridges
    for (let i = 0; i < 180; i++) {
      const x = (i * 53) % size;
      const y = (i * 29) % size;
      const w = 2 + (i % 5);
      const h = 38 + (i * 17) % 90;
      ctx.fillStyle = i % 3 === 0 ? visual.barkDark : '#000000';
      ctx.globalAlpha = i % 3 === 0 ? 0.55 : 0.22;
      ctx.fillRect(x, y, w, h);
      if (y + h > size) ctx.fillRect(x, y - size, w, h);
    }
    for (let i = 0; i < 80; i++) {
      const x = (i * 67 + 11) % size;
      const y = (i * 43) % size;
      ctx.fillStyle = '#ffffff';
      ctx.globalAlpha = 0.08;
      ctx.fillRect(x, y, 2, 26 + (i % 34));
    }
  }
  ctx.globalAlpha = 1.0;
  return canvas;
}

/**
 * Generates a high-contrast UV Unwrap Verification Checker Canvas with explicit U/V axes
 */
export function ConstructUvCheckerCanvas() {
  const size = 256;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d');
  const cells = 8;
  const cell = size / cells;
  for (let y = 0; y < cells; y++) {
    for (let x = 0; x < cells; x++) {
      const even = (x + y) % 2 === 0;
      ctx.fillStyle = even ? '#262930' : '#4a5568';
      ctx.fillRect(x * cell, y * cell, cell, cell);
      if (x === 0 || y === 0) {
        ctx.fillStyle = x === 0 ? '#ffb454' : '#34c759';
        ctx.fillRect(x * cell + 2, y * cell + 2, cell - 4, 4);
      }
      ctx.fillStyle = even ? '#cbd5e1' : '#e2e8f0';
      ctx.font = 'bold 10px monospace';
      ctx.fillText(`${x},${cells - 1 - y}`, x * cell + 5, y * cell + 20);
    }
  }
  return canvas;
}

/**
 * Generates procedural species-matched Foliage Spray Cutout Canvas (Broadleaf, Conifer, Willow, Succulent)
 */
export function ConstructLeafTextureCanvas(visual) {
  const size = 256;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0, 0, size, size);

  const style = visual.leafStyle || 'broadleaf';

  function drawBlade(cx, cy, rx, ry, rot, fill) {
    ctx.save();
    ctx.translate(cx, cy);
    ctx.rotate(rot);
    ctx.beginPath();
    ctx.moveTo(0, ry);
    ctx.bezierCurveTo(rx, ry * 0.35, rx * 0.85, -ry * 0.5, 0, -ry);
    ctx.bezierCurveTo(-rx * 0.85, -ry * 0.5, -rx, ry * 0.35, 0, ry);
    ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();
    ctx.strokeStyle = 'rgba(0,0,0,0.22)';
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    ctx.moveTo(0, ry * 0.85);
    ctx.lineTo(0, -ry * 0.78);
    ctx.stroke();
    ctx.restore();
  }

  if (style === 'conifer') {
    // Needle spray fan
    ctx.strokeStyle = visual.bark;
    ctx.lineWidth = 4;
    ctx.beginPath();
    ctx.moveTo(128, 244);
    ctx.lineTo(128, 28);
    ctx.stroke();
    for (let i = 0; i < 38; i++) {
      const y = 230 - i * 5.2;
      const spread = 24 + Math.sin((i / 38) * Math.PI) * 64;
      ctx.strokeStyle = i % 2 === 0 ? visual.leaf : visual.leafAlt;
      ctx.lineWidth = 4.5;
      ctx.beginPath();
      ctx.moveTo(128, y);
      ctx.lineTo(128 - spread, y - 24);
      ctx.moveTo(128, y);
      ctx.lineTo(128 + spread, y - 24);
      ctx.stroke();
    }
  } else if (style === 'willow') {
    // Slender drooping willow / bamboo lanceolate cluster
    for (let i = 0; i < 9; i++) {
      const ang = (i - 4) * 0.19;
      drawBlade(128 + Math.sin(ang) * 44, 134 - Math.cos(ang) * 18, 12, 74, ang, i % 2 === 0 ? visual.leaf : visual.leafAlt);
    }
  } else {
    // Broadleaf multi-leaf botanical spray (5-7 overlapping leaves on a petiole)
    ctx.strokeStyle = visual.bark;
    ctx.lineWidth = 3;
    ctx.beginPath();
    ctx.moveTo(128, 248);
    ctx.lineTo(128, 52);
    ctx.stroke();
    const blades = [
      [128, 64, 34, 52, 0, visual.leafAlt],
      [82, 108, 32, 48, -0.58, visual.leaf],
      [174, 108, 32, 48, 0.58, visual.leaf],
      [74, 166, 30, 44, -0.78, visual.leafAlt],
      [182, 166, 30, 44, 0.78, visual.leafAlt],
      [104, 198, 24, 36, -0.45, visual.leaf],
      [152, 198, 24, 36, 0.45, visual.leaf],
    ];
    for (const b of blades) {
      drawBlade(b[0], b[1], b[2], b[3], b[4], b[5]);
    }
  }
  return canvas;
}

//------------------------------------------------------------------------------------------------------------------------
//                      1-LEAF ANALYTICAL SDF BAKER (1 MEMORY, INSTANCED N TIMES) + WOOD CAPSULES
//------------------------------------------------------------------------------------------------------------------------

function sdTaperedCapsule(px, py, pz, ax, ay, az, bx, by, bz, rA, rB) {
  const pax = px - ax, pay = py - ay, paz = pz - az;
  const bax = bx - ax, bay = by - ay, baz = bz - az;
  const baDot = bax * bax + bay * bay + baz * baz;
  const h = Math.max(0.0, Math.min(1.0, baDot > 1e-8 ? (pax * bax + pay * bay + paz * baz) / baDot : 0.0));
  const dx = pax - bax * h;
  const dy = pay - bay * h;
  const dz = paz - baz * h;
  const r = rA + (rB - rA) * h;
  return Math.hypot(dx, dy, dz) - r;
}

function sdSegment2D(px, py, ax, ay, bx, by, r) {
  const pax = px - ax, pay = py - ay;
  const bax = bx - ax, bay = by - ay;
  const dot = bax * bax + bay * bay;
  const h = Math.max(0.0, Math.min(1.0, dot > 1e-8 ? (pax * bax + pay * bay) / dot : 0.0));
  return Math.hypot(pax - bax * h, pay - bay * h) - r;
}

/**
 * Exact 2D analytical silhouette of 1 canonical leaf in normalized coordinates:
 *   lx in [-0.5, 0.5] (blade width), ly in [0.0, 1.0] (petiole base -> tip).
 */
export function EvaluateCanonicalLeaf2D(lx, ly, leafShape = 'broadleaf') {
  const ax = Math.abs(lx);
  if (leafShape === 'needle' || leafShape === 'scale') {
    // 3-needle conifer fascicle spray (crisp analytical needles, zero blob!)
    const n0 = sdSegment2D(lx, ly, 0.0, 0.04, 0.0, 0.96, 0.045);
    const n1 = sdSegment2D(lx, ly, 0.0, 0.08, 0.24, 0.88, 0.038);
    const n2 = sdSegment2D(lx, ly, 0.0, 0.08, -0.24, 0.88, 0.038);
    return Math.min(n0, Math.min(n1, n2));
  }
  if (leafShape === 'willow' || leafShape === 'palm') {
    // Slender lance blade
    const t = Math.max(0.0, Math.min(1.0, ly));
    const halfW = 0.16 * Math.sin(Math.pow(t, 0.85) * Math.PI);
    const dy = ly < 0.02 ? (0.02 - ly) : ly > 0.98 ? (ly - 0.98) : 0.0;
    return Math.hypot(Math.max(0.0, ax - halfW), dy) + Math.min(0.0, Math.max(ax - halfW, Math.max(0.02 - ly, ly - 0.98)));
  }
  if (leafShape === 'lobed') {
    // Classic Oak multi-lobed blade with wavy analytical margin
    const t = Math.max(0.0, Math.min(1.0, (ly - 0.06) / 0.90));
    const baseEnv = 0.44 * Math.sin(Math.pow(t, 0.78) * Math.PI) * (0.72 + 0.35 * Math.sin(t * Math.PI * 0.8));
    const lobeWave = 0.085 * Math.sin(t * Math.PI * 7.0) * Math.sin(t * Math.PI);
    const halfW = Math.max(0.03, baseEnv + lobeWave);
    const dx = ax - halfW;
    const dy = Math.max(0.06 - ly, ly - 0.96);
    const blade = Math.hypot(Math.max(dx, 0.0), Math.max(dy, 0.0)) + Math.min(Math.max(dx, dy), 0.0);
    const petiole = sdSegment2D(lx, ly, 0.0, 0.0, 0.0, 0.12, 0.028);
    return Math.min(blade, petiole);
  }
  if (leafShape === 'compound') {
    // Compound leaflet pair + terminal leaflet
    const d0 = sdSegment2D(lx, ly, 0.0, 0.02, 0.0, 0.92, 0.025);
    const lTop = Math.hypot(lx / 0.24, (ly - 0.80) / 0.18) * 0.18 - 0.16;
    const lMid = Math.hypot((ax - 0.22) / 0.20, (ly - 0.56) / 0.14) * 0.14 - 0.12;
    const lLow = Math.hypot((ax - 0.20) / 0.18, (ly - 0.30) / 0.12) * 0.12 - 0.10;
    return Math.min(d0, Math.min(lTop, Math.min(lMid, lLow)));
  }
  // Broadleaf / Aspen heart-ovate blade with tapered tip
  const t = Math.max(0.0, Math.min(1.0, (ly - 0.08) / 0.88));
  const halfW = 0.46 * Math.sin(Math.pow(t, 0.62) * Math.PI);
  const dx = ax - halfW;
  const dy = Math.max(0.08 - ly, ly - 0.96);
  const blade = Math.hypot(Math.max(dx, 0.0), Math.max(dy, 0.0)) + Math.min(Math.max(dx, dy), 0.0);
  const petiole = sdSegment2D(lx, ly, 0.0, 0.0, 0.0, 0.14, 0.026);
  return Math.min(blade, petiole);
}

/**
 * Exact 3D analytical SDF of 1 canonical leaf with physical blade thickness & midrib camber.
 */
export function EvaluateCanonicalLeafSdf(lx, ly, lz, leafShape = 'broadleaf', halfThickness = 0.035) {
  const d2 = EvaluateCanonicalLeaf2D(lx, ly, leafShape);
  const camber = 0.032 * Math.sin(Math.max(0.0, Math.min(1.0, ly)) * Math.PI) * (1.0 - Math.min(1.0, 4.0 * lx * lx));
  const dz = Math.abs(lz - camber) - halfThickness;
  const outside = Math.hypot(Math.max(d2, 0.0), Math.max(dz, 0.0));
  const inside = Math.min(Math.max(d2, dz), 0.0);
  return outside + inside;
}

/**
 * Bakes 1 single canonical analytical leaf SDF into a compact 32x16x32 R16F-equivalent brick (32 KB!).
 * That single brick in memory is then instanced N times across the canopy using inverse leaf transforms.
 */
export function BakeCanonicalLeafSdf3D(leafShape = 'broadleaf', thicknessNorm = 0.045) {
  const nx = 32, ny = 32, nz = 16;
  const data = new Float32Array(nx * ny * nz);
  const minX = -0.62, maxX = 0.62;
  const minY = -0.08, maxY = 1.08;
  const minZ = -0.22, maxZ = 0.22;
  const halfT = Math.max(0.012, thicknessNorm * 0.5);

  let idx = 0;
  for (let iz = 0; iz < nz; iz++) {
    const lz = minZ + (iz / (nz - 1)) * (maxZ - minZ);
    for (let iy = 0; iy < ny; iy++) {
      const ly = minY + (iy / (ny - 1)) * (maxY - minY);
      for (let ix = 0; ix < nx; ix++) {
        const lx = minX + (ix / (nx - 1)) * (maxX - minX);
        data[idx++] = EvaluateCanonicalLeafSdf(lx, ly, lz, leafShape, halfT);
      }
    }
  }
  return {
    leafShape,
    thicknessNorm,
    nx, ny, nz,
    minX, maxX, minY, maxY, minZ, maxZ,
    bytesR16F: nx * ny * nz * 2, // 32 * 32 * 16 * 2 bytes = 32,768 bytes (32 KB!)
    data,
  };
}

/**
 * Trilinear lookup into the single baked 32x16x32 canonical leaf SDF brick.
 */
export function SampleBakedLeafSdf3D(baked, lx, ly, lz) {
  const fx = (lx - baked.minX) / (baked.maxX - baked.minX);
  const fy = (ly - baked.minY) / (baked.maxY - baked.minY);
  const fz = (lz - baked.minZ) / (baked.maxZ - baked.minZ);
  if (fx < 0.0 || fx > 1.0 || fy < 0.0 || fy > 1.0 || fz < 0.0 || fz > 1.0) {
    // Distance to brick AABB + boundary distance
    const dx = Math.max(baked.minX - lx, 0.0, lx - baked.maxX);
    const dy = Math.max(baked.minY - ly, 0.0, ly - baked.maxY);
    const dz = Math.max(baked.minZ - lz, 0.0, lz - baked.maxZ);
    return Math.hypot(dx, dy, dz) + 0.08;
  }
  const ix = Math.min(baked.nx - 1, Math.max(0, Math.round(fx * (baked.nx - 1))));
  const iy = Math.min(baked.ny - 1, Math.max(0, Math.round(fy * (baked.ny - 1))));
  const iz = Math.min(baked.nz - 1, Math.max(0, Math.round(fz * (baked.nz - 1))));
  return baked.data[(iz * baked.ny + iy) * baked.nx + ix];
}

/**
 * Builds ONE single thick 3D canonical leaf shell mesh (top, bottom, extruded rim)
 * matching the baked 1-Leaf SDF silhouette. Instanced across the entire tree canopy via 1 GPU buffer!
 */
export function BuildCanonicalLeafSdfMeshData(leafShape = 'broadleaf', thicknessNorm = 0.045) {
  const halfT = Math.max(0.012, thicknessNorm * 0.5);
  const outline = [];
  const segs = 18;

  if (leafShape === 'needle' || leafShape === 'scale') {
    // 3 sharp extruded needles sharing 1 prototype mesh
    const needles = [
      { tx: 0.0, ty: 0.96, w: 0.042 },
      { tx: 0.24, ty: 0.88, w: 0.036 },
      { tx: -0.24, ty: 0.88, w: 0.036 },
    ];
    const positions = [];
    const normals = [];
    for (const nd of needles) {
      const len = Math.hypot(nd.tx, nd.ty);
      const px = -nd.ty / len * nd.w;
      const py = nd.tx / len * nd.w;
      const pts = [
        [-px, -py], [px, py], [nd.tx + px * 0.25, nd.ty + py * 0.25], [nd.tx - px * 0.25, nd.ty - py * 0.25],
      ];
      // Top & bottom quads + side walls
      const pushTri = (a, b, c, nx, ny, nz) => {
        positions.push(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
        normals.push(nx, ny, nz, nx, ny, nz, nx, ny, nz);
      };
      const t = pts.map((p) => [p[0], p[1], halfT]);
      const b = pts.map((p) => [p[0], p[1], -halfT]);
      pushTri(t[0], t[1], t[2], 0, 0, 1);
      pushTri(t[0], t[2], t[3], 0, 0, 1);
      pushTri(b[0], b[2], b[1], 0, 0, -1);
      pushTri(b[0], b[3], b[2], 0, 0, -1);
      for (let i = 0; i < 4; i++) {
        const j = (i + 1) % 4;
        pushTri(t[i], b[i], b[j], 0, 1, 0);
        pushTri(t[i], b[j], t[j], 0, 1, 0);
      }
    }
    return { positions: new Float32Array(positions), normals: new Float32Array(normals) };
  }

  // Sample exact analytical contour on left & right blade margins
  for (let i = 0; i <= segs; i++) {
    const t = i / segs;
    const ly = 0.04 + t * 0.92;
    let halfW = 0.45 * Math.sin(Math.pow(t, 0.65) * Math.PI);
    if (leafShape === 'lobed') {
      halfW = Math.max(0.03, 0.44 * Math.sin(Math.pow(t, 0.78) * Math.PI) * (0.72 + 0.35 * Math.sin(t * Math.PI * 0.8)) + 0.085 * Math.sin(t * Math.PI * 7.0) * Math.sin(t * Math.PI));
    } else if (leafShape === 'willow' || leafShape === 'palm') {
      halfW = 0.16 * Math.sin(Math.pow(t, 0.85) * Math.PI);
    } else if (leafShape === 'compound') {
      halfW = 0.38 * Math.sin(t * Math.PI) * (0.65 + 0.35 * Math.abs(Math.sin(t * Math.PI * 3.0)));
    }
    outline.push({ ly, halfW: Math.max(0.015, halfW) });
  }

  const ring = [];
  for (let i = 0; i <= segs; i++) ring.push([outline[i].halfW, outline[i].ly]);
  for (let i = segs - 1; i >= 1; i--) ring.push([-outline[i].halfW, outline[i].ly]);

  const positions = [];
  const normals = [];
  const pushTri = (a, b, c, nx, ny, nz) => {
    positions.push(a[0], a[1], a[2], b[0], b[1], b[2], c[0], c[1], c[2]);
    normals.push(nx, ny, nz, nx, ny, nz, nx, ny, nz);
  };

  const centerTop = [0.0, 0.48, halfT + 0.025];
  const centerBot = [0.0, 0.48, -halfT + 0.015];
  for (let i = 0; i < ring.length; i++) {
    const p0 = ring[i];
    const p1 = ring[(i + 1) % ring.length];
    const cam0 = 0.02 * Math.sin(p0[1] * Math.PI);
    const cam1 = 0.02 * Math.sin(p1[1] * Math.PI);
    const t0 = [p0[0], p0[1], halfT + cam0];
    const t1 = [p1[0], p1[1], halfT + cam1];
    const b0 = [p0[0], p0[1], -halfT + cam0];
    const b1 = [p1[0], p1[1], -halfT + cam1];
    // Top face, bottom face, and thick outer bevel rim
    pushTri(centerTop, t0, t1, 0, 0, 1);
    pushTri(centerBot, b1, b0, 0, 0, -1);
    const ex = p1[1] - p0[1], ey = -(p1[0] - p0[0]);
    const el = Math.hypot(ex, ey) || 1.0;
    pushTri(t0, b0, b1, ex / el, ey / el, 0);
    pushTri(t0, b1, t1, ex / el, ey / el, 0);
  }
  return { positions: new Float32Array(positions), normals: new Float32Array(normals) };
}

/**
 * Renders the baked 1-Leaf Analytical SDF Prototype (left: top-down XY isolines; right: side YZ thickness profile).
 */
export function RenderCanonicalLeafSdfPreview(canvas, bakedLeaf, emissiveRgb = [0.28, 0.92, 0.54], emissiveStrength = 0.0) {
  const w = canvas.width;
  const h = canvas.height;
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(w, h);
  const data = img.data;
  const splitX = Math.floor(w * 0.64);

  for (let py = 0; py < h; py++) {
    const ly = bakedLeaf.maxY - (py / (h - 1)) * (bakedLeaf.maxY - bakedLeaf.minY);
    for (let px = 0; px < w; px++) {
      let d;
      if (px < splitX) {
        const lx = bakedLeaf.minX + (px / (splitX - 1)) * (bakedLeaf.maxX - bakedLeaf.minX);
        d = SampleBakedLeafSdf3D(bakedLeaf, lx, ly, 0.015);
      } else {
        const lz = bakedLeaf.minZ + ((px - splitX) / Math.max(1, w - splitX - 1)) * (bakedLeaf.maxZ - bakedLeaf.minZ);
        d = SampleBakedLeafSdf3D(bakedLeaf, 0.0, ly, lz);
      }

      const idx = (py * w + px) * 4;
      if (px === splitX) {
        data[idx] = 68; data[idx + 1] = 72; data[idx + 2] = 82; data[idx + 3] = 255;
        continue;
      }
      const absD = Math.abs(d);
      const iso = 0.80 + 0.20 * Math.cos(absD * 58.0);
      const zero = Math.exp(-absD * 85.0);
      let r, g, b;
      if (d < 0.0) {
        r = (58 + emissiveRgb[0] * emissiveStrength * 55) * iso + 210 * zero;
        g = (196 + emissiveRgb[1] * emissiveStrength * 55) * iso + 245 * zero;
        b = (118 + emissiveRgb[2] * emissiveStrength * 55) * iso + 210 * zero;
      } else {
        const glow = emissiveStrength > 0.01 ? Math.exp(-absD * 6.5) * Math.min(1.5, emissiveStrength * 0.45) : 0.0;
        const fade = Math.exp(-absD * 3.2);
        r = (16 + 22 * fade) * iso + emissiveRgb[0] * 195 * glow + 220 * zero;
        g = (22 + 54 * fade) * iso + emissiveRgb[1] * 195 * glow + 240 * zero;
        b = (30 + 68 * fade) * iso + emissiveRgb[2] * 195 * glow + 230 * zero;
      }
      data[idx] = Math.min(255, Math.max(0, r));
      data[idx + 1] = Math.min(255, Math.max(0, g));
      data[idx + 2] = Math.min(255, Math.max(0, b));
      data[idx + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  ctx.fillStyle = '#c8d0dc';
  ctx.font = '600 9px "DM Sans", sans-serif';
  ctx.fillText('XY SILHOUETTE (1 BAKED LEAF)', 7, 13);
  ctx.fillText('YZ THICKNESS', splitX + 7, 13);
}

const _pA = new Float32Array(3);
const _pB = new Float32Array(3);

/**
 * Evaluates wind-deformed wood tapered capsules AND instanced 1-Leaf SDF rigid transforms
 * using the exact same UnifiedWindField.
 */
export function EvaluateWindDeformedSdfPrimitives(specimen, timeSec, wind, maxLeafInstances = 640) {
  const capsules = [];
  for (const cap of specimen.sdfCapsules) {
    DisplacePointByUnifiedWind(
      _pA, cap.a[0], cap.a[1], cap.a[2], 0, 0,
      cap.windA[0], cap.windA[1], cap.windA[2], cap.windA[3],
      cap.pivot[0], cap.pivot[1], cap.pivot[2], specimen.height, timeSec, wind
    );
    DisplacePointByUnifiedWind(
      _pB, cap.b[0], cap.b[1], cap.b[2], 0, 0,
      cap.windB[0], cap.windB[1], cap.windB[2], cap.windB[3],
      cap.pivot[0], cap.pivot[1], cap.pivot[2], specimen.height, timeSec, wind
    );
    capsules.push({
      ax: _pA[0], ay: _pA[1], az: _pA[2],
      bx: _pB[0], by: _pB[1], bz: _pB[2],
      rA: cap.rA, rB: cap.rB, level: cap.level,
    });
  }

  // Instanced 1-Leaf SDF transforms (strided evenly across full canopy if leaf count > maxLeafInstances)
  const srcLeaves = specimen.leafSdfInstances || [];
  const stride = Math.max(1, Math.floor(srcLeaves.length / Math.max(1, maxLeafInstances)));
  const leafInstances = [];
  for (let i = 0; i < srcLeaves.length && leafInstances.length < maxLeafInstances; i += stride) {
    const lf = srcLeaves[i];
    DisplacePointByUnifiedWind(
      _pA, lf.pos[0], lf.pos[1], lf.pos[2], 0, 0,
      lf.windAttr[0], lf.windAttr[1], lf.windAttr[2], lf.windAttr[3],
      lf.pivot[0], lf.pivot[1], lf.pivot[2], specimen.height, timeSec, wind
    );
    leafInstances.push({
      px: _pA[0], py: _pA[1], pz: _pA[2],
      dir: lf.dir,
      side: lf.side,
      normal: lf.normal,
      scaleX: lf.scaleX,
      scaleY: lf.scaleY,
    });
  }

  return { capsules, leafInstances };
}

/**
 * Renders a live vertical XY cross-section of the Global Distance Field (GDF) using:
 *   1) Proper tapered Wood Capsules (trunk + branches)
 *   2) Instanced 1-Leaf Baked SDF Brick (NO blobby spheres — crisp instanced leaf SDFs!)
 *   3) Emissive Leaf Radiance Glow illuminating the surrounding trunk & ground when emissiveStrength > 0
 */
export function RenderGdfCrossSection(canvas, deformedSdf, specimenHeight, bakedLeaf = null, opts = {}) {
  const w = canvas.width;
  const h = canvas.height;
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(w, h);
  const data = img.data;
  const span = Math.max(6.0, specimenHeight * 1.25);
  const halfX = span * 0.55;
  const maxY = span * 1.08;
  const emissiveStrength = opts.emissiveStrength || 0.0;
  const emissiveRgb = opts.emissiveRgb || [0.28, 0.92, 0.54];
  const leafSdfEnabled = opts.leafSdfEnabled !== false;

  // Filter leaf instances near the Z=0 slice for crisp real-time 60fps instanced leaf SDF evaluation
  const sliceLeaves = [];
  if (leafSdfEnabled && bakedLeaf && deformedSdf.leafInstances) {
    for (let i = 0; i < deformedSdf.leafInstances.length; i++) {
      const lf = deformedSdf.leafInstances[i];
      const maxS = Math.max(lf.scaleX, lf.scaleY) * 1.35;
      if (Math.abs(lf.pz) < maxS + 0.55) {
        sliceLeaves.push({
          px: lf.px, py: lf.py, pz: lf.pz,
          sx: lf.side[0], sy: lf.side[1], sz: lf.side[2],
          dx: lf.dir[0],  dy: lf.dir[1],  dz: lf.dir[2],
          nx: lf.normal[0], ny: lf.normal[1], nz: lf.normal[2],
          invX: 1.0 / Math.max(0.05, lf.scaleX),
          invY: 1.0 / Math.max(0.05, lf.scaleY),
          scale: Math.max(lf.scaleX, lf.scaleY),
          rad: maxS,
        });
        if (sliceLeaves.length >= 96) break;
      }
    }
  }

  for (let py = 0; py < h; py++) {
    const wy = (1.0 - py / (h - 1)) * maxY - 0.8;
    for (let px = 0; px < w; px++) {
      const wx = ((px / (w - 1)) * 2.0 - 1.0) * halfX;
      let dWood = 1e6;
      for (let i = 0; i < deformedSdf.capsules.length; i++) {
        const c = deformedSdf.capsules[i];
        const dc = sdTaperedCapsule(wx, wy, 0.0, c.ax, c.ay, c.az, c.bx, c.by, c.bz, c.rA, c.rB);
        if (dc < dWood) dWood = dc;
      }

      // Evaluate Instanced 1-Leaf SDF (transform world point into local leaf frame -> sample 1 baked 32 KB brick!)
      let dLeaf = 1e6;
      for (let i = 0; i < sliceLeaves.length; i++) {
        const lf = sliceLeaves[i];
        const rx = wx - lf.px, ry = wy - lf.py, rz = -lf.pz;
        const distSq = rx * rx + ry * ry + rz * rz;
        if (distSq > (lf.rad + 1.8) * (lf.rad + 1.8)) {
          const coarse = Math.sqrt(distSq) - lf.rad;
          if (coarse < dLeaf) dLeaf = coarse;
          continue;
        }
        const lx = (rx * lf.sx + ry * lf.sy + rz * lf.sz) * lf.invX;
        const ly = (rx * lf.dx + ry * lf.dy + rz * lf.dz) * lf.invY;
        const lz = (rx * lf.nx + ry * lf.ny + rz * lf.nz) * lf.invY;
        const dl = SampleBakedLeafSdf3D(bakedLeaf, lx, ly, lz) * lf.scale;
        if (dl < dLeaf) dLeaf = dl;
      }

      const dGround = wy;
      const d = Math.min(dGround, Math.min(dWood, dLeaf));

      const idx = (py * w + px) * 4;
      const absD = Math.abs(d);
      const iso = 0.80 + 0.20 * Math.cos(absD * 10.5);
      const zeroLine = Math.exp(-absD * 18.0);
      let r, g, b;

      if (dLeaf <= dWood && dLeaf <= dGround && dLeaf < 0.08) {
        // Instanced Baked Leaf SDF interior (crisp leaf green + emissive boost!)
        const em = Math.min(1.6, emissiveStrength * 0.45);
        r = (76 + emissiveRgb[0] * 180 * em) * iso + 225 * zeroLine;
        g = (212 + emissiveRgb[1] * 140 * em) * iso + 250 * zeroLine;
        b = (128 + emissiveRgb[2] * 180 * em) * iso + 225 * zeroLine;
      } else if (d < 0.0) {
        // Proper Tapered Wood Capsule / Ground interior (warm amber)
        r = 228 * iso + 255 * zeroLine;
        g = 148 * iso + 255 * zeroLine;
        b = 74 * iso + 255 * zeroLine;
      } else {
        // Exterior signed distance isolines
        const fade = Math.exp(-absD * 0.24);
        r = (20 + 44 * fade) * iso + 225 * zeroLine;
        g = (36 + 104 * fade) * iso + 235 * zeroLine;
        b = (52 + 136 * fade) * iso + 245 * zeroLine;
      }

      // Radiosity Emissive Halo: glowing instanced leaves cast real light onto trunk, branches, and ground!
      if (emissiveStrength > 0.02 && dLeaf < 8.5) {
        const radHalo = Math.exp(-Math.max(0.0, dLeaf) * 0.65) * Math.min(1.4, emissiveStrength * 0.36);
        r += emissiveRgb[0] * 210 * radHalo;
        g += emissiveRgb[1] * 210 * radHalo;
        b += emissiveRgb[2] * 210 * radHalo;
      }

      data[idx] = Math.min(255, Math.max(0, r));
      data[idx + 1] = Math.min(255, Math.max(0, g));
      data[idx + 2] = Math.min(255, Math.max(0, b));
      data[idx + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
}
