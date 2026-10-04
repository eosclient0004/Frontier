// Procedural paint targets with non-overlapping UV layouts at a consistent
// texel density, plus a Wavefront OBJ importer. Every mesh carries tangents
// so generated height detail can be shaded as a tangent-space normal map.

class MeshBuilder {
  constructor() {
    this.Positions = [];
    this.Normals = [];
    this.Uvs = [];
    this.Indices = [];
  }
  Vertex(P, N, Uv) {
    this.Positions.push(P[0], P[1], P[2]);
    this.Normals.push(N[0], N[1], N[2]);
    this.Uvs.push(Uv[0], Uv[1]);
    return this.Positions.length / 3 - 1;
  }
  // Grid surface: Evaluate(u, v) → { P, N }. Parameter directions must give an
  // outward cross(dP/du, dP/dv) unless Flip is set.
  Surface(SegmentsU, SegmentsV, Evaluate, MapUv, Flip = false) {
    const Base = this.Positions.length / 3;
    for (let J = 0; J <= SegmentsV; J++)
      for (let I = 0; I <= SegmentsU; I++) {
        const U = I / SegmentsU;
        const V = J / SegmentsV;
        const { P, N } = Evaluate(U, V);
        this.Vertex(P, N, MapUv(U, V));
      }
    const Row = SegmentsU + 1;
    for (let J = 0; J < SegmentsV; J++)
      for (let I = 0; I < SegmentsU; I++) {
        const A = Base + J * Row + I;
        const B = A + 1;
        const C = A + Row + 1;
        const D = A + Row;
        if (Flip) this.Indices.push(A, C, B, A, D, C);
        else this.Indices.push(A, B, C, A, C, D);
      }
  }
  Build(Key, Name) {
    return FinalizeMesh({
      Key,
      Name,
      Positions: new Float32Array(this.Positions),
      Normals: new Float32Array(this.Normals),
      Uvs: new Float32Array(this.Uvs),
      Indices: new Uint32Array(this.Indices),
    });
  }
}

const Normalize = (A) => {
  const L = Math.hypot(A[0], A[1], A[2]) || 1;
  return [A[0] / L, A[1] / L, A[2] / L];
};

function ComputeTangents(Mesh) {
  const Count = Mesh.Positions.length / 3;
  const Tan = new Float32Array(Count * 3);
  const Bit = new Float32Array(Count * 3);
  const P = Mesh.Positions;
  const T = Mesh.Uvs;
  const I = Mesh.Indices;
  for (let K = 0; K < I.length; K += 3) {
    const A = I[K], B = I[K + 1], C = I[K + 2];
    const E1 = [P[B * 3] - P[A * 3], P[B * 3 + 1] - P[A * 3 + 1], P[B * 3 + 2] - P[A * 3 + 2]];
    const E2 = [P[C * 3] - P[A * 3], P[C * 3 + 1] - P[A * 3 + 1], P[C * 3 + 2] - P[A * 3 + 2]];
    const DU1 = T[B * 2] - T[A * 2], DV1 = T[B * 2 + 1] - T[A * 2 + 1];
    const DU2 = T[C * 2] - T[A * 2], DV2 = T[C * 2 + 1] - T[A * 2 + 1];
    const Det = DU1 * DV2 - DU2 * DV1;
    if (Math.abs(Det) < 1e-12) continue;
    const R = 1 / Det;
    const S = [(E1[0] * DV2 - E2[0] * DV1) * R, (E1[1] * DV2 - E2[1] * DV1) * R, (E1[2] * DV2 - E2[2] * DV1) * R];
    const Q = [(E2[0] * DU1 - E1[0] * DU2) * R, (E2[1] * DU1 - E1[1] * DU2) * R, (E2[2] * DU1 - E1[2] * DU2) * R];
    for (const V of [A, B, C])
      for (let Axis = 0; Axis < 3; Axis++) {
        Tan[V * 3 + Axis] += S[Axis];
        Bit[V * 3 + Axis] += Q[Axis];
      }
  }
  const Out = new Float32Array(Count * 4);
  const N = Mesh.Normals;
  for (let V = 0; V < Count; V++) {
    const Nv = [N[V * 3], N[V * 3 + 1], N[V * 3 + 2]];
    let Tv = [Tan[V * 3], Tan[V * 3 + 1], Tan[V * 3 + 2]];
    const D = Nv[0] * Tv[0] + Nv[1] * Tv[1] + Nv[2] * Tv[2];
    Tv = [Tv[0] - Nv[0] * D, Tv[1] - Nv[1] * D, Tv[2] - Nv[2] * D];
    if (Math.hypot(...Tv) < 1e-8) {
      Tv = Math.abs(Nv[1]) < 0.95 ? [-Nv[2], 0, Nv[0]] : [1, 0, 0];
    }
    Tv = Normalize(Tv);
    const Bv = [Bit[V * 3], Bit[V * 3 + 1], Bit[V * 3 + 2]];
    const Cr = [Nv[1] * Tv[2] - Nv[2] * Tv[1], Nv[2] * Tv[0] - Nv[0] * Tv[2], Nv[0] * Tv[1] - Nv[1] * Tv[0]];
    const W = Cr[0] * Bv[0] + Cr[1] * Bv[1] + Cr[2] * Bv[2] < 0 ? -1 : 1;
    Out.set([Tv[0], Tv[1], Tv[2], W], V * 4);
  }
  return Out;
}

