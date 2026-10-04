// Column-major 4×4 matrices and small vector helpers shared by the renderer,
// camera, decal gizmo and projection painting.

export const Clamp = (Value, Minimum, Maximum) =>
  Math.min(Maximum, Math.max(Minimum, Value));
export const Lerp = (A, B, T) => A + (B - A) * T;

export const Vec3 = {
  Add: (A, B) => [A[0] + B[0], A[1] + B[1], A[2] + B[2]],
  Sub: (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]],
  Scale: (A, S) => [A[0] * S, A[1] * S, A[2] * S],
  Dot: (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2],
  Cross: (A, B) => [
    A[1] * B[2] - A[2] * B[1],
    A[2] * B[0] - A[0] * B[2],
    A[0] * B[1] - A[1] * B[0],
  ],
  Length: (A) => Math.hypot(A[0], A[1], A[2]),
  Normalize: (A) => {
    const L = Math.hypot(A[0], A[1], A[2]) || 1;
    return [A[0] / L, A[1] / L, A[2] / L];
  },
};

export const Mat4 = {
  Identity: () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
  Multiply(A, B) {
    const Out = new Array(16);
    for (let Column = 0; Column < 4; Column++)
      for (let Row = 0; Row < 4; Row++) {
        let Sum = 0;
        for (let K = 0; K < 4; K++) Sum += A[K * 4 + Row] * B[Column * 4 + K];
        Out[Column * 4 + Row] = Sum;
      }
    return Out;
  },
  Perspective(FieldOfView, Aspect, Near, Far) {
    const F = 1 / Math.tan(FieldOfView / 2);
    const Range = 1 / (Near - Far);
    return [
      F / Aspect, 0, 0, 0,
      0, F, 0, 0,
      0, 0, (Far + Near) * Range, -1,
      0, 0, 2 * Far * Near * Range, 0,
    ];
  },
  LookAt(Eye, Target, Up) {
    const Z = Vec3.Normalize(Vec3.Sub(Eye, Target));
    const X = Vec3.Normalize(Vec3.Cross(Up, Z));
    const Y = Vec3.Cross(Z, X);
    return [
      X[0], Y[0], Z[0], 0,
      X[1], Y[1], Z[1], 0,
      X[2], Y[2], Z[2], 0,
      -Vec3.Dot(X, Eye), -Vec3.Dot(Y, Eye), -Vec3.Dot(Z, Eye), 1,
    ];
  },
  Invert(M) {
    const Inv = new Array(16);
    Inv[0] = M[5] * M[10] * M[15] - M[5] * M[11] * M[14] - M[9] * M[6] * M[15] + M[9] * M[7] * M[14] + M[13] * M[6] * M[11] - M[13] * M[7] * M[10];
    Inv[4] = -M[4] * M[10] * M[15] + M[4] * M[11] * M[14] + M[8] * M[6] * M[15] - M[8] * M[7] * M[14] - M[12] * M[6] * M[11] + M[12] * M[7] * M[10];
    Inv[8] = M[4] * M[9] * M[15] - M[4] * M[11] * M[13] - M[8] * M[5] * M[15] + M[8] * M[7] * M[13] + M[12] * M[5] * M[11] - M[12] * M[7] * M[9];
    Inv[12] = -M[4] * M[9] * M[14] + M[4] * M[10] * M[13] + M[8] * M[5] * M[14] - M[8] * M[6] * M[13] - M[12] * M[5] * M[10] + M[12] * M[6] * M[9];
    Inv[1] = -M[1] * M[10] * M[15] + M[1] * M[11] * M[14] + M[9] * M[2] * M[15] - M[9] * M[3] * M[14] - M[13] * M[2] * M[11] + M[13] * M[3] * M[10];
    Inv[5] = M[0] * M[10] * M[15] - M[0] * M[11] * M[14] - M[8] * M[2] * M[15] + M[8] * M[3] * M[14] + M[12] * M[2] * M[11] - M[12] * M[3] * M[10];
    Inv[9] = -M[0] * M[9] * M[15] + M[0] * M[11] * M[13] + M[8] * M[1] * M[15] - M[8] * M[3] * M[13] - M[12] * M[1] * M[11] + M[12] * M[3] * M[9];
    Inv[13] = M[0] * M[9] * M[14] - M[0] * M[10] * M[13] - M[8] * M[1] * M[14] + M[8] * M[2] * M[13] + M[12] * M[1] * M[10] - M[12] * M[2] * M[9];
    Inv[2] = M[1] * M[6] * M[15] - M[1] * M[7] * M[14] - M[5] * M[2] * M[15] + M[5] * M[3] * M[14] + M[13] * M[2] * M[7] - M[13] * M[3] * M[6];
    Inv[6] = -M[0] * M[6] * M[15] + M[0] * M[7] * M[14] + M[4] * M[2] * M[15] - M[4] * M[3] * M[14] - M[12] * M[2] * M[7] + M[12] * M[3] * M[6];
    Inv[10] = M[0] * M[5] * M[15] - M[0] * M[7] * M[13] - M[4] * M[1] * M[15] + M[4] * M[3] * M[13] + M[12] * M[1] * M[7] - M[12] * M[3] * M[5];
    Inv[14] = -M[0] * M[5] * M[14] + M[0] * M[6] * M[13] + M[4] * M[1] * M[14] - M[4] * M[2] * M[13] - M[12] * M[1] * M[6] + M[12] * M[2] * M[5];
    Inv[3] = -M[1] * M[6] * M[11] + M[1] * M[7] * M[10] + M[5] * M[2] * M[11] - M[5] * M[3] * M[10] - M[9] * M[2] * M[7] + M[9] * M[3] * M[6];
    Inv[7] = M[0] * M[6] * M[11] - M[0] * M[7] * M[10] - M[4] * M[2] * M[11] + M[4] * M[3] * M[10] + M[8] * M[2] * M[7] - M[8] * M[3] * M[6];
    Inv[11] = -M[0] * M[5] * M[11] + M[0] * M[7] * M[9] + M[4] * M[1] * M[11] - M[4] * M[3] * M[9] - M[8] * M[1] * M[7] + M[8] * M[3] * M[5];
    Inv[15] = M[0] * M[5] * M[10] - M[0] * M[6] * M[9] - M[4] * M[1] * M[10] + M[4] * M[2] * M[9] + M[8] * M[1] * M[6] - M[8] * M[2] * M[5];
    let Determinant = M[0] * Inv[0] + M[1] * Inv[4] + M[2] * Inv[8] + M[3] * Inv[12];
    if (!Determinant) return Mat4.Identity();
    Determinant = 1 / Determinant;
    return Inv.map((Value) => Value * Determinant);
  },
  TransformPoint(M, P) {
    const X = M[0] * P[0] + M[4] * P[1] + M[8] * P[2] + M[12];
    const Y = M[1] * P[0] + M[5] * P[1] + M[9] * P[2] + M[13];
    const Z = M[2] * P[0] + M[6] * P[1] + M[10] * P[2] + M[14];
    const W = M[3] * P[0] + M[7] * P[1] + M[11] * P[2] + M[15];
    return [X, Y, Z, W];
  },
};

