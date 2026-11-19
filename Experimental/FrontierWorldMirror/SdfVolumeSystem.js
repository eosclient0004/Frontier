// Frontier World Mirror signed-distance-field volume pipeline.
//
// This is intentionally a real dense signed field, not a visual slider: it samples
// the current analytical scene into a Float32 voxel volume, preserves world-space
// bounds/spacing, supports cancellation between Z slices, and exposes trilinear
// sampling for renderer/tool consumers.  The current gallery is made of analytical
// primitives, so evaluating its exact primitive SDFs avoids a lossy mesh conversion.

const Clamp = (Value, Minimum, Maximum) => Math.max(Minimum, Math.min(Maximum, Value));
const Dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Length = (V) => Math.hypot(V[0], V[1], V[2]);
const Sub = (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]];

export const SdfQualityPresets = Object.freeze([
  { id: 32, label: "Preview · 32³", steps: 8 },
  { id: 48, label: "Balanced · 48³", steps: 12 },
  { id: 64, label: "High · 64³", steps: 16 },
]);

export const CanonicalSdfSettings = Object.freeze({
  SdfResolution: 48,
  SdfPadding: 0.12,
  SdfConeSteps: 12,
});

export function ResolveSdfSettings(Source = {}) {
  const Resolution = Number(Source.SdfResolution);
  const Preset = SdfQualityPresets.find((Entry) => Entry.id === Resolution) || SdfQualityPresets[1];
  return {
    SdfResolution: Preset.id,
    SdfPadding: Clamp(Number(Source.SdfPadding) || CanonicalSdfSettings.SdfPadding, 0.02, 0.45),
    SdfConeSteps: Clamp(Math.round(Number(Source.SdfConeSteps) || Preset.steps), 4, 24),
  };
}

function RotateColumns(Rotation = [0, 0, 0]) {
  const [Rx, Ry, Rz] = Rotation;
  const Cx = Math.cos(Rx || 0), Sx = Math.sin(Rx || 0);
  const Cy = Math.cos(Ry || 0), Sy = Math.sin(Ry || 0);
  const Cz = Math.cos(Rz || 0), Sz = Math.sin(Rz || 0);
  return [
    [Cz * Cy, Sz * Cy, -Sy],
    [Cz * Sy * Sx - Sz * Cx, Sz * Sy * Sx + Cz * Cx, Cy * Sx],
    [Cz * Sy * Cx + Sz * Sx, Sz * Sy * Cx - Cz * Sx, Cy * Cx],
  ];
}

function ToLocal(Point, Primitive) {
  const Relative = Sub(Point, Primitive.Center || [0, 0, 0]);
  const [C0, C1, C2] = RotateColumns(Primitive.RotationRad);
  return [Dot(C0, Relative), Dot(C1, Relative), Dot(C2, Relative)];
}

function SdBox(Point, Half) {
  const Q = [Math.abs(Point[0]) - Half[0], Math.abs(Point[1]) - Half[1], Math.abs(Point[2]) - Half[2]];
  return Length([Math.max(Q[0], 0), Math.max(Q[1], 0), Math.max(Q[2], 0)]) + Math.min(Math.max(Q[0], Math.max(Q[1], Q[2])), 0);
}

function SdEllipsoid(Point, Half) {
  const Safe = Half.map((V) => Math.max(V, 1e-4));
  const Scaled = [Point[0] / Safe[0], Point[1] / Safe[1], Point[2] / Safe[2]];
  // First-order exact-on-surface ellipsoid distance.  It remains signed and
  // conservative enough for cone marching on non-uniformly scaled spheres.
  return (Length(Scaled) - 1) * Math.min(...Safe);
}

function SdCappedCylinder(Point, Half) {
  const Radius = Math.max(Half[0], Half[1]);
  const Q = [Math.hypot(Point[0], Point[1]) - Radius, Math.abs(Point[2]) - Half[2]];
  return Math.min(Math.max(Q[0], Q[1]), 0) + Math.hypot(Math.max(Q[0], 0), Math.max(Q[1], 0));
}

function SdTorus(Point, Half) {
  const Major = Math.max(Half[0], Half[1]) * 0.68;
  const Minor = Math.max(0.025, Half[2] * 0.55);
  const Qx = Math.hypot(Point[0], Point[1]) - Major;
  return Math.hypot(Qx, Point[2]) - Minor;
}

function SdCone(Point, Half) {
  // Stable capped-frustum approximation matching the gallery cone's wide base.
  const Radius = Math.max(Half[0], Half[1]);
  const Bottom = -0.78 * Half[2];
  const Top = Half[2];
  const Height = Math.max(1e-4, Top - Bottom);
  const T = Clamp((Point[2] - Bottom) / Height, 0, 1);
  const RadiusAtZ = (1 - T) * Radius * 0.72 + T * Radius * 0.13;
  const Radial = Math.hypot(Point[0], Point[1]) - RadiusAtZ;
  const Vertical = Math.max(Bottom - Point[2], Point[2] - Top);
  return Math.min(Math.max(Radial, Vertical), 0) + Math.hypot(Math.max(Radial, 0), Math.max(Vertical, 0));
}

