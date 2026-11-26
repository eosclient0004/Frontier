// LightingLabApp.js — Scene luminaire builder, orbital camera, UI wiring & animation loop

import {
  buildPackedIesTextureAtlas,
  IES_PROFILE_METADATA,
  paintPolarGoniometer,
} from "./PhotometricGoniometry.js";
import {
  bakeEnvironmentHarmonics,
  buildUnifiedPartitionAndPack,
  kelvinToLinearRgb,
  LUMINAIRE_CATEGORY,
} from "./PhotometricPartitionPacking.js";
import { LightingGpuViewport } from "./LightingGpuViewport.js";

function norm3(v) {
  const l = Math.hypot(v[0], v[1], v[2]);
  return l > 1e-6 ? [v[0] / l, v[1] / l, v[2] / l] : [0, -1, 0];
}

function cross3(a, b) {
  return [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
  ];
}

function rotateY(v, yaw) {
  const c = Math.cos(yaw);
  const s = Math.sin(yaw);
  return [c * v[0] - s * v[2], v[1], s * v[0] + c * v[2]];
}

const CAMERA_PRESETS = {
  hero: { yaw: 2.38, pitch: 0.34, dist: 6.8, target: [0.0, 0.72, 0.4] },
  cutoff: { yaw: 0.14, pitch: 0.18, dist: 5.4, target: [0.0, 0.65, 3.2] },
  rear: { yaw: -0.68, pitch: 0.30, dist: 5.8, target: [-0.2, 0.65, -0.8] },
  plan: { yaw: 1.57, pitch: 1.18, dist: 10.2, target: [0.0, 0.3, 0.5] },
};

const ENV_SETTINGS = [
  {
    name: "Lobby 1 · Cyclorama",
    sunDir: norm3([0.3, 0.85, 0.4]),
    sunColor: [0.0, 0.0, 0.0],
    exposure: 1.15,
  },
  {
    name: "Lobby 2 · Forest Stage",
    sunDir: norm3([0.62, 0.58, 0.52]),
    sunColor: [3.2, 2.75, 2.15],
    exposure: 0.92,
  },
  {
    name: "Lobby 3 · Racing Pit & Portal",
    sunDir: norm3([-0.82, 0.44, 0.36]),
    sunColor: [4.1, 2.95, 1.95],
    exposure: 0.98,
  },
];

const ENGINE_NOTES = [
  "Engine 0 (MatCap): Zero raytraces. Optional SH2 Environment + Top-4 analytical luminaire modulation keeps beams & portals readable.",
  "Engine 2 (SDF / GtxLowEnd): Direct lights stay 100% analytical (LTC Quad, Tube Capsule, 128x32 IES); SDF handles soft cone penumbra & AO!",
  "Engine 1 (OpenPBR Direct): Dual-layer GGX + Clearcoat with crisp BVH-style shadows & analytical PriorityCut luminaires.",
  "Engine 3 (RealTime GI): Adds chromatic Stokes near-field bounce & Surfel Radiosity bleed from tail lamps, DRLs, portals & emissive pylons.",
  "Engine 4 (Path Tracer): Unbiased progressive R2/Sobol (0,2) secondary reflections + diffuse GI bounces converging smoothly over frames.",
];

const SCHEDULER_NOTES = [
  "PriorityCut Split8: Deterministic O(log N) best-first cut over world-space bounds — crisp clearcoat & zero temporal lag.",
  "PriorityCut Split4: Ultra-lean 4-cut budget for GTX low-end hardware with smooth cluster tail compensation.",
  "Brute-Force All: Evaluates every luminaire at O(N) cost for 1:1 visual reference comparison.",
  "DEFECT DEMO · Frustum Clustered: Drops behind-camera lights from reflections & overflows cell slots on chrome sphere & wet floor!",
  "DEFECT DEMO · ReSTIR DI: Single-ray RIS sparkle on clearcoat + stale temporal history trails when vehicle moves!",
];

