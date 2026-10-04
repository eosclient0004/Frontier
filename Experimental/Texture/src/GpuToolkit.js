// Thin WebGL2 helpers: programs with cached uniform setters, textures,
// framebuffers and a fullscreen triangle pair.

// Programs link asynchronously when KHR_parallel_shader_compile is present:
// the driver compiles on worker threads, Ready() polls without blocking and
// the first Use() waits only if the program is still compiling.
export class Program {
  constructor(GL, VertexSource, FragmentSource, Name = "program") {
    this.GL = GL;
    this.Name = Name;
    this.Sources = [VertexSource, FragmentSource];
    this.Parallel = GL.getExtension("KHR_parallel_shader_compile");
    const Shaders = [[GL.VERTEX_SHADER, VertexSource], [GL.FRAGMENT_SHADER, FragmentSource]].map(([Type, Source]) => {
      const Shader = GL.createShader(Type);
      GL.shaderSource(Shader, Source);
      GL.compileShader(Shader);
      return Shader;
    });
    this.Shaders = Shaders;
    this.Handle = GL.createProgram();
    Shaders.forEach((Shader) => GL.attachShader(this.Handle, Shader));
    GL.linkProgram(this.Handle);
    this.Linked = false;
    this.Locations = new Map();
    this.Units = new Map();
    this.NextUnit = 0;
  }
  Ready() {
    if (this.Linked) return true;
    if (this.Parallel && !this.GL.getProgramParameter(this.Handle, this.Parallel.COMPLETION_STATUS_KHR)) return false;
    this.Finish();
    return true;
  }
  Finish() {
    if (this.Linked) return;
    const GL = this.GL;
    if (!GL.getProgramParameter(this.Handle, GL.LINK_STATUS)) {
      const Logs = this.Shaders.map((Shader, Index) => {
        if (GL.getShaderParameter(Shader, GL.COMPILE_STATUS)) return "";
        const Lines = this.Sources[Index].split("\n").map((Line, Number) => `${Number + 1}: ${Line}`);
        return `${GL.getShaderInfoLog(Shader)}\n${Lines.join("\n")}`;
      }).filter(Boolean);
      const Log = GL.getProgramInfoLog(this.Handle);
      console.error(`${this.Name} shader error\n${Log}\n${Logs.join("\n")}`);
      throw new Error(`${this.Name}: ${Log || Logs[0]?.split("\n")[0] || "link failed"}`);
    }
    this.Shaders.forEach((Shader) => { GL.detachShader(this.Handle, Shader); GL.deleteShader(Shader); });
    this.Shaders = [];
    this.Sources = null;
    this.Linked = true;
  }
  Dispose() {
    this.GL.deleteProgram(this.Handle);
  }
  Use() {
    if (!this.Linked) this.Finish();
    this.GL.useProgram(this.Handle);
    this.NextUnit = 0;
    return this;
  }
  Location(Name) {
    if (!this.Locations.has(Name))
      this.Locations.set(Name, this.GL.getUniformLocation(this.Handle, Name));
    return this.Locations.get(Name);
  }
  Int(Name, Value) { this.GL.uniform1i(this.Location(Name), Value); return this; }
  Float(Name, Value) { this.GL.uniform1f(this.Location(Name), Value); return this; }
  Vec2(Name, A, B) { this.GL.uniform2f(this.Location(Name), A, B); return this; }
  Vec3(Name, V) { this.GL.uniform3f(this.Location(Name), V[0], V[1], V[2]); return this; }
  Vec4(Name, V) { this.GL.uniform4f(this.Location(Name), V[0], V[1], V[2], V[3]); return this; }
  Vec4Array(Name, Values) { this.GL.uniform4fv(this.Location(Name), Values); return this; }
  Vec3Array(Name, Values) { this.GL.uniform3fv(this.Location(Name), Values); return this; }
  Vec2Array(Name, Values) { this.GL.uniform2fv(this.Location(Name), Values); return this; }
  Mat2(Name, M) { this.GL.uniformMatrix2fv(this.Location(Name), false, M); return this; }
  Mat4(Name, M) { this.GL.uniformMatrix4fv(this.Location(Name), false, M); return this; }
  Texture(Name, Texture) {
    const Location = this.Location(Name);
    if (Location === null) return this;
    const Unit = this.NextUnit++;
    this.GL.activeTexture(this.GL.TEXTURE0 + Unit);
    this.GL.bindTexture(this.GL.TEXTURE_2D, Texture);
    this.GL.uniform1i(Location, Unit);
    return this;
  }
}

