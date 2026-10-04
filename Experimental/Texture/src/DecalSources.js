// Rasterizes SVG and text decal sources into canvases for upload. SVGs are
// decoded through <img>, which sandboxes scripts and external resources.

export const SAMPLE_SVGS = [
  {
    id: "badge",
    name: "Frontier badge",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200"><circle cx="100" cy="100" r="92" fill="none" stroke="#f2f2f2" stroke-width="10"/><circle cx="100" cy="100" r="72" fill="#f2f2f2"/><path d="M107 42c5 30-27 30-12 53-15-4-15-19-15-19-32 46 42 73 55 27 4-23-12-46-28-61" fill="#e0603a"/><text x="100" y="186" font-family="sans-serif" font-size="15" font-weight="700" text-anchor="middle" fill="#f2f2f2" letter-spacing="4">FRONTIER</text></svg>`,
  },
  {
    id: "hazard",
    name: "Hazard stripes",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 400 100"><defs><pattern id="h" width="50" height="100" patternUnits="userSpaceOnUse" patternTransform="skewX(-35)"><rect width="25" height="100" fill="#f5c400"/><rect x="25" width="25" height="100" fill="#151515"/></pattern></defs><rect width="400" height="100" rx="6" fill="url(#h)"/></svg>`,
  },
  {
    id: "star",
    name: "Star",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><path d="m50 4 13.5 30 32.5 3.5-24.3 22 6.9 32L50 75.2 21.4 91.5l6.9-32L4 37.5 36.5 34Z" fill="#ffffff"/></svg>`,
  },
  {
    id: "arrow",
    name: "Direction arrow",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 100"><path d="M10 35h110V8l72 42-72 42V65H10Z" fill="#ffffff"/></svg>`,
  },
  {
    id: "barcode",
    name: "Serial barcode",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 240 110"><rect width="240" height="110" rx="6" fill="#f4f4f0"/>${[3,1,2,1,1,3,2,1,1,2,3,1,1,1,2,2,1,3,1,2,1,1,2,3,1,2,1,1,3,1,2,1,1,2,2,3,1,1,2,1]
      .reduce((Acc, W, I) => { const X = Acc.X; Acc.S += I % 2 === 0 ? `<rect x="${X}" y="10" width="${W * 2.2}" height="68" fill="#111"/>` : ""; Acc.X += W * 2.2; return Acc; }, { X: 18, S: "" }).S}<text x="120" y="98" font-family="monospace" font-size="14" text-anchor="middle" fill="#111" letter-spacing="3">FX-2026-0042</text></svg>`,
  },
  {
    id: "vent",
    name: "Vent grille",
    svg: `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 120"><rect x="4" y="4" width="192" height="112" rx="16" fill="none" stroke="#fff" stroke-width="6"/>${[0,1,2,3,4,5].map((K) => `<rect x="26" y="${20 + K * 14}" width="148" height="7" rx="3.5" fill="#fff"/>`).join("")}</svg>`,
  },
];

export const FONT_OPTIONS = [
  { id: "DM Sans", label: "DM Sans" },
  { id: "system-ui", label: "System UI" },
  { id: "Georgia, serif", label: "Serif" },
  { id: "ui-monospace, monospace", label: "Monospace" },
  { id: "Impact, 'Arial Black', sans-serif", label: "Impact / Black" },
  { id: "'Brush Script MT', cursive", label: "Script" },
];

export const DEFAULT_DECAL = Object.freeze({
  Source: "text",
  Svg: SAMPLE_SVGS[0].svg,
  SvgName: SAMPLE_SVGS[0].name,
  Text: "FRONTIER",
  Font: "DM Sans",
  Weight: 700,
  Italic: false,
  Align: "center",
  LetterSpacing: 0.08,
  LineHeight: 1.1,
  TextColor: "#ffffff",
  Outline: 0,
  OutlineColor: "#000000",
  Mapping: "projected",
  Position: [0, 0, 1],
  Normal: [0, 0, 1],
  Up: [0, 1, 0],
  Rotation: 0,
  Size: 0.9,
  Stretch: 1,
  Depth: 0.25,
  AngleLimit: 75,
  TwoSided: false,
  FlipX: false,
  FlipY: false,
  UvCenter: [0.5, 0.5],
  UvSize: 0.3,
  SourceColor: true,
});

