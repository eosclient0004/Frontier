/* Frontier Texture Paint — brush engine.
   Dab-stamp strokes with spacing, stabilizer, symmetry and scatter;
   smudge, flood fill, eyedropper and shape commit. Strokes paint albedo plus
   any enabled PBR channels in one pass (material paint). */

import { makeCanvas } from "./Decals.js";

function hexToRgb(Hex) {
  const Clean = String(Hex || "#ffffff").replace("#", "");
  const Full = Clean.length === 3 ? Clean.split("").map((C) => C + C).join("") : Clean;
  const Int = parseInt(Full.slice(0, 6), 16);
  if (Number.isNaN(Int)) return [255, 255, 255];
  return [(Int >> 16) & 255, (Int >> 8) & 255, Int & 255];
}

export class BrushEngine {
  constructor() {
    this.tool = "paint";
    this.brush = {
      size: 64, hardness: 0.2, opacity: 1, flow: 0.6, spacing: 0.1,
      roundness: 1, angle: 0, scatter: 0, smoothing: 0.4, tip: "round", smudge: 0.65,
    };
    this.fgColor = "#ece9e2";
    this.bgColor = "#1b1b1b";
    this.material = {
      paintAlbedo: true,
      paintMetal: false, metal: 0.0,
      paintRough: false, rough: 0.5,
      paintHeight: false, height: 0.5,
      paintEmissive: false, emissive: 0.0, emissiveColor: "#ffffff",
    };
    this.symmetry = "none";
    this.pressureSize = true;
    this.pressureOpacity = true;
    this.shapeKind = "rect";
    this.shapeFilled = true;
    this.fillTolerance = 24;
    this.fillContiguous = true;
    this.strokeSeed = 1;
    this.resetStroke();
    this.stampCache = { key: "", canvas: null };
  }

  resetStroke() {
    this.painting = false;
    this.doc = null;
    this.layer = null;
    this.capture = null;
    this.dirty = null;
    this.smooth = null;
    this.lastDab = null;
    this.smudgeHold = null;
    this.shapeStart = null;
    this.shapeNow = null;
  }

  /* ---------------- Stroke lifecycle ---------------- */

  strokeTargets() {
    // Which layer channels this stroke will write.
    if (this.layer && this.layer.maskSelected && this.layer.channels.mask) return ["mask"];
    if (this.tool === "smudge") return ["albedo"];
    if (this.tool === "eraser") {
      const Keys = ["albedo"];
      for (const Key of ["metal", "rough", "emissive", "height"]) {
        if (this.layer && this.layer.channels[Key]) Keys.push(Key);
      }
      return Keys;
    }
    const Keys = [];
    if (this.material.paintAlbedo) Keys.push("albedo");
    if (this.material.paintMetal) Keys.push("metal");
    if (this.material.paintRough) Keys.push("rough");
    if (this.material.paintHeight) Keys.push("height");
    if (this.material.paintEmissive) Keys.push("emissive");
    return Keys.length ? Keys : ["albedo"];
  }

  beginStroke(Doc, Layer, X, Y, Pressure = 1) {
    if (!Doc || !Layer || Layer.locked) return false;
    if (Layer.kind !== "paint") return false;
    this.doc = Doc;
    this.layer = Layer;
    this.strokeSeed = (Math.random() * 1e9) | 0;
    const Targets = this.tool === "shape" ? this.strokeTargets() : this.strokeTargets();
    this.capture = {};
    for (const Key of Targets) {
      const Existing = Layer.channels[Key];
      const Clone = makeCanvas(Doc.width, Doc.height);
      if (Existing) Clone.getContext("2d").drawImage(Existing, 0, 0);
      else if (Key === "albedo") {
        Layer.channels.albedo = makeCanvas(Doc.width, Doc.height);
      }
      this.capture[Key] = Clone;
    }
    // Lazily allocate painted scalar channels now (clone already holds blank).
    for (const Key of Targets) {
      if (!Layer.channels[Key]) Layer.channels[Key] = makeCanvas(Doc.width, Doc.height);
    }
    this.dirty = null;
    this.smooth = { x: X, y: Y };
    this.lastDab = null;
    this.smudgeHold = null;
    if (this.tool === "shape") {
      this.shapeStart = { x: X, y: Y };
      this.shapeNow = { x: X, y: Y };
      this.painting = true;
      return true;
    }
    if (this.tool === "smudge") this.primeSmudge(X, Y);
    this.painting = true;
    this.dabAt(X, Y, Pressure);
    return true;
  }