export function EvaluatePrimitiveSdf(Point, Primitive) {
  const Local = ToLocal(Point, Primitive);
  const Half = (Primitive.HalfExtents || [0.5, 0.5, 0.5]).map((Value) => Math.max(0.005, Math.abs(Value)));
  switch (Primitive.Shape) {
    case 1:
      return SdEllipsoid(Local, Half);
    case 2:
    case 5:
      return SdCappedCylinder(Local, Half);
    case 3:
      return SdCone(Local, Half);
    case 4:
      return SdTorus(Local, Half);
    default:
      return SdBox(Local, Half);
  }
}

function VehicleBoundsSdf(Point, Vehicle) {
  // The vehicle's triangle BLAS is used for direct BVH/path tracing.  The SDF
  // cache intentionally uses a conservative chassis proxy so the global field
  // can represent it without an expensive CPU mesh voxelization pass.
  const Scale = Vehicle.Scale || [1, 1, 1];
  const Proxy = {
    Center: [Vehicle.Position?.[0] || 0, Vehicle.Position?.[1] || 0, (Vehicle.Position?.[2] || 0) + 0.55 * Scale[2]],
    RotationRad: (Vehicle.Rotation || [0, 0, 0]).map((Degrees) => (Number(Degrees) || 0) * Math.PI / 180),
    HalfExtents: [1.85 * Scale[0], 0.83 * Scale[1], 0.58 * Scale[2]],
    Shape: 0,
  };
  return EvaluatePrimitiveSdf(Point, Proxy);
}

function SceneBounds(Scene, Padding) {
  const Minimum = [Infinity, Infinity, Infinity];
  const Maximum = [-Infinity, -Infinity, -Infinity];
  const Include = (Center, Radius) => {
    for (let Axis = 0; Axis < 3; Axis++) {
      Minimum[Axis] = Math.min(Minimum[Axis], Center[Axis] - Radius);
      Maximum[Axis] = Math.max(Maximum[Axis], Center[Axis] + Radius);
    }
  };
  for (const Primitive of Scene?.Primitives || []) {
    const H = Primitive.HalfExtents || [0.5, 0.5, 0.5];
    Include(Primitive.Center || [0, 0, 0], Math.hypot(H[0], H[1], H[2]));
  }
  for (const Vehicle of Scene?.Vehicles || []) {
    const S = Vehicle.Scale || [1, 1, 1];
    Include(Vehicle.Position || [0, 0, 0], 2.4 * Math.max(...S));
  }
  if (!Number.isFinite(Minimum[0])) {
    return { Minimum: [-1, -1, -1], Maximum: [1, 1, 1], Size: [2, 2, 2] };
  }
  const Centre = Minimum.map((Value, Axis) => 0.5 * (Value + Maximum[Axis]));
  const Span = Math.max(...Minimum.map((Value, Axis) => Maximum[Axis] - Value), 0.1) * (1 + Padding * 2);
  const Min = Centre.map((Value) => Value - Span * 0.5);
  return { Minimum: Min, Maximum: Min.map((Value) => Value + Span), Size: [Span, Span, Span] };
}

export function SceneSdfSignature(Scene, Settings = {}) {
  const Relevant = {
    primitives: (Scene?.Primitives || []).map((P) => [P.Id, P.Shape, P.Center, P.RotationRad, P.HalfExtents, P.GiContributor, P.GlassShell]),
    vehicles: (Scene?.Vehicles || []).map((V) => [V.Id, V.Position, V.Rotation, V.Scale]),
    settings: ResolveSdfSettings(Settings),
  };
  return JSON.stringify(Relevant);
}