function UvEdges(Mesh) {
  const Seen = new Set();
  const Lines = [];
  const I = Mesh.Indices;
  const T = Mesh.Uvs;
  const Push = (A, B) => {
    const Key = A < B ? A * 4294967296 + B : B * 4294967296 + A;
    if (Seen.has(Key)) return;
    Seen.add(Key);
    Lines.push(T[A * 2], T[A * 2 + 1], T[B * 2], T[B * 2 + 1]);
  };
  for (let K = 0; K < I.length; K += 3) {
    Push(I[K], I[K + 1]);
    Push(I[K + 1], I[K + 2]);
    Push(I[K + 2], I[K]);
  }
  return new Float32Array(Lines);
}

function FinalizeMesh(Mesh) {
  const Min = [Infinity, Infinity, Infinity];
  const Max = [-Infinity, -Infinity, -Infinity];
  for (let K = 0; K < Mesh.Positions.length; K += 3)
    for (let Axis = 0; Axis < 3; Axis++) {
      Min[Axis] = Math.min(Min[Axis], Mesh.Positions[K + Axis]);
      Max[Axis] = Math.max(Max[Axis], Mesh.Positions[K + Axis]);
    }
  Mesh.Bounds = { Min, Max };
  Mesh.Tangents = ComputeTangents(Mesh);
  Mesh.UvLines = UvEdges(Mesh);
  Mesh.TriangleCount = Mesh.Indices.length / 3;
  return Mesh;
}

// Maps a unit square onto a rectangle inside UV space.
const Rect = (X, Y, W, H) => (U, V) => [X + U * W, Y + V * H];

function Sphere() {
  const B = new MeshBuilder();
  B.Surface(128, 64, (U, V) => {
    const Phi = U * Math.PI * 2;
    const Theta = V * Math.PI;
    const R = Math.sin(Theta);
    const N = [R * Math.sin(Phi), -Math.cos(Theta), R * Math.cos(Phi)];
    return { P: N, N };
  }, Rect(0, 0, 1, 1));
  return B.Build("sphere", "UV sphere");
}

function RoundedCube() {
  const B = new MeshBuilder();
  const Half = 0.82;
  const Radius = 0.16 / Half;
  const Faces = [
    { N: [0, 0, 1], U: [1, 0, 0], V: [0, 1, 0], Cell: [1, 1] },
    { N: [1, 0, 0], U: [0, 0, -1], V: [0, 1, 0], Cell: [2, 1] },
    { N: [-1, 0, 0], U: [0, 0, 1], V: [0, 1, 0], Cell: [0, 1] },
    { N: [0, 1, 0], U: [1, 0, 0], V: [0, 0, -1], Cell: [1, 0] },
    { N: [0, -1, 0], U: [1, 0, 0], V: [0, 0, 1], Cell: [0, 0] },
    { N: [0, 0, -1], U: [-1, 0, 0], V: [0, 1, 0], Cell: [2, 0] },
  ];
  const Cell = 1 / 3;
  const Pad = 0.008;
  for (const Face of Faces) {
    const X0 = Face.Cell[0] * Cell + Pad;
    const Y0 = 1 / 6 + Face.Cell[1] * Cell + Pad;
    B.Surface(56, 56, (U, V) => {
      const S = U * 2 - 1;
      const T = V * 2 - 1;
      const Q = [0, 1, 2].map((A) => Face.N[A] + Face.U[A] * S + Face.V[A] * T);
      const Inner = Q.map((C) => Math.max(-(1 - Radius), Math.min(1 - Radius, C)));
      const D = Normalize(Q.map((C, A) => C - Inner[A]));
      const P = Inner.map((C, A) => (C + D[A] * Radius) * Half);
      return { P, N: D };
    }, Rect(X0, Y0, Cell - Pad * 2, Cell - Pad * 2));
  }
  return B.Build("cube", "Rounded cube");
}