  strokeTo(X, Y, Pressure = 1) {
    if (!this.painting) return;
    if (this.tool === "shape") {
      this.shapeNow = { x: X, y: Y };
      return;
    }
    const Amount = 1 - Math.min(0.92, Math.max(0, this.brush.smoothing) * 0.9);
    this.smooth.x += (X - this.smooth.x) * Amount;
    this.smooth.y += (Y - this.smooth.y) * Amount;
    const Size = this.brush.size;
    const Step = Math.max(1, Size * Math.max(0.02, this.brush.spacing));
    let Cursor = this.lastDab || { x: this.smooth.x, y: this.smooth.y };
    // First dab of the move is already down; walk from last dab toward smooth.
    const Dx = this.smooth.x - Cursor.x;
    const Dy = this.smooth.y - Cursor.y;
    const Distance = Math.hypot(Dx, Dy);
    if (Distance < Step && this.lastDab) {
      // Still stamp lightly so slow moves build up with flow.
      if (this.tool === "smudge") this.smudgeDab(this.smooth.x, this.smooth.y);
      return;
    }
    const Steps = Math.max(1, Math.floor(Distance / Step));
    for (let Index = 1; Index <= Steps; Index++) {
      const T = Index / Steps;
      const Px = Cursor.x + Dx * T;
      const Py = Cursor.y + Dy * T;
      this.dabAt(Px, Py, Pressure);
    }
  }

  endStroke() {
    if (!this.painting) return null;
    if (this.tool === "shape" && this.shapeStart && this.shapeNow) {
      this.commitShape();
    }
    const Layer = this.layer;
    const Doc = this.doc;
    const Label = this.tool === "eraser" ? "Erase" : this.tool === "smudge" ? "Smudge" : this.tool === "shape" ? `Shape ${this.shapeKind}` : "Paint stroke";
    const Rect = this.dirty;
    const Capture = this.capture;
    this.resetStroke();
    if (Layer && Doc && Rect) {
      Layer.thumbDirty = true;
      Doc.markDirty();
      Doc.pushStrokeHistory(Label, Layer, Capture, Rect);
      return Label;
    }
    return null;
  }

  cancelStroke() {
    if (!this.painting || !this.layer || !this.capture) {
      this.resetStroke();
      return;
    }
    for (const [Key, Clone] of Object.entries(this.capture)) {
      const Live = this.layer.channels[Key];
      if (Live) {
        const Context = Live.getContext("2d");
        Context.save();
        Context.globalCompositeOperation = "source-over";
        Context.clearRect(0, 0, Live.width, Live.height);
        Context.drawImage(Clone, 0, 0);
        Context.restore();
      }
    }
    this.layer.thumbDirty = true;
    if (this.doc) this.doc.markDirty();
    this.resetStroke();
  }

  /* ---------------- Dabs ---------------- */

  mirrorPoints(X, Y) {
    const Doc = this.doc;
    const Points = [[X, Y]];
    if (this.symmetry === "x" || this.symmetry === "quad") Points.push([Doc.width - X, Y]);
    if (this.symmetry === "y" || this.symmetry === "quad") Points.push([X, Doc.height - Y]);
    if (this.symmetry === "quad") Points.push([Doc.width - X, Doc.height - Y]);
    return Points;
  }

  dabAt(X, Y, Pressure = 1) {
    // Smudge has no meaning on a mask — fall back to reveal painting.
    if (this.tool === "smudge" && this.layer && this.layer.maskSelected && this.layer.channels.mask) {
      const Points = this.mirrorPoints(X, Y);
      for (const [Px, Py] of Points) this.paintDab(Px, Py, Pressure);
      this.lastDab = { x: X, y: Y };
      return;
    }
    const Points = this.mirrorPoints(X, Y);
    for (const [Px, Py] of Points) {
      if (this.tool === "smudge") this.smudgeDab(Px, Py);
      else this.paintDab(Px, Py, Pressure);
    }
    this.lastDab = { x: X, y: Y };
  }

  dabAlpha(Pressure) {
    const Flow = this.brush.flow;
    const Opacity = this.brush.opacity;
    const P = this.pressureOpacity ? 0.25 + 0.75 * Pressure : 1;
    return Math.max(0, Math.min(1, Flow * Opacity * P));
  }

