// SceneSurfaceBuilder.js — Procedural triangle surface buffers for the GT coupe,
// calibration spheres, softbox, lobby architectures, and instanced emissive pylons.

export function createSurfaceBufferAccumulator() {
  const positions = [];
  const normals = [];
  const matIds = [];
  const indices = [];

  const pushVertex = (px, py, pz, nx, ny, nz, matId) => {
    const idx = positions.length / 3;
    positions.push(px, py, pz);
    normals.push(nx, ny, nz);
    matIds.push(matId);
    return idx;
  };

  const pushTri = (i0, i1, i2) => {
    indices.push(i0, i1, i2);
  };

  const pushQuad = (p0, p1, p2, p3, matId) => {
    const ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
    const vx = p3[0] - p0[0], vy = p3[1] - p0[1], vz = p3[2] - p0[2];
    let nx = uy * vz - uz * vy;
    let ny = uz * vx - ux * vz;
    let nz = ux * vy - uy * vx;
    const len = Math.hypot(nx, ny, nz) || 1;
    nx /= len; ny /= len; nz /= len;

    const a = pushVertex(p0[0], p0[1], p0[2], nx, ny, nz, matId);
    const b = pushVertex(p1[0], p1[1], p1[2], nx, ny, nz, matId);
    const c = pushVertex(p2[0], p2[1], p2[2], nx, ny, nz, matId);
    const d = pushVertex(p3[0], p3[1], p3[2], nx, ny, nz, matId);
    pushTri(a, b, c);
    pushTri(a, c, d);
  };

  const addBox = (cx, cy, cz, hx, hy, hz, matId) => {
    const x0 = cx - hx, x1 = cx + hx;
    const y0 = cy - hy, y1 = cy + hy;
    const z0 = cz - hz, z1 = cz + hz;
    pushQuad([x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1], matId); // +Z
    pushQuad([x1, y0, z0], [x0, y0, z0], [x0, y1, z0], [x1, y1, z0], matId); // -Z
    pushQuad([x1, y0, z1], [x1, y0, z0], [x1, y1, z0], [x1, y1, z1], matId); // +X
    pushQuad([x0, y0, z0], [x0, y0, z1], [x0, y1, z1], [x0, y1, z0], matId); // -X
    pushQuad([x0, y1, z1], [x1, y1, z1], [x1, y1, z0], [x0, y1, z0], matId); // +Y
    pushQuad([x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1], matId); // -Y
  };

  const addSphere = (cx, cy, cz, radius, segsLat, segsLon, matId) => {
    const startVert = positions.length / 3;
    for (let lat = 0; lat <= segsLat; ++lat) {
      const v = lat / segsLat;
      const theta = v * Math.PI;
      const sinT = Math.sin(theta);
      const cosT = Math.cos(theta);
      for (let lon = 0; lon <= segsLon; ++lon) {
        const u = lon / segsLon;
        const phi = u * Math.PI * 2;
        const nx = Math.cos(phi) * sinT;
        const ny = cosT;
        const nz = Math.sin(phi) * sinT;
        pushVertex(cx + nx * radius, cy + ny * radius, cz + nz * radius, nx, ny, nz, matId);
      }
    }
    const rowStride = segsLon + 1;
    for (let lat = 0; lat < segsLat; ++lat) {
      for (let lon = 0; lon < segsLon; ++lon) {
        const a = startVert + lat * rowStride + lon;
        const b = a + rowStride;
        pushTri(a, a + 1, b + 1);
        pushTri(a, b + 1, b);
      }
    }
  };

  const addCylinderY = (cx, cy, cz, halfH, radius, segs, matId) => {
    const y0 = cy - halfH;
    const y1 = cy + halfH;
    for (let i = 0; i < segs; ++i) {
      const a0 = (i / segs) * Math.PI * 2;
      const a1 = ((i + 1) / segs) * Math.PI * 2;
      const x0 = Math.cos(a0), z0 = Math.sin(a0);
      const x1 = Math.cos(a1), z1 = Math.sin(a1);
      const v0 = pushVertex(cx + x0 * radius, y0, cz + z0 * radius, x0, 0, z0, matId);
      const v1 = pushVertex(cx + x0 * radius, y1, cz + z0 * radius, x0, 0, z0, matId);
      const v2 = pushVertex(cx + x1 * radius, y1, cz + z1 * radius, x1, 0, z1, matId);
      const v3 = pushVertex(cx + x1 * radius, y0, cz + z1 * radius, x1, 0, z1, matId);
      pushTri(v0, v1, v2);
      pushTri(v0, v2, v3);
    }
    const topCenter = pushVertex(cx, y1, cz, 0, 1, 0, matId);
    const botCenter = pushVertex(cx, y0, cz, 0, -1, 0, matId);
    for (let i = 0; i < segs; ++i) {
      const a0 = (i / segs) * Math.PI * 2;
      const a1 = ((i + 1) / segs) * Math.PI * 2;
      const t0 = pushVertex(cx + Math.cos(a0) * radius, y1, cz + Math.sin(a0) * radius, 0, 1, 0, matId);
      const t1 = pushVertex(cx + Math.cos(a1) * radius, y1, cz + Math.sin(a1) * radius, 0, 1, 0, matId);
      pushTri(topCenter, t1, t0);
      const b0 = pushVertex(cx + Math.cos(a0) * radius, y0, cz + Math.sin(a0) * radius, 0, -1, 0, matId);
      const b1 = pushVertex(cx + Math.cos(a1) * radius, y0, cz + Math.sin(a1) * radius, 0, -1, 0, matId);
      pushTri(botCenter, b0, b1);
    }
  };

  const addWheelX = (cx, cy, cz, halfW, radius, segs, matId) => {
    const x0 = cx - halfW;
    const x1 = cx + halfW;
    for (let i = 0; i < segs; ++i) {
      const a0 = (i / segs) * Math.PI * 2;
      const a1 = ((i + 1) / segs) * Math.PI * 2;
      const y0 = Math.cos(a0), z0 = Math.sin(a0);
      const y1 = Math.cos(a1), z1 = Math.sin(a1);
      const v0 = pushVertex(x0, cy + y0 * radius, cz + z0 * radius, 0, y0, z0, matId);
      const v1 = pushVertex(x1, cy + y0 * radius, cz + z0 * radius, 0, y0, z0, matId);
      const v2 = pushVertex(x1, cy + y1 * radius, cz + z1 * radius, 0, y1, z1, matId);
      const v3 = pushVertex(x0, cy + y1 * radius, cz + z1 * radius, 0, y1, z1, matId);
      pushTri(v0, v1, v2);
      pushTri(v0, v2, v3);
    }
    const cR = pushVertex(x1, cy, cz, 1, 0, 0, matId);
    const cL = pushVertex(x0, cy, cz, -1, 0, 0, matId);
    for (let i = 0; i < segs; ++i) {
      const a0 = (i / segs) * Math.PI * 2;
      const a1 = ((i + 1) / segs) * Math.PI * 2;
      const r0 = pushVertex(x1, cy + Math.cos(a0) * radius, cz + Math.sin(a0) * radius, 1, 0, 0, matId);
      const r1 = pushVertex(x1, cy + Math.cos(a1) * radius, cz + Math.sin(a1) * radius, 1, 0, 0, matId);
      pushTri(cR, r1, r0);
      const l0 = pushVertex(x0, cy + Math.cos(a0) * radius, cz + Math.sin(a0) * radius, -1, 0, 0, matId);
      const l1 = pushVertex(x0, cy + Math.cos(a1) * radius, cz + Math.sin(a1) * radius, -1, 0, 0, matId);
      pushTri(cL, l0, l1);
    }
  };

  // Lofted symmetric cross-section belt with smooth vertex normals
  const addLoftedSectionBelt = (stations, matId) => {
    const ringLen = stations[0].ring.length;
    const rings = [];
    for (let s = 0; s < stations.length; ++s) {
      const st = stations[s];
      const ringVerts = [];
      for (let k = 0; k < ringLen; ++k) {
        const pt = st.ring[k];
        // Compute finite-difference normal along belt u (around cross section) and v (z station)
        const ptPrevU = st.ring[(k - 1 + ringLen) % ringLen];
        const ptNextU = st.ring[(k + 1) % ringLen];
        const stPrev = stations[Math.max(0, s - 1)];
        const stNext = stations[Math.min(stations.length - 1, s + 1)];
        const pPrevV = stPrev.ring[k];
        const pNextV = stNext.ring[k];

        const ux = ptNextU[0] - ptPrevU[0];
        const uy = ptNextU[1] - ptPrevU[1];
        const uz = 0;
        const vx = pNextV[0] - pPrevV[0];
        const vy = pNextV[1] - pPrevV[1];
        const vz = stNext.z - stPrev.z;

        let nx = uy * vz - uz * vy;
        let ny = uz * vx - ux * vz;
        let nz = ux * vy - uy * vx;
        const len = Math.hypot(nx, ny, nz) || 1;
        nx /= len; ny /= len; nz /= len;
        ringVerts.push(pushVertex(pt[0], pt[1], st.z, nx, ny, nz, matId));
      }
      rings.push(ringVerts);
    }

    for (let s = 0; s < stations.length - 1; ++s) {
      const rA = rings[s];
      const rB = rings[s + 1];
      for (let k = 0; k < ringLen; ++k) {
        const kn = (k + 1) % ringLen;
        pushTri(rA[k], rA[kn], rB[kn]);
        pushTri(rA[k], rB[kn], rB[k]);
      }
    }

    // Cap front & back stations
    const capStation = (st, flip) => {
      let cx = 0, cy = 0;
      for (const p of st.ring) { cx += p[0]; cy += p[1]; }
      cx /= ringLen; cy /= ringLen;
      const nz = flip ? -1 : 1;
      const center = pushVertex(cx, cy, st.z, 0, 0, nz, matId);
      const edge = st.ring.map((p) => pushVertex(p[0], p[1], st.z, 0, 0, nz, matId));
      for (let k = 0; k < ringLen; ++k) {
        const kn = (k + 1) % ringLen;
        if (flip) pushTri(center, edge[kn], edge[k]);
        else pushTri(center, edge[k], edge[kn]);
      }
    };
    capStation(stations[0], true);
    capStation(stations[stations.length - 1], false);
  };

  return {
    addBox,
    addSphere,
    addCylinderY,
    addWheelX,
    addLoftedSectionBelt,
    pushQuad,
    finalize: () => ({
      positions: new Float32Array(positions),
      normals: new Float32Array(normals),
      matIds: new Float32Array(matIds),
      indices: new Uint16Array(indices),
    }),
  };
}

