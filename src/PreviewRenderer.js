/* Frontier Texture Paint — WebGL2 PBR material preview.
   Shades the live composite (albedo / metallic / roughness / normal /
   emissive) on turntable primitives with a studio rig + ACES output. */

const VertexShader = `#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uViewProj;
out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
void main() {
  vec4 World = uModel * vec4(aPosition, 1.0);
  vWorldPos = World.xyz;
  vNormal = mat3(uModel) * aNormal;
  vUv = aUv;
  gl_Position = uViewProj * World;
}`;

const FragmentShader = `#version 300 es
precision highp float;
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUv;
uniform sampler2D uAlbedo;
uniform sampler2D uMetallic;
uniform sampler2D uRoughness;
uniform sampler2D uNormal;
uniform sampler2D uEmissive;
uniform vec3 uCamera;
uniform float uTime;
uniform float uNormalStrength;
out vec4 oColor;

vec3 sampleNormal(vec2 uv, vec3 normal, vec3 worldPos) {
  vec3 tangentNormal = texture(uNormal, uv).xyz * 2.0 - 1.0;
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
}

vec3 aces(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main() {
  vec3 albedo = pow(texture(uAlbedo, vUv).rgb, vec3(2.2));
  float metallic = texture(uMetallic, vUv).r;
  float roughness = clamp(texture(uRoughness, vUv).r, 0.04, 1.0);
  vec3 emissive = texture(uEmissive, vUv).rgb * 1.6;
  vec3 N = sampleNormal(vUv, vNormal, vWorldPos);
  vec3 V = normalize(uCamera - vWorldPos);

  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  vec3 base = albedo * (1.0 - metallic);

  // Studio rig: key + rim + fill.
  vec3 lightDirs[3];
  lightDirs[0] = normalize(vec3(0.55, 0.75, 0.6));
  lightDirs[1] = normalize(vec3(-0.7, 0.25, -0.55));
  lightDirs[2] = normalize(vec3(-0.15, -0.6, 0.75));
  vec3 lightColors[3];
  lightColors[0] = vec3(3.2, 3.05, 2.85);
  lightColors[1] = vec3(0.85, 1.0, 1.35);
  lightColors[2] = vec3(0.55, 0.5, 0.5);

  vec3 color = base * vec3(0.10, 0.105, 0.115); // hemisphere floor
  float shininess = mix(220.0, 14.0, roughness);
  for (int i = 0; i < 3; i++) {
    vec3 L = lightDirs[i];
    float NdotL = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    float spec = pow(NdotH, shininess) * (shininess * 0.25 + 0.5);
    vec3 specColor = mix(vec3(1.0), albedo, metallic);
    color += (base / 3.14159 * (1.0 - F * 0.85) * NdotL + F * specColor * spec * NdotL) * lightColors[i] * 0.55;
  }
  // Soft top sheen for dielectrics.
  color += base * pow(max(N.y * 0.5 + 0.5, 0.0), 2.0) * 0.12 * (1.0 - metallic);
  color += emissive;
  color = aces(color * 1.05);
  color = pow(color, vec3(1.0 / 2.2));
  // Vignette + dither.
  vec2 centered = vUv - 0.5;
  void centered;
  float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) / 255.0;
  oColor = vec4(color + dither, 1.0);
}`;

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
  // 24 verts, per-face normals, 0..1 UVs.
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
  // Top cap fan.
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

export class MaterialPreview {
  constructor(Canvas) {
    this.canvas = Canvas;
    this.gl = null;
    this.ready = false;
    this.error = "";
    this.mesh = "sphere";
    this.yaw = 0.6;
    this.pitch = 0.32;
    this.distance = 3.1;
    this.turntable = true;
    this.dragging = false;
    this.normalStrength = 1;
    this.textures = {};
    this.textureStamp = 0;
    this.meshes = {};
    this.onError = null;
  }

