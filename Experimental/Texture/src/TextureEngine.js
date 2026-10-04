import {
  Program,
  CreateTexture,
  CreateQuad,
  CreateMeshBuffers,
  TargetCache,
} from "./GpuToolkit.js";
import * as Shaders from "./ShaderLibrary.js";
import { Mat4, HexToRgb, DecalFrame } from "./MathLibrary.js";
import {
  PatternIndex,
  MaskGeneratorIndex,
  BlendIndex,
  BLEND_MODES,
  HEIGHT_BLENDS,
} from "./MaterialSpecification.js";

export const ENVIRONMENTS = {
  studio: {
    label: "Studio softbox",
    Sky: [0.16, 0.17, 0.19], Horizon: [0.09, 0.09, 0.1], Ground: [0.025, 0.025, 0.028],
    Lights: [
      { Dir: [0.35, 0.75, 0.55], Size: 0.45, Color: [9, 8.7, 8.2] },
      { Dir: [-0.85, 0.25, 0.3], Size: 0.3, Color: [2.2, 2.3, 2.6] },
      { Dir: [0.1, 0.3, -0.95], Size: 0.25, Color: [3.2, 3.2, 3.4] },
      { Dir: [0, -1, 0], Size: 0.01, Color: [0, 0, 0] },
    ],
  },
  warm: {
    label: "Golden hour",
    Sky: [0.22, 0.27, 0.38], Horizon: [0.42, 0.28, 0.17], Ground: [0.05, 0.035, 0.025],
    Lights: [
      { Dir: [0.75, 0.2, 0.55], Size: 0.12, Color: [40, 24, 11] },
      { Dir: [-0.6, 0.6, -0.4], Size: 0.6, Color: [1.2, 1.5, 2.2] },
      { Dir: [0, -1, 0], Size: 0.01, Color: [0, 0, 0] },
      { Dir: [0, -1, 0], Size: 0.01, Color: [0, 0, 0] },
    ],
  },
  overcast: {
    label: "Overcast",
    Sky: [0.62, 0.66, 0.72], Horizon: [0.42, 0.44, 0.46], Ground: [0.08, 0.08, 0.075],
    Lights: [
      { Dir: [0.2, 0.95, 0.2], Size: 0.9, Color: [1.6, 1.65, 1.75] },
      { Dir: [0, -1, 0], Size: 0.01, Color: [0, 0, 0] },
      { Dir: [0, -1, 0], Size: 0.01, Color: [0, 0, 0] },
      { Dir: [0, -1, 0], Size: 0.01, Color: [0, 0, 0] },
    ],
  },
  neon: {
    label: "Neon night",
    Sky: [0.02, 0.02, 0.04], Horizon: [0.03, 0.02, 0.05], Ground: [0.01, 0.01, 0.012],
    Lights: [
      { Dir: [0.7, 0.4, 0.55], Size: 0.35, Color: [2, 6, 12] },
      { Dir: [-0.8, 0.3, 0.2], Size: 0.3, Color: [12, 2, 7] },
      { Dir: [0, 0.9, -0.4], Size: 0.25, Color: [3, 3, 3.5] },
      { Dir: [0, -1, 0], Size: 0.01, Color: [0, 0, 0] },
    ],
  },
};

export const VIEW_CHANNELS = [
  { id: "lit", label: "Lit material", index: 0 },
  { id: "color", label: "Base color", index: 1 },
  { id: "roughness", label: "Roughness", index: 2 },
  { id: "metallic", label: "Metallic", index: 3 },
  { id: "height", label: "Height", index: 4 },
  { id: "normal", label: "Normal", index: 5 },
  { id: "emissive", label: "Emissive", index: 6 },
  { id: "mask", label: "Layer mask", index: 7 },
  { id: "checker", label: "UV checker", index: 8 },
];

const Normalize = (V) => {
  const L = Math.hypot(V[0], V[1], V[2]) || 1;
  return [V[0] / L, V[1] / L, V[2] / L];
};

// A GPU failure the UI can explain: Code is "webgl2" (no context), "float"
// (missing extension) or "lost" (context lost after start-up).
export function GpuError(Code, Message, Details = {}) {
  const ErrorValue = new Error(Message);
  ErrorValue.Code = Code;
  ErrorValue.Details = Details;
  return ErrorValue;
}

// Probes what the browser offers so a failure can name its cause.
export function ProbeGraphics(StatusMessages = []) {
  const Probe = (Type) => {
    try {
      const Context = document.createElement("canvas").getContext(Type);
      if (!Context) return null;
      const Info = Context.getExtension("WEBGL_debug_renderer_info");
      const Renderer = Info ? Context.getParameter(Info.UNMASKED_RENDERER_WEBGL) : Context.getParameter(Context.RENDERER);
      Context.getExtension("WEBGL_lose_context")?.loseContext();
      return String(Renderer || "unknown renderer");
    } catch {
      return null;
    }
  };
  return {
    WebGL2: Probe("webgl2"),
    WebGL1: Probe("webgl") || Probe("experimental-webgl"),
    WebGPU: typeof navigator !== "undefined" && "gpu" in navigator,
    StatusMessages: [...new Set(StatusMessages.filter(Boolean))],
    UserAgent: typeof navigator !== "undefined" ? navigator.userAgent : "",
  };
}

export class TextureEngine {
  // Tries progressively less demanding context attributes; some drivers refuse
  // antialiasing or a high-performance adapter but accept a plain context.
  static CreateContext(Canvas) {
    const Messages = [];
    const OnError = (Event) => Messages.push(Event.statusMessage);
    Canvas.addEventListener("webglcontextcreationerror", OnError);
    const Base = { alpha: false, premultipliedAlpha: false, preserveDrawingBuffer: false };
    const Attempts = [
      { ...Base, antialias: true, powerPreference: "high-performance" },
      { ...Base, antialias: false, powerPreference: "default" },
      { ...Base, antialias: false, depth: true, stencil: false, failIfMajorPerformanceCaveat: false, powerPreference: "low-power" },
    ];
    let GL = null;
    for (const Attributes of Attempts) {
      try {
        GL = Canvas.getContext("webgl2", Attributes);
      } catch (ErrorValue) {
        Messages.push(ErrorValue.message);
      }
      if (GL && !GL.isContextLost()) break;
      GL = null;
    }
    Canvas.removeEventListener("webglcontextcreationerror", OnError);
    if (!GL) throw GpuError("webgl2", "The browser refused to create a WebGL2 context.", ProbeGraphics(Messages));
    return GL;
  }