export function buildVehicleGeometry() {
  const b = createSurfaceBufferAccumulator();

  // Cross-section helper: counter-clockwise around XY (facing +Z): bottom-left -> bottom-right -> belt-right -> roof-right -> roof-left -> belt-left
  const makeProfile = (z, wSill, yBot, wBelt, yBelt, wRoof, yRoof) => ({
    z,
    ring: [
      [-wSill, yBot],
      [wSill, yBot],
      [wBelt, yBelt],
      [wRoof, yRoof],
      [-wRoof, yRoof],
      [-wBelt, yBelt],
    ],
  });

  // Sculpted fastback GT coupe hull (-2.18 rear to +2.15 front nose)
  b.addLoftedSectionBelt(
    [
      makeProfile(-2.18, 0.84, 0.24, 0.92, 0.72, 0.72, 0.78),
      makeProfile(-1.90, 0.88, 0.21, 0.95, 0.75, 0.73, 0.85),
      makeProfile(-1.55, 0.91, 0.19, 0.97, 0.77, 0.71, 0.95),
      makeProfile(-1.05, 0.92, 0.17, 0.95, 0.76, 0.65, 1.12),
      makeProfile(-0.35, 0.91, 0.17, 0.94, 0.75, 0.64, 1.19),
      makeProfile(0.25, 0.91, 0.17, 0.95, 0.75, 0.66, 1.14),
      makeProfile(0.78, 0.90, 0.17, 0.97, 0.72, 0.74, 0.82),
      makeProfile(1.18, 0.89, 0.18, 0.96, 0.70, 0.73, 0.75),
      makeProfile(1.62, 0.86, 0.18, 0.92, 0.63, 0.70, 0.66),
      makeProfile(1.98, 0.81, 0.18, 0.85, 0.51, 0.66, 0.56),
      makeProfile(2.15, 0.68, 0.19, 0.72, 0.36, 0.56, 0.42),
    ],
    10.0 // Sapphire clearcoat metallic car paint
  );

  // Smoked fastback cabin glass canopy (windshield + roof glass + side glass)
  b.addLoftedSectionBelt(
    [
      makeProfile(-1.42, 0.67, 0.82, 0.69, 0.95, 0.60, 1.00),
      makeProfile(-0.85, 0.68, 0.82, 0.70, 1.05, 0.59, 1.16),
      makeProfile(-0.20, 0.68, 0.82, 0.70, 1.08, 0.59, 1.21),
      makeProfile(0.28, 0.69, 0.80, 0.71, 1.04, 0.61, 1.15),
      makeProfile(0.75, 0.70, 0.76, 0.72, 0.84, 0.63, 0.84),
    ],
    11.0 // Dark glass
  );

  // Rear carbon GT wing + posts
  b.addBox(0.0, 1.02, -2.02, 0.88, 0.024, 0.16, 12.0);
  b.addBox(-0.44, 0.88, -1.97, 0.025, 0.14, 0.07, 12.0);
  b.addBox(0.44, 0.88, -1.97, 0.025, 0.14, 0.07, 12.0);

  // 4 Wheels: tires (matId 12) + forged alloy rims (matId 13)
  const wheelOffsets = [
    [-0.86, 0.34, 1.32],
    [0.86, 0.34, 1.32],
    [-0.88, 0.34, -1.28],
    [0.88, 0.34, -1.28],
  ];
  for (const [wx, wy, wz] of wheelOffsets) {
    b.addWheelX(wx, wy, wz, 0.14, 0.34, 24, 12.0);
    b.addWheelX(wx + (wx > 0 ? 0.015 : -0.015), wy, wz, 0.145, 0.23, 20, 13.0);
  }

  // Front LED Headlamp + DRL optical blades (matId 14)
  b.addBox(-0.68, 0.49, 2.08, 0.18, 0.038, 0.06, 14.0);
  b.addBox(0.68, 0.49, 2.08, 0.18, 0.038, 0.06, 14.0);
  b.addBox(0.0, 0.46, 2.13, 0.82, 0.014, 0.03, 14.0);

  // Rear full-width ruby LED taillight bar (matId 15)
  b.addBox(0.0, 0.63, -2.18, 0.84, 0.034, 0.035, 15.0);

  return b.finalize();
}