function buildActiveLuminaires(labConfiguration) {
  const list = [];
  const {
    envPreset,
    softboxEnabled,
    fresnelEnabled,
    rimTubeEnabled,
    portalEnabled,
    lowBeamEnabled,
    highBeamEnabled,
    drlEnabled,
    brakeEnabled,
    headlampProfileOrdinal,
    emissivePylonCount,
    carPos,
    carYaw,
  } = labConfiguration;

  const scaleColor = (kelvin, intensity) => {
    const c = kelvinToLinearRgb(kelvin);
    return [c[0] * intensity, c[1] * intensity, c[2] * intensity];
  };

  // 1. Studio Overhead LTC Quad Softbox
  if (softboxEnabled) {
    const softInt = envPreset === 0 ? 6.2 : 3.0;
    list.push({
      category: LUMINAIRE_CATEGORY.AREA_QUAD_LTC,
      pos: [0.2, 4.40, 0.2],
      rightAxis: [1.0, 0.0, 0.0],
      upAxis: [0.0, 0.0, -1.0],
      halfW: 1.85,
      halfH: 0.85,
      twoSided: 0.0,
      collimation: 0.35,
      rgbIntensity: scaleColor(5600, softInt),
    });
  }

  // 2. Studio Barn-Door Fresnel IES Key
  if (fresnelEnabled) {
    list.push({
      category: LUMINAIRE_CATEGORY.PHOTOMETRIC_IES,
      pos: [-3.4, 3.3, -1.6],
      beamAxis: norm3([3.4, -2.6, 1.8]),
      zeroAzimuth: [0.0, 0.0, 1.0],
      rangeMeters: 14.0,
      radiusMeters: 0.14,
      profileOrdinal: 4, // Barn-Door Fresnel
      spreadDeg: 42,
      rgbIntensity: scaleColor(4800, 18.0),
    });
  }

  // 3. Linear Studio Rim Capsule Bar
  if (rimTubeEnabled) {
    list.push({
      category: LUMINAIRE_CATEGORY.TUBE_CAPSULE,
      segA: [2.4, 2.55, -2.5],
      segB: [2.4, 2.55, 2.4],
      radiusMeters: 0.085,
      rangeMeters: 9.0,
      rgbIntensity: scaleColor(6400, 3.2),
    });
  }

  // 4. Lobby 3 Archway Sky Portal Quad (or Lobby 2 stage bounce)
  if (portalEnabled && envPreset >= 1) {
    list.push({
      category: LUMINAIRE_CATEGORY.AREA_QUAD_LTC,
      pos: [-5.9, 1.65, 0.8],
      rightAxis: [0.0, 0.0, 1.0],
      upAxis: [0.0, 1.0, 0.0],
      halfW: 2.1,
      halfH: 1.35,
      twoSided: 0.0,
      collimation: 0.8,
      rgbIntensity: envPreset === 2 ? scaleColor(4500, 4.4) : scaleColor(6500, 2.6),
    });
  }

  // Helper to place car-mounted luminaires in world space
  const carToWorld = (local) => {
    const r = rotateY(local, carYaw);
    return [carPos[0] + r[0], carPos[1] + r[1], carPos[2] + r[2]];
  };
  const carDirToWorld = (localDir) => norm3(rotateY(localDir, carYaw));

  // 5. Dual Automotive Front Photometric IES Headlamps
  if (lowBeamEnabled || highBeamEnabled) {
    const forward = carDirToWorld([0.0, highBeamEnabled ? -0.02 : -0.05, 1.0]);
    const rightAz = carDirToWorld([1.0, 0.0, 0.0]);
    const activeProfile = highBeamEnabled ? 1 : headlampProfileOrdinal;
    const beamPower = highBeamEnabled ? 42.0 : 28.0;
    const beamKelvin = highBeamEnabled ? 5800 : 5400;

    list.push({
      category: LUMINAIRE_CATEGORY.PHOTOMETRIC_IES,
      pos: carToWorld([-0.68, 0.52, 2.16]),
      beamAxis: forward,
      zeroAzimuth: rightAz,
      rangeMeters: 16.0,
      radiusMeters: 0.05,
      profileOrdinal: activeProfile,
      spreadDeg: highBeamEnabled ? 30 : 56,
      rgbIntensity: scaleColor(beamKelvin, beamPower),
    });
    list.push({
      category: LUMINAIRE_CATEGORY.PHOTOMETRIC_IES,
      pos: carToWorld([0.68, 0.52, 2.16]),
      beamAxis: forward,
      zeroAzimuth: rightAz,
      rangeMeters: 16.0,
      radiusMeters: 0.05,
      profileOrdinal: activeProfile,
      spreadDeg: highBeamEnabled ? 30 : 56,
      rgbIntensity: scaleColor(beamKelvin, beamPower),
    });
  }

  // 6. Automotive Linear Capsule DRL Blade + Ruby Tail/Brake Bar
  if (drlEnabled) {
    list.push({
      category: LUMINAIRE_CATEGORY.TUBE_CAPSULE,
      segA: carToWorld([-0.82, 0.49, 2.12]),
      segB: carToWorld([0.82, 0.49, 2.12]),
      radiusMeters: 0.028,
      rangeMeters: 4.5,
      rgbIntensity: scaleColor(6000, 1.6),
    });
  }

  list.push({
    category: LUMINAIRE_CATEGORY.TUBE_CAPSULE,
    segA: carToWorld([-0.82, 0.61, -2.18]),
    segB: carToWorld([0.82, 0.61, -2.18]),
    radiusMeters: 0.032,
    rangeMeters: 5.5,
    rgbIntensity: [brakeEnabled ? 6.8 : 2.1, 0.08, 0.05],
  });

  // 7. Many Emissive / Point Pylons (3 rings x 16 slots = 48 max, exact match to O(1) shader ring SDF!)
  const pylonTotal = Math.min(48, emissivePylonCount);
  for (let i = 0; i < pylonTotal; ++i) {
    const ring = Math.floor(i / 16);
    const sector = i % 16;
    const rad = 4.25 + ring * 1.2;
    const phase = ring * 0.19635;
    const ang = sector * ((Math.PI * 2.0) / 16.0) + phase;
    const pos = [Math.cos(ang) * rad, 0.40 + 0.10 * Math.sin(i * 1.7), Math.sin(ang) * rad + 0.5];

    const hue = (i * 0.27) % 1.0;
    const r = Math.max(0.1, Math.min(1.0, Math.abs(hue * 6 - 3) - 1));
    const g = Math.max(0.1, Math.min(1.0, 2 - Math.abs(hue * 6 - 2)));
    const b = Math.max(0.1, Math.min(1.0, 2 - Math.abs(hue * 6 - 4)));

    list.push({
      category: LUMINAIRE_CATEGORY.POINT_SPHERE,
      pos,
      radiusMeters: 0.09,
      rangeMeters: 5.8,
      rgbIntensity: [r * 3.8, g * 3.8, b * 3.8],
    });
  }

  return list;
}