export const FORMATS = {
  RGBA8: { Internal: "RGBA8", Format: "RGBA", Type: "UNSIGNED_BYTE", Bytes: 4 },
  RGBA16F: { Internal: "RGBA16F", Format: "RGBA", Type: "HALF_FLOAT", Bytes: 8 },
  RGBA32F: { Internal: "RGBA32F", Format: "RGBA", Type: "FLOAT", Bytes: 16 },
  R16F: { Internal: "R16F", Format: "RED", Type: "HALF_FLOAT", Bytes: 2 },
};

export function CreateTexture(GL, Width, Height, FormatName, Options = {}) {
  const Format = FORMATS[FormatName];
  const Texture = GL.createTexture();
  GL.bindTexture(GL.TEXTURE_2D, Texture);
  const Levels = Options.Mipmaps ? Math.floor(Math.log2(Math.max(Width, Height))) + 1 : 1;
  GL.texStorage2D(GL.TEXTURE_2D, Levels, GL[Format.Internal], Width, Height);
  const Filter = Options.Linear ? GL.LINEAR : GL.NEAREST;
  GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER, Options.Mipmaps ? GL.LINEAR_MIPMAP_LINEAR : Filter);
  GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MAG_FILTER, Filter);
  const Wrap = Options.Repeat ? GL.REPEAT : GL.CLAMP_TO_EDGE;
  GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_S, Wrap);
  GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_T, Wrap);
  Texture.Width = Width;
  Texture.Height = Height;
  Texture.FormatName = FormatName;
  Texture.Bytes = Width * Height * Format.Bytes * (Options.Mipmaps ? 1.34 : 1);
  return Texture;
}

export function CreateFramebuffer(GL, Textures, Depth = null) {
  const Framebuffer = GL.createFramebuffer();
  GL.bindFramebuffer(GL.FRAMEBUFFER, Framebuffer);
  Textures.forEach((Texture, Index) => {
    if (Texture) GL.framebufferTexture2D(GL.FRAMEBUFFER, GL.COLOR_ATTACHMENT0 + Index, GL.TEXTURE_2D, Texture, 0);
  });
  if (Depth) GL.framebufferRenderbuffer(GL.FRAMEBUFFER, GL.DEPTH_ATTACHMENT, GL.RENDERBUFFER, Depth);
  GL.drawBuffers(Textures.map((Texture, Index) => (Texture ? GL.COLOR_ATTACHMENT0 + Index : GL.NONE)));
  const Status = GL.checkFramebufferStatus(GL.FRAMEBUFFER);
  if (Status !== GL.FRAMEBUFFER_COMPLETE) {
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
    GL.deleteFramebuffer(Framebuffer);
    throw new Error(`Framebuffer incomplete (0x${Status.toString(16)})`);
  }
  return Framebuffer;
}

// Draws into a temporary framebuffer built from the target textures, then
// releases it. Framebuffer objects are cheap compared with the passes.
export class TargetCache {
  constructor(GL) {
    this.GL = GL;
    this.Cache = new Map();
  }
  Bind(Textures, Depth = null) {
    const Key = Textures.map((Texture) => (Texture ? Texture.Id ?? (Texture.Id = TargetCache.Counter++) : "_")).join(",") + (Depth ? `|d${Depth.Id ?? (Depth.Id = TargetCache.Counter++)}` : "");
    let Framebuffer = this.Cache.get(Key);
    if (!Framebuffer) {
      Framebuffer = CreateFramebuffer(this.GL, Textures, Depth);
      Framebuffer.Textures = Textures;
      this.Cache.set(Key, Framebuffer);
      if (this.Cache.size > 160) this.Prune();
    }
    this.GL.bindFramebuffer(this.GL.FRAMEBUFFER, Framebuffer);
    const Size = Textures.find(Boolean);
    this.GL.viewport(0, 0, Size.Width, Size.Height);
    return Framebuffer;
  }
  Forget(Texture) {
    for (const [Key, Framebuffer] of this.Cache)
      if (Framebuffer.Textures.includes(Texture)) {
        this.GL.deleteFramebuffer(Framebuffer);
        this.Cache.delete(Key);
      }
  }
  Prune() {
    let Count = 0;
    for (const [Key, Framebuffer] of this.Cache) {
      if (Count++ > 60) break;
      this.GL.deleteFramebuffer(Framebuffer);
      this.Cache.delete(Key);
    }
  }
}
TargetCache.Counter = 1;

