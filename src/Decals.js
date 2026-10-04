/* Frontier Texture Paint — non-destructive SVG + text decal rasterizer.
   Decal layers keep their vector source and transform; this module bakes them
   into the layer's channel canvases on demand. */

import { FontFamily } from "./Presets.js";

export function makeCanvas(Width, Height) {
  const Canvas = document.createElement("canvas");
  Canvas.width = Math.max(1, Math.round(Width));
  Canvas.height = Math.max(1, Math.round(Height));
  return Canvas;
}

/** Deterministic PRNG for seeded patterns. */
export function mulberry32(Seed) {
  let State = Seed >>> 0;
  return () => {
    State |= 0;
    State = (State + 0x6d2b79f5) | 0;
    let Mix = Math.imul(State ^ (State >>> 15), 1 | State);
    Mix = (Mix + Math.imul(Mix ^ (Mix >>> 7), 61 | Mix)) ^ Mix;
    return ((Mix ^ (Mix >>> 14)) >>> 0) / 4294967296;
  };
}

function loadImage(Source) {
  return new Promise((Resolve, Reject) => {
    const Image = new window.Image();
    Image.onload = () => Resolve(Image);
    Image.onerror = () => Reject(new Error("Could not decode image"));
    Image.src = Source;
  });
}

async function rasterizeSVG(SvgText, Width, Height) {
  const Blob = new window.Blob([SvgText], { type: "image/svg+xml;charset=utf-8" });
  const Url = URL.createObjectURL(Blob);
  try {
    return await loadImage(Url);
  } finally {
    setTimeout(() => URL.revokeObjectURL(Url), 4000);
  }
}

/**
 * Resolve the drawable artwork for a decal layer. Returns { image, w, h }
 * or null while the source is still loading. `onReady` fires when an async
 * load finishes so the document can recomposite.
 */
export function resolveDecalArtwork(Layer, DocumentWidth, DocumentHeight, onReady) {
  const Params = Layer.params;
  if (Layer.kind === "svg") {
    const Key = `svg:${Params.svg.length}:${hashString(Params.svg)}`;
    const Cache = Layer.artCache;
    if (Cache && Cache.key === Key && Cache.image) return Cache;
    if (Cache && Cache.key === Key && Cache.pending) return null;
    Layer.artCache = { key: Key, image: null, w: 0, h: 0, pending: true };
    rasterizeSVG(Params.svg, DocumentWidth, DocumentHeight)
      .then((Image) => {
        Layer.artCache = { key: Key, image: Image, w: Image.naturalWidth || 512, h: Image.naturalHeight || 512, pending: false };
        onReady && onReady();
      })
      .catch(() => {
        Layer.artCache = { key: Key, image: null, w: 0, h: 0, pending: false, error: true };
        onReady && onReady();
      });
    return null;
  }
  if (Layer.kind === "image") {
    const Source = Params.dataURL || "";
    if (!Source) return null;
    const Key = `img:${Source.length}:${hashString(Source.slice(0, 4096))}:${hashString(Source.slice(-4096))}`;
    const Cache = Layer.artCache;
    if (Cache && Cache.key === Key && !Cache.pending) return Cache.image ? Cache : null;
    if (Cache && Cache.key === Key && Cache.pending) return null;
    Layer.artCache = { key: Key, image: null, w: 0, h: 0, pending: true };
    loadImage(Source)
      .then((Image) => {
        Layer.artCache = { key: Key, image: Image, w: Image.naturalWidth || DocumentWidth, h: Image.naturalHeight || DocumentHeight, pending: false };
        onReady && onReady();
      })
      .catch(() => {
        Layer.artCache = { key: Key, image: null, w: 0, h: 0, pending: false, error: true };
        onReady && onReady();
      });
    return null;
  }
  return null;
}

function hashString(Text) {
  let Hash = 5381;
  for (let Index = 0; Index < Text.length; Index++) {
    Hash = ((Hash << 5) + Hash + Text.charCodeAt(Index)) | 0;
  }
  return (Hash >>> 0).toString(36);
}

/** Draw text artwork into a scratch canvas, return the canvas + metrics. */
export function renderTextArtwork(Params, DocumentWidth, DocumentHeight) {
  const Scale = Math.max(64, DocumentWidth);
  const PixelSize = Math.max(4, Params.size * Scale * (Params.scale || 1));
  const Lines = String(Params.text ?? "Text").split("\n");
  const Family = FontFamily(Params.font);
  const Weight = Params.weight || 400;
  const Style = Params.italic ? "italic" : "normal";
  const Measure = makeCanvas(8, 8).getContext("2d");
  const Font = `${Style} ${Weight} ${PixelSize}px ${Family}`;
  Measure.font = Font;
  const Spacing = (Params.spacing || 0) * PixelSize;
  let Widest = 1;
  for (const Line of Lines) {
    const Width = Measure.measureText(Line).width + Spacing * Math.max(0, Line.length - 1);
    Widest = Math.max(Widest, Width);
  }
  const LineHeight = PixelSize * (Params.lineHeight || 1.15);
  const Pad = PixelSize * 0.25 + (Params.stroke || 0) * PixelSize * 2 + 4;
  const Canvas = makeCanvas(Math.ceil(Widest + Pad * 2), Math.ceil(LineHeight * Lines.length + Pad * 2));
  const Context = Canvas.getContext("2d");
  Context.font = Font;
  Context.textBaseline = "alphabetic";
  Context.lineJoin = "round";
  Context.miterLimit = 2;
  const Align = Params.align || "center";
  Lines.forEach((Line, Index) => {
    const Y = Pad + PixelSize * 0.82 + Index * LineHeight;
    let X = Pad;
    const LineWidth = Measure.measureText(Line).width + Spacing * Math.max(0, Line.length - 1);
    if (Align === "center") X = Canvas.width / 2;
    if (Align === "right") X = Canvas.width - Pad;
    drawSpacedLine(Context, Line, X, Y, Align, Spacing, Params);
    void LineWidth;
  });
  return Canvas;
}