// Builds an orthonormal decal frame from a surface normal and a preferred up.
export const DecalFrame = (Normal, PreferredUp, Rotation = 0) => {
  const N = Vec3.Normalize(Normal);
  let Up = PreferredUp;
  if (Math.abs(Vec3.Dot(N, Vec3.Normalize(Up))) > 0.98)
    Up = Math.abs(N[1]) < 0.9 ? [0, 1, 0] : [0, 0, -1];
  let Right = Vec3.Normalize(Vec3.Cross(Up, N));
  let TrueUp = Vec3.Cross(N, Right);
  const C = Math.cos(Rotation);
  const S = Math.sin(Rotation);
  const R = Vec3.Add(Vec3.Scale(Right, C), Vec3.Scale(TrueUp, S));
  const U = Vec3.Add(Vec3.Scale(Right, -S), Vec3.Scale(TrueUp, C));
  return { Right: R, Up: U, Normal: N };
};

export const HexToRgb = (Hex) => {
  const Value = String(Hex || "#000000").replace("#", "");
  const Full =
    Value.length === 3
      ? Value.split("").map((C) => C + C).join("")
      : Value.padEnd(6, "0").slice(0, 6);
  return [0, 2, 4].map((Index) => parseInt(Full.slice(Index, Index + 2), 16) / 255);
};
export const RgbToHex = (Rgb) =>
  "#" +
  Rgb.map((Value) =>
    Math.round(Clamp(Value, 0, 1) * 255)
      .toString(16)
      .padStart(2, "0"),
  ).join("");