  constructor(Canvas) {
    this.Canvas = Canvas;
    const GL = TextureEngine.CreateContext(Canvas);
    if (!GL.getExtension("EXT_color_buffer_float"))
      throw GpuError("float", "This GPU can run WebGL2 but cannot render to floating-point textures (EXT_color_buffer_float), which the layer compositor needs.");
    GL.getExtension("OES_texture_float_linear");
    this.GL = GL;
    this.MaxTexture = GL.getParameter(GL.MAX_TEXTURE_SIZE);
    this.Targets = new TargetCache(GL);
    this.Quad = CreateQuad(GL);
    this.Allocated = new Set();
    const P = (Vertex, Fragment, Name) => new Program(GL, Vertex, Fragment, Name);
    const F = Shaders.FullscreenVertex;
    this.Programs = {
      Maps: P(Shaders.MapsVertex, Shaders.MapsFragment, "maps"),
      Curvature: P(F, Shaders.CurvatureFragment, "curvature"),
      Pick: P(Shaders.PickVertex, Shaders.PickFragment, "pick"),
      Stroke: P(F, Shaders.StrokeFragment, "stroke"),
      Commit: P(F, Shaders.CommitFragment, "commit"),
      MaskCommit: P(F, Shaders.MaskCommitFragment, "mask-commit"),
      Composite: P(F, Shaders.CompositeFragment, "composite"),
      Normal: P(F, Shaders.NormalFragment, "normal"),
      Dilate: P(F, Shaders.DilateFragment, "dilate"),
      Viewport: P(Shaders.ViewportVertex, Shaders.ViewportFragment, "viewport"),
      Line: P(Shaders.LineVertex, Shaders.LineFragment, "line"),
      Background: P(F, Shaders.BackgroundFragment, "background"),
      UvView: P(Shaders.UvViewVertex, Shaders.UvViewFragment, "uv-view"),
      UvLine: P(Shaders.UvLineVertex, Shaders.LineFragment, "uv-line"),
      Preview: P(F, Shaders.PreviewFragment, "preview"),
      Decode: P(F, Shaders.DecodeFragment, "decode"),
      Blit: P(F, Shaders.BlitFragment, "blit"),
    };
    this.White = this.SolidTexture([255, 255, 255, 255]);
    this.Black = this.SolidTexture([0, 0, 0, 0]);
    this.Bitmaps = new Map();
    this.Decals = new Map();
    this.Pick = null;
    this.Lost = false;
    Canvas.addEventListener("webglcontextlost", (Event) => {
      Event.preventDefault();
      this.Lost = true;
      this.OnContextLost?.();
    });
  }