// Revolves a (radius, height) profile. Side band UVs follow arc length; flat
// caps become separate disc islands at the same texel density.
function Lathe(Key, Name, Profile, Caps) {
  const B = new MeshBuilder();
  const Lengths = [0];
  for (let K = 1; K < Profile.length; K++)
    Lengths.push(
      Lengths[K - 1] +
        Math.hypot(Profile[K][0] - Profile[K - 1][0], Profile[K][1] - Profile[K - 1][1]),
    );
  const Total = Lengths[Lengths.length - 1];
  const MaxRadius = Math.max(...Profile.map((P) => P[0]));
  const Circumference = Math.PI * 2 * MaxRadius;
  const Density = 0.98 / Circumference;
  const BandHeight = Math.min(0.62, Total * Density);
  const BandY = 0.99 - BandHeight;
  const Sample = (V) => {
    const Target = V * Total;
    let K = 1;
    while (K < Lengths.length - 1 && Lengths[K] < Target) K++;
    const T = (Target - Lengths[K - 1]) / Math.max(1e-9, Lengths[K] - Lengths[K - 1]);
    const R = Profile[K - 1][0] + (Profile[K][0] - Profile[K - 1][0]) * T;
    const Y = Profile[K - 1][1] + (Profile[K][1] - Profile[K - 1][1]) * T;
    const DR = Profile[K][0] - Profile[K - 1][0];
    const DY = Profile[K][1] - Profile[K - 1][1];
    return { R, Y, DR, DY };
  };
  const Steps = Math.max(64, Profile.length * 4);
  B.Surface(128, Steps, (U, V) => {
    const Phi = U * Math.PI * 2;
    const { R, Y, DR, DY } = Sample(V);
    const L = Math.hypot(DR, DY) || 1;
    const Nr = DY / L;
    const Ny = -DR / L;
    return {
      P: [R * Math.sin(Phi), Y, R * Math.cos(Phi)],
      N: Normalize([Nr * Math.sin(Phi), Ny, Nr * Math.cos(Phi)]),
    };
  }, Rect(0.01, BandY, 0.98, BandHeight));
  let CapX = 0.02;
  for (const Cap of Caps) {
    const Size = Cap.Radius * 2 * Density;
    const CenterX = CapX + Size / 2;
    const CenterY = Math.min(BandY - 0.02 - Size / 2, 0.02 + Size / 2 + 0.2);
    CapX += Size + 0.03;
    const Up = Cap.Up;
    B.Surface(96, 24, (U, V) => {
      const Phi = U * Math.PI * 2;
      const Rho = V * Cap.Radius;
      return {
        P: [Rho * Math.sin(Phi), Cap.Y, Rho * Math.cos(Phi)],
        N: [0, Up ? 1 : -1, 0],
      };
    }, (U, V) => {
      const Phi = U * Math.PI * 2;
      const Rho = (V * Size) / 2;
      return [CenterX + Rho * Math.sin(Phi), CenterY + (Up ? -1 : 1) * Rho * Math.cos(Phi)];
    }, Up);
  }
  return B.Build(Key, Name);
}

function Can() {
  const Radius = 0.62;
  const Half = 0.86;
  const Rim = 0.07;
  const Profile = [];
  for (let K = 0; K <= 8; K++) {
    const A = -Math.PI / 2 + (K / 8) * (Math.PI / 2);
    Profile.push([Radius - Rim + Math.cos(A) * Rim, -Half + Rim + Math.sin(A) * Rim]);
  }
  for (let K = 0; K <= 8; K++) {
    const A = (K / 8) * (Math.PI / 2);
    Profile.push([Radius - Rim + Math.cos(A) * Rim, Half - Rim + Math.sin(A) * Rim]);
  }
  Profile[0][0] = Radius - Rim;
  Profile[Profile.length - 1][0] = Radius - Rim;
  return Lathe("can", "Cylinder can", Profile, [
    { Radius: Radius - Rim, Y: Half, Up: true },
    { Radius: Radius - Rim, Y: -Half, Up: false },
  ]);
}