export function CreateQuad(GL) {
  const Vao = GL.createVertexArray();
  GL.bindVertexArray(Vao);
  const Buffer = GL.createBuffer();
  GL.bindBuffer(GL.ARRAY_BUFFER, Buffer);
  GL.bufferData(GL.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1]), GL.STATIC_DRAW);
  GL.enableVertexAttribArray(0);
  GL.vertexAttribPointer(0, 2, GL.FLOAT, false, 0, 0);
  GL.bindVertexArray(null);
  return Vao;
}

export function CreateMeshBuffers(GL, Mesh) {
  const Vao = GL.createVertexArray();
  GL.bindVertexArray(Vao);
  const Buffers = [];
  const Attribute = (Location, Data, Size) => {
    const Buffer = GL.createBuffer();
    GL.bindBuffer(GL.ARRAY_BUFFER, Buffer);
    GL.bufferData(GL.ARRAY_BUFFER, Data, GL.STATIC_DRAW);
    GL.enableVertexAttribArray(Location);
    GL.vertexAttribPointer(Location, Size, GL.FLOAT, false, 0, 0);
    Buffers.push(Buffer);
  };
  Attribute(0, Mesh.Positions, 3);
  Attribute(1, Mesh.Normals, 3);
  Attribute(2, Mesh.Uvs, 2);
  Attribute(3, Mesh.Tangents, 4);
  const Index = GL.createBuffer();
  GL.bindBuffer(GL.ELEMENT_ARRAY_BUFFER, Index);
  GL.bufferData(GL.ELEMENT_ARRAY_BUFFER, Mesh.Indices, GL.STATIC_DRAW);
  Buffers.push(Index);
  GL.bindVertexArray(null);

  // Edge list for wireframe and conservative UV rasterization.
  const Edges = new Uint32Array(Mesh.Indices.length * 2);
  for (let K = 0, E = 0; K < Mesh.Indices.length; K += 3) {
    const A = Mesh.Indices[K], B = Mesh.Indices[K + 1], C = Mesh.Indices[K + 2];
    Edges[E++] = A; Edges[E++] = B; Edges[E++] = B; Edges[E++] = C; Edges[E++] = C; Edges[E++] = A;
  }
  const EdgeVao = GL.createVertexArray();
  GL.bindVertexArray(EdgeVao);
  GL.bindBuffer(GL.ARRAY_BUFFER, Buffers[0]);
  GL.enableVertexAttribArray(0);
  GL.vertexAttribPointer(0, 3, GL.FLOAT, false, 0, 0);
  GL.bindBuffer(GL.ARRAY_BUFFER, Buffers[1]);
  GL.enableVertexAttribArray(1);
  GL.vertexAttribPointer(1, 3, GL.FLOAT, false, 0, 0);
  GL.bindBuffer(GL.ARRAY_BUFFER, Buffers[2]);
  GL.enableVertexAttribArray(2);
  GL.vertexAttribPointer(2, 2, GL.FLOAT, false, 0, 0);
  const EdgeIndex = GL.createBuffer();
  GL.bindBuffer(GL.ELEMENT_ARRAY_BUFFER, EdgeIndex);
  GL.bufferData(GL.ELEMENT_ARRAY_BUFFER, Edges, GL.STATIC_DRAW);
  Buffers.push(EdgeIndex);
  GL.bindVertexArray(null);

  const UvVao = GL.createVertexArray();
  GL.bindVertexArray(UvVao);
  const UvBuffer = GL.createBuffer();
  GL.bindBuffer(GL.ARRAY_BUFFER, UvBuffer);
  GL.bufferData(GL.ARRAY_BUFFER, Mesh.UvLines, GL.STATIC_DRAW);
  GL.enableVertexAttribArray(0);
  GL.vertexAttribPointer(0, 2, GL.FLOAT, false, 0, 0);
  Buffers.push(UvBuffer);
  GL.bindVertexArray(null);

  return {
    Vao,
    EdgeVao,
    UvVao,
    Count: Mesh.Indices.length,
    EdgeCount: Edges.length,
    UvLineCount: Mesh.UvLines.length / 2,
    Dispose() {
      Buffers.forEach((Buffer) => GL.deleteBuffer(Buffer));
      GL.deleteVertexArray(Vao);
      GL.deleteVertexArray(EdgeVao);
      GL.deleteVertexArray(UvVao);
    },
  };
}