function initLabApp() {
  const glCanvas = document.getElementById("glCanvas");
  const polarCanvas = document.getElementById("polarCanvas");
  const hudTelemetry = document.getElementById("hudTelemetry");
  const hudDiagnosis = document.getElementById("hudDiagnosis");

  let viewport;
  try {
    viewport = new LightingGpuViewport(glCanvas);
  } catch (err) {
    hudDiagnosis.className = "hud-note alert";
    hudDiagnosis.textContent = `WebGL2 initialization error: ${err.message}`;
    return;
  }
  const iesAtlas = buildPackedIesTextureAtlas();
  viewport.uploadIesAtlas(iesAtlas);

  const labCfg = {
    renderEngine: 3, // RealTime GI default
    schedulerMode: 0, // PriorityCut Split8
    debugLens: 0,
    envPreset: 0, // Cyclorama
    matCapLightBlend: true,
    sdfVoxelDefectDemo: false,
    driveVehicle: false,
    softboxEnabled: true,
    fresnelEnabled: true,
    rimTubeEnabled: true,
    portalEnabled: true,
    lowBeamEnabled: true,
    highBeamEnabled: false,
    drlEnabled: true,
    brakeEnabled: false,
    turnSignalEnabled: false,
    headlampProfileOrdinal: 0,
    emissivePylonCount: 16,
    carPos: [0, 0, 0],
    carYaw: 0,
    prevCarPos: [0, 0, 0],
    prevCarYaw: 0,
  };

  let cam = { ...CAMERA_PRESETS.hero };
  let isDragging = false;
  let lastMouse = [0, 0];
  let dirtyLighting = true;
  let fpsAvg = 60;
  let prevTimestamp = performance.now();

  const rebuildLighting = () => {
    const luminaires = buildActiveLuminaires(labCfg);
    const packed = buildUnifiedPartitionAndPack(luminaires);
    viewport.uploadLightRegister(packed.texData);
    labCfg._lastPacked = packed;
    dirtyLighting = false;
  };

  const refreshPolar = () => {
    paintPolarGoniometer(polarCanvas, labCfg.headlampProfileOrdinal, iesAtlas);
  };

  const refreshDiagnosis = () => {
    let text = ENGINE_NOTES[labCfg.renderEngine] + " ";
    let cls = "hud-note";
    if (labCfg.schedulerMode >= 3) {
      text = SCHEDULER_NOTES[labCfg.schedulerMode];
      cls = "hud-note alert";
    } else if (labCfg.renderEngine === 1 && labCfg.sdfVoxelDefectDemo) {
      text = "DEFECT DEMO · Voxelized Direct SDF Grid (0.45m cells): sharp ECE low-beam cutoff & clearcoat highlights smear completely!";
      cls = "hud-note alert";
    } else if (labCfg.schedulerMode === 0) {
      text += " (" + SCHEDULER_NOTES[0] + ")";
      cls = "hud-note good";
    }
    hudDiagnosis.className = cls;
    hudDiagnosis.textContent = text;
  };

  // Wire segmented button groups
  const bindGroup = (selector, onSelect) => {
    const buttons = Array.from(document.querySelectorAll(selector));
    buttons.forEach((btn) => {
      btn.addEventListener("click", () => {
        buttons.forEach((b) => b.classList.remove("active"));
        btn.classList.add("active");
        onSelect(Number(btn.dataset.val));
        viewport.resetAccumulation();
      });
    });
  };

  bindGroup("[data-engine]", (val) => {
    labCfg.renderEngine = val;
    refreshDiagnosis();
  });

  bindGroup("[data-scheduler]", (val) => {
    labCfg.schedulerMode = val;
    refreshDiagnosis();
  });

  bindGroup("[data-env]", (val) => {
    labCfg.envPreset = val;
    dirtyLighting = true;
    refreshDiagnosis();
  });

  document.querySelectorAll("[data-camera]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const preset = CAMERA_PRESETS[btn.dataset.camera];
      if (preset) {
        cam = { ...preset, target: [...preset.target] };
        viewport.resetAccumulation();
      }
    });
  });

  // Checkboxes
  const bindToggle = (id, key, extra) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.checked = Boolean(labCfg[key]);
    el.addEventListener("change", () => {
      labCfg[key] = el.checked;
      dirtyLighting = true;
      viewport.resetAccumulation();
      if (extra) extra();
    });
  };

  bindToggle("chkDriveVehicle", "driveVehicle");
  bindToggle("chkSdfVoxelDefect", "sdfVoxelDefectDemo", refreshDiagnosis);
  bindToggle("chkMatCapLight", "matCapLightBlend");
  bindToggle("chkSoftbox", "softboxEnabled");
  bindToggle("chkFresnel", "fresnelEnabled");
  bindToggle("chkRimTube", "rimTubeEnabled");
  bindToggle("chkPortal", "portalEnabled");
  bindToggle("chkLowBeam", "lowBeamEnabled");
  bindToggle("chkHighBeam", "highBeamEnabled");
  bindToggle("chkDrl", "drlEnabled");
  bindToggle("chkBrake", "brakeEnabled");
  bindToggle("chkTurnSignal", "turnSignalEnabled");

  // Debug Lens & IES Profile selects
  const lensSelect = document.getElementById("selDebugLens");
  lensSelect.addEventListener("change", () => {
    labCfg.debugLens = Number(lensSelect.value);
    viewport.resetAccumulation();
  });

  const profileSelect = document.getElementById("selIesProfile");
  IES_PROFILE_METADATA.forEach((meta) => {
    const opt = document.createElement("option");
    opt.value = String(meta.ordinal);
    opt.textContent = `${meta.shortTag} · ${meta.label}`;
    profileSelect.appendChild(opt);
  });
  profileSelect.value = "0";
  profileSelect.addEventListener("change", () => {
    labCfg.headlampProfileOrdinal = Number(profileSelect.value);
    dirtyLighting = true;
    refreshPolar();
    viewport.resetAccumulation();
  });

  // Emissive pylon stress slider
  const pylonSlider = document.getElementById("rngPylons");
  const pylonLabel = document.getElementById("lblPylons");
  pylonSlider.addEventListener("input", () => {
    labCfg.emissivePylonCount = Number(pylonSlider.value);
    pylonLabel.textContent = String(labCfg.emissivePylonCount);
    dirtyLighting = true;
    viewport.resetAccumulation();
  });

  // Camera cut button (instant jump to reveal ReSTIR DI vs PriorityCut stability)
  document.getElementById("btnCameraCut").addEventListener("click", () => {
    cam.yaw += 1.45;
    cam.pitch = Math.max(0.16, Math.min(0.85, cam.pitch));
    viewport.resetAccumulation();
  });

  // Orbit & zoom controls
  glCanvas.addEventListener("pointerdown", (e) => {
    isDragging = true;
    lastMouse = [e.clientX, e.clientY];
    glCanvas.setPointerCapture(e.pointerId);
  });
  glCanvas.addEventListener("pointermove", (e) => {
    if (!isDragging) return;
    const dx = e.clientX - lastMouse[0];
    const dy = e.clientY - lastMouse[1];
    lastMouse = [e.clientX, e.clientY];
    cam.yaw -= dx * 0.0065;
    cam.pitch = Math.max(0.08, Math.min(1.38, cam.pitch + dy * 0.0055));
    viewport.resetAccumulation();
  });
  glCanvas.addEventListener("pointerup", () => {
    isDragging = false;
  });
  glCanvas.addEventListener("wheel", (e) => {
    e.preventDefault();
    cam.dist = Math.max(2.8, Math.min(18.0, cam.dist * (1 + e.deltaY * 0.001)));
    viewport.resetAccumulation();
  }, { passive: false });

  refreshPolar();
  refreshDiagnosis();
  rebuildLighting();

  function tick(nowMs) {
    const dt = Math.max(0.001, (nowMs - prevTimestamp) * 0.001);
    prevTimestamp = nowMs;
    fpsAvg = fpsAvg * 0.92 + (1.0 / dt) * 0.08;

    const timeSec = nowMs * 0.001;

    // Optional vehicle motion simulation (demonstrates zero-lag PriorityCut vs ReSTIR ghosting)
    labCfg.prevCarPos = [...labCfg.carPos];
    labCfg.prevCarYaw = labCfg.carYaw;
    if (labCfg.driveVehicle) {
      const a = timeSec * 0.65;
      const nx = Math.sin(a) * 1.15;
      const nz = Math.sin(a * 0.5) * 0.85;
      const dx = Math.cos(a) * 1.15 * 0.65;
      const dz = Math.cos(a * 0.5) * 0.85 * 0.325;
      labCfg.carPos = [nx, 0.0, nz];
      labCfg.carYaw = Math.atan2(dx, dz) * 0.38;
      dirtyLighting = true;
      viewport.resetAccumulation();
    } else if (Math.abs(labCfg.carPos[0]) > 0.001 || Math.abs(labCfg.carYaw) > 0.001) {
      labCfg.carPos = [0, 0, 0];
      labCfg.carYaw = 0;
      dirtyLighting = true;
    }

    if (dirtyLighting) {
      rebuildLighting();
    }

    const env = ENV_SETTINGS[labCfg.envPreset];
    const envSh = bakeEnvironmentHarmonics(labCfg.envPreset, env.sunDir);

    const cp = Math.cos(cam.pitch);
    const sp = Math.sin(cam.pitch);
    const cy = Math.cos(cam.yaw);
    const sy = Math.sin(cam.yaw);
    const camPos = [
      cam.target[0] + sy * cp * cam.dist,
      cam.target[1] + sp * cam.dist,
      cam.target[2] + cy * cp * cam.dist,
    ];
    const camForward = norm3([
      cam.target[0] - camPos[0],
      cam.target[1] - camPos[1],
      cam.target[2] - camPos[2],
    ]);
    const camRight = norm3(cross3(camForward, [0, 1, 0]));
    const camUp = norm3(cross3(camRight, camForward));

    viewport.renderViewport({
      renderScale: Math.min(window.devicePixelRatio || 1.0, 1.0),
      camPos,
      camRight,
      camUp,
      camForward,
      tanHalfFov: Math.tan((44 * Math.PI) / 360),
      luminaireCount: labCfg._lastPacked.luminaireCount,
      partitionCellCount: labCfg._lastPacked.partitionCellCount,
      renderEngine: labCfg.renderEngine,
      schedulerMode: labCfg.schedulerMode,
      debugLens: labCfg.debugLens,
      envPreset: labCfg.envPreset,
      matCapLightBlend: labCfg.matCapLightBlend,
      sdfVoxelDefectDemo: labCfg.sdfVoxelDefectDemo,
      timeSec,
      exposure: env.exposure,
      carPos: labCfg.carPos,
      carYaw: labCfg.carYaw,
      prevCarPos: labCfg.prevCarPos,
      prevCarYaw: labCfg.prevCarYaw,
      headlampIntensity: labCfg.lowBeamEnabled || labCfg.highBeamEnabled ? 1.0 : 0.0,
      highBeamActive: labCfg.highBeamEnabled,
      drlActive: labCfg.drlEnabled,
      brakeActive: labCfg.brakeEnabled,
      turnSignalActive: labCfg.turnSignalEnabled,
      emissivePylonCount: labCfg.emissivePylonCount,
      sunDir: env.sunDir,
      sunColor: env.sunColor,
      envSh,
    });

    const evalCap =
      labCfg.renderEngine === 0
        ? "4 (SH2+Top4)"
        : labCfg.schedulerMode === 0
          ? "<=10 (Split8)"
          : labCfg.schedulerMode === 1
            ? "<=6 (Split4)"
            : labCfg.schedulerMode === 2
              ? `${labCfg._lastPacked.luminaireCount} (O(N))`
              : labCfg.schedulerMode === 3
                ? "Frustum Trunc"
                : "1 RIS (Lag)";

    hudTelemetry.innerHTML = `
      <div class="hud-stat"><span>Luminaires:</span> <strong>${labCfg._lastPacked.luminaireCount}</strong></div>
      <div class="hud-stat"><span>Partition Cells:</span> <strong>${labCfg._lastPacked.partitionCellCount}</strong></div>
      <div class="hud-stat"><span>Eval/Px:</span> <strong>${evalCap}</strong></div>
      <div class="hud-stat"><span>PT Samples:</span> <strong>${labCfg.renderEngine === 4 ? viewport.accumFrame : "RT"}</strong></div>
      <div class="hud-stat"><span>FPS:</span> <strong>${Math.round(fpsAvg)}</strong></div>
    `;

    requestAnimationFrame(tick);
  }

  requestAnimationFrame(tick);
}

window.addEventListener("DOMContentLoaded", initLabApp);
