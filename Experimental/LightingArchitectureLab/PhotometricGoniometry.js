// PhotometricGoniometry.js — Measured-style 128x32 LM-63 Type-C candela LUT synthesis & polar diagram renderer

export const IES_THETA_SAMPLES = 128;
export const IES_PHI_SAMPLES = 32;
export const IES_PROFILE_COUNT = 8;

export const IES_PROFILE_METADATA = [
  { ordinal: 0, label: "ECE R112 Low Beam", shortTag: "ECE LOW", defaultKelvin: 5400, beamDeg: 58 },
  { ordinal: 1, label: "Automotive High Beam", shortTag: "HIGH BEAM", defaultKelvin: 5800, beamDeg: 26 },
  { ordinal: 2, label: "Automotive Fog Blade", shortTag: "FOG LAMP", defaultKelvin: 3000, beamDeg: 84 },
  { ordinal: 3, label: "Ruby Tail & Brake Optic", shortTag: "TAIL OPTIC", defaultKelvin: 1900, beamDeg: 110 },
  { ordinal: 4, label: "Barn-Door Fresnel", shortTag: "FRESNEL", defaultKelvin: 4800, beamDeg: 38 },
  { ordinal: 5, label: "Garage Downlight", shortTag: "DOWNLIGHT", defaultKelvin: 5200, beamDeg: 64 },
  { ordinal: 6, label: "Asymmetric Wall Wash", shortTag: "WALL WASH", defaultKelvin: 4200, beamDeg: 70 },
  { ordinal: 7, label: "Batwing Soft Fill", shortTag: "BATWING", defaultKelvin: 5000, beamDeg: 86 },
];

function clamp(v, lo, hi) {
  return Math.max(lo, Math.min(hi, v));
}

function smoothstep(edge0, edge1, x) {
  const t = clamp((x - edge0) / (edge1 - edge0), 0, 1);
  return t * t * (3 - 2 * t);
}

