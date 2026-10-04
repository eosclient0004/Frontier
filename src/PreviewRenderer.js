/* Frontier Texture Paint — PBR material preview with layered fallbacks.
   WebGL2 first, then WebGL1 (ES 1.00 shaders), then a dependency-free CPU
   ray-traced software renderer — so the 3D view survives blocked GPUs,
   driver blocklists and strict browser policies instead of erroring out. */

/* ---------------- Shader sources (built per GL version) ---------------- */

function buildVertexSource(IsGL2) {
  const Head = IsGL2
    ? `#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;`
    : `attribute vec3 aPosition;
attribute vec3 aNormal;
attribute vec2 aUv;
varying vec3 vWorldPos;
varying vec3 vNormal;
varying vec2 vUv;`;
  return `${Head}
uniform mat4 uModel;
uniform mat4 uViewProj;
void main() {
  vec4 World = uModel * vec4(aPosition, 1.0);
  vWorldPos = World.xyz;
  vNormal = mat3(uModel) * aNormal;
  vUv = aUv;
  gl_Position = uViewProj * World;
}`;
}

function buildFragmentSource(IsGL2) {
  const Head = IsGL2
    ? `#version 300 es
#define USE_DERIVATIVES 1
precision highp float;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
out vec4 oColor;`
    : `#extension GL_OES_standard_derivatives : enable
#ifdef GL_OES_standard_derivatives
#define USE_DERIVATIVES 1
#endif
precision highp float;
varying vec3 vWorldPos;
varying vec3 vNormal;
varying vec2 vUv;`;
  const Sample = IsGL2 ? "texture" : "texture2D";
  const Output = (Value) => (IsGL2 ? `oColor = ${Value};` : `gl_FragColor = ${Value};`);
  return `${Head}
uniform sampler2D uAlbedo;
uniform sampler2D uMetallic;
uniform sampler2D uRoughness;
uniform sampler2D uNormal;
uniform sampler2D uEmissive;
uniform vec3 uCamera;
uniform float uNormalStrength;

vec3 sampleNormal(vec2 uv, vec3 normal, vec3 worldPos) {
#ifdef USE_DERIVATIVES
  vec3 tangentNormal = ${Sample}(uNormal, uv).xyz * 2.0 - 1.0;
  tangentNormal.xy *= uNormalStrength;
  vec3 q0 = dFdx(worldPos);
  vec3 q1 = dFdy(worldPos);
  vec2 st0 = dFdx(uv);
  vec2 st1 = dFdy(uv);
  vec3 n = normalize(normal);
  vec3 t = normalize(q0 * st1.t - q1 * st0.t);
  vec3 b = -normalize(cross(n, t));
  mat3 tbn = mat3(t, b, n);
  return normalize(tbn * tangentNormal);
#else
  return normalize(normal);
#endif
}

vec3 aces(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 shadeLight(vec3 L, vec3 lightColor, vec3 N, vec3 V, vec3 base, vec3 F0, vec3 specColor, float shininess) {
  float NdotL = max(dot(N, L), 0.0);
  vec3 H = normalize(L + V);
  float NdotH = max(dot(N, H), 0.0);
  float VdotH = max(dot(V, H), 0.0);
  vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
  float spec = pow(NdotH, shininess) * (shininess * 0.25 + 0.5);
  return (base / 3.14159 * (1.0 - F * 0.85) * NdotL + F * specColor * spec * NdotL) * lightColor * 0.55;
}

void main() {
  vec3 albedo = pow(${Sample}(uAlbedo, vUv).rgb, vec3(2.2));
  float metallic = ${Sample}(uMetallic, vUv).r;
  float roughness = clamp(${Sample}(uRoughness, vUv).r, 0.04, 1.0);
  vec3 emissive = ${Sample}(uEmissive, vUv).rgb * 1.6;
  vec3 N = sampleNormal(vUv, vNormal, vWorldPos);
  vec3 V = normalize(uCamera - vWorldPos);

  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  vec3 base = albedo * (1.0 - metallic);

  vec3 color = base * vec3(0.10, 0.105, 0.115);
  float shininess = mix(220.0, 14.0, roughness);
  vec3 specColor = mix(vec3(1.0), albedo, metallic);
  color += shadeLight(normalize(vec3(0.55, 0.75, 0.6)), vec3(2.8, 2.68, 2.5), N, V, base, F0, specColor, shininess);
  color += shadeLight(normalize(vec3(-0.7, 0.25, -0.55)), vec3(0.85, 1.0, 1.35), N, V, base, F0, specColor, shininess);
  color += shadeLight(normalize(vec3(-0.15, -0.6, 0.75)), vec3(0.55, 0.5, 0.5), N, V, base, F0, specColor, shininess);
  color += base * pow(max(N.y * 0.5 + 0.5, 0.0), 2.0) * 0.12 * (1.0 - metallic);
  color += emissive;
  color = aces(color);
  color = pow(color, vec3(1.0 / 2.2));
  float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) / 255.0;
  ${Output("vec4(color + dither, 1.0)")}
}`;
}

/* ---------------- Matrix + geometry helpers (unchanged) ---------------- */

function multiplyMatrices(A, B) {
  const Out = new Float32Array(16);
  for (let Row = 0; Row < 4; Row++) {
    for (let Col = 0; Col < 4; Col++) {
      Out[Col * 4 + Row] =
        A[Row] * B[Col * 4] + A[4 + Row] * B[Col * 4 + 1] + A[8 + Row] * B[Col * 4 + 2] + A[12 + Row] * B[Col * 4 + 3];
    }
  }
  return Out;
}

function perspective(FovY, Aspect, Near, Far) {
  const F = 1 / Math.tan(FovY / 2);
  const Out = new Float32Array(16);
  Out[0] = F / Aspect;
  Out[5] = F;
  Out[10] = (Far + Near) / (Near - Far);
  Out[11] = -1;
  Out[14] = (2 * Far * Near) / (Near - Far);
  return Out;
}