  // ---------------------------------------------------------------- textures
  SolidTexture(Rgba) {
    const GL = this.GL;
    const Texture = GL.createTexture();
    GL.bindTexture(GL.TEXTURE_2D, Texture);
    GL.texImage2D(GL.TEXTURE_2D, 0, GL.RGBA8, 1, 1, 0, GL.RGBA, GL.UNSIGNED_BYTE, new Uint8Array(Rgba));
    GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER, GL.NEAREST);
    GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MAG_FILTER, GL.NEAREST);
    Texture.Width = Texture.Height = 1;
    return Texture;
  }
  Allocate(Width, Height, Format, Options = {}) {
    const Texture = CreateTexture(this.GL, Width, Height, Format, Options);
    Texture.Tag = Options.Tag || "system";
    this.Allocated.add(Texture);
    return Texture;
  }
  Release(Texture) {
    if (!Texture || !this.Allocated.has(Texture)) return;
    this.Targets.Forget(Texture);
    this.GL.deleteTexture(Texture);
    this.Allocated.delete(Texture);
  }
  Clear(Textures, Values) {
    const GL = this.GL;
    this.Targets.Bind(Textures);
    Textures.forEach((Texture, Index) => {
      if (Texture) GL.clearBufferfv(GL.COLOR, Index, Values[Index] || Values[0]);
    });
  }
  MemoryUsage(Tag) {
    let Bytes = 0;
    for (const Texture of this.Allocated) if (!Tag || Texture.Tag === Tag) Bytes += Texture.Bytes;
    return Bytes;
  }
  // Frees layer-owned textures that are referenced neither by documents nor
  // by any undo history entry.
  CollectGarbage(Live) {
    for (const Texture of [...this.Allocated])
      if (Texture.Tag === "layer" && !Live.has(Texture)) this.Release(Texture);
  }

  CreatePaintTextures(Resolution) {
    const Textures = [
      this.Allocate(Resolution, Resolution, "RGBA8", { Tag: "layer" }),
      this.Allocate(Resolution, Resolution, "RGBA16F", { Tag: "layer" }),
      this.Allocate(Resolution, Resolution, "RGBA8", { Tag: "layer" }),
      this.Allocate(Resolution, Resolution, "RGBA8", { Tag: "layer" }),
    ];
    this.Clear(Textures, [[0, 0, 0, 0]]);
    return Textures;
  }
  CreateMaskTexture(Resolution, Value = 1) {
    const Texture = this.Allocate(Resolution, Resolution, "RGBA8", { Tag: "layer" });
    this.Clear([Texture], [[Value, Value, Value, 1]]);
    return Texture;
  }
  CopyTexture(Source) {
    const Copy = this.Allocate(Source.Width, Source.Height, Source.FormatName, { Tag: "layer" });
    this.Blit(Source, Copy);
    return Copy;
  }
  Blit(Source, Target, Linear = false) {
    const GL = this.GL;
    // Bind() attaches to GL.FRAMEBUFFER (read + draw), so resolve both first.
    const Read = this.Targets.Bind([Source]);
    const Draw = this.Targets.Bind([Target]);
    GL.bindFramebuffer(GL.READ_FRAMEBUFFER, Read);
    GL.bindFramebuffer(GL.DRAW_FRAMEBUFFER, Draw);
    GL.blitFramebuffer(0, 0, Source.Width, Source.Height, 0, 0, Target.Width, Target.Height, GL.COLOR_BUFFER_BIT, Linear ? GL.LINEAR : GL.NEAREST);
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
  }
  Resample(Source, Resolution) {
    const Target = this.Allocate(Resolution, Resolution, Source.FormatName, { Tag: "layer" });
    this.Blit(Source, Target, true);
    return Target;
  }

  // ------------------------------------------------------------- documents
  CreateDocument(Doc, Mesh) {
    const R = Doc.Resolution;
    const A = (Format, Options) => this.Allocate(R, R, Format, Options);
    Doc.Gpu = {
      Resolution: R,
      Mesh: null,
      Buffers: null,
      Position: null,
      Normal: null,
      Accum: [
        [A("RGBA16F"), A("RGBA16F"), A("RGBA16F")],
        [A("RGBA16F"), A("RGBA16F"), A("RGBA16F")],
      ],
      Cache: [A("RGBA16F"), A("RGBA16F"), A("RGBA16F")],
      CacheSignature: null,
      NormalOut: A("RGBA16F"),
      Final: [
        A("RGBA16F", { Linear: true, Mipmaps: true }),
        A("RGBA16F", { Linear: true, Mipmaps: true }),
        A("RGBA16F", { Linear: true, Mipmaps: true }),
        A("RGBA16F", { Linear: true, Mipmaps: true }),
      ],
      MaskView: A("RGBA16F", { Linear: true }),
      Stroke: A("R16F"),
      Dirty: true,
    };
    this.SetMesh(Doc, Mesh);
  }
  DisposeDocument(Doc) {
    const G = Doc.Gpu;
    if (!G) return;
    [G.Position, G.Normal, ...G.Accum[0], ...G.Accum[1], ...G.Cache, G.NormalOut, ...G.Final, G.MaskView, G.Stroke].forEach((T) => this.Release(T));
    G.Buffers?.Dispose();
    Doc.Gpu = null;
  }
  ResizeDocument(Doc, Mesh) {
    this.DisposeDocument(Doc);
    this.CreateDocument(Doc, Mesh);
  }
  SetMesh(Doc, Mesh) {
    const GL = this.GL;
    const G = Doc.Gpu;
    G.Buffers?.Dispose();
    G.Mesh = Mesh;
    G.Buffers = CreateMeshBuffers(GL, Mesh);
    this.Release(G.Position);
    this.Release(G.Normal);
    const R = G.Resolution;
    G.Position = this.Allocate(R, R, "RGBA32F");
    const RawNormal = this.Allocate(R, R, "RGBA16F");
    G.Normal = this.Allocate(R, R, "RGBA16F");
    this.Targets.Bind([G.Position, RawNormal]);
    GL.clearBufferfv(GL.COLOR, 0, [0, 0, 0, 0]);
    GL.clearBufferfv(GL.COLOR, 1, [0, 0, 1, 0]);
    GL.disable(GL.DEPTH_TEST);
    GL.disable(GL.CULL_FACE);
    GL.disable(GL.BLEND);
    this.Programs.Maps.Use();
    // Edges first so true triangle interiors always win on overlap.
    GL.bindVertexArray(G.Buffers.EdgeVao);
    GL.drawElements(GL.LINES, G.Buffers.EdgeCount, GL.UNSIGNED_INT, 0);
    GL.bindVertexArray(G.Buffers.Vao);
    GL.drawElements(GL.TRIANGLES, G.Buffers.Count, GL.UNSIGNED_INT, 0);
    this.Targets.Bind([G.Normal]);
    this.Programs.Curvature.Use().Texture("u_Position", G.Position).Texture("u_Normal", RawNormal);
    this.DrawQuad();
    this.Release(RawNormal);
    G.CacheSignature = null;
    G.Dirty = true;
    this.PickDirty = true;
  }
  DrawQuad() {
    this.GL.bindVertexArray(this.Quad);
    this.GL.drawArrays(this.GL.TRIANGLES, 0, 6);
  }

  // -------------------------------------------------------------- materials
  BitmapTexture(DataUrl) {
    if (!DataUrl) return null;
    let Entry = this.Bitmaps.get(DataUrl);
    if (!Entry) {
      Entry = { Texture: null, Ready: false };
      this.Bitmaps.set(DataUrl, Entry);
      const Image_ = new Image();
      Image_.onload = () => {
        const GL = this.GL;
        const Texture = GL.createTexture();
        GL.bindTexture(GL.TEXTURE_2D, Texture);
        GL.pixelStorei(GL.UNPACK_FLIP_Y_WEBGL, true);
        GL.texImage2D(GL.TEXTURE_2D, 0, GL.RGBA8, GL.RGBA, GL.UNSIGNED_BYTE, Image_);
        GL.pixelStorei(GL.UNPACK_FLIP_Y_WEBGL, false);
        GL.generateMipmap(GL.TEXTURE_2D);
        GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER, GL.LINEAR_MIPMAP_LINEAR);
        GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_S, GL.REPEAT);
        GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_T, GL.REPEAT);
        Entry.Texture = Texture;
        Entry.Ready = true;
        this.OnAssetReady?.();
      };
      Image_.src = DataUrl;
    }
    return Entry.Ready ? Entry.Texture : null;
  }
  SetMaterial(ProgramRef, Index, Material) {
    const Name = (Field) => `u_Material[${Index}].${Field}`;
    const Bitmap = this.BitmapTexture(Material.Bitmap);
    const Emissive = HexToRgb(Material.Emissive).map((C) => C * Material.EmissiveStrength);
    ProgramRef.Vec3(Name("Color"), HexToRgb(Material.Color))
      .Vec3(Name("Color2"), HexToRgb(Material.Color2))
      .Vec2(Name("Roughness"), Material.Roughness, Material.Roughness2)
      .Vec2(Name("Metallic"), Material.Metallic, Material.Metallic2)
      .Float(Name("Height"), Material.Height)
      .Float(Name("HeightAmount"), Material.HeightAmount)
      .Vec3(Name("Emissive"), Emissive)
      .Float(Name("EmissiveFromPattern"), Material.EmissiveFromPattern ? 1 : 0)
      .Int(Name("Pattern"), PatternIndex(Material.Pattern))
      .Float(Name("Scale"), Material.Scale)
      .Float(Name("Rotation"), Material.Rotation)
      .Float(Name("Contrast"), Material.Contrast)
      .Float(Name("Balance"), Material.Balance)
      .Float(Name("Warp"), Material.Warp)
      .Float(Name("Seed"), Material.Seed)
      .Float(Name("Grain"), Material.Grain)
      .Int(Name("Mapping"), Material.Mapping === "uv" ? 1 : 0)
      .Int(Name("UseBitmap"), Bitmap ? 1 : 0)
      .Float(Name("BitmapScale"), Material.BitmapScale)
      .Float(Name("BitmapHeight"), Material.BitmapHeight);
    ProgramRef.Texture(Index === 0 ? "u_Bitmap0" : "u_Bitmap1", Bitmap || this.White);
  }
  SetEnvironment(ProgramRef, Environment) {
    const Preset = ENVIRONMENTS[Environment.Preset] || ENVIRONMENTS.studio;
    ProgramRef.Vec3("u_Sky", Preset.Sky)
      .Vec3("u_Horizon", Preset.Horizon)
      .Vec3("u_Ground", Preset.Ground)
      .Vec4Array("u_Lights", Preset.Lights.flatMap((L) => [...Normalize(L.Dir), L.Size]))
      .Vec3Array("u_LightColors", Preset.Lights.flatMap((L) => L.Color))
      .Float("u_EnvRotation", (Environment.Rotation * Math.PI) / 180)
      .Float("u_Exposure", Environment.Exposure);
  }

  // ----------------------------------------------------------------- decals
  // Decal canvases are uploaded once per source signature.
  DecalTexture(Signature, Rasterize) {
    let Entry = this.Decals.get(Signature);
    if (!Entry) {
      Entry = { Texture: null, Aspect: 1, Ready: false, Error: null, Used: performance.now() };
      this.Decals.set(Signature, Entry);
      Promise.resolve()
        .then(Rasterize)
        .then((Canvas) => {
          const GL = this.GL;
          const Texture = GL.createTexture();
          GL.bindTexture(GL.TEXTURE_2D, Texture);
          GL.pixelStorei(GL.UNPACK_FLIP_Y_WEBGL, true);
          GL.texImage2D(GL.TEXTURE_2D, 0, GL.RGBA8, GL.RGBA, GL.UNSIGNED_BYTE, Canvas);
          GL.pixelStorei(GL.UNPACK_FLIP_Y_WEBGL, false);
          GL.generateMipmap(GL.TEXTURE_2D);
          GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_MIN_FILTER, GL.LINEAR_MIPMAP_LINEAR);
          GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_S, GL.CLAMP_TO_EDGE);
          GL.texParameteri(GL.TEXTURE_2D, GL.TEXTURE_WRAP_T, GL.CLAMP_TO_EDGE);
          const Anisotropy = GL.getExtension("EXT_texture_filter_anisotropic");
          if (Anisotropy) GL.texParameterf(GL.TEXTURE_2D, Anisotropy.TEXTURE_MAX_ANISOTROPY_EXT, 8);
          Entry.Texture = Texture;
          Entry.Aspect = Canvas.width / Canvas.height;
          Entry.Ready = true;
          this.OnAssetReady?.(Signature);
        })
        .catch((ErrorValue) => {
          Entry.Error = ErrorValue.message || String(ErrorValue);
          this.OnAssetReady?.(Signature, Entry.Error);
        });
      if (this.Decals.size > 40) {
        const Oldest = [...this.Decals.entries()].filter(([, E]) => E.Ready).sort((A, B) => A[1].Used - B[1].Used)[0];
        if (Oldest) {
          this.GL.deleteTexture(Oldest[1].Texture);
          this.Decals.delete(Oldest[0]);
        }
      }
    }
    Entry.Used = performance.now();
    return Entry;
  }

  // -------------------------------------------------------------- composite
  LayerSignature(Layer) {
    return `${Layer.Id}:${Layer.Revision}:${Layer.Visible ? 1 : 0}`;
  }
  BindLayerUniforms(ProgramRef, Doc, Layer, Stroke) {
    const G = Doc.Gpu;
    const Type = { paint: 1, fill: 2, decal: 3 }[Layer.Type];
    const C = Layer.Channels;
    ProgramRef.Int("u_Type", Type)
      .Float("u_Opacity", Layer.Opacity)
      .Int("u_Blend", BlendIndex(BLEND_MODES, Layer.Blend))
      .Int("u_HeightBlend", BlendIndex(HEIGHT_BLENDS, Layer.HeightBlend))
      .Vec4("u_Channels", [C.color ? 1 : 0, C.roughness ? 1 : 0, C.metallic ? 1 : 0, C.height ? 1 : 0])
      .Float("u_EmissiveChannel", C.emissive ? 1 : 0);
    const Mask = Layer.Mask;
    ProgramRef.Int("u_MaskEnabled", Mask.Enabled ? 1 : 0)
      .Int("u_MaskPainted", Mask.Painted && Layer.Gpu.Mask ? 1 : 0)
      .Int("u_MaskGenerator", MaskGeneratorIndex(Mask.Generator))
      .Vec4("u_MaskParams", [Mask.Scale, Mask.Contrast, Mask.Offset, Mask.Seed])
      .Vec2("u_MaskExtra", Mask.Invert ? 1 : 0, Mask.Breakup ?? 0.4)
      .Vec2("u_HeightRange", G.Mesh.Bounds.Min[1], G.Mesh.Bounds.Max[1])
      .Texture("u_Mask", Layer.Gpu.Mask || this.White);
    const Paint = Layer.Gpu.Textures || [this.Black, this.Black, this.Black, this.Black];
    ProgramRef.Texture("u_T0", Paint[0]).Texture("u_T1", Paint[1]).Texture("u_T2", Paint[2]).Texture("u_T3", Paint[3]);
    this.SetMaterial(ProgramRef, 0, Layer.Material);
    let DecalTexture = this.Black;
    let DecalReady = 0;
    if (Layer.Type === "decal" && Layer.DecalEntry?.Ready) {
      DecalTexture = Layer.DecalEntry.Texture;
      DecalReady = 1;
    }
    ProgramRef.Texture("u_Decal", DecalTexture).Int("u_DecalReady", DecalReady);
    if (Layer.Type === "decal") {
      const D = Layer.Decal;
      const Aspect = Layer.DecalEntry?.Aspect || 1;
      const Width = D.Size;
      const Height = (D.Size / Aspect) * D.Stretch;
      if (D.Mapping === "projected") {
        const Frame = DecalFrame(D.Normal, D.Up, (D.Rotation * Math.PI) / 180);
        const Right = Frame.Right.map((V) => (V * (D.FlipX ? -1 : 1)) / Width);
        const Up = Frame.Up.map((V) => (V * (D.FlipY ? -1 : 1)) / Height);
        ProgramRef.Int("u_DecalMapping", 0)
          .Vec3("u_DecalOrigin", D.Position)
          .Vec3("u_DecalRight", Right)
          .Vec3("u_DecalUp", Up)
          .Vec3("u_DecalNormal", Frame.Normal.map((V) => V / Math.max(0.005, D.Depth)))
          .Vec2("u_DecalAngle", Math.cos((D.AngleLimit * Math.PI) / 180), 0.06)
          .Int("u_DecalTwoSided", D.TwoSided ? 1 : 0);
      } else {
        const Angle = (D.Rotation * Math.PI) / 180;
        const UvWidth = D.UvSize;
        const UvHeight = (D.UvSize / Aspect) * D.Stretch;
        const C_ = Math.cos(Angle), S_ = Math.sin(Angle);
        const SX = (D.FlipX ? -1 : 1) / UvWidth;
        const SY = (D.FlipY ? -1 : 1) / UvHeight;
        // Local = Scale · R(-angle) · (uv - center); column-major mat2.
        ProgramRef.Int("u_DecalMapping", 1)
          .Vec2("u_DecalCenter", D.UvCenter[0], D.UvCenter[1])
          .Mat2("u_DecalInverse", [C_ * SX, -S_ * SY, S_ * SX, C_ * SY]);
      }
      ProgramRef.Int("u_DecalSourceColor", D.SourceColor ? 1 : 0);
    }
    const IsStrokeLayer = Stroke && Stroke.LayerId === Layer.Id;
    ProgramRef.Int("u_StrokeMode", IsStrokeLayer ? (Stroke.Target === "mask" ? 2 : 1) : 0);
    if (IsStrokeLayer) this.BindBrush(ProgramRef, Stroke);
    else ProgramRef.Texture("u_Stroke", this.Black).Texture("u_Bitmap1", this.White);
  }
  BindBrush(ProgramRef, Stroke) {
    const C = Stroke.Channels;
    ProgramRef.Texture("u_Stroke", Stroke.Texture)
      .Vec4("u_BrushChannels", [C.color ? 1 : 0, C.roughness ? 1 : 0, C.metallic ? 1 : 0, C.height ? 1 : 0])
      .Float("u_BrushEmissive", C.emissive ? 1 : 0)
      .Float("u_BrushOpacity", Stroke.Opacity)
      .Int("u_Erase", Stroke.Erase ? 1 : 0)
      .Float("u_MaskValue", Stroke.MaskValue);
    this.SetMaterial(ProgramRef, 1, Stroke.Material);
  }

  Composite(Doc, Stroke = null, FocusLayerId = null) {
    const GL = this.GL;
    const G = Doc.Gpu;
    GL.disable(GL.BLEND);
    GL.disable(GL.DEPTH_TEST);
    GL.disable(GL.CULL_FACE);
    const Layers = Doc.Layers.filter((L) => L.Visible && L.Opacity > 0);
    const FocusIndex = FocusLayerId ? Layers.findIndex((L) => L.Id === FocusLayerId) : -1;
    const Base = Doc.Base;
    const BaseColor = HexToRgb(Base.Color);
    const BaseValues = [[...BaseColor, 1], [Base.Roughness, Base.Metallic, Base.Height, 1], [0, 0, 0, 1]];
    const Program_ = this.Programs.Composite;
    let Current = G.Accum[0];
    let Next = G.Accum[1];
    let Start = 0;
    const BelowSignature = FocusIndex > 0 ? `${Doc.BaseRevision}|${G.MapsRevision ?? 0}|` + Layers.slice(0, FocusIndex).map((L) => this.LayerSignature(L)).join(",") : null;
    if (BelowSignature && G.CacheSignature === BelowSignature) {
      Current = G.Cache;
      Start = FocusIndex;
    } else this.Clear(Current, BaseValues);
    for (let Index = Start; Index < Layers.length; Index++) {
      const Layer = Layers[Index];
      if (Current === G.Cache) Next = G.Accum[0];
      this.Targets.Bind(Next);
      Program_.Use()
        .Int("u_OutputMask", 0)
        .Texture("u_Prev0", Current[0])
        .Texture("u_Prev1", Current[1])
        .Texture("u_Prev2", Current[2])
        .Texture("u_Position", G.Position)
        .Texture("u_Normal", G.Normal);
      this.BindLayerUniforms(Program_, Doc, Layer, Stroke);
      this.DrawQuad();
      const Previous = Current;
      Current = Next;
      Next = Previous === G.Cache ? G.Accum[1] : Previous;
      if (BelowSignature && Index === FocusIndex - 1) {
        for (let K = 0; K < 3; K++) this.Blit(Current[K], G.Cache[K]);
        G.CacheSignature = BelowSignature;
      }
    }
    if (BelowSignature && FocusIndex === 0) G.CacheSignature = null;
    if (Current === G.Cache) {
      for (let K = 0; K < 3; K++) this.Blit(G.Cache[K], G.Accum[0][K]);
      Current = G.Accum[0];
    }
    this.Targets.Bind([G.NormalOut]);
    this.Programs.Normal.Use()
      .Texture("u_Height", Current[1])
      .Texture("u_Position", G.Position)
      .Float("u_Strength", Doc.NormalStrength);
    this.DrawQuad();
    this.Targets.Bind(G.Final);
    this.Programs.Dilate.Use()
      .Texture("u_Source0", Current[0])
      .Texture("u_Source1", Current[1])
      .Texture("u_Source2", Current[2])
      .Texture("u_Source3", G.NormalOut)
      .Texture("u_Position", G.Position)
      .Int("u_Radius", Math.max(4, Math.round(G.Resolution / 128)));
    this.DrawQuad();
    for (const Texture of G.Final) {
      GL.bindTexture(GL.TEXTURE_2D, Texture);
      GL.generateMipmap(GL.TEXTURE_2D);
    }
    G.Dirty = false;
  }

  RenderMask(Doc, Layer, Stroke) {
    const G = Doc.Gpu;
    if (!Layer) {
      this.Clear([G.MaskView], [[1, 1, 1, 1]]);
      return;
    }
    this.GL.disable(this.GL.BLEND);
    const Program_ = this.Programs.Composite;
    this.Targets.Bind([G.MaskView, null, null]);
    Program_.Use()
      .Int("u_OutputMask", 1)
      .Texture("u_Prev0", this.Black)
      .Texture("u_Prev1", this.Black)
      .Texture("u_Prev2", this.Black)
      .Texture("u_Position", G.Position)
      .Texture("u_Normal", G.Normal);
    this.BindLayerUniforms(Program_, Doc, Layer, Stroke);
    this.DrawQuad();
  }

  // ---------------------------------------------------------------- strokes
  BeginStroke(Doc) {
    this.Clear([Doc.Gpu.Stroke], [[0, 0, 0, 0]]);
  }
  // Dabs are [x, y, radius, flow, angle, seed] in pick-buffer pixels (3D) or
  // UV units (UV mode).
  StampDabs(Doc, Dabs, Brush, View) {
    if (!Dabs.length) return;
    const GL = this.GL;
    const G = Doc.Gpu;
    const Program_ = this.Programs.Stroke;
    this.Targets.Bind([G.Stroke]);
    GL.enable(GL.BLEND);
    GL.blendEquation(GL.FUNC_ADD);
    GL.blendFunc(GL.ONE_MINUS_DST_COLOR, GL.ONE);
    Program_.Use()
      .Texture("u_Position", G.Position)
      .Texture("u_Normal", G.Normal)
      .Texture("u_Pick", View.Mode === 0 && this.Pick ? this.Pick.Textures[0] : this.Black)
      .Int("u_Mode", View.Mode)
      .Int("u_Tip", { round: 0, square: 1, noise: 2, splatter: 3 }[Brush.Tip] ?? 0)
      .Float("u_Hardness", Brush.Hardness)
      .Float("u_Roundness", Brush.Roundness)
      .Int("u_Backface", Brush.Backface ? 1 : 0)
      .Vec2("u_Resolution", G.Resolution, G.Resolution);
    if (View.Mode === 0)
      Program_.Mat4("u_ViewProjection", View.ViewProjection)
        .Mat4("u_View", View.View)
        .Vec3("u_Camera", View.Camera)
        .Vec2("u_Viewport", View.Width, View.Height);
    for (let Offset = 0; Offset < Dabs.length; Offset += 32) {
      const Batch = Dabs.slice(Offset, Offset + 32);
      Program_.Int("u_DabCount", Batch.length)
        .Vec4Array("u_Dabs", new Float32Array(Array.from({ length: 32 }, (_, K) => (Batch[K] ? Batch[K].slice(0, 4) : [0, 0, 0, 0])).flat()))
        .Vec2Array("u_DabExtra", new Float32Array(Array.from({ length: 32 }, (_, K) => (Batch[K] ? [Batch[K][4], Batch[K][5]] : [0, 0])).flat()));
      if (View.Mode === 1) {
        // Scissor UV strokes to the dab bounds.
        let MinX = Infinity, MinY = Infinity, MaxX = -Infinity, MaxY = -Infinity;
        for (const D of Batch) {
          const R = D[2] * 1.5;
          MinX = Math.min(MinX, D[0] - R); MaxX = Math.max(MaxX, D[0] + R);
          MinY = Math.min(MinY, D[1] - R); MaxY = Math.max(MaxY, D[1] + R);
        }
        const Size = G.Resolution;
        const X0 = Math.max(0, Math.floor(MinX * Size)), Y0 = Math.max(0, Math.floor(MinY * Size));
        const X1 = Math.min(Size, Math.ceil(MaxX * Size)), Y1 = Math.min(Size, Math.ceil(MaxY * Size));
        if (X1 <= X0 || Y1 <= Y0) continue;
        GL.enable(GL.SCISSOR_TEST);
        GL.scissor(X0, Y0, X1 - X0, Y1 - Y0);
      }
      this.DrawQuad();
      GL.disable(GL.SCISSOR_TEST);
    }
    GL.disable(GL.BLEND);
  }
  // Returns the replaced textures so the caller can store them for undo.
  CommitStroke(Doc, Layer, Stroke) {
    const GL = this.GL;
    const G = Doc.Gpu;
    GL.disable(GL.BLEND);
    if (Stroke.Target === "mask") {
      const Old = Layer.Gpu.Mask;
      const New = this.Allocate(G.Resolution, G.Resolution, "RGBA8", { Tag: "layer" });
      this.Targets.Bind([New]);
      this.Programs.MaskCommit.Use()
        .Texture("u_Mask", Old)
        .Texture("u_Stroke", G.Stroke)
        .Float("u_Value", Stroke.MaskValue)
        .Float("u_BrushOpacity", Stroke.Opacity);
      this.DrawQuad();
      Layer.Gpu.Mask = New;
      return { Mask: Old };
    }
    const C = Stroke.Channels;
    const Needed = [
      C.color,
      C.roughness || C.metallic || C.height,
      C.roughness || C.metallic || C.height || C.emissive,
      C.emissive,
    ];
    const Old = Layer.Gpu.Textures;
    const New = Old.map((Texture, Index) =>
      Needed[Index] ? this.Allocate(G.Resolution, G.Resolution, Texture.FormatName, { Tag: "layer" }) : null,
    );
    if (!New.some(Boolean)) return { Textures: {} };
    this.Targets.Bind(New);
    const Program_ = this.Programs.Commit.Use()
      .Texture("u_T0", Old[0])
      .Texture("u_T1", Old[1])
      .Texture("u_T2", Old[2])
      .Texture("u_T3", Old[3])
      .Texture("u_Position", G.Position)
      .Texture("u_Normal", G.Normal);
    this.BindBrush(Program_, { ...Stroke, Texture: G.Stroke });
    this.DrawQuad();
    const Replaced = {};
    Layer.Gpu.Textures = Old.map((Texture, Index) => {
      if (!New[Index]) return Texture;
      Replaced[Index] = Texture;
      return New[Index];
    });
    return { Textures: Replaced };
  }

  // ------------------------------------------------------------------- pick
  EnsurePick(Width, Height) {
    const GL = this.GL;
    if (this.Pick && this.Pick.Width === Width && this.Pick.Height === Height) return;
    if (this.Pick) {
      this.Pick.Textures.forEach((T) => this.Release(T));
      GL.deleteRenderbuffer(this.Pick.Depth);
    }
    const Depth = GL.createRenderbuffer();
    GL.bindRenderbuffer(GL.RENDERBUFFER, Depth);
    GL.renderbufferStorage(GL.RENDERBUFFER, GL.DEPTH_COMPONENT24, Width, Height);
    this.Pick = {
      Width,
      Height,
      Depth,
      Textures: [this.Allocate(Width, Height, "RGBA32F"), this.Allocate(Width, Height, "RGBA32F"), this.Allocate(Width, Height, "RGBA32F")],
    };
    this.PickDirty = true;
  }
  RenderPick(Doc, View) {
    const GL = this.GL;
    this.EnsurePick(View.Width, View.Height);
    this.Targets.Bind(this.Pick.Textures, this.Pick.Depth);
    GL.clearBufferfv(GL.COLOR, 0, [0, 0, 0, 0]);
    GL.clearBufferfv(GL.COLOR, 1, [0, 0, 0, 0]);
    GL.clearBufferfv(GL.COLOR, 2, [0, 0, 0, 0]);
    GL.clearBufferfi(GL.DEPTH_STENCIL, 0, 1, 0);
    GL.enable(GL.DEPTH_TEST);
    GL.depthFunc(GL.LESS);
    GL.disable(GL.BLEND);
    GL.disable(GL.CULL_FACE);
    this.Programs.Pick.Use().Mat4("u_ViewProjection", View.ViewProjection).Mat4("u_View", View.View);
    GL.bindVertexArray(Doc.Gpu.Buffers.Vao);
    GL.drawElements(GL.TRIANGLES, Doc.Gpu.Buffers.Count, GL.UNSIGNED_INT, 0);
    GL.disable(GL.DEPTH_TEST);
    this.PickDirty = false;
  }
  ReadPick(X, Y) {
    if (!this.Pick) return null;
    const GL = this.GL;
    X = Math.floor(X);
    Y = Math.floor(Y);
    if (X < 0 || Y < 0 || X >= this.Pick.Width || Y >= this.Pick.Height) return null;
    const Framebuffer = this.Targets.Bind(this.Pick.Textures, this.Pick.Depth);
    GL.bindFramebuffer(GL.READ_FRAMEBUFFER, Framebuffer);
    const Read = (Index) => {
      const Pixel = new Float32Array(4);
      GL.readBuffer(GL.COLOR_ATTACHMENT0 + Index);
      GL.readPixels(X, Y, 1, 1, GL.RGBA, GL.FLOAT, Pixel);
      return Pixel;
    };
    const Pick = Read(0);
    if (Pick[3] < 0.5) {
      GL.readBuffer(GL.COLOR_ATTACHMENT0);
      return null;
    }
    const Position = Read(1);
    const Normal = Read(2);
    GL.readBuffer(GL.COLOR_ATTACHMENT0);
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
    return { Uv: [Pick[0], Pick[1]], Depth: Pick[2], Position: [...Position.slice(0, 3)], Normal: Normalize([...Normal.slice(0, 3)]) };
  }
  ReadFinalTexel(Doc, Uv) {
    const GL = this.GL;
    const G = Doc.Gpu;
    const X = Math.min(G.Resolution - 1, Math.max(0, Math.floor(Uv[0] * G.Resolution)));
    const Y = Math.min(G.Resolution - 1, Math.max(0, Math.floor(Uv[1] * G.Resolution)));
    const Result = [];
    for (const Texture of [G.Final[0], G.Final[1], G.Final[2]]) {
      const Framebuffer = this.Targets.Bind([Texture]);
      GL.bindFramebuffer(GL.READ_FRAMEBUFFER, Framebuffer);
      const Pixel = new Float32Array(4);
      GL.readPixels(X, Y, 1, 1, GL.RGBA, GL.FLOAT, Pixel);
      Result.push([...Pixel]);
    }
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
    return { Color: Result[0].slice(0, 3), Roughness: Result[1][0], Metallic: Result[1][1], Height: Result[1][2], Emissive: Result[2].slice(0, 3) };
  }

  // --------------------------------------------------------------- viewport
  Resize(Width, Height) {
    if (this.Canvas.width !== Width || this.Canvas.height !== Height) {
      this.Canvas.width = Width;
      this.Canvas.height = Height;
    }
  }
  ChannelUniforms(ProgramRef, Doc, View) {
    const G = Doc.Gpu;
    ProgramRef.Texture("u_Color", G.Final[0])
      .Texture("u_Surface", G.Final[1])
      .Texture("u_Emissive", G.Final[2])
      .Texture("u_NormalMap", G.Final[3])
      .Texture("u_MaskView", G.MaskView)
      .Int("u_Channel", View.Channel)
      .Float("u_HeightView", View.HeightView ?? 2);
  }
  Render3D(Doc, View, Rect) {
    const GL = this.GL;
    const G = Doc.Gpu;
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
    GL.viewport(Rect.X, Rect.Y, Rect.Width, Rect.Height);
    GL.enable(GL.SCISSOR_TEST);
    GL.scissor(Rect.X, Rect.Y, Rect.Width, Rect.Height);
    GL.disable(GL.DEPTH_TEST);
    GL.disable(GL.BLEND);
    const Background = this.Programs.Background.Use()
      .Mat4("u_InverseViewProjection", Mat4.Invert(View.ViewProjection))
      .Int("u_ShowEnvironment", Doc.Environment.Show ? 1 : 0);
    this.SetEnvironment(Background, Doc.Environment);
    this.DrawQuad();
    GL.clear(GL.DEPTH_BUFFER_BIT);
    GL.enable(GL.DEPTH_TEST);
    GL.depthFunc(GL.LESS);
    GL.enable(GL.POLYGON_OFFSET_FILL);
    GL.polygonOffset(1, 1);
    const Program_ = this.Programs.Viewport.Use()
      .Mat4("u_ViewProjection", View.ViewProjection)
      .Vec3("u_Camera", View.Camera);
    this.ChannelUniforms(Program_, Doc, View);
    this.SetEnvironment(Program_, Doc.Environment);
    GL.bindVertexArray(G.Buffers.Vao);
    GL.drawElements(GL.TRIANGLES, G.Buffers.Count, GL.UNSIGNED_INT, 0);
    GL.disable(GL.POLYGON_OFFSET_FILL);
    if (View.Wireframe) {
      GL.enable(GL.BLEND);
      GL.blendFunc(GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA);
      GL.depthFunc(GL.LEQUAL);
      GL.depthMask(false);
      this.Programs.Line.Use().Mat4("u_ViewProjection", View.ViewProjection).Vec4("u_Color", [1, 1, 1, 0.09]);
      GL.bindVertexArray(G.Buffers.EdgeVao);
      GL.drawElements(GL.LINES, G.Buffers.EdgeCount, GL.UNSIGNED_INT, 0);
      GL.depthMask(true);
      GL.disable(GL.BLEND);
    }
    GL.disable(GL.DEPTH_TEST);
    GL.disable(GL.SCISSOR_TEST);
  }
  RenderUv(Doc, View, Rect) {
    const GL = this.GL;
    const G = Doc.Gpu;
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
    GL.viewport(Rect.X, Rect.Y, Rect.Width, Rect.Height);
    GL.enable(GL.SCISSOR_TEST);
    GL.scissor(Rect.X, Rect.Y, Rect.Width, Rect.Height);
    GL.clearColor(0.062, 0.062, 0.062, 1);
    GL.clear(GL.COLOR_BUFFER_BIT);
    GL.disable(GL.DEPTH_TEST);
    const Channel = View.Channel === 0 ? 1 : View.Channel;
    const Program_ = this.Programs.UvView.Use().Vec4("u_Rect", View.UvRect);
    this.ChannelUniforms(Program_, Doc, { ...View, Channel });
    Program_.Texture("u_Position", G.Position).Float("u_Checker", 12).Int("u_DimOutside", 1);
    this.DrawQuad();
    if (View.Wireframe) {
      GL.enable(GL.BLEND);
      GL.blendFunc(GL.SRC_ALPHA, GL.ONE_MINUS_SRC_ALPHA);
      this.Programs.UvLine.Use().Vec4("u_Rect", View.UvRect).Vec4("u_Color", [1, 1, 1, 0.1]);
      GL.bindVertexArray(G.Buffers.UvVao);
      GL.drawArrays(GL.LINES, 0, G.Buffers.UvLineCount);
      GL.disable(GL.BLEND);
    }
    GL.disable(GL.SCISSOR_TEST);
  }

  // Material library thumbnail.
  RenderPreview(Material, Size, Environment) {
    const GL = this.GL;
    if (!this.PreviewTarget || this.PreviewTarget.Width !== Size) {
      this.Release(this.PreviewTarget);
      this.PreviewTarget = this.Allocate(Size, Size, "RGBA8");
    }
    this.Targets.Bind([this.PreviewTarget]);
    GL.disable(GL.BLEND);
    GL.clearBufferfv(GL.COLOR, 0, [0, 0, 0, 0]);
    const Program_ = this.Programs.Preview.Use();
    this.SetMaterial(Program_, 0, Material);
    this.SetEnvironment(Program_, Environment);
    this.DrawQuad();
    const Pixels = new Uint8Array(Size * Size * 4);
    GL.readPixels(0, 0, Size, Size, GL.RGBA, GL.UNSIGNED_BYTE, Pixels);
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
    return Pixels;
  }

  // Channel thumbnails: draw a channel into a small RGBA8 target and read it.
  RenderChannelThumbnail(Doc, Channel, Size) {
    const GL = this.GL;
    if (!this.ThumbTarget || this.ThumbTarget.Width !== Size) {
      this.Release(this.ThumbTarget);
      this.ThumbTarget = this.Allocate(Size, Size, "RGBA8");
    }
    this.Targets.Bind([this.ThumbTarget]);
    GL.disable(GL.BLEND);
    const Program_ = this.Programs.UvView.Use().Vec4("u_Rect", [-1, -1, 2, 2]);
    this.ChannelUniforms(Program_, Doc, { Channel, HeightView: 2 });
    Program_.Texture("u_Position", Doc.Gpu.Position).Float("u_Checker", 4).Int("u_DimOutside", 0);
    this.DrawQuad();
    const Pixels = new Uint8Array(Size * Size * 4);
    GL.readPixels(0, 0, Size, Size, GL.RGBA, GL.UNSIGNED_BYTE, Pixels);
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
    return Pixels;
  }

  // ---------------------------------------------------------------- readback
  ReadTexture(Texture) {
    const GL = this.GL;
    const Framebuffer = this.Targets.Bind([Texture]);
    GL.bindFramebuffer(GL.READ_FRAMEBUFFER, Framebuffer);
    let Pixels;
    if (Texture.FormatName === "RGBA8") {
      Pixels = new Uint8Array(Texture.Width * Texture.Height * 4);
      GL.readPixels(0, 0, Texture.Width, Texture.Height, GL.RGBA, GL.UNSIGNED_BYTE, Pixels);
    } else {
      Pixels = new Float32Array(Texture.Width * Texture.Height * 4);
      GL.readPixels(0, 0, Texture.Width, Texture.Height, GL.RGBA, GL.FLOAT, Pixels);
    }
    GL.bindFramebuffer(GL.FRAMEBUFFER, null);
    return Pixels;
  }
  // Uploads raw pixels into a fresh layer texture.
  UploadLayerTexture(Resolution, Format, Pixels) {
    const GL = this.GL;
    const Texture = this.Allocate(Resolution, Resolution, Format, { Tag: "layer" });
    GL.bindTexture(GL.TEXTURE_2D, Texture);
    GL.pixelStorei(GL.UNPACK_ALIGNMENT, 1);
    if (Format === "RGBA8")
      GL.texSubImage2D(GL.TEXTURE_2D, 0, 0, 0, Resolution, Resolution, GL.RGBA, GL.UNSIGNED_BYTE, Pixels);
    else GL.texSubImage2D(GL.TEXTURE_2D, 0, 0, 0, Resolution, Resolution, GL.RGBA, GL.FLOAT, Pixels);
    return Texture;
  }
}