function Bottle() {
  const Points = [
    [0.42, -0.95], [0.5, -0.9], [0.52, -0.8], [0.52, 0.12], [0.5, 0.26],
    [0.4, 0.42], [0.26, 0.55], [0.18, 0.64], [0.16, 0.8], [0.17, 0.86],
    [0.17, 0.95], [0.14, 0.97],
  ];
  // Catmull-Rom smoothing of the silhouette.
  const Profile = [];
  for (let K = 0; K < Points.length - 1; K++) {
    const P0 = Points[Math.max(0, K - 1)];
    const P1 = Points[K];
    const P2 = Points[K + 1];
    const P3 = Points[Math.min(Points.length - 1, K + 2)];
    for (let S = 0; S < 6; S++) {
      const T = S / 6;
      const T2 = T * T, T3 = T2 * T;
      Profile.push([0, 1].map((A) =>
        0.5 * (2 * P1[A] + (-P0[A] + P2[A]) * T + (2 * P0[A] - 5 * P1[A] + 4 * P2[A] - P3[A]) * T2 + (-P0[A] + 3 * P1[A] - 3 * P2[A] + P3[A]) * T3)));
    }
  }
  Profile.push(Points[Points.length - 1]);
  return Lathe("bottle", "Bottle", Profile, [
    { Radius: 0.14, Y: 0.97, Up: true },
    { Radius: 0.42, Y: -0.95, Up: false },
  ]);
}

function Torus() {
  const B = new MeshBuilder();
  const Major = 0.72;
  const Minor = 0.34;
  B.Surface(160, 72, (U, V) => {
    const Phi = U * Math.PI * 2;
    const Theta = V * Math.PI * 2;
    const N = [Math.cos(Theta) * Math.sin(Phi), Math.sin(Theta), Math.cos(Theta) * Math.cos(Phi)];
    const Ring = Major + Minor * Math.cos(Theta);
    return { P: [Ring * Math.sin(Phi), Minor * Math.sin(Theta), Ring * Math.cos(Phi)], N };
  }, Rect(0.005, 0.25, 0.99, 0.5));
  return B.Build("torus", "Torus");
}

function Plane() {
  const B = new MeshBuilder();
  B.Surface(64, 64, (U, V) => ({ P: [U * 2 - 1, V * 2 - 1, 0], N: [0, 0, 1] }), Rect(0, 0, 1, 1));
  return B.Build("plane", "Plane");
}

export const MESH_FACTORIES = {
  cube: RoundedCube,
  sphere: Sphere,
  can: Can,
  bottle: Bottle,
  torus: Torus,
  plane: Plane,
};
export const MESH_OPTIONS = [
  { id: "cube", label: "Rounded cube" },
  { id: "sphere", label: "UV sphere" },
  { id: "can", label: "Cylinder can" },
  { id: "bottle", label: "Bottle" },
  { id: "torus", label: "Torus" },
  { id: "plane", label: "Plane" },
];

const MeshCache = new Map();
export function CreateMesh(Key) {
  if (!MESH_FACTORIES[Key]) Key = "cube";
  if (!MeshCache.has(Key)) MeshCache.set(Key, MESH_FACTORIES[Key]());
  return MeshCache.get(Key);
}