export function SanitizeDecal(Input = {}) {
  const D = structuredClone(DEFAULT_DECAL);
  const Number_ = (Key, Low, High) => {
    const V = Number(Input[Key]);
    if (Number.isFinite(V)) D[Key] = Math.min(High, Math.max(Low, V));
  };
  const Vector = (Key, Length) => {
    if (Array.isArray(Input[Key]) && Input[Key].length === Length && Input[Key].every(Number.isFinite)) D[Key] = [...Input[Key]];
  };
  if (Input.Source === "svg" || Input.Source === "text") D.Source = Input.Source;
  if (typeof Input.Svg === "string" && Input.Svg.length < 4_000_000) D.Svg = Input.Svg;
  if (typeof Input.SvgName === "string") D.SvgName = Input.SvgName.slice(0, 80);
  if (typeof Input.Text === "string") D.Text = Input.Text.slice(0, 400);
  if (typeof Input.Font === "string") D.Font = Input.Font.slice(0, 120);
  Number_("Weight", 100, 900);
  if (Input.Italic !== undefined) D.Italic = Boolean(Input.Italic);
  if (["left", "center", "right"].includes(Input.Align)) D.Align = Input.Align;
  Number_("LetterSpacing", -0.2, 1);
  Number_("LineHeight", 0.6, 3);
  for (const Key of ["TextColor", "OutlineColor"])
    if (typeof Input[Key] === "string" && /^#[0-9a-f]{6}$/i.test(Input[Key])) D[Key] = Input[Key];
  Number_("Outline", 0, 0.3);
  if (Input.Mapping === "uv" || Input.Mapping === "projected") D.Mapping = Input.Mapping;
  Vector("Position", 3);
  Vector("Normal", 3);
  Vector("Up", 3);
  Vector("UvCenter", 2);
  Number_("Rotation", -180, 180);
  Number_("Size", 0.02, 4);
  Number_("Stretch", 0.1, 10);
  Number_("Depth", 0.01, 2);
  Number_("AngleLimit", 1, 180);
  Number_("UvSize", 0.01, 2);
  for (const Key of ["TwoSided", "FlipX", "FlipY", "SourceColor"])
    if (Input[Key] !== undefined) D[Key] = Boolean(Input[Key]);
  return D;
}

export const DecalSignature = (D) =>
  D.Source === "svg"
    ? `svg:${D.Svg.length}:${HashString(D.Svg)}`
    : `text:${HashString(JSON.stringify([D.Text, D.Font, D.Weight, D.Italic, D.Align, D.LetterSpacing, D.LineHeight, D.TextColor, D.Outline, D.OutlineColor]))}`;

function HashString(Text) {
  let H = 2166136261;
  for (let K = 0; K < Text.length; K++) {
    H ^= Text.charCodeAt(K);
    H = Math.imul(H, 16777619);
  }
  return (H >>> 0).toString(36);
}

// Returns width/height from the SVG's viewBox or size attributes.
export function SvgDimensions(Markup) {
  const Document_ = new DOMParser().parseFromString(Markup, "image/svg+xml");
  const Root = Document_.documentElement;
  if (!Root || Root.nodeName.toLowerCase() !== "svg" || Document_.querySelector("parsererror"))
    throw new Error("That file is not a valid SVG document.");
  const ViewBox = (Root.getAttribute("viewBox") || "").trim().split(/[\s,]+/).map(Number);
  let Width = parseFloat(Root.getAttribute("width"));
  let Height = parseFloat(Root.getAttribute("height"));
  if (ViewBox.length === 4 && ViewBox.every(Number.isFinite) && ViewBox[2] > 0 && ViewBox[3] > 0) {
    if (!Width || !Height) { Width = ViewBox[2]; Height = ViewBox[3]; }
  }
  if (!Width || !Height) { Width = 300; Height = 150; }
  return { Width, Height, Root };
}

export async function RasterizeSvg(Markup, Longest = 1024) {
  const { Width, Height, Root } = SvgDimensions(Markup);
  if (!Root.getAttribute("xmlns")) Root.setAttribute("xmlns", "http://www.w3.org/2000/svg");
  if (!Root.getAttribute("viewBox")) Root.setAttribute("viewBox", `0 0 ${Width} ${Height}`);
  const Scale = Longest / Math.max(Width, Height);
  const W = Math.max(8, Math.round(Width * Scale));
  const H = Math.max(8, Math.round(Height * Scale));
  Root.setAttribute("width", W);
  Root.setAttribute("height", H);
  const Serialized = new XMLSerializer().serializeToString(Root);
  const Url = URL.createObjectURL(new Blob([Serialized], { type: "image/svg+xml" }));
  try {
    const Image_ = new Image();
    Image_.decoding = "async";
    await new Promise((Resolve, Reject) => {
      Image_.onload = Resolve;
      Image_.onerror = () => Reject(new Error("The SVG could not be rendered by the browser."));
      Image_.src = Url;
    });
    const Canvas = document.createElement("canvas");
    Canvas.width = W;
    Canvas.height = H;
    Canvas.getContext("2d").drawImage(Image_, 0, 0, W, H);
    return Canvas;
  } finally {
    URL.revokeObjectURL(Url);
  }
}

export async function RasterizeText(D) {
  const Size = 180;
  const Style = `${D.Italic ? "italic " : ""}${D.Weight} ${Size}px ${D.Font.includes(",") || D.Font.includes("'") ? D.Font : `"${D.Font}"`}`;
  try { await document.fonts.load(Style, D.Text || "A"); } catch {}
  const Lines = (D.Text || " ").split("\n");
  const Measure = document.createElement("canvas").getContext("2d");
  Measure.font = Style;
  const Spacing = D.LetterSpacing * Size;
  const LineWidth = (Line) => {
    let Width = 0;
    for (const Character of Line) Width += Measure.measureText(Character).width + Spacing;
    return Math.max(1, Width - Spacing);
  };
  const Widths = Lines.map(LineWidth);
  const Outline = D.Outline * Size;
  const Pad = Math.ceil(Size * 0.12 + Outline);
  const LineStep = Size * D.LineHeight;
  let Width = Math.ceil(Math.max(...Widths) + Pad * 2);
  let Height = Math.ceil(LineStep * (Lines.length - 1) + Size * 1.18 + Pad * 2);
  const Scale = Math.min(1, 2048 / Width, 2048 / Height);
  const Canvas = document.createElement("canvas");
  Canvas.width = Math.max(8, Math.round(Width * Scale));
  Canvas.height = Math.max(8, Math.round(Height * Scale));
  const C = Canvas.getContext("2d");
  C.scale(Scale, Scale);
  C.font = Style;
  C.textBaseline = "alphabetic";
  C.lineJoin = "round";
  Lines.forEach((Line, Index) => {
    const LineWidth_ = Widths[Index];
    let X = D.Align === "left" ? Pad : D.Align === "right" ? Width - Pad - LineWidth_ : (Width - LineWidth_) / 2;
    const Y = Pad + Size * 0.92 + Index * LineStep;
    for (const Character of Line) {
      if (Outline > 0) {
        C.strokeStyle = D.OutlineColor;
        C.lineWidth = Outline * 2;
        C.strokeText(Character, X, Y);
      }
      C.fillStyle = D.TextColor;
      C.fillText(Character, X, Y);
      X += Measure.measureText(Character).width + Spacing;
    }
  });
  return Canvas;
}

export const RasterizeDecal = (D) => (D.Source === "svg" ? RasterizeSvg(D.Svg) : RasterizeText(D));