  dabSize(Pressure) {
    const P = this.pressureSize ? 0.3 + 0.7 * Pressure : 1;
    return Math.max(1, this.brush.size * P);
  }

  paintDab(X, Y, Pressure) {
    const Layer = this.layer;
    const Doc = this.doc;
    const Size = this.dabSize(Pressure);
    const Alpha = this.dabAlpha(Pressure);
    if (Alpha <= 0.001) return;
    const Scatter = this.brush.scatter * Size * 0.5;
    const Jx = Scatter ? (Math.random() * 2 - 1) * Scatter : 0;
    const Jy = Scatter ? (Math.random() * 2 - 1) * Scatter : 0;
    const Targets = this.strokeTargets();
    for (const Key of Targets) {
      const Canvas = Layer.channels[Key];
      if (!Canvas) continue;
      const Context = Canvas.getContext("2d");
      Context.save();
      if (this.tool === "eraser") {
        Context.globalCompositeOperation = "destination-out";
        this.stamp(Context, X + Jx, Y + Jy, Size, Alpha, [255, 255, 255]);
      } else if (Key === "mask") {
        Context.globalCompositeOperation = "source-over";
        this.stamp(Context, X + Jx, Y + Jy, Size, Alpha, [255, 255, 255]);
      } else if (Key === "albedo") {
        Context.globalCompositeOperation = "source-over";
        this.stamp(Context, X + Jx, Y + Jy, Size, Alpha, hexToRgb(this.fgColor));
      } else if (Key === "metal") {
        const Gray = Math.round(this.material.metal * 255);
        Context.globalCompositeOperation = "source-over";
        this.stamp(Context, X + Jx, Y + Jy, Size, Alpha, [Gray, Gray, Gray]);
      } else if (Key === "rough") {
        const Gray = Math.round(this.material.rough * 255);
        Context.globalCompositeOperation = "source-over";
        this.stamp(Context, X + Jx, Y + Jy, Size, Alpha, [Gray, Gray, Gray]);
      } else if (Key === "height") {
        const Gray = Math.round(this.material.height * 255);
        Context.globalCompositeOperation = "source-over";
        this.stamp(Context, X + Jx, Y + Jy, Size, Alpha, [Gray, Gray, Gray]);
      } else if (Key === "emissive") {
        const [R, G, B] = hexToRgb(this.material.emissiveColor);
        const Strength = Math.min(1, this.material.emissive);
        Context.globalCompositeOperation = "source-over";
        this.stamp(Context, X + Jx, Y + Jy, Size, Alpha * Math.max(0.01, Strength), [R, G, B]);
      }
      Context.restore();
    }
    this.growDirty(X + Jx, Y + Jy, Size);
    Layer.thumbDirty = true;
    Doc.markDirty(false);
    Doc.dirty = true;
  }

  stamp(Context, X, Y, Size, Alpha, Rgb) {
    const Stamp = this.getStamp(Size);
    Context.save();
    Context.globalAlpha = Alpha;
    Context.translate(X, Y);
    Context.rotate(((this.brush.angle || 0) * Math.PI) / 180 + (this.brush.tip === "chalk" ? Math.random() * Math.PI : 0));
    Context.scale(1, Math.max(0.05, this.brush.roundness));
    if (this.brush.tip === "square") {
      const Half = Size / 2;
      const [R, G, B] = Rgb;
      Context.fillStyle = `rgba(${R},${G},${B},1)`;
      // Hardness softens square edges via shadow-free inset gradient: approximate
      // with nested fills.
      Context.fillRect(-Half, -Half, Size, Size);
      if (this.brush.hardness < 0.95) {
        const Inset = Half * (1 - this.brush.hardness) * 0.9;
        Context.globalAlpha = Alpha * 0.35;
        Context.fillRect(-Half - Inset * 0.4, -Half - Inset * 0.4, Size + Inset * 0.8, Size + Inset * 0.8);
      }
    } else {
      // Tint the white stamp via offscreen multiply, then draw.
      const Tinted = this.tintStamp(Stamp, Rgb);
      Context.drawImage(Tinted, -Size / 2, -Size / 2, Size, Size);
    }
    Context.restore();
  }

