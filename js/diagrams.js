//============================================================================================================================================
//  DIAGRAMS.JS — INTERACTIVE SVG INSTRUMENTS FOR THE PROJECT-ZERO INSPECTOR CARDS
//============================================================================================================================================

const SVG_NS = "http://www.w3.org/2000/svg";

function svgEl(tag, attrs = {}, ...kids) {
  const node = document.createElementNS(SVG_NS, tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v !== undefined && v !== null) node.setAttribute(k, String(v));
  }
  for (const kid of kids) {
    if (!kid) continue;
    node.append(typeof kid === "string" ? document.createTextNode(kid) : kid);
  }
  return node;
}

// 1. Live 64-Bin Hypsometric Elevation Histogram SVG
export function buildElevationHistogramSVG(stats, env) {
  const W = 340;
  const H = 96;
  const padL = 8;
  const padR = 8;
  const padT = 12;
  const padB = 20;
  const plotW = W - padL - padR;
  const plotH = H - padT - padB;

  const hist = stats.histogram || new Float32Array(64);
  const pts = [];
  for (let i = 0; i < hist.length; i++) {
    const x = padL + (i / (hist.length - 1)) * plotW;
    const y = padT + plotH - Math.max(0.02, hist[i]) * plotH;
    pts.push(`${x.toFixed(1)},${y.toFixed(1)}`);
  }

  const areaPath = `M ${padL},${padT + plotH} L ${pts.join(" L ")} L ${padL + plotW},${padT + plotH} Z`;
  const linePath = `M ${pts.join(" L ")}`;

  const span = Math.max(100, stats.maxElev - stats.minElev);
  const waterFrac = Math.max(0, Math.min(1, ((env.waterLevelM ?? 145) - stats.minElev) / span));
  const waterX = padL + waterFrac * plotW;

  const svg = svgEl(
    "svg",
    { viewBox: `0 0 ${W} ${H}` },
    svgEl(
      "defs",
      {},
      svgEl(
        "linearGradient",
        { id: "histGrad", x1: "0", y1: "0", x2: "0", y2: "1" },
        svgEl("stop", { offset: "0%", "stop-color": "#ffb454", "stop-opacity": "0.52" }),
        svgEl("stop", { offset: "100%", "stop-color": "#ffb454", "stop-opacity": "0.04" })
      )
    ),
    svgEl("line", { x1: padL, y1: padT + plotH, x2: padL + plotW, y2: padT + plotH, stroke: "#2c2c2c" }),
    svgEl("path", { d: areaPath, fill: "url(#histGrad)" }),
    svgEl("path", { d: linePath, fill: "none", stroke: "#ffb454", "stroke-width": "1.6" }),
    env.waterEnabled
      ? svgEl("line", {
          x1: waterX.toFixed(1),
          y1: padT,
          x2: waterX.toFixed(1),
          y2: padT + plotH,
          stroke: "#38b2ac",
          "stroke-dasharray": "3 2",
          "stroke-width": "1.2",
        })
      : null,
    svgEl("text", { x: padL, y: H - 5, fill: "#7a7a7a", "font-size": "9" }, `${stats.minElev} m`),
    svgEl("text", { x: W * 0.5, y: H - 5, fill: "#9e9e9e", "font-size": "9", "text-anchor": "middle" }, `Mean ${stats.meanElev} m`),
    svgEl("text", { x: W - padR, y: H - 5, fill: "#d0d0d0", "font-size": "9", "text-anchor": "end" }, `Peak ${stats.maxElev} m`)
  );
  return svg;
}

// 2. Procedural Mask Altitude & Slope Transfer Window SVG
export function buildMaskWindowSVG(layer) {
  const W = 340;
  const H = 84;
  const padX = 10;
  const padT = 12;
  const padB = 20;
  const plotW = W - padX * 2;
  const plotH = H - padT - padB;

  const altMin = layer.altMin ?? layer.maskAltMin ?? 0;
  const altMax = layer.altMax ?? layer.maskAltMax ?? 3000;
  const feather = layer.altFeather ?? 160;
  const useAlt = layer.useAltMask ?? true;

  const slopeMin = layer.slopeMin ?? layer.maskSlopeMin ?? 0;
  const slopeMax = layer.slopeMax ?? layer.maskSlopeMax ?? 90;
  const sFeather = layer.slopeFeather ?? 8;
  const useSlope = layer.useSlopeMask ?? true;

  const smooth = (a, b, x) => {
    const t = Math.max(0, Math.min(1, (x - a) / (b - a || 1e-5)));
    return t * t * (3 - 2 * t);
  };

  const altPts = [];
  const slopePts = [];
  const N = 64;
  for (let i = 0; i <= N; i++) {
    const t = i / N;
    const x = padX + t * plotW;

    const h = t * 3200;
    let wAlt = useAlt
      ? smooth(altMin - feather, altMin + feather, h) * (1 - smooth(altMax - feather, altMax + feather, h))
      : 1.0;
    if (layer.altInvert) wAlt = 1 - wAlt;
    const yAlt = padT + plotH - wAlt * plotH;
    altPts.push(`${x.toFixed(1)},${yAlt.toFixed(1)}`);

    const deg = t * 90;
    let wSlope = useSlope
      ? smooth(slopeMin - sFeather, slopeMin + sFeather, deg) * (1 - smooth(slopeMax - sFeather, slopeMax + sFeather, deg))
      : 1.0;
    if (layer.slopeInvert) wSlope = 1 - wSlope;
    const ySlope = padT + plotH - wSlope * plotH;
    slopePts.push(`${x.toFixed(1)},${ySlope.toFixed(1)}`);
  }

  return svgEl(
    "svg",
    { viewBox: `0 0 ${W} ${H}` },
    svgEl("line", { x1: padX, y1: padT + plotH, x2: padX + plotW, y2: padT + plotH, stroke: "#2c2c2c" }),
    svgEl("path", { d: `M ${altPts.join(" L ")}`, fill: "none", stroke: "#5bdb87", "stroke-width": "1.8" }),
    svgEl("path", {
      d: `M ${slopePts.join(" L ")}`,
      fill: "none",
      stroke: "#ffb454",
      "stroke-width": "1.6",
      "stroke-dasharray": "4 2",
    }),
    svgEl("text", { x: padX, y: H - 5, fill: "#5bdb87", "font-size": "9" }, `Altitude (${altMin}–${altMax}m)`),
    svgEl("text", { x: W - padX, y: H - 5, fill: "#ffb454", "font-size": "9", "text-anchor": "end" }, `Slope (${slopeMin}°–${slopeMax}°)`)
  );
}