function lookAt(Eye, Center, Up) {
  const Z = normalize3([Eye[0] - Center[0], Eye[1] - Center[1], Eye[2] - Center[2]]);
  const X = normalize3(cross3(Up, Z));
  const Y = cross3(Z, X);
  return new Float32Array([
    X[0], Y[0], Z[0], 0,
    X[1], Y[1], Z[1], 0,
    X[2], Y[2], Z[2], 0,
    -(X[0] * Eye[0] + X[1] * Eye[1] + X[2] * Eye[2]),
    -(Y[0] * Eye[0] + Y[1] * Eye[1] + Y[2] * Eye[2]),
    -(Z[0] * Eye[0] + Z[1] * Eye[1] + Z[2] * Eye[2]),
    1,
  ]);
}

function rotationY(Angle) {
  const Cos = Math.cos(Angle);
  const Sin = Math.sin(Angle);
  return new Float32Array([Cos, 0, -Sin, 0, 0, 1, 0, 0, Sin, 0, Cos, 0, 0, 0, 0, 1]);
}

function normalize3(V) {
  const Length = Math.hypot(V[0], V[1], V[2]) || 1;
  return [V[0] / Length, V[1] / Length, V[2] / Length];
}

function cross3(A, B) {
  return [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
}

function buildSphere() {
  const LatBands = 40;
  const LongBands = 56;
  const Positions = [];
  const Normals = [];
  const Uvs = [];
  const Indices = [];
  for (let Lat = 0; Lat <= LatBands; Lat++) {
    const Theta = (Lat * Math.PI) / LatBands;
    const SinTheta = Math.sin(Theta);
    const CosTheta = Math.cos(Theta);
    for (let Long = 0; Long <= LongBands; Long++) {
      const Phi = (Long * 2 * Math.PI) / LongBands;
      const X = CosTheta;
      const Y = SinTheta * Math.cos(Phi);
      const Z = SinTheta * Math.sin(Phi);
      Positions.push(X, Y, Z);
      Normals.push(X, Y, Z);
      Uvs.push(Long / LongBands, 1 - Lat / LatBands);
    }
  }
  for (let Lat = 0; Lat < LatBands; Lat++) {
    for (let Long = 0; Long < LongBands; Long++) {
      const A = Lat * (LongBands + 1) + Long;
      const B = A + LongBands + 1;
      Indices.push(A, B, A + 1, B, B + 1, A + 1);
    }
  }
  return { positions: new Float32Array(Positions), normals: new Float32Array(Normals), uvs: new Float32Array(Uvs), indices: new Uint16Array(Indices) };
}

function buildCube() {
  const Faces = [
    { n: [1, 0, 0], u: [0, 0, -1], v: [0, 1, 0] },
    { n: [-1, 0, 0], u: [0, 0, 1], v: [0, 1, 0] },
    { n: [0, 1, 0], u: [1, 0, 0], v: [0, 0, 1] },
    { n: [0, -1, 0], u: [1, 0, 0], v: [0, 0, -1] },
    { n: [0, 0, 1], u: [1, 0, 0], v: [0, 1, 0] },
    { n: [0, 0, -1], u: [-1, 0, 0], v: [0, 1, 0] },
  ];
  const Positions = [];
  const Normals = [];
  const Uvs = [];
  const Indices = [];
  Faces.forEach((Face, Fi) => {
    const [Nx, Ny, Nz] = Face.n;
    const Corners = [[-1, -1], [1, -1], [1, 1], [-1, 1]];
    const Base = Fi * 4;
    Corners.forEach(([Su, Sv], Ci) => {
      Positions.push(
        Nx * 0.5 + Face.u[0] * Su * 0.5 + Face.v[0] * Sv * 0.5,
        Ny * 0.5 + Face.u[1] * Su * 0.5 + Face.v[1] * Sv * 0.5,
        Nz * 0.5 + Face.u[2] * Su * 0.5 + Face.v[2] * Sv * 0.5,
      );
      Normals.push(Nx, Ny, Nz);
      Uvs.push(Ci === 0 || Ci === 3 ? 0 : 1, Ci < 2 ? 0 : 1);
    });
    Indices.push(Base, Base + 1, Base + 2, Base, Base + 2, Base + 3);
  });
  return { positions: new Float32Array(Positions), normals: new Float32Array(Normals), uvs: new Float32Array(Uvs), indices: new Uint16Array(Indices) };
}

function buildPlane() {
  return {
    positions: new Float32Array([-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0]),
    normals: new Float32Array([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1]),
    uvs: new Float32Array([0, 0, 1, 0, 1, 1, 0, 1]),
    indices: new Uint16Array([0, 1, 2, 0, 2, 3]),
  };
}

function buildCylinder() {
  const Segments = 48;
  const Positions = [];
  const Normals = [];
  const Uvs = [];
  const Indices = [];
  for (let Row = 0; Row <= 1; Row++) {
    const Y = Row === 0 ? -1 : 1;
    for (let Index = 0; Index <= Segments; Index++) {
      const Angle = (Index / Segments) * Math.PI * 2;
      const X = Math.cos(Angle);
      const Z = Math.sin(Angle);
      Positions.push(X, Y, Z);
      Normals.push(X, 0, Z);
      Uvs.push(Index / Segments, Row);
    }
  }
  for (let Index = 0; Index < Segments; Index++) {
    const A = Index;
    const B = Index + Segments + 1;
    Indices.push(A, A + 1, B, B, A + 1, B + 1);
  }
  const CapBase = Positions.length / 3;
  Positions.push(0, 1, 0);
  Normals.push(0, 1, 0);
  Uvs.push(0.5, 0.5);
  for (let Index = 0; Index <= Segments; Index++) {
    const Angle = (Index / Segments) * Math.PI * 2;
    Positions.push(Math.cos(Angle), 1, Math.sin(Angle));
    Normals.push(0, 1, 0);
    Uvs.push(0.5 + Math.cos(Angle) * 0.5, 0.5 + Math.sin(Angle) * 0.5);
  }
  for (let Index = 0; Index < Segments; Index++) {
    Indices.push(CapBase, CapBase + 1 + Index, CapBase + 2 + Index);
  }
  return { positions: new Float32Array(Positions), normals: new Float32Array(Normals), uvs: new Float32Array(Uvs), indices: new Uint16Array(Indices) };
}

const BUILDERS = { sphere: buildSphere, cube: buildCube, plane: buildPlane, cylinder: buildCylinder };

/* ---------------- Software fallback: CPU ray-traced PBR sphere ---------------- */

const SOFTWARE_LIGHTS = [
  { dir: [0.55, 0.75, 0.6], color: [2.8, 2.68, 2.5] },
  { dir: [-0.7, 0.25, -0.55], color: [0.85, 1.0, 1.35] },
  { dir: [-0.15, -0.6, 0.75], color: [0.55, 0.5, 0.5] },
];
for (const Light of SOFTWARE_LIGHTS) {
  const Length = Math.hypot(Light.dir[0], Light.dir[1], Light.dir[2]);
  Light.dir = [Light.dir[0] / Length, Light.dir[1] / Length, Light.dir[2] / Length];
}

function acesFilm(X) {
  return Math.max(0, Math.min(1, (X * (2.51 * X + 0.03)) / (X * (2.43 * X + 0.59) + 0.14)));
}

export class SoftwarePreview {
  constructor(Canvas) {
    this.display = Canvas;
    this.dctx = null;
    this.internal = 384;
    this.buffer = null;
    this.bctx = null;
    this.image = null;
    this.maps = null;
    this.stamp = -1;
    this.down = {};
    this.mesh = "sphere"; // software always shades a sphere
    this.yaw = 0.6;
    this.pitch = 0.32;
    this.distance = 3.9;
    this.turntable = true;
    this.dragging = false;
    this.normalStrength = 1;
    this.dirty = true;
    this.lastFrame = 0;
    this.dynamic = "";
    this.quality = "high";
    this.lastUploadMs = 0;
  }

  initialize() {
    try {
      this.dctx = this.display.getContext("2d");
    } catch {
      this.dctx = null;
    }
    if (!this.dctx) return false;
    this.allocate();
    return true;
  }

  setQuality(Quality) {
    this.quality = Quality === "low" ? "low" : "high";
    const Size = this.quality === "low" ? 192 : 384;
    if (Size !== this.internal) {
      this.internal = Size;
      this.allocate();
    }
  }

  allocate() {
    this.buffer = document.createElement("canvas");
    this.buffer.width = this.internal;
    this.buffer.height = this.internal;
    this.bctx = this.buffer.getContext("2d");
    this.image = this.bctx.createImageData(this.internal, this.internal);
    this.dirty = true;
  }

  /** Downsample the composite maps to 256² for cache-friendly CPU sampling. */
  uploadTextures(Composite, Stamp) {
    if (Stamp === this.stamp) return;
    const Now = typeof performance !== "undefined" ? performance.now() : 0;
    if (this.stamp >= 0 && Now - this.lastUploadMs < 120) return; // stroke in flight — catch up next frame
    this.lastUploadMs = Now;
    this.stamp = Stamp;
    const Size = 256;
    const Take = (Name, Source) => {
      let Canvas = this.down[Name];
      if (!Canvas) {
        Canvas = document.createElement("canvas");
        Canvas.width = Size;
        Canvas.height = Size;
        this.down[Name] = Canvas;
      }
      const Context = Canvas.getContext("2d");
      Context.save();
      Context.globalCompositeOperation = "source-over";
      Context.clearRect(0, 0, Size, Size);
      Context.drawImage(Source, 0, 0, Size, Size);
      Context.restore();
      const Data = Context.getImageData(0, 0, Size, Size);
      return { d: Data.data, w: Size, h: Size };
    };
    this.maps = {
      albedo: Take("albedo", Composite.albedo),
      metallic: Take("metallic", Composite.metallic),
      roughness: Take("roughness", Composite.roughness),
      normal: Take("normal", Composite.normal),
      emissive: Take("emissive", Composite.emissive),
    };
    this.dirty = true;
  }

  resize(PixelRatioCap = 2) {
    const Ratio = Math.min(window.devicePixelRatio || 1, PixelRatioCap);
    const Rect = this.display.getBoundingClientRect();
    const Width = Math.max(2, Math.floor(Rect.width * Ratio));
    const Height = Math.max(2, Math.floor(Rect.height * Ratio));
    if (this.display.width !== Width || this.display.height !== Height) {
      this.display.width = Width;
      this.display.height = Height;
    }
    this.dirty = true;
  }

  render(_TimeSeconds, DeltaSeconds) {
    if (!this.dctx || !this.maps) return;
    if (this.turntable && !this.dragging) {
      this.yaw += DeltaSeconds * 0.22;
      this.dirty = true;
    }
    const Want = this.dynamic ? 192 : this.quality === "low" ? 192 : 384;
    if (Want !== this.internal) {
      this.internal = Want;
      this.allocate();
    }
    const Now = typeof performance !== "undefined" ? performance.now() : 0;
    const Gap = this.dynamic === "turntable" ? 80 : 0;
    if (!this.dirty || Now - this.lastFrame < Gap) return;
    this.lastFrame = Now;
    this.dirty = false;
    this.trace();
    this.bctx.putImageData(this.image, 0, 0);
    const Dw = this.display.width;
    const Dh = this.display.height;
    const Scale = Math.max(Dw, Dh) / this.internal;
    const W = this.internal * Scale;
    const H = this.internal * Scale;
    this.dctx.save();
    this.dctx.imageSmoothingEnabled = true;
    this.dctx.imageSmoothingQuality = "high";
    this.dctx.drawImage(this.buffer, (Dw - W) / 2, (Dh - H) / 2, W, H);
    this.dctx.restore();
  }

  trace() {
    const S = this.internal;
    const Out = this.image.data;
    const Maps = this.maps;
    const Alb = Maps.albedo;
    const Met = Maps.metallic;
    const Rgh = Maps.roughness;
    const Nrm = Maps.normal;
    const Emi = Maps.emissive;
    // Camera basis (matches the GL orbit rig).
    const Cp = Math.cos(this.pitch);
    const Sp = Math.sin(this.pitch);
    const Cy = Math.cos(this.yaw);
    const Sy = Math.sin(this.yaw);
    const Dist = this.distance;
    const Ex = Cp * Sy * Dist;
    const Ey = Sp * Dist;
    const Ez = Cp * Cy * Dist;
    const El = Math.hypot(Ex, Ey, Ez) || 1;
    const Fx = -Ex / El;
    const Fy = -Ey / El;
    const Fz = -Ez / El;
    let Rx = -Fz;
    let Ry = 0;
    let Rz = Fx;
    const Rl = Math.hypot(Rx, Ry, Rz) || 1;
    Rx /= Rl;
    Ry /= Rl;
    Rz /= Rl;
    const Ux = Ry * Fz - Rz * Fy;
    const Uy = Rz * Fx - Rx * Fz;
    const Uz = Rx * Fy - Ry * Fx;
    const TanH = Math.tan((32 * Math.PI) / 360);
    const Eye2 = Ex * Ex + Ey * Ey + Ez * Ez;
    const NormalStrength = this.normalStrength;
    const TwoPi = Math.PI * 2;

    for (let Py = 0; Py < S; Py++) {
      const NdcY = 1 - ((Py + 0.5) / S) * 2;
      for (let Px = 0; Px < S; Px++) {
        const NdcX = ((Px + 0.5) / S) * 2 - 1;
        const Index = (Py * S + Px) * 4;
        let Dx = Fx + NdcX * TanH * Rx + NdcY * TanH * Ux;
        let Dy = Fy + NdcX * TanH * Ry + NdcY * TanH * Uy;
        let Dz = Fz + NdcX * TanH * Rz + NdcY * TanH * Uz;
        const Dl = Math.hypot(Dx, Dy, Dz) || 1;
        Dx /= Dl;
        Dy /= Dl;
        Dz /= Dl;
        const B = Ex * Dx + Ey * Dy + Ez * Dz;
        const Disc = B * B - (Eye2 - 1);
        if (Disc <= 0) {
          this.backgroundPixel(Out, Index, NdcX, NdcY, Px, Py);
          continue;
        }
        const T = -B - Math.sqrt(Disc);
        if (T <= 0) {
          this.backgroundPixel(Out, Index, NdcX, NdcY, Px, Py);
          continue;
        }
        // Unit-sphere hit: position doubles as the object-space normal.
        const Nx = Ex + Dx * T;
        const Ny = Ey + Dy * T;
        const Nz = Ez + Dz * T;
        // UVs matching buildSphere (poles on ±X): u = φ/2π, v = 1 − θ/π.
        let U = Math.atan2(Nz, Ny) / TwoPi;
        U -= Math.floor(U);
        const ClampedX = Nx < -1 ? -1 : Nx > 1 ? 1 : Nx;
        const V = 1 - Math.acos(ClampedX) / Math.PI;
        const Tx = Math.min(255, (U * 256) | 0);
        const Ty = Math.min(255, (V * 256) | 0);
        const Ti = (Ty * 256 + Tx) * 4;
        const Ar = Alb.d[Ti] / 255;
        const Ag = Alb.d[Ti + 1] / 255;
        const Ab = Alb.d[Ti + 2] / 255;
        const Metal = Met.d[Ti] / 255;
        let Rough = Rgh.d[Ti] / 255;
        Rough = Rough < 0.04 ? 0.04 : Rough > 1 ? 1 : Rough;
        // Tangent frame around the X-pole sphere parameterization:
        // T = normalize(cross([1,0,0], N)) = normalize([0, Nz, -Ny]).
        let Tx1 = Nz;
        let Tx2 = -Ny;
        let Tl = Math.hypot(Tx1, Tx2);
        let Wx;
        let Wy;
        let Wz;
        if (Tl < 1e-4) {
          Wx = Nx;
          Wy = Ny;
          Wz = Nz;
        } else {
          Tx1 /= Tl;
          Tx2 /= Tl;
          // B = cross(N, T)
          const Bx = Ny * Tx2 - Nz * Tx1;
          const By = Nz * 0 - Nx * Tx2;
          const Bz = Nx * Tx1 - Ny * 0;
          let Tsx = (Nrm.d[Ti] / 255) * 2 - 1;
          let Tsy = (Nrm.d[Ti + 1] / 255) * 2 - 1;
          const Tsz = (Nrm.d[Ti + 2] / 255) * 2 - 1;
          Tsx *= NormalStrength;
          Tsy *= NormalStrength;
          Wx = 0 * Tsx + Bx * Tsy + Nx * Tsz;
          Wy = Tx1 * Tsx + By * Tsy + Ny * Tsz;
          Wz = Tx2 * Tsx + Bz * Tsy + Nz * Tsz;
          const Wl = Math.hypot(Wx, Wy, Wz) || 1;
          Wx /= Wl;
          Wy /= Wl;
          Wz /= Wl;
        }
        // View vector (world).
        let Vx = Ex - (Ex + Dx * T);
        let Vy = Ey - (Ey + Dy * T);
        let Vz = Ez - (Ez + Dz * T);
        const Vl = Math.hypot(Vx, Vy, Vz) || 1;
        Vx /= Vl;
        Vy /= Vl;
        Vz /= Vl;
        // sRGB → linear (matches the GL shader).
        const Lr = Math.pow(Ar, 2.2);
        const Lg = Math.pow(Ag, 2.2);
        const Lb = Math.pow(Ab, 2.2);
        const F0r = 0.04 + (Lr - 0.04) * Metal;
        const F0g = 0.04 + (Lg - 0.04) * Metal;
        const F0b = 0.04 + (Lb - 0.04) * Metal;
        const Br = Lr * (1 - Metal);
        const Bg = Lg * (1 - Metal);
        const Bb = Lb * (1 - Metal);
        let Cr = Br * 0.1;
        let Cg = Bg * 0.105;
        let Cb = Bb * 0.115;
        const Shininess = 220 + (14 - 220) * Rough;
        const SpecBoost = Shininess * 0.25 + 0.5;
        const Sr = 1 + (Lr - 1) * Metal;
        const Sg = 1 + (Lg - 1) * Metal;
        const Sb = 1 + (Lb - 1) * Metal;
        for (let Li = 0; Li < 3; Li++) {
          const Light = SOFTWARE_LIGHTS[Li];
          const Lx = Light.dir[0];
          const Ly = Light.dir[1];
          const Lz = Light.dir[2];
          const NdotL = Wx * Lx + Wy * Ly + Wz * Lz;
          if (NdotL <= 0) continue;
          let Hx = Lx + Vx;
          let Hy = Ly + Vy;
          let Hz = Lz + Vz;
          const Hl = Math.hypot(Hx, Hy, Hz) || 1;
          Hx /= Hl;
          Hy /= Hl;
          Hz /= Hl;
          const NdotH = Wx * Hx + Wy * Hy + Wz * Hz;
          const VdotH = Vx * Hx + Vy * Hy + Vz * Hz;
          const ClampedH = NdotH > 0 ? NdotH : 0;
          const ClampedV = VdotH > 0 ? (VdotH > 1 ? 1 : VdotH) : 0;
          const Fres = 1 - ClampedV;
          const Fres2 = Fres * Fres;
          const Fres5 = Fres2 * Fres2 * Fres;
          const Fr = F0r + (1 - F0r) * Fres5;
          const Fg = F0g + (1 - F0g) * Fres5;
          const Fb = F0b + (1 - F0b) * Fres5;
          const Spec = Math.pow(ClampedH, Shininess) * SpecBoost;
          const Lc = Light.color;
          const K = NdotL * 0.55;
          Cr += ((Br / 3.14159) * (1 - Fr * 0.85) * NdotL + Fr * Sr * Spec * NdotL) * Lc[0] * 0.55;
          Cg += ((Bg / 3.14159) * (1 - Fg * 0.85) * NdotL + Fg * Sg * Spec * NdotL) * Lc[1] * 0.55;
          Cb += ((Bb / 3.14159) * (1 - Fb * 0.85) * NdotL + Fb * Sb * Spec * NdotL) * Lc[2] * 0.55;
          void K;
        }
        const SheenBase = Wy * 0.5 + 0.5;
        const Sheen = SheenBase > 0 ? SheenBase * SheenBase : 0;
        Cr += Br * Sheen * 0.12 * (1 - Metal);
        Cg += Bg * Sheen * 0.12 * (1 - Metal);
        Cb += Bb * Sheen * 0.12 * (1 - Metal);
        Cr += (Emi.d[Ti] / 255) * 1.6;
        Cg += (Emi.d[Ti + 1] / 255) * 1.6;
        Cb += (Emi.d[Ti + 2] / 255) * 1.6;
        const Dith = ((((Px * 197 + Py * 2029) & 1023) / 1023) - 0.5) * (2 / 255);
        Out[Index] = Math.round(Math.min(1, Math.max(0, Math.pow(acesFilm(Cr), 1 / 2.2) + Dith)) * 255);
        Out[Index + 1] = Math.round(Math.min(1, Math.max(0, Math.pow(acesFilm(Cg), 1 / 2.2) + Dith)) * 255);
        Out[Index + 2] = Math.round(Math.min(1, Math.max(0, Math.pow(acesFilm(Cb), 1 / 2.2) + Dith)) * 255);
        Out[Index + 3] = 255;
      }
    }
  }

  backgroundPixel(Out, Index, NdcX, NdcY, Px, Py) {
    // Output-referred, matching the GL clear color (which bypasses tonemap).
    let Bg = 0.075 + 0.02 * (0.5 - NdcY * 0.5);
    const Gdx = NdcX;
    const Gdy = NdcY - 0.5;
    Bg += 0.035 * Math.exp(-(Gdx * Gdx * 0.7 + Gdy * Gdy) * 2.2);
    const Sx = NdcX / 0.8;
    const Sy = (NdcY + 1.02) / 0.22;
    Bg *= 1 - 0.55 * Math.exp(-(Sx * Sx + Sy * Sy));
    Bg *= 1 - 0.12 * (NdcX * NdcX + NdcY * NdcY);
    if (Bg < 0) Bg = 0;
    if (Bg > 1) Bg = 1;
    const Dith = ((((Px * 197 + Py * 2029) & 1023) / 1023) - 0.5) * (2 / 255);
    const Gray = Math.round(Math.min(1, Math.max(0, Bg + Dith)) * 255);
    Out[Index] = Gray;
    Out[Index + 1] = Gray;
    Out[Index + 2] = Math.min(255, Math.round(Gray * 1.03));
    Out[Index + 3] = 255;
  }
}

/* ---------------- Facade: WebGL2 → WebGL1 → software ---------------- */

function tryContext(Canvas, Kind, Attributes) {
  try {
    return Canvas.getContext(Kind, Attributes) || null;
  } catch {
    return null;
  }
}

const MODE_LABELS = { webgl2: "WebGL2", webgl1: "WebGL1", software: "Software" };

export class MaterialPreview {
  constructor(Canvas) {
    this.canvas = Canvas;
    this.gl = null;
    this.glVersion = 0;
    this.program = null;
    this.uniforms = null;
    this.software = null;
    this.mode = "webgl2";
    this.ready = false;
    this.error = "";
    this.mesh = "sphere";
    this.yaw = 0.6;
    this.pitch = 0.32;
    this.distance = 3.9;
    this.turntable = true;
    this.dragging = false;
    this.normalStrength = 1;
    this.quality = "high";
    this.textures = {};
    this.textureStamp = -1;
    this.lastUploadMs = 0;
    this.meshes = {};
    this.onError = null;
    this.onFallback = null;
    Canvas.addEventListener("webglcontextlost", (Event) => {
      Event.preventDefault();
      if (this.mode === "software" || !this.ready) return;
      this.startSoftware("WebGL context was lost — continuing with the software preview.");
    });
  }

  get modeLabel() {
    return MODE_LABELS[this.mode] || this.mode;
  }

  setQuality(Quality) {
    this.quality = Quality === "low" ? "low" : "high";
    if (this.software) this.software.setQuality(this.quality);
    this.resize();
  }

  initialize() {
    this.destroy();
    this.error = "";
    let Gl =
      tryContext(this.canvas, "webgl2", {
        antialias: true, alpha: false, depth: true, stencil: false,
        failIfMajorPerformanceCaveat: false, powerPreference: "high-performance",
      }) || tryContext(this.canvas, "webgl2", {});
    let Version = Gl ? 2 : 0;
    if (!Gl) {
      Gl =
        tryContext(this.canvas, "webgl", {
          antialias: true, alpha: false, depth: true, stencil: false,
          failIfMajorPerformanceCaveat: false,
        }) ||
        tryContext(this.canvas, "webgl", {}) ||
        tryContext(this.canvas, "experimental-webgl", {});
      if (Gl) Version = 1;
    }
    if (Gl) {
      try {
        this.startGL(Gl, Version);
        return true;
      } catch (ErrorValue) {
        try {
          const Lose = Gl.getExtension("WEBGL_lose_context");
          if (Lose) Lose.loseContext();
        } catch {
          /* ignore */
        }
        this.gl = null;
      }
    }
    return this.startSoftware(
      Version === 0
        ? "WebGL is unavailable in this browser — using the software material preview."
        : "WebGL setup failed — using the software material preview.",
    );
  }

  startGL(Gl, Version) {
    this.teardownGL();
    this.software = null;
    this.gl = Gl;
    this.glVersion = Version;
    this.mode = Version === 2 ? "webgl2" : "webgl1";
    const IsGL2 = Version === 2;
    const Program = this.linkProgram(buildVertexSource(IsGL2), buildFragmentSource(IsGL2));
    this.program = Program;
    this.uniforms = {
      model: Gl.getUniformLocation(Program, "uModel"),
      viewProj: Gl.getUniformLocation(Program, "uViewProj"),
      camera: Gl.getUniformLocation(Program, "uCamera"),
      time: Gl.getUniformLocation(Program, "uTime"),
      normalStrength: Gl.getUniformLocation(Program, "uNormalStrength"),
      albedo: Gl.getUniformLocation(Program, "uAlbedo"),
      metallic: Gl.getUniformLocation(Program, "uMetallic"),
      roughness: Gl.getUniformLocation(Program, "uRoughness"),
      normal: Gl.getUniformLocation(Program, "uNormal"),
      emissive: Gl.getUniformLocation(Program, "uEmissive"),
    };
    // uTime is reserved for future animated effects; silence unused lookups.
    void this.uniforms.time;
    this.textures = {};
    for (const [Index, Name] of ["albedo", "metallic", "roughness", "normal", "emissive"].entries()) {
      const Texture = Gl.createTexture();
      Gl.activeTexture(Gl.TEXTURE0 + Index);
      Gl.bindTexture(Gl.TEXTURE_2D, Texture);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.REPEAT);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.REPEAT);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR_MIPMAP_LINEAR);
      Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
      Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, 1, 1, 0, Gl.RGBA, Gl.UNSIGNED_BYTE, new Uint8Array([128, 128, 128, 255]));
      this.textures[Name] = Texture;
    }
    this.meshes = {};
    for (const [Name, Builder] of Object.entries(BUILDERS)) {
      this.meshes[Name] = this.uploadMesh(Builder());
    }
    Gl.enable(Gl.DEPTH_TEST);
    Gl.enable(Gl.CULL_FACE);
    this.textureStamp = -1;
    this.ready = true;
    this.resize();
    if (this.mode === "webgl1" && this.onFallback) {
      this.onFallback(this.mode, "WebGL2 unavailable — using WebGL1 for the 3D preview.");
    }
  }

  startSoftware(Reason) {
    this.teardownGL();
    const Software = new SoftwarePreview(this.canvas);
    Software.setQuality(this.quality);
    if (!Software.initialize()) {
      this.ready = false;
      this.error = "Neither WebGL nor Canvas2D is available in this browser.";
      if (this.onError) this.onError(this.error);
      return false;
    }
    this.software = Software;
    this.mode = "software";
    this.ready = true;
    this.resize();
    if (this.onFallback) this.onFallback(this.mode, Reason);
    return true;
  }

  teardownGL() {
    if (this.gl) {
      try {
        const Lose = this.gl.getExtension("WEBGL_lose_context");
        if (Lose) Lose.loseContext();
      } catch {
        /* ignore */
      }
    }
    this.gl = null;
    this.glVersion = 0;
    this.program = null;
    this.uniforms = null;
    this.textures = {};
    this.meshes = {};
  }

  linkProgram(VertexSource, FragmentSource) {
    const Gl = this.gl;
    const Compile = (Type, Source) => {
      const Shader = Gl.createShader(Type);
      Gl.shaderSource(Shader, Source);
      Gl.compileShader(Shader);
      if (!Gl.getShaderParameter(Shader, Gl.COMPILE_STATUS)) {
        throw new Error(`Preview shader failed: ${Gl.getShaderInfoLog(Shader)}`);
      }
      return Shader;
    };
    const Program = Gl.createProgram();
    Gl.attachShader(Program, Compile(Gl.VERTEX_SHADER, VertexSource));
    Gl.attachShader(Program, Compile(Gl.FRAGMENT_SHADER, FragmentSource));
    Gl.bindAttribLocation(Program, 0, "aPosition");
    Gl.bindAttribLocation(Program, 1, "aNormal");
    Gl.bindAttribLocation(Program, 2, "aUv");
    Gl.linkProgram(Program);
    if (!Gl.getProgramParameter(Program, Gl.LINK_STATUS)) {
      throw new Error(`Preview link failed: ${Gl.getProgramInfoLog(Program)}`);
    }
    return Program;
  }

  uploadMesh(Data) {
    const Gl = this.gl;
    const IsGL2 = this.glVersion === 2;
    let Vao = null;
    if (IsGL2) {
      Vao = Gl.createVertexArray();
      Gl.bindVertexArray(Vao);
    }
    const Buffers = [];
    const Bind = (Location, Array, Size) => {
      const Buffer = Gl.createBuffer();
      Gl.bindBuffer(Gl.ARRAY_BUFFER, Buffer);
      Gl.bufferData(Gl.ARRAY_BUFFER, Array, Gl.STATIC_DRAW);
      Buffers.push({ buffer: Buffer, location: Location, size: Size });
      if (IsGL2) {
        Gl.enableVertexAttribArray(Location);
        Gl.vertexAttribPointer(Location, Size, Gl.FLOAT, false, 0, 0);
      }
    };
    Bind(0, Data.positions, 3);
    Bind(1, Data.normals, 3);
    Bind(2, Data.uvs, 2);
    const IndexBuffer = Gl.createBuffer();
    Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, IndexBuffer);
    Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, Data.indices, Gl.STATIC_DRAW);
    if (IsGL2) Gl.bindVertexArray(null);
    return { vao: Vao, buffers: Buffers, indices: IndexBuffer, count: Data.indices.length };
  }

  uploadTextures(Composite, Stamp) {
    if (!this.ready) return;
    if (this.mode === "software") {
      this.software.uploadTextures(Composite, Stamp);
      return;
    }
    if (Stamp === this.textureStamp) return;
    const Now = typeof performance !== "undefined" && performance.now ? performance.now() : 0;
    if (this.textureStamp >= 0 && Now - this.lastUploadMs < 120) return;
    this.lastUploadMs = Now;
    this.textureStamp = Stamp;
    const Gl = this.gl;
    const Jobs = [
      ["albedo", Composite.albedo],
      ["metallic", Composite.metallic],
      ["roughness", Composite.roughness],
      ["normal", Composite.normal],
      ["emissive", Composite.emissive],
    ];
    Jobs.forEach(([Name, Canvas], Index) => {
      Gl.activeTexture(Gl.TEXTURE0 + Index);
      Gl.bindTexture(Gl.TEXTURE_2D, this.textures[Name]);
      if (this.glVersion === 1) {
        // WebGL1: repeat wrap + mipmaps require power-of-two textures.
        const Pot = (Value) => Value > 0 && (Value & (Value - 1)) === 0;
        if (!Pot(Canvas.width) || !Pot(Canvas.height)) {
          Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
          Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
          Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
        }
      }
      try {
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, Gl.RGBA, Gl.UNSIGNED_BYTE, Canvas);
        Gl.generateMipmap(Gl.TEXTURE_2D);
      } catch {
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
        try {
          Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, Gl.RGBA, Gl.UNSIGNED_BYTE, Canvas);
        } catch {
          /* keep placeholder */
        }
      }
    });
  }

  /** Ray-cast a pointer to sphere UVs (buildSphere convention). Null on miss. */
  pickSphere(ClientX, ClientY) {
    const Rect = this.canvas.getBoundingClientRect();
    let NdcX = 0;
    let NdcY = 0;
    let Aspect = 1;
    if (this.mode === "software") {
      // Software upscales a centered square (cover-fit).
      const Size = Math.max(1, Math.max(Rect.width, Rect.height));
      const Left = Rect.left + (Rect.width - Size) / 2;
      const Top = Rect.top + (Rect.height - Size) / 2;
      NdcX = ((ClientX - Left) / Size) * 2 - 1;
      NdcY = 1 - ((ClientY - Top) / Size) * 2;
    } else {
      const W = Math.max(1, Rect.width);
      const H = Math.max(1, Rect.height);
      NdcX = ((ClientX - Rect.left) / W) * 2 - 1;
      NdcY = 1 - ((ClientY - Rect.top) / H) * 2;
      Aspect = this.canvas.width / Math.max(1, this.canvas.height);
    }
    // Camera basis mirrors render()/trace().
    const Cp = Math.cos(this.pitch);
    const Sp = Math.sin(this.pitch);
    const Cy = Math.cos(this.yaw);
    const Sy = Math.sin(this.yaw);
    const Ex = Cp * Sy * this.distance;
    const Ey = Sp * this.distance;
    const Ez = Cp * Cy * this.distance;
    const El = Math.hypot(Ex, Ey, Ez) || 1;
    const Fx = -Ex / El;
    const Fy = -Ey / El;
    const Fz = -Ez / El;
    let Rx = -Fz;
    let Ry = 0;
    let Rz = Fx;
    const Rl = Math.hypot(Rx, Ry, Rz) || 1;
    Rx /= Rl;
    Ry /= Rl;
    Rz /= Rl;
    const Ux = Ry * Fz - Rz * Fy;
    const Uy = Rz * Fx - Rx * Fz;
    const Uz = Rx * Fy - Ry * Fx;
    const TanH = Math.tan((32 * Math.PI) / 360);
    let Dx = Fx + NdcX * TanH * Aspect * Rx + NdcY * TanH * Ux;
    let Dy = Fy + NdcX * TanH * Aspect * Ry + NdcY * TanH * Uy;
    let Dz = Fz + NdcX * TanH * Aspect * Rz + NdcY * TanH * Uz;
    const Dl = Math.hypot(Dx, Dy, Dz) || 1;
    Dx /= Dl;
    Dy /= Dl;
    Dz /= Dl;
    const B = Ex * Dx + Ey * Dy + Ez * Dz;
    const Disc = B * B - (Ex * Ex + Ey * Ey + Ez * Ez - 1);
    if (Disc <= 0) return null;
    const T = -B - Math.sqrt(Disc);
    if (T <= 0) return null;
    const Nx = Ex + Dx * T;
    const Ny = Ey + Dy * T;
    const Nz = Ez + Dz * T;
    let U = Math.atan2(Nz, Ny) / (Math.PI * 2);
    U -= Math.floor(U);
    const ClampedX = Nx < -1 ? -1 : Nx > 1 ? 1 : Nx;
    return { u: U, v: 1 - Math.acos(ClampedX) / Math.PI };
  }

  resize() {
    if (this.mode === "software" && this.software) {
      this.software.resize(this.quality === "low" ? 1 : 2);
      return;
    }
    if (!this.gl) return;
    const Cap = this.quality === "low" ? 1 : 2;
    const Ratio = Math.min(window.devicePixelRatio || 1, Cap);
    const Rect = this.canvas.getBoundingClientRect();
    const Width = Math.max(2, Math.floor(Rect.width * Ratio));
    const Height = Math.max(2, Math.floor(Rect.height * Ratio));
    if (this.canvas.width !== Width || this.canvas.height !== Height) {
      this.canvas.width = Width;
      this.canvas.height = Height;
    }
    this.gl.viewport(0, 0, this.canvas.width, this.canvas.height);
  }

  render(TimeSeconds, DeltaSeconds) {
    if (!this.ready) return;
    if (this.canvas.hidden) return;
    if (typeof document !== "undefined" && document.hidden) return;
    if (this.turntable && !this.dragging) this.yaw += DeltaSeconds * 0.22;
    if (this.mode === "software") {
      const Software = this.software;
      const Moved =
        Software.yaw !== this.yaw ||
        Software.pitch !== this.pitch ||
        Software.distance !== this.distance ||
        Software.normalStrength !== this.normalStrength;
      Software.yaw = this.yaw;
      Software.pitch = this.pitch;
      Software.distance = this.distance;
      Software.turntable = false; // facade already advanced yaw
      Software.dragging = this.dragging;
      Software.normalStrength = this.normalStrength;
      const Turning = this.turntable && !this.dragging;
      Software.dynamic = Turning ? "turntable" : Moved ? "orbit" : "";
      if (Moved) Software.dirty = true;
      Software.render(TimeSeconds, DeltaSeconds);
      return;
    }
    const Gl = this.gl;
    const Eye = [
      Math.cos(this.pitch) * Math.sin(this.yaw) * this.distance,
      Math.sin(this.pitch) * this.distance,
      Math.cos(this.pitch) * Math.cos(this.yaw) * this.distance,
    ];
    const Aspect = this.canvas.width / Math.max(1, this.canvas.height);
    const View = lookAt(Eye, [0, 0, 0], [0, 1, 0]);
    const Projection = perspective((32 * Math.PI) / 180, Aspect, 0.1, 50);
    const ViewProj = multiplyMatrices(Projection, View);
    Gl.clearColor(0.075, 0.075, 0.08, 1);
    Gl.clear(Gl.COLOR_BUFFER_BIT | Gl.DEPTH_BUFFER_BIT);
    Gl.useProgram(this.program);
    Gl.uniformMatrix4fv(this.uniforms.model, false, rotationY(0));
    Gl.uniformMatrix4fv(this.uniforms.viewProj, false, ViewProj);
    Gl.uniform3fv(this.uniforms.camera, new Float32Array(Eye));
    if (this.uniforms.time) Gl.uniform1f(this.uniforms.time, TimeSeconds);
    Gl.uniform1f(this.uniforms.normalStrength, this.normalStrength);
    ["albedo", "metallic", "roughness", "normal", "emissive"].forEach((Name, Index) => {
      Gl.activeTexture(Gl.TEXTURE0 + Index);
      Gl.bindTexture(Gl.TEXTURE_2D, this.textures[Name]);
      Gl.uniform1i(this.uniforms[Name], Index);
    });
    const Mesh = this.meshes[this.mesh] || this.meshes.sphere;
    if (this.glVersion === 2) {
      Gl.bindVertexArray(Mesh.vao);
      Gl.drawElements(Gl.TRIANGLES, Mesh.count, Gl.UNSIGNED_SHORT, 0);
      Gl.bindVertexArray(null);
    } else {
      for (const Attribute of Mesh.buffers) {
        Gl.bindBuffer(Gl.ARRAY_BUFFER, Attribute.buffer);
        Gl.enableVertexAttribArray(Attribute.location);
        Gl.vertexAttribPointer(Attribute.location, Attribute.size, Gl.FLOAT, false, 0, 0);
      }
      Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, Mesh.indices);
      Gl.drawElements(Gl.TRIANGLES, Mesh.count, Gl.UNSIGNED_SHORT, 0);
      for (const Attribute of Mesh.buffers) {
        Gl.disableVertexAttribArray(Attribute.location);
      }
    }
  }

  destroy() {
    this.teardownGL();
    this.software = null;
    this.ready = false;
  }
}