// Wavefront OBJ with UVs. Polygons are fanned; missing normals are smoothed by
// welded position. The result is centered and scaled to a unit radius.
export function ParseObj(Text, Name = "Imported mesh") {
  const V = [], VT = [], VN = [];
  const Positions = [], Normals = [], Uvs = [], Indices = [];
  const Lookup = new Map();
  let MissingNormals = false;
  let MissingUvs = false;
  const Resolve = (Index, Length) => {
    const I = parseInt(Index, 10);
    return I < 0 ? Length + I : I - 1;
  };
  const Corner = (Token) => {
    const Cached = Lookup.get(Token);
    if (Cached !== undefined) return Cached;
    const [Vi, Ti, Ni] = Token.split("/");
    const P = V[Resolve(Vi, V.length)];
    if (!P) throw new Error("OBJ face references a missing vertex.");
    const T = Ti ? VT[Resolve(Ti, VT.length)] : null;
    const N = Ni ? VN[Resolve(Ni, VN.length)] : null;
    if (!T) MissingUvs = true;
    if (!N) MissingNormals = true;
    Positions.push(...P);
    Uvs.push(...(T || [0, 0]));
    Normals.push(...(N || [0, 0, 0]));
    const Index = Positions.length / 3 - 1;
    Lookup.set(Token, Index);
    return Index;
  };
  for (const RawLine of Text.split(/\r?\n/)) {
    const Line = RawLine.trim();
    if (!Line || Line[0] === "#") continue;
    const Parts = Line.split(/\s+/);
    const Tag = Parts[0];
    if (Tag === "v") V.push(Parts.slice(1, 4).map(Number));
    else if (Tag === "vt") VT.push([Number(Parts[1]), Number(Parts[2] ?? 0)]);
    else if (Tag === "vn") VN.push(Parts.slice(1, 4).map(Number));
    else if (Tag === "f") {
      const Corners = Parts.slice(1).map(Corner);
      for (let K = 1; K + 1 < Corners.length; K++)
        Indices.push(Corners[0], Corners[K], Corners[K + 1]);
    }
  }
  if (!Indices.length) throw new Error("The OBJ file contains no faces.");
  if (MissingUvs || !VT.length)
    throw new Error("The OBJ file has no texture coordinates. Unwrap it before painting.");
  if (Positions.length / 3 > 1_500_000) throw new Error("The OBJ file is too dense to paint in the browser.");
  if (MissingNormals) {
    const Welded = new Map();
    const Key = (I) => `${Positions[I * 3].toFixed(5)},${Positions[I * 3 + 1].toFixed(5)},${Positions[I * 3 + 2].toFixed(5)}`;
    const Accum = new Float32Array(Positions.length);
    for (let K = 0; K < Indices.length; K += 3) {
      const [A, B, C] = [Indices[K], Indices[K + 1], Indices[K + 2]];
      const E1 = [0, 1, 2].map((X) => Positions[B * 3 + X] - Positions[A * 3 + X]);
      const E2 = [0, 1, 2].map((X) => Positions[C * 3 + X] - Positions[A * 3 + X]);
      const F = [E1[1] * E2[2] - E1[2] * E2[1], E1[2] * E2[0] - E1[0] * E2[2], E1[0] * E2[1] - E1[1] * E2[0]];
      for (const I of [A, B, C]) {
        const K2 = Key(I);
        if (!Welded.has(K2)) Welded.set(K2, [0, 0, 0]);
        const W = Welded.get(K2);
        W[0] += F[0]; W[1] += F[1]; W[2] += F[2];
      }
    }
    for (let I = 0; I < Positions.length / 3; I++) {
      const N = Normalize(Welded.get(Key(I)) || [0, 1, 0]);
      Accum.set(N, I * 3);
    }
    for (let I = 0; I < Positions.length; I++) Normals[I] = Accum[I];
  }
  const Min = [Infinity, Infinity, Infinity], Max = [-Infinity, -Infinity, -Infinity];
  for (let K = 0; K < Positions.length; K += 3)
    for (let A = 0; A < 3; A++) {
      Min[A] = Math.min(Min[A], Positions[K + A]);
      Max[A] = Math.max(Max[A], Positions[K + A]);
    }
  const Center = Min.map((M, A) => (M + Max[A]) / 2);
  let Radius = 0;
  for (let K = 0; K < Positions.length; K += 3)
    Radius = Math.max(Radius, Math.hypot(Positions[K] - Center[0], Positions[K + 1] - Center[1], Positions[K + 2] - Center[2]));
  const Scale = 1.05 / (Radius || 1);
  for (let K = 0; K < Positions.length; K += 3)
    for (let A = 0; A < 3; A++) Positions[K + A] = (Positions[K + A] - Center[A]) * Scale;
  for (let K = 0; K < Normals.length; K += 3) {
    const N = Normalize([Normals[K], Normals[K + 1], Normals[K + 2]]);
    Normals[K] = N[0]; Normals[K + 1] = N[1]; Normals[K + 2] = N[2];
  }
  // When the whole layout sits in another UDIM-style tile, shift it home.
  for (let Axis = 0; Axis < 2; Axis++) {
    let Low = Infinity;
    let High = -Infinity;
    for (let K = Axis; K < Uvs.length; K += 2) {
      Low = Math.min(Low, Uvs[K]);
      High = Math.max(High, Uvs[K]);
    }
    const Tile = Math.floor(Low + 1e-6);
    if (Tile !== 0 && High - Tile <= 1 + 1e-6)
      for (let K = Axis; K < Uvs.length; K += 2) Uvs[K] -= Tile;
  }
  return FinalizeMesh({
    Key: "custom",
    Name,
    Positions: new Float32Array(Positions),
    Normals: new Float32Array(Normals),
    Uvs: new Float32Array(Uvs),
    Indices: new Uint32Array(Indices),
  });
}