export async function BakeSceneSdf(
  Scene,
  Settings = {},
  Progress = () => {},
  Cancelled = () => false,
) {
  const Resolved = ResolveSdfSettings(Settings);
  const Bounds = SceneBounds(Scene, Resolved.SdfPadding);
  const Resolution = Resolved.SdfResolution;
  const Spacing = Bounds.Size[0] / Resolution;
  const Values = new Float32Array(Resolution * Resolution * Resolution);
  let MinimumDistance = Infinity;
  let MaximumDistance = -Infinity;
  const Signature = SceneSdfSignature(Scene, Resolved);

  for (let Z = 0; Z < Resolution; Z++) {
    if (Cancelled()) throw new Error("SDF bake cancelled.");
    for (let Y = 0; Y < Resolution; Y++) {
      for (let X = 0; X < Resolution; X++) {
        const Point = [
          Bounds.Minimum[0] + (X + 0.5) * Spacing,
          Bounds.Minimum[1] + (Y + 0.5) * Spacing,
          Bounds.Minimum[2] + (Z + 0.5) * Spacing,
        ];
        let Distance = Infinity;
        for (const Primitive of Scene?.Primitives || []) {
          if (Primitive.GlassShell) continue;
          Distance = Math.min(Distance, EvaluatePrimitiveSdf(Point, Primitive));
        }
        for (const Vehicle of Scene?.Vehicles || []) {
          Distance = Math.min(Distance, VehicleBoundsSdf(Point, Vehicle));
        }
        if (!Number.isFinite(Distance)) Distance = Bounds.Size[0];
        const Index = (Z * Resolution + Y) * Resolution + X;
        Values[Index] = Distance;
        MinimumDistance = Math.min(MinimumDistance, Distance);
        MaximumDistance = Math.max(MaximumDistance, Distance);
      }
    }
    Progress((Z + 1) / Resolution);
    // Yield at a deterministic slice boundary: cancellation does not leave a
    // partially-published volume and the browser stays responsive.
    await new Promise((Resolve) => setTimeout(Resolve, 0));
  }

  return {
    Kind: "Frontier.AnalyticalSceneSdf.v1",
    Units: "metres",
    Layout: "X fastest, then Y, then Z; voxel-centred Float32 signed distance",
    Resolution,
    Bounds,
    Spacing,
    Values,
    Signed: true,
    Method: "Analytical primitive union + conservative ControlVehicle chassis proxy",
    PrimitiveCount: (Scene?.Primitives || []).length,
    VehicleCount: (Scene?.Vehicles || []).length,
    MinimumDistance,
    MaximumDistance,
    Settings: Resolved,
    Signature,
  };
}

export function SampleSdfTrilinear(Volume, Point) {
  if (!Volume?.Values?.length || !Array.isArray(Volume.Bounds?.Minimum)) return Infinity;
  const N = Volume.Resolution;
  const Q = Point.map((Value, Axis) => (Value - Volume.Bounds.Minimum[Axis]) / Volume.Spacing - 0.5);
  if (Q.some((Value) => Value < 0 || Value > N - 1)) return Infinity;
  const X0 = Math.floor(Q[0]), Y0 = Math.floor(Q[1]), Z0 = Math.floor(Q[2]);
  const X1 = Math.min(N - 1, X0 + 1), Y1 = Math.min(N - 1, Y0 + 1), Z1 = Math.min(N - 1, Z0 + 1);
  const Tx = Q[0] - X0, Ty = Q[1] - Y0, Tz = Q[2] - Z0;
  const At = (X, Y, Z) => Volume.Values[(Z * N + Y) * N + X];
  const Lerp = (A, B, T) => A + (B - A) * T;
  const C00 = Lerp(At(X0, Y0, Z0), At(X1, Y0, Z0), Tx);
  const C10 = Lerp(At(X0, Y1, Z0), At(X1, Y1, Z0), Tx);
  const C01 = Lerp(At(X0, Y0, Z1), At(X1, Y0, Z1), Tx);
  const C11 = Lerp(At(X0, Y1, Z1), At(X1, Y1, Z1), Tx);
  return Lerp(Lerp(C00, C10, Ty), Lerp(C01, C11, Ty), Tz);
}

export function SerializeSdfVolume(Volume) {
  if (!Volume?.Values) throw new Error("No SDF volume is ready to export.");
  const Header = JSON.stringify({
    Kind: Volume.Kind,
    Units: Volume.Units,
    Layout: Volume.Layout,
    Resolution: Volume.Resolution,
    Bounds: Volume.Bounds,
    Spacing: Volume.Spacing,
    Signed: Volume.Signed,
    Method: Volume.Method,
    PrimitiveCount: Volume.PrimitiveCount,
    VehicleCount: Volume.VehicleCount,
    MinimumDistance: Volume.MinimumDistance,
    MaximumDistance: Volume.MaximumDistance,
    Settings: Volume.Settings,
  });
  const Encoder = new TextEncoder();
  const HeaderBytes = Encoder.encode(Header);
  const Buffer = new ArrayBuffer(8 + HeaderBytes.length + Volume.Values.byteLength);
  const View = new DataView(Buffer);
  View.setUint32(0, 0x46534446, true); // "FSDF"
  View.setUint32(4, HeaderBytes.length, true);
  new Uint8Array(Buffer, 8, HeaderBytes.length).set(HeaderBytes);
  new Uint8Array(Buffer, 8 + HeaderBytes.length).set(new Uint8Array(Volume.Values.buffer, Volume.Values.byteOffset, Volume.Values.byteLength));
  return Buffer;
}