  getStamp(Size) {
    const Key = [Math.round(Size), this.brush.hardness.toFixed(3), this.brush.tip, this.strokeSeed % 97].join("|");
    if (this.stampCache.key === Key && this.stampCache.canvas) return this.stampCache.canvas;
    const Diameter = Math.max(2, Math.ceil(Size));
    const Canvas = makeCanvas(Diameter, Diameter);
    const Context = Canvas.getContext("2d");
    const Radius = Diameter / 2;
    const Hard = Math.max(0, Math.min(1, this.brush.hardness));
    if (this.brush.tip === "chalk") {
      // Speckled grain stamp.
      const Image = Context.createImageData(Diameter, Diameter);
      let Seed = this.strokeSeed || 1;
      const Random = () => {
        Seed = (Seed * 1664525 + 1013904223) | 0;
        return ((Seed >>> 9) & 0xffff) / 0xffff;
      };
      for (let Y = 0; Y < Diameter; Y++) {
        for (let X = 0; X < Diameter; X++) {
          const Dx = (X - Radius + 0.5) / Radius;
          const Dy = (Y - Radius + 0.5) / Radius;
          const Distance = Math.hypot(Dx, Dy);
          if (Distance > 1) continue;
          const Falloff = Distance <= Hard ? 1 : 1 - (Distance - Hard) / Math.max(0.001, 1 - Hard);
          const Grain = Random() < 0.35 + 0.5 * Falloff ? 1 : 0;
          const Alpha = Math.round(255 * Falloff * Grain * (0.55 + 0.45 * Random()));
          const Index = (Y * Diameter + X) * 4;
          Image.data[Index] = 255;
          Image.data[Index + 1] = 255;
          Image.data[Index + 2] = 255;
          Image.data[Index + 3] = Alpha;
        }
      }
      Context.putImageData(Image, 0, 0);
    } else {
      const Gradient = Context.createRadialGradient(Radius, Radius, 0, Radius, Radius, Radius);
      const Stop = Math.max(0.02, Math.min(0.98, Hard));
      Gradient.addColorStop(0, "rgba(255,255,255,1)");
      Gradient.addColorStop(Stop, "rgba(255,255,255,1)");
      Gradient.addColorStop(1, "rgba(255,255,255,0)");
      Context.fillStyle = Gradient;
      Context.fillRect(0, 0, Diameter, Diameter);
    }
    this.stampCache = { key: Key, canvas: Canvas };
    return Canvas;
  }

  tintStamp(Stamp, Rgb) {
    const Key = `${Rgb.join(",")}`;
    if (this.stampCache.tintKey === Key && this.stampCache.tinted) return this.stampCache.tinted;
    const Canvas = makeCanvas(Stamp.width, Stamp.height);
    const Context = Canvas.getContext("2d");
    Context.drawImage(Stamp, 0, 0);
    Context.globalCompositeOperation = "source-in";
    Context.fillStyle = `rgb(${Rgb[0]},${Rgb[1]},${Rgb[2]})`;
    Context.fillRect(0, 0, Canvas.width, Canvas.height);
    this.stampCache.tintKey = Key;
    this.stampCache.tinted = Canvas;
    return Canvas;
  }

  growDirty(X, Y, Size) {
    const Radius = Size / 2 + 2;
    const Box = { x: X - Radius, y: Y - Radius, w: Radius * 2, h: Radius * 2 };
    if (!this.dirty) this.dirty = Box;
    else {
      const MinX = Math.min(this.dirty.x, Box.x);
      const MinY = Math.min(this.dirty.y, Box.y);
      const MaxX = Math.max(this.dirty.x + this.dirty.w, Box.x + Box.w);
      const MaxY = Math.max(this.dirty.y + this.dirty.h, Box.y + Box.h);
      this.dirty = { x: MinX, y: MinY, w: MaxX - MinX, h: MaxY - MinY };
    }
  }

  /* ---------------- Smudge ---------------- */

  primeSmudge(X, Y) {
    const Size = Math.max(2, Math.ceil(this.brush.size));
    this.smudgeHold = makeCanvas(Size, Size);
    const Context = this.smudgeHold.getContext("2d");
    Context.clearRect(0, 0, Size, Size);
    Context.drawImage(this.layer.channels.albedo, X - Size / 2, Y - Size / 2, Size, Size, 0, 0, Size, Size);
  }

