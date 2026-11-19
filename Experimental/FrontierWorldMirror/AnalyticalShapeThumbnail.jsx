import React from "react";

// Asset thumbnails are intentionally declarative glyphs. The live World Mirror
// renderer is GPU-only and no background CPU raytracer is imported to decorate
// the editor chrome.
const ShapeGlyph = ({ PropId, Shape }) => {
  const Key = `${PropId || ""} ${Shape || ""}`.toLowerCase();
  if (Key.includes("vehicle") || Key.includes("car")) return "▱";
  if (Key.includes("camera")) return "◉";
  if (Key.includes("light")) return "✦";
  if (Key.includes("cone")) return "▲";
  if (Key.includes("sphere")) return "●";
  if (Key.includes("cylinder")) return "▯";
  if (Key.includes("torus")) return "⊙";
  if (Key.includes("box") || Key.includes("cube")) return "◆";
  return "◇";
};

export function AnalyticalShapeThumbnail({
  PropId = null,
  Shape = null,
  Size = 48,
  ClassName = "",
  Style = undefined,
}) {
  return (
    <span
      aria-hidden="true"
      className={"analytical-thumb-glyph " + ClassName}
      data-thumb-shape={PropId || Shape || "asset"}
      style={{
        display: "inline-flex",
        alignItems: "center",
        justifyContent: "center",
        width: Size,
        height: Size,
        background: "transparent",
        color: "rgba(220,224,232,0.72)",
        fontSize: Math.max(10, Math.round(Size * 0.48)),
        fontWeight: 300,
        lineHeight: 1,
        flexShrink: 0,
        ...Style,
      }}
    >
      <ShapeGlyph PropId={PropId} Shape={Shape} />
    </span>
  );
}

export default AnalyticalShapeThumbnail;