  initialize() {
    try {
      const Gl = this.canvas.getContext("webgl2", { antialias: true, alpha: false });
      if (!Gl) throw new Error("WebGL2 is not available in this browser.");
      this.gl = Gl;
      const Program = this.linkProgram(VertexShader, FragmentShader);
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
      for (const [Index, Name] of ["albedo", "metallic", "roughness", "normal", "emissive"].entries()) {
        const Texture = Gl.createTexture();
        Gl.activeTexture(Gl.TEXTURE0 + Index);
        Gl.bindTexture(Gl.TEXTURE_2D, Texture);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.REPEAT);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.REPEAT);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR_MIPMAP_LINEAR);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.LINEAR);
        // 1px placeholder until the composite uploads.
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, 1, 1, 0, Gl.RGBA, Gl.UNSIGNED_BYTE, new Uint8Array([128, 128, 128, 255]));
        this.textures[Name] = Texture;
      }
      for (const [Name, Builder] of Object.entries(BUILDERS)) {
        this.meshes[Name] = this.uploadMesh(Builder());
      }
      Gl.enable(Gl.DEPTH_TEST);
      Gl.enable(Gl.CULL_FACE);
      this.ready = true;
      this.canvas.addEventListener("webglcontextlost", (Event) => {
        Event.preventDefault();
        this.ready = false;
        this.error = "WebGL context was lost.";
        if (this.onError) this.onError(this.error);
      });
    } catch (ErrorValue) {
      this.ready = false;
      this.error = ErrorValue.message || String(ErrorValue);
      if (this.onError) this.onError(this.error);
    }
    return this.ready;
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
    const Vao = Gl.createVertexArray();
    Gl.bindVertexArray(Vao);
    const Bind = (Location, Array, Size) => {
      const Buffer = Gl.createBuffer();
      Gl.bindBuffer(Gl.ARRAY_BUFFER, Buffer);
      Gl.bufferData(Gl.ARRAY_BUFFER, Array, Gl.STATIC_DRAW);
      Gl.enableVertexAttribArray(Location);
      Gl.vertexAttribPointer(Location, Size, Gl.FLOAT, false, 0, 0);
    };
    Bind(0, Data.positions, 3);
    Bind(1, Data.normals, 3);
    Bind(2, Data.uvs, 2);
    const IndexBuffer = Gl.createBuffer();
    Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, IndexBuffer);
    Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, Data.indices, Gl.STATIC_DRAW);
    Gl.bindVertexArray(null);
    return { vao: Vao, count: Data.indices.length };
  }

  uploadTextures(Composite, Stamp) {
    if (!this.ready || Stamp === this.textureStamp) return;
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
      Gl.pixelStorei(Gl.UNPACK_FLIP_Y_WEBGL, false);
      try {
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, Gl.RGBA, Gl.UNSIGNED_BYTE, Canvas);
        Gl.generateMipmap(Gl.TEXTURE_2D);
      } catch {
        // NPOT fallback — plain linear, no mipmaps.
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.LINEAR);
        try {
          Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA, Gl.RGBA, Gl.UNSIGNED_BYTE, Canvas);
        } catch {
          /* keep placeholder */
        }
      }
    });
  }

  resize() {
    if (!this.ready) return;
    const Ratio = Math.min(window.devicePixelRatio || 1, 2);
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
    if (this.turntable && !this.dragging) this.yaw += DeltaSeconds * 0.22;
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
    Gl.uniform1f(this.uniforms.time, TimeSeconds);
    Gl.uniform1f(this.uniforms.normalStrength, this.normalStrength);
    ["albedo", "metallic", "roughness", "normal", "emissive"].forEach((Name, Index) => {
      Gl.activeTexture(Gl.TEXTURE0 + Index);
      Gl.bindTexture(Gl.TEXTURE_2D, this.textures[Name]);
      Gl.uniform1i(this.uniforms[Name], Index);
    });
    const Mesh = this.meshes[this.mesh] || this.meshes.sphere;
    Gl.bindVertexArray(Mesh.vao);
    Gl.drawElements(Gl.TRIANGLES, Mesh.count, Gl.UNSIGNED_SHORT, 0);
    Gl.bindVertexArray(null);
  }

  destroy() {
    if (!this.gl) return;
    const Extension = this.gl.getExtension("WEBGL_lose_context");
    if (Extension) Extension.loseContext();
    this.gl = null;
    this.ready = false;
  }
}