  smudgeDab(X, Y) {
    const Layer = this.layer;
    const Doc = this.doc;
    const Size = Math.max(2, this.brush.size);
    const Strength = Math.max(0.05, Math.min(1, this.brush.smudge * this.brush.flow * 1.4));
    const Target = Layer.channels.albedo.getContext("2d");
    Target.save();
    Target.globalCompositeOperation = "source-over";
    Target.globalAlpha = Strength * this.brush.opacity;
    // Deposit the held paint with a soft mask.
    Target.save();
    Target.translate(X, Y);
    Target.scale(1, Math.max(0.05, this.brush.roundness));
    const Mask = this.getStamp(Size);
    Target.drawImage(this.smudgeHold, -Size / 2, -Size / 2, Size, Size);
    Target.globalCompositeOperation = "destination-in";
    Target.drawImage(Mask, -Size / 2, -Size / 2, Size, Size);
    Target.restore();
    Target.restore();
    // Pick up fresh paint under the cursor, mixed with the hold.
    const HoldContext = this.smudgeHold.getContext("2d");
    HoldContext.save();
    HoldContext.globalCompositeOperation = "source-over";
    HoldContext.globalAlpha = 1 - Strength * 0.5;
    HoldContext.drawImage(HoldContext.canvas, 0, 0);
    HoldContext.globalAlpha = 1;
    HoldContext.drawImage(Layer.channels.albedo, X - Size / 2, Y - Size / 2, Size, Size, 0, 0, Size, Size);
    HoldContext.restore();
    this.growDirty(X, Y, Size);
    Layer.thumbDirty = true;
    Doc.markDirty(false);
    Doc.dirty = true;
  }

  /* ---------------- Shapes ---------------- */

  get shapePreview() {
    if (!this.painting || this.tool !== "shape" || !this.shapeStart || !this.shapeNow) return null;
    return { kind: this.shapeKind, x0: this.shapeStart.x, y0: this.shapeStart.y, x1: this.shapeNow.x, y1: this.shapeNow.y };
  }

  commitShape() {
    const Preview = this.shapePreview;
    if (!Preview) return;
    const Layer = this.layer;
    const Doc = this.doc;
    const MinX = Math.min(Preview.x0, Preview.x1);
    const MinY = Math.min(Preview.y0, Preview.y1);
    const MaxX = Math.max(Preview.x0, Preview.x1);
    const MaxY = Math.max(Preview.y0, Preview.y1);
    const Pad = this.shapeKind === "line" ? this.brush.size : 2;
    const Targets = this.strokeTargets().filter((Key) => Key !== "mask");
    if (Layer.maskSelected && Layer.channels.mask) {
      drawShapeOn(Layer.channels.mask.getContext("2d"), Preview, "#ffffff", this.brush.size, this.shapeFilled, "source-over", 1);
      this.dirty = { x: MinX - Pad, y: MinY - Pad, w: MaxX - MinX + Pad * 2, h: MaxY - MinY + Pad * 2 };
      return;
    }
    for (const Key of Targets) {
      const Canvas = Layer.channels[Key];
      if (!Canvas) continue;
      const Context = Canvas.getContext("2d");
      const Opacity = this.tool === "eraser" ? 1 : this.brush.opacity;
      const Op = this.tool === "eraser" ? "destination-out" : "source-over";
      if (Key === "albedo") drawShapeOn(Context, Preview, this.fgColor, this.brush.size, this.shapeFilled, Op, Opacity);
      else if (Key === "metal") drawShapeOn(Context, Preview, gray(this.material.metal), this.brush.size, this.shapeFilled, Op, Opacity);
      else if (Key === "rough") drawShapeOn(Context, Preview, gray(this.material.rough), this.brush.size, this.shapeFilled, Op, Opacity);
      else if (Key === "height") drawShapeOn(Context, Preview, gray(this.material.height), this.brush.size, this.shapeFilled, Op, Opacity);
      else if (Key === "emissive") drawShapeOn(Context, Preview, this.material.emissiveColor, this.brush.size, this.shapeFilled, Op, Opacity * Math.min(1, Math.max(0.01, this.material.emissive)));
    }
    this.dirty = { x: MinX - Pad, y: MinY - Pad, w: MaxX - MinX + Pad * 2, h: MaxY - MinY + Pad * 2 };
    Doc.markDirty(false);
    Doc.dirty = true;
  }