function drawSpacedLine(Context, Line, X, Y, Align, Spacing, Params) {
  const Glyphs = [...Line];
  if (!Glyphs.length) return;
  const Widths = Glyphs.map((Glyph) => Context.measureText(Glyph).width);
  const Total = Widths.reduce((Sum, Width) => Sum + Width, 0) + Spacing * (Glyphs.length - 1);
  let Cursor = Align === "center" ? X - Total / 2 : Align === "right" ? X - Total : X;
  const StrokeWidth = (Params.stroke || 0) * parseFloat(Context.font);
  Glyphs.forEach((Glyph, Index) => {
    if (StrokeWidth > 0.25) {
      Context.lineWidth = StrokeWidth;
      Context.strokeStyle = Params.strokeColor || "#111111";
      Context.strokeText(Glyph, Cursor, Y);
    }
    Context.fillStyle = Params.color || "#ece9e2";
    Context.fillText(Glyph, Cursor, Y);
    Cursor += Widths[Index] + Spacing;
  });
}

/**
 * Compute the decal's oriented bounding box in texture pixels.
 * Params: x, y (0..1 center), scale (fraction of doc width applied to art width),
 * rotation (deg), flipX/flipY. Art aspect preserved.
 */
export function decalBounds(Params, ArtWidth, ArtHeight, DocumentWidth, DocumentHeight) {
  const ArtAspect = ArtWidth / Math.max(1, ArtHeight);
  const Width = Math.max(1, (Params.scale || 0.5) * DocumentWidth);
  const Height = Width / ArtAspect;
  const CenterX = (Params.x ?? 0.5) * DocumentWidth;
  const CenterY = (Params.y ?? 0.5) * DocumentHeight;
  const Angle = ((Params.rotation || 0) * Math.PI) / 180;
  const Cos = Math.cos(Angle);
  const Sin = Math.sin(Angle);
  const Hx = Width / 2;
  const Hy = Height / 2;
  const Corners = [
    [-Hx, -Hy], [Hx, -Hy], [Hx, Hy], [-Hx, Hy],
  ].map(([Dx, Dy]) => [CenterX + Dx * Cos - Dy * Sin, CenterY + Dx * Sin + Dy * Cos]);
  return { centerX: CenterX, centerY: CenterY, width: Width, height: Height, angle: Angle, corners: Corners };
}

/** Paint the decal artwork through its transform into `Context`. */
export function drawDecalArtwork(Context, Art, ArtWidth, ArtHeight, Layer, DocumentWidth, DocumentHeight) {
  const Params = Layer.params;
  const Bounds = decalBounds(Params, ArtWidth, ArtHeight, DocumentWidth, DocumentHeight);
  Context.save();
  Context.translate(Bounds.centerX, Bounds.centerY);
  Context.rotate(Bounds.angle);
  Context.scale(Params.flipX ? -1 : 1, Params.flipY ? -1 : 1);
  Context.globalAlpha = 1;
  Context.drawImage(Art, -Bounds.width / 2, -Bounds.height / 2, Bounds.width, Bounds.height);
  if (Params.tint && Layer.kind !== "text") {
    Context.globalCompositeOperation = "source-atop";
    Context.fillStyle = Params.tint;
    Context.fillRect(-Bounds.width / 2, -Bounds.height / 2, Bounds.width, Bounds.height);
    // Re-apply artwork luminance so tint behaves like a multiply glaze:
    Context.globalCompositeOperation = "multiply";
    Context.drawImage(Art, -Bounds.width / 2, -Bounds.height / 2, Bounds.width, Bounds.height);
  }
  Context.restore();
  return Bounds;
}

/** Fill `Target` with gray(Value) masked by the alpha of `AlphaSource`. */
export function fillChannelFromAlpha(Target, AlphaSource, Value, Color) {
  const Context = Target.getContext("2d");
  Context.save();
  Context.clearRect(0, 0, Target.width, Target.height);
  Context.drawImage(AlphaSource, 0, 0);
  Context.globalCompositeOperation = "source-in";
  if (Color) {
    Context.fillStyle = Color;
    Context.globalAlpha = Math.max(0, Math.min(2.5, Value));
    Context.fillRect(0, 0, Target.width, Target.height);
  } else {
    const Gray = Math.round(Math.max(0, Math.min(1, Value)) * 255);
    Context.fillStyle = `rgb(${Gray},${Gray},${Gray})`;
    Context.fillRect(0, 0, Target.width, Target.height);
  }
  Context.restore();
}