export function evaluateAnalyticalProfile(profileOrdinal, thetaRad, phiRad) {
  if (thetaRad > Math.PI * 0.54) {
    return 0.0;
  }
  const sinT = Math.sin(thetaRad);
  const cosT = Math.cos(thetaRad);
  // Local luminaire basis: +Z = forward BeamAxis, +X = right ZeroAzimuth, +Y = up OrthoAxis
  const lx = sinT * Math.cos(phiRad);
  const ly = sinT * Math.sin(phiRad);
  const lz = cosT;

  const degPerRad = 180.0 / Math.PI;
  const yawDeg = Math.atan2(lx, Math.max(lz, 1e-4)) * degPerRad;
  const pitchDeg = Math.asin(clamp(ly, -1.0, 1.0)) * degPerRad;
  const thetaDeg = thetaRad * degPerRad;

  switch (profileOrdinal) {
    case 0: {
      // ECE R112 Low Beam: horizontal cutoff at -0.6 deg on oncoming side, 15 deg kick-up on roadside (+lx)
      const kickup = clamp(yawDeg * Math.tan((15.0 * Math.PI) / 180.0), 0.0, 6.0);
      const cutoffDeg = -0.6 + kickup;
      const glareGate = 1.0 - smoothstep(cutoffDeg - 0.85, cutoffDeg + 1.05, pitchDeg);
      const hotZone = Math.exp(-Math.pow((yawDeg - 2.5) / 8.5, 2) - Math.pow((pitchDeg + 1.9) / 2.9, 2));
      const roadCarpet = Math.exp(-Math.pow(yawDeg / 26.0, 2) - Math.pow((pitchDeg + 6.5) / 9.2, 2));
      const wideSide = Math.exp(-Math.pow(yawDeg / 42.0, 2) - Math.pow((pitchDeg + 12.0) / 13.5, 2));
      const intensity = glareGate * (0.98 * hotZone + 0.52 * roadCarpet + 0.24 * wideSide) + 0.008 * Math.max(0.0, cosT);
      return clamp(intensity, 0.0, 1.0);
    }
    case 1: {
      // High Beam: intense narrow throw + oval foreground apron
      const pencil = Math.exp(-Math.pow(yawDeg / 6.2, 2) - Math.pow(pitchDeg / 4.4, 2));
      const apron = Math.exp(-Math.pow(yawDeg / 20.0, 2) - Math.pow((pitchDeg + 2.0) / 10.5, 2));
      const spill = 0.14 * Math.exp(-Math.pow(thetaDeg / 38.0, 2));
      return clamp((0.92 * pencil + 0.34 * apron + spill) * smoothstep(72.0, 48.0, thetaDeg), 0.0, 1.0);
    }
    case 2: {
      // Fog Blade: low flat fan cutting off sharply near horizon
      const upperGate = 1.0 - smoothstep(-2.4, 0.2, pitchDeg);
      const lowerGate = smoothstep(-34.0, -18.0, pitchDeg);
      const fan = Math.exp(-Math.pow(yawDeg / 38.0, 2) - Math.pow((pitchDeg + 6.2) / 6.5, 2));
      return clamp(upperGate * lowerGate * (0.88 * fan + 0.16 * Math.exp(-Math.pow(yawDeg / 52.0, 2))), 0.0, 1.0);
    }
    case 3: {
      // Ruby Tail & Brake Optic
      const wide = Math.exp(-Math.pow(yawDeg / 48.0, 2) - Math.pow(pitchDeg / 28.0, 2));
      const center = Math.exp(-Math.pow(thetaDeg / 18.0, 2));
      return clamp((0.46 * wide + 0.54 * center) * Math.max(0.0, cosT), 0.0, 1.0);
    }
    case 4: {
      // Barn-Door Fresnel: soft rectangular shuttered profile
      const rx = Math.abs(yawDeg) / 19.0;
      const ry = Math.abs(pitchDeg) / 15.0;
      const superQuad = Math.pow(Math.pow(rx, 2.8) + Math.pow(ry, 2.8), 1.0 / 2.8);
      const barndoor = 1.0 - smoothstep(0.76, 1.24, superQuad);
      const core = 0.85 + 0.15 * Math.cos(clamp(thetaDeg / 24.0, 0.0, 1.0) * Math.PI * 0.5);
      return clamp(barndoor * core * Math.max(0.0, cosT), 0.0, 1.0);
    }
    case 5: {
      // Garage Downlight: smooth architectural cone
      const lobe = Math.exp(-Math.pow(thetaDeg / 29.0, 2.2));
      return clamp(lobe * Math.pow(Math.max(0.0, cosT), 0.7), 0.0, 1.0);
    }
    case 6: {
      // Asymmetric Wall Wash: directional grazing wash toward -OrthoAxis / wall
      const wash = Math.exp(-Math.pow((pitchDeg + 16.0) / 15.0, 2) - Math.pow(yawDeg / 32.0, 2));
      const glareClip = 1.0 - smoothstep(14.0, 26.0, pitchDeg);
      return clamp(wash * glareClip * Math.max(0.0, cosT), 0.0, 1.0);
    }
    case 7:
    default: {
      // Batwing Soft Fill: twin off-axis lobes for wide uniform floor bounce
      const wing = Math.exp(-Math.pow((Math.abs(thetaDeg) - 29.0) / 14.5, 2));
      const nadir = 0.34 * Math.exp(-Math.pow(thetaDeg / 22.0, 2));
      return clamp((wing + nadir) * Math.max(0.0, cosT), 0.0, 1.0);
    }
  }
}

export function buildPackedIesTextureAtlas() {
  // 128 (theta) x (8 * 32 = 256) (profile * phi) R32F candela texture
  const totalTexels = IES_THETA_SAMPLES * IES_PHI_SAMPLES * IES_PROFILE_COUNT;
  const atlas = new Float32Array(totalTexels);
  for (let p = 0; p < IES_PROFILE_COUNT; ++p) {
    for (let phiIdx = 0; phiIdx < IES_PHI_SAMPLES; ++phiIdx) {
      const phiRad = (phiIdx / IES_PHI_SAMPLES) * Math.PI * 2.0;
      const rowOffset = (p * IES_PHI_SAMPLES + phiIdx) * IES_THETA_SAMPLES;
      for (let thetaIdx = 0; thetaIdx < IES_THETA_SAMPLES; ++thetaIdx) {
        const thetaRad = (thetaIdx / (IES_THETA_SAMPLES - 1)) * Math.PI;
        atlas[rowOffset + thetaIdx] = evaluateAnalyticalProfile(p, thetaRad, phiRad);
      }
    }
  }
  return atlas;
}