// 3. Thermal Talus Repose Angle Instrument SVG
export function buildReposeAngleSVG(reposeAngle = 36) {
  const W = 340;
  const H = 86;
  const rad = (reposeAngle * Math.PI) / 180;
  const ox = 44;
  const oy = H - 16;
  const run = 135;
  const rise = Math.min(H - 26, Math.tan(rad) * run * 0.72);

  return svgEl(
    "svg",
    { viewBox: `0 0 ${W} ${H}` },
    svgEl("line", { x1: 20, y1: oy, x2: W - 20, y2: oy, stroke: "#333333" }),
    svgEl("polygon", {
      points: `${ox},${oy} ${ox + run},${oy} ${ox + run},${oy - rise}`,
      fill: "rgba(229, 154, 84, 0.22)",
      stroke: "#e59a54",
      "stroke-width": "1.5",
    }),
    svgEl("text", { x: ox + 36, y: oy - 6, fill: "#f0c38e", "font-size": "10" }, `θc = ${reposeAngle}°`),
    svgEl("text", { x: ox + run + 18, y: oy - rise * 0.5, fill: "#8c8c8c", "font-size": "9.5" }, "Critical Talus Cone")
  );
}

// 4. Celestial Sun Hemisphere Gizmo SVG
export function buildSunOrbitSVG(azimuthDeg, elevationDeg, onDragSun) {
  const W = 340;
  const H = 136;
  const cx = W * 0.5;
  const cy = H * 0.54;
  const rx = 98;
  const ry = 46;

  const az = (azimuthDeg * Math.PI) / 180;
  const el = (Math.max(0, Math.min(90, elevationDeg)) * Math.PI) / 180;
  const horiz = Math.cos(el);
  const sx = cx + Math.sin(az) * horiz * rx;
  const sy = cy + Math.cos(az) * horiz * ry * 0.55 - Math.sin(el) * 48;

  const svg = svgEl(
    "svg",
    { viewBox: `0 0 ${W} ${H}`, style: "cursor: crosshair;" },
    svgEl("ellipse", { cx, cy, rx, ry: ry * 0.55, fill: "rgba(255,255,255,0.02)", stroke: "#343434" }),
    svgEl("path", {
      d: `M ${cx - rx},${cy} A ${rx} 52 0 0 1 ${cx + rx},${cy}`,
      fill: "none",
      stroke: "#2d2d2d",
      "stroke-dasharray": "3 3",
    }),
    svgEl("line", { x1: cx, y1: cy, x2: sx, y2: sy, stroke: "#ffb454", "stroke-width": "1.5" }),
    svgEl("circle", { cx, cy, r: 3.5, fill: "#888888" }),
    svgEl("circle", { cx: sx, cy: sy, r: 8, fill: "#ffb454", stroke: "#fff", "stroke-width": "1.5" }),
    svgEl("text", { x: cx, y: cy - ry * 0.55 - 6, fill: "#777", "font-size": "9", "text-anchor": "middle" }, "N"),
    svgEl("text", { x: cx + rx + 10, y: cy + 3, fill: "#777", "font-size": "9" }, "E"),
    svgEl("text", { x: cx, y: cy + ry * 0.55 + 12, fill: "#777", "font-size": "9", "text-anchor": "middle" }, "S"),
    svgEl("text", { x: cx - rx - 14, y: cy + 3, fill: "#777", "font-size": "9" }, "W")
  );

  if (onDragSun) {
    let dragging = false;
    const updateFromEvent = (ev) => {
      const rect = svg.getBoundingClientRect();
      const px = ((ev.clientX - rect.left) / rect.width) * W - cx;
      const py = ((ev.clientY - rect.top) / rect.height) * H - cy;
      let newAz = Math.round(((Math.atan2(px, Math.max(-ry, Math.min(ry, py * 1.6))) * 180) / Math.PI + 360) % 360);
      let newEl = Math.round(Math.max(2, Math.min(88, (-py / 54) * 75 + 22)));
      onDragSun(newAz, newEl);
    };
    svg.addEventListener("pointerdown", (ev) => {
      dragging = true;
      svg.setPointerCapture(ev.pointerId);
      updateFromEvent(ev);
    });
    svg.addEventListener("pointermove", (ev) => {
      if (dragging) updateFromEvent(ev);
    });
    svg.addEventListener("pointerup", () => {
      dragging = false;
    });
  }

  return svg;
}