  /* ---------------- Flood fill ---------------- */

  floodFill(Doc, Layer, X, Y) {
    if (!Doc || !Layer || Layer.locked || Layer.kind !== "paint") return 0;
    const Targets = this.strokeTargets().filter((Key) => Key !== "emissive" || true);
    const Albedo = Layer.channels.albedo;
    const Width = Doc.width;
    const Height = Doc.height;
    const StartX = Math.max(0, Math.min(Width - 1, Math.floor(X)));
    const StartY = Math.max(0, Math.min(Height - 1, Math.floor(Y)));
    const Data = Albedo.getContext("2d").getImageData(0, 0, Width, Height);
    const Pixels = Data.data;
    const StartIndex = (StartY * Width + StartX) * 4;
    const Target = [Pixels[StartIndex], Pixels[StartIndex + 1], Pixels[StartIndex + 2], Pixels[StartIndex + 3]];
    const Tolerance = Math.max(0, Math.min(100, this.fillTolerance)) * 2.55;
    const Matches = (Index) => {
      const Dr = Pixels[Index] - Target[0];
      const Dg = Pixels[Index + 1] - Target[1];
      const Db = Pixels[Index + 2] - Target[2];
      const Da = (Pixels[Index + 3] - Target[3]) * 1.5;
      return Math.sqrt(Dr * Dr + Dg * Dg + Db * Db + Da * Da) <= Tolerance;
    };
    const Filled = new Uint8Array(Width * Height);
    let MinX = Width;
    let MinY = Height;
    let MaxX = -1;
    let MaxY = -1;
    if (this.fillContiguous) {
      const Stack = [StartX, StartY];
      const Seen = new Uint8Array(Width * Height);
      while (Stack.length) {
        const Y0 = Stack.pop();
        const X0 = Stack.pop();
        if (X0 < 0 || Y0 < 0 || X0 >= Width || Y0 >= Height) continue;
        const Flat = Y0 * Width + X0;
        if (Seen[Flat]) continue;
        Seen[Flat] = 1;
        if (!Matches(Flat * 4)) continue;
        Filled[Flat] = 1;
        if (X0 < MinX) MinX = X0;
        if (Y0 < MinY) MinY = Y0;
        if (X0 > MaxX) MaxX = X0;
        if (Y0 > MaxY) MaxY = Y0;
        Stack.push(X0 + 1, Y0, X0 - 1, Y0, X0, Y0 + 1, X0, Y0 - 1);
      }
    } else {
      for (let Index = 0; Index < Width * Height; Index++) {
        if (!Matches(Index * 4)) continue;
        Filled[Index] = 1;
        const X0 = Index % Width;
        const Y0 = Math.floor(Index / Width);
        if (X0 < MinX) MinX = X0;
        if (Y0 < MinY) MinY = Y0;
        if (X0 > MaxX) MaxX = X0;
        if (Y0 > MaxY) MaxY = Y0;
      }
    }
    if (MaxX < 0) return 0;
    // Snapshot before-state for history.
    const Capture = {};
    for (const Key of Targets) {
      if (!Layer.channels[Key]) Layer.channels[Key] = makeCanvas(Width, Height);
      const Clone = makeCanvas(Width, Height);
      Clone.getContext("2d").drawImage(Layer.channels[Key], 0, 0);
      Capture[Key] = Clone;
    }
    // Build a region mask canvas, then stamp each channel through it.
    const MaskCanvas = makeCanvas(Width, Height);
    const MaskContext = MaskCanvas.getContext("2d");
    const MaskImage = MaskContext.createImageData(Width, Height);
    for (let Index = 0; Index < Width * Height; Index++) {
      if (Filled[Index]) {
        MaskImage.data[Index * 4 + 3] = 255;
        MaskImage.data[Index * 4] = 255;
        MaskImage.data[Index * 4 + 1] = 255;
        MaskImage.data[Index * 4 + 2] = 255;
      }
    }
    MaskContext.putImageData(MaskImage, 0, 0);
    const ApplyThrough = (Canvas, Color, Alpha) => {
      const Context = Canvas.getContext("2d");
      const Temp = makeCanvas(Width, Height);
      const TempContext = Temp.getContext("2d");
      TempContext.fillStyle = Color;
      TempContext.globalAlpha = Alpha;
      TempContext.fillRect(0, 0, Width, Height);
      TempContext.globalCompositeOperation = "destination-in";
      TempContext.globalAlpha = 1;
      TempContext.drawImage(MaskCanvas, 0, 0);
      Context.save();
      Context.globalCompositeOperation = "source-over";
      Context.drawImage(Temp, 0, 0);
      Context.restore();
    };
    for (const Key of Targets) {
      if (Key === "albedo") ApplyThrough(Layer.channels.albedo, this.fgColor, this.brush.opacity);
      else if (Key === "mask") ApplyThrough(Layer.channels.mask, "#ffffff", this.brush.opacity);
      else if (Key === "metal") ApplyThrough(Layer.channels.metal, gray(this.material.metal), this.brush.opacity);
      else if (Key === "rough") ApplyThrough(Layer.channels.rough, gray(this.material.rough), this.brush.opacity);
      else if (Key === "height") ApplyThrough(Layer.channels.height, gray(this.material.height), this.brush.opacity);
      else if (Key === "emissive") ApplyThrough(Layer.channels.emissive, this.material.emissiveColor, this.brush.opacity);
    }
    Layer.thumbDirty = true;
    Doc.markDirty();
    Doc.pushStrokeHistory("Fill", Layer, Capture, { x: MinX, y: MinY, w: MaxX - MinX + 1, h: MaxY - MinY + 1 });
    return (MaxX - MinX + 1) * (MaxY - MinY + 1);
  }