export function paintPolarGoniometer(canvas, profileOrdinal, iesAtlas) {
  if (!canvas || !iesAtlas) return;
  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  ctx.fillStyle = "#0b0d12";
  ctx.fillRect(0, 0, w, h);

  const cx = w * 0.5;
  const cy = h * 0.44;
  const radius = Math.min(w * 0.41, h * 0.39);

  // Concentric candela rings
  ctx.strokeStyle = "rgba(148, 163, 184, 0.14)";
  ctx.lineWidth = 1;
  for (let rIdx = 1; rIdx <= 4; ++rIdx) {
    ctx.beginPath();
    ctx.arc(cx, cy, (radius * rIdx) / 4, 0, Math.PI * 2);
    ctx.stroke();
  }

  // Angular spokes every 30 deg
  for (let deg = 0; deg < 360; deg += 30) {
    const rad = (deg * Math.PI) / 180;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.sin(rad) * radius, cy + Math.cos(rad) * radius);
    ctx.stroke();
  }

  const samplePlane = (phiRad, strokeColor, fillColor) => {
    ctx.beginPath();
    const steps = 128;
    for (let s = 0; s <= steps; ++s) {
      const angle = (s / steps) * Math.PI * 2.0 - Math.PI;
      const absTheta = Math.abs(angle);
      const effectivePhi = angle >= 0 ? phiRad : (phiRad + Math.PI) % (Math.PI * 2.0);
      const thetaFloat = (absTheta / Math.PI) * (IES_THETA_SAMPLES - 1);
      const t0 = Math.min(IES_THETA_SAMPLES - 2, Math.floor(thetaFloat));
      const tf = thetaFloat - t0;

      const phiFloat = (effectivePhi / (Math.PI * 2.0)) * IES_PHI_SAMPLES;
      const p0 = Math.floor(phiFloat) % IES_PHI_SAMPLES;
      const p1 = (p0 + 1) % IES_PHI_SAMPLES;
      const pf = phiFloat - Math.floor(phiFloat);

      const base = profileOrdinal * IES_PHI_SAMPLES * IES_THETA_SAMPLES;
      const v00 = iesAtlas[base + p0 * IES_THETA_SAMPLES + t0];
      const v10 = iesAtlas[base + p0 * IES_THETA_SAMPLES + t0 + 1];
      const v01 = iesAtlas[base + p1 * IES_THETA_SAMPLES + t0];
      const v11 = iesAtlas[base + p1 * IES_THETA_SAMPLES + t0 + 1];
      const val = (v00 * (1 - tf) + v10 * tf) * (1 - pf) + (v01 * (1 - tf) + v11 * tf) * pf;

      // Draw beam pointing downward in polar chart (theta = 0 at bottom)
      const px = cx + Math.sin(angle) * val * radius;
      const py = cy + Math.cos(angle) * val * radius;
      if (s === 0) ctx.moveTo(px, py);
      else ctx.lineTo(px, py);
    }
    ctx.closePath();
    ctx.fillStyle = fillColor;
    ctx.fill();
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 1.8;
    ctx.stroke();
  };

  // C0 horizontal plane (amber) & C90 vertical plane (blue)
  samplePlane(0.0, "#f59e0b", "rgba(245, 158, 11, 0.14)");
  samplePlane(Math.PI * 0.5, "#38bdf8", "rgba(56, 189, 248, 0.16)");

  // Legend
  ctx.font = "10px monospace";
  ctx.fillStyle = "#38bdf8";
  ctx.fillText("C90 Vertical", 10, h - 10);
  ctx.fillStyle = "#f59e0b";
  ctx.fillText("C0 Horizontal", w - 92, h - 10);
}