export function buildStaticEnvironmentGeometry(envPreset) {
  const b = createSurfaceBufferAccumulator();

  // Floor plane (34m x 34m)
  b.pushQuad([-17, 0, 17], [17, 0, 17], [17, 0, -17], [-17, 0, -17], 1.0);

  // Environment architectural walls / calibration cutoff screen
  if (envPreset === 0) {
    // Lobby 1: Dark Cyclorama walls + front calibration wall (seamless corner join)
    b.addBox(0.0, 1.9, 7.6, 6.8, 1.9, 0.18, 2.0);
    b.addBox(-6.8, 2.0, 0.4, 0.2, 2.0, 7.3, 2.0);
    b.addBox(0.0, 2.0, -6.8, 6.8, 2.0, 0.2, 2.0);
  } else if (envPreset === 1) {
    // Lobby 2: Sunlit Forest Stage + sculpted tree trunks casting crisp solar/GDF shadows
    b.addBox(0.0, 1.7, 7.8, 7.2, 1.7, 0.22, 2.0);
    b.addCylinderY(5.6, 2.5, -2.8, 2.5, 0.28, 14, 7.0);
    b.addCylinderY(-5.8, 2.5, -2.2, 2.5, 0.25, 14, 7.0);
    b.addCylinderY(6.2, 2.5, 3.2, 2.5, 0.30, 14, 7.0);
    b.addCylinderY(-6.0, 2.5, 3.6, 2.5, 0.26, 14, 7.0);
  } else {
    // Lobby 3: Afternoon Racing Pit Archway & Stadium Portal
    b.addBox(0.0, 2.2, 7.8, 8.5, 2.2, 0.2, 2.0);
    b.addBox(-6.2, 3.35, 0.5, 0.24, 0.95, 7.5, 2.0);
    b.addBox(-6.2, 1.35, -3.5, 0.28, 1.35, 2.0, 2.0);
    b.addBox(-6.2, 1.35, 5.2, 0.28, 1.35, 2.1, 2.0);
    b.addBox(0.0, 4.35, 1.2, 7.5, 0.22, 7.5, 2.0);
  }

  // Three smooth PBR calibration spheres
  b.addSphere(-2.55, 0.46, 2.4, 0.46, 22, 32, 3.0);  // Chrome Mirror
  b.addSphere(-2.55, 0.46, 1.0, 0.46, 22, 32, 4.0);  // Clearcoat Signal Orange
  b.addSphere(-2.55, 0.46, -0.4, 0.46, 22, 32, 5.0); // Matte Clay Diffuse

  // Overhead Softbox diffuser panel (elevated to frame hero 3/4 view cleanly)
  b.addBox(0.2, 4.46, 0.2, 1.85, 0.04, 0.85, 6.0);

  return b.finalize();
}

export function buildSinglePylonGeometry() {
  const b = createSurfaceBufferAccumulator();
  b.addBox(0.0, 0.0, 0.0, 0.065, 0.28, 0.065, 20.0);
  return b.finalize();
}