  /* ---------------- Eyedropper ---------------- */

  sample(Doc, X, Y) {
    if (!Doc) return null;
    Doc.renderComposite();
    const StartX = Math.max(0, Math.min(Doc.width - 1, Math.floor(X)));
    const StartY = Math.max(0, Math.min(Doc.height - 1, Math.floor(Y)));
    const Read = (Canvas) => {
      const Data = Canvas.getContext("2d").getImageData(StartX, StartY, 1, 1).data;
      return [Data[0], Data[1], Data[2], Data[3]];
    };
    const Albedo = Read(Doc.composite.albedo);
    const Metal = Read(Doc.composite.metallic)[0] / 255;
    const Rough = Read(Doc.composite.roughness)[0] / 255;
    const Height = Read(Doc.composite.height)[0] / 255;
    const Emissive = Read(Doc.composite.emissive);
    return {
      color: `#${[Albedo[0], Albedo[1], Albedo[2]].map((C) => C.toString(16).padStart(2, "0")).join("")}`,
      alpha: Albedo[3] / 255,
      metal: Metal,
      rough: Rough,
      height: Height,
      emissive: Emissive,
    };
  }
}

function gray(Value) {
  const Gray = Math.round(Math.max(0, Math.min(1, Value)) * 255);
  return `rgb(${Gray},${Gray},${Gray})`;
}

function drawShapeOn(Context, Preview, Color, BrushSize, Filled, Op, Alpha) {
  const X0 = Math.min(Preview.x0, Preview.x1);
  const Y0 = Math.min(Preview.y0, Preview.y1);
  const W = Math.abs(Preview.x1 - Preview.x0);
  const H = Math.abs(Preview.y1 - Preview.y0);
  Context.save();
  Context.globalCompositeOperation = Op;
  Context.globalAlpha = Alpha;
  Context.fillStyle = Color;
  Context.strokeStyle = Color;
  Context.lineWidth = Math.max(1, BrushSize * 0.25);
  Context.lineCap = "round";
  Context.lineJoin = "round";
  if (Preview.kind === "line") {
    Context.lineWidth = Math.max(1, BrushSize * 0.5);
    Context.beginPath();
    Context.moveTo(Preview.x0, Preview.y0);
    Context.lineTo(Preview.x1, Preview.y1);
    Context.stroke();
  } else if (Preview.kind === "ellipse") {
    Context.beginPath();
    Context.ellipse(X0 + W / 2, Y0 + H / 2, Math.max(0.5, W / 2), Math.max(0.5, H / 2), 0, 0, Math.PI * 2);
    if (Filled) Context.fill();
    else Context.stroke();
  } else {
    if (Filled) Context.fillRect(X0, Y0, Math.max(1, W), Math.max(1, H));
    else Context.strokeRect(X0, Y0, Math.max(1, W), Math.max(1, H));
  }
  Context.restore();
}
