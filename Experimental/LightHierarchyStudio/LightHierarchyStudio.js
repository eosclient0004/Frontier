/*============================================================================================================================================
                                                        LIGHTHIERARCHYSTUDIO.JS
============================================================================================================================================*/
// 🧩 Project-Zero Unified 3-System Lighting Studio Controller:
//    (1) Static Lights -> Auto-Baked 2-Cascade Toroidal Clipmap (Fine follows focus/probe + Coarse covers world)
//    (2) Dynamic Lights -> 1 Cube-Grid/Pod (95.1 KB/pod) + Nanite Distance Light BVH
//    (3) Unified All-3 Hybrid + Side-by-Side GPU Cost (ms), Evals/px, VRAM & RMSE% Comparison Table

import {
  SCENE_SCALES,
  ONE_CUBE_GRID_POD_KB,
  CreateDefaultLightComponents,
  KelvinToTintRgb,
  ExpandComponentsToRawLights,
  BuildNaiveMergedLights,
  BuildCubeGridPods,
  BuildLightBvh,
  ComputeLightCutForReceiver,
  BakeToroidalClipmap,
  ComputeModeVramMb,
  EvaluateModeErrorStats,
} from './LightEngine.js';
import { LightViewportRenderer } from './LightRenderer.js';

const MODE_META = {
  exact:   { id: 0, label: 'Exact (LOD 0)',          title: 'Exact (LOD 0 Reference)',                   sub: 'All 32 individual emitters + analytic shadows', dot: 'green' },
  naive:   { id: 1, label: 'Naive Point',            title: 'Naive Point Merge (Averaged)',               sub: 'Averages headlights @ center -> huge error!',   dot: 'coral' },
  pods:    { id: 2, label: '1 Cube-Grid/Pod',        title: '1 Cube-Grid / Pod (95.1 KB / Pod)',          sub: 'Tight sub-fixture pods preserve directional beams', dot: 'cyan' },
  clipmap: { id: 3, label: 'Toroidal Clipmap',       title: 'Static Toroidal Clipmap + Dynamic Pods',     sub: '2-cascade toroidal static bake + dynamic pods', dot: 'amber' },
  unified: { id: 4, label: 'Unified All-3 (Nanite)', title: 'Unified All-3 (Nanite + Clipmap + Pod)',     sub: 'Static Toroidal Clipmap + Dynamic Nanite BVH + Error Split', dot: 'green' },
};

const state = {
  mode: 'unified', // Default to the Combined All-3 Unified system!
  debugView: 0,    // 0=Lit, 1=Error x8, 2=Toroidal Clipmap, 3=Static vs Dynamic, 4=Cube-Grid Pods
  sceneScale: 'medium',
  autoBake: true,
  fineRes: 16,
  lodThreshold: 0.24,
  maxActiveLights: 12,
  hybridErrorGate: 0.18,
  errorGain: 8.0,
  floorRoughness: 0.28,
  showGrid: true,
  showLights: true,
  animateDrone: true,
  probePos: [0.0, 2.2],
  activePage: 0,
  selectedComponentId: 'comp-hl-left',
  components: CreateDefaultLightComponents(),
  timeSec: 0,
  refineStep: 0,
  errorHistory: [],

  // Orbit Camera
  yaw: 0.42,
  pitch: 0.68,
  dist: 20.5,
  target: [0.0, 0.4, 0.2],
};

const vpTruth  = new LightViewportRenderer(document.getElementById('CanvasTruth'),  document.getElementById('OverlayTruth'));
const vpApprox = new LightViewportRenderer(document.getElementById('CanvasApprox'), document.getElementById('OverlayApprox'));
const vpError  = new LightViewportRenderer(document.getElementById('CanvasError'),  document.getElementById('OverlayError'));
const allViewports = [vpTruth, vpApprox, vpError];

let rawLights = [];
let staticLights = [];
let dynamicLights = [];
let naiveLights = [];
let allPods = [];
let staticPods = [];
let dynPods = [];
let dynBvh = null;
let staticBvh = null;
let dynCut = [];
let staticPromoCut = [];
let staticClipmap = null;
let staticDirty = true;
let lastFineOriginKey = '';
let gpuBenchRows = [];
let benchPending = true;
let benchDebounceTimer = null;
let fpsSmooth = 60;
let lastFrameMs = performance.now();

function BuildCameraFrame() {
  const cp = Math.cos(state.pitch), sp = Math.sin(state.pitch);
  const cy = Math.cos(state.yaw), sy = Math.sin(state.yaw);
  const pos = [
    state.target[0] + sy * cp * state.dist,
    state.target[1] + sp * state.dist,
    state.target[2] + cy * cp * state.dist,
  ];
  const fwd = [state.target[0] - pos[0], state.target[1] - pos[1], state.target[2] - pos[2]];
  const fl = Math.hypot(fwd[0], fwd[1], fwd[2]) || 1;
  fwd[0] /= fl; fwd[1] /= fl; fwd[2] /= fl;
  const upRef = Math.abs(fwd[1]) > 0.985 ? [0, 0, -1] : [0, 1, 0];
  const right = [
    fwd[1] * upRef[2] - fwd[2] * upRef[1],
    fwd[2] * upRef[0] - fwd[0] * upRef[2],
    fwd[0] * upRef[1] - fwd[1] * upRef[0],
  ];
  const rl = Math.hypot(right[0], right[1], right[2]) || 1;
  right[0] /= rl; right[1] /= rl; right[2] /= rl;
  const up = [
    right[1] * fwd[2] - right[2] * fwd[1],
    right[2] * fwd[0] - right[0] * fwd[2],
    right[0] * fwd[1] - right[1] * fwd[0],
  ];
  return {
    pos: new Float32Array(pos),
    fwd: new Float32Array(fwd),
    right: new Float32Array(right),
    up: new Float32Array(up),
    tanHalfFov: Math.tan((44 * Math.PI) / 360),
  };
}

function ScheduleGpuBenchmark(delayMs = 180) {
  if (benchDebounceTimer) clearTimeout(benchDebounceTimer);
  benchDebounceTimer = setTimeout(() => { benchPending = true; }, delayMs);
}

/**
 * Rebuilds Dynamic lights every frame (zero static stall!) and AUTOMATICALLY re-bakes the Static 2-Cascade
 * Toroidal Clipmap whenever a static light changes OR the focus/probe scrolls to a new toroidal cell!
 */
function SyncLightingSystems(forceStaticBake = false) {
  const scaleCfg = SCENE_SCALES[state.sceneScale];
  rawLights = ExpandComponentsToRawLights(state.components, state.timeSec, state.animateDrone);
  staticLights = rawLights.filter((l) => l.mobility === 'static');
  dynamicLights = rawLights.filter((l) => l.mobility === 'dynamic');

  naiveLights = BuildNaiveMergedLights(rawLights);
  allPods = BuildCubeGridPods(rawLights);
  staticPods = allPods.filter((p) => p.mobility === 'static');
  dynPods = allPods.filter((p) => p.mobility === 'dynamic');

  // Dynamic Nanite-for-Lights Agglomerative BVH (runs ONLY on dynamic lights!)
  dynBvh = BuildLightBvh(dynamicLights);
  dynCut = dynBvh
    ? ComputeLightCutForReceiver(dynBvh, state.probePos[0], 0.1, state.probePos[1], state.lodThreshold, state.maxActiveLights)
    : [];

  // Static Nanite BVH (only evaluated in rare high-error static clipmap cells in Unified All-3!)
  if (staticDirty || !staticBvh) staticBvh = BuildLightBvh(staticLights);
  staticPromoCut = staticBvh
    ? ComputeLightCutForReceiver(staticBvh, state.probePos[0], 0.1, state.probePos[1], state.lodThreshold * 0.85, state.maxActiveLights)
    : [];

  // Toroidal Fine Cascade Snap Check: only re-bake static toroidal clipmap when focus crosses a cell boundary OR static light edited!
  const cellSize = (scaleCfg.fineHalfSpan * 2.0) / state.fineRes;
  const originX = Math.round(state.probePos[0] / cellSize);
  const originZ = Math.round(state.probePos[1] / cellSize);
  const originKey = `${state.sceneScale}:${state.fineRes}:${originX},${originZ}`;

  if (forceStaticBake || !staticClipmap || (state.autoBake && (staticDirty || originKey !== lastFineOriginKey))) {
    staticClipmap = BakeToroidalClipmap(staticLights, state.probePos[0], state.probePos[1], scaleCfg, state.fineRes);
    for (const vp of allViewports) vp.uploadToroidalClipmap(staticClipmap);
    staticDirty = false;
    lastFineOriginKey = originKey;

    // Update Auto-Bake & Toroidal Clipmap Telemetry
    document.getElementById('FootBakeMs').textContent = `${staticClipmap.bakeMs.toFixed(1)} ms`;
    document.getElementById('MetricBakeTime').innerHTML = `${staticClipmap.bakeMs.toFixed(1)}<small>ms</small>`;
    document.getElementById('MetricFineSpan').innerHTML = `${(scaleCfg.fineHalfSpan * 2).toFixed(1)}<small>m</small>`;
    document.getElementById('MetricCellSize').innerHTML = `${staticClipmap.fine.cellSize.toFixed(2)}<small>m</small>`;
    document.getElementById('MetricClipVram').innerHTML = `${Math.round(staticClipmap.totalVramKb * 16)}<small>KB</small>`;
    document.getElementById('FootToroidalCell').textContent = `(${originX},${originZ})`;
  }

  document.getElementById('StatStaticCount').textContent = String(staticLights.length);
  document.getElementById('StatDynamicCount').textContent = String(dynamicLights.length);
  document.getElementById('FootPodCount').textContent = String(allPods.length);
  document.getElementById('OutlinerNodeCount').textContent = `${state.components.length} light rigs · ${rawLights.length} emitters`;
  UpdateHeadlightDiagnostic();
}

function GetApproxLightArrayForMode(modeKey) {
  if (modeKey === 'exact') return rawLights;
  if (modeKey === 'naive') return naiveLights;
  if (modeKey === 'pods') return allPods;
  if (modeKey === 'clipmap') return dynPods;
  return dynCut; // unified: dynamic Nanite BVH cut (+ static toroidal clipmap + staticPods promotion)
}

//------------------------------------------------------------------------------------------------------------------------
//                          SIDE-BY-SIDE GPU COST (ms), EVALS/PX, VRAM & RMSE% BENCHMARK
//------------------------------------------------------------------------------------------------------------------------

function RunSideBySideGpuBenchmark(cam, commonParams) {
  const gl = vpApprox.gl;
  const benchKeys = ['exact', 'naive', 'pods', 'clipmap', 'unified'];
  const evalCtx = {
    rawLights, naiveLights, allPods, dynPods, dynBvh, staticBvh, staticClipmap,
    opts: { errorThreshold: state.lodThreshold, maxActiveLights: state.maxActiveLights, hybridErrorGate: state.hybridErrorGate },
  };
  const w = vpApprox.glCanvas.width;
  const h = vpApprox.glCanvas.height;
  const iters = 5;
  const rows = [];

  for (const key of benchKeys) {
    const modeId = MODE_META[key].id;
    const approxArr = GetApproxLightArrayForMode(key);

    // Warm-up pass
    vpApprox.render(cam, rawLights, approxArr, staticPromoCut, { ...commonParams, lightMode: modeId, debugView: 0 });
    gl.finish();

    const t0 = performance.now();
    for (let k = 0; k < iters; k++) {
      vpApprox.render(cam, rawLights, approxArr, staticPromoCut, { ...commonParams, lightMode: modeId, debugView: 0 });
    }
    gl.finish();
    const gpuMs = Math.max(0.05, (performance.now() - t0) / iters);

    const errStats = EvaluateModeErrorStats(key, evalCtx);
    const podCount = key === 'clipmap' || key === 'unified' ? dynPods.length : allPods.length;
    const vram = ComputeModeVramMb(key, w, h, rawLights.length, podCount, staticClipmap);

    rows.push({
      key,
      label: MODE_META[key].label,
      gpuMs,
      fps: Math.min(9999, Math.round(1000 / gpuMs)),
      avgEvals: errStats.avgEvals,
      lightAtlasKb: vram.lightAtlasKb,
      totalVramMb: vram.totalVramMb,
      baseRtMb: vram.baseRtMb,
      rmsePct: errStats.rmsePct,
      rmse: errStats.rmse,
    });
  }

  gpuBenchRows = rows;
  RenderBenchmarkTable();
}

function FormatKbOrMb(kb) {
  return kb >= 1024 ? `${(kb / 1024).toFixed(2)} MB` : `${kb.toFixed(1)} KB`;
}

function RenderBenchmarkTable() {
  const tbody = document.getElementById('BenchTableBody');
  if (!tbody || gpuBenchRows.length === 0) return;

  const refRow = gpuBenchRows[0];
  tbody.innerHTML = '';

  for (const r of gpuBenchRows) {
    const isExact = r.key === 'exact';
    const isActive = r.key === state.mode;
    const speedup = refRow.gpuMs / Math.max(0.05, r.gpuMs);
    const errCls = isExact || r.rmsePct <= 5.0 ? 'b-err-ok' : r.rmsePct <= 14.0 ? 'b-err-mid' : 'b-err-bad';

    const tr = document.createElement('tr');
    tr.className = `bench-row${isActive ? ' active' : ''}`;
    tr.innerHTML = `
      <td>${r.label}</td>
      <td>${r.gpuMs.toFixed(2)}<span class="b-speed">${speedup.toFixed(1)}&times;</span></td>
      <td>${r.avgEvals.toFixed(1)}</td>
      <td>${FormatKbOrMb(r.lightAtlasKb)}</td>
      <td>${r.totalVramMb.toFixed(2)} MB</td>
      <td class="${errCls}">${isExact ? '0.0% (Ref)' : `${r.rmsePct.toFixed(1)}%`}</td>
    `;
    tr.addEventListener('click', () => SelectMode(r.key));
    tbody.appendChild(tr);
  }

  const curRow = gpuBenchRows.find((r) => r.key === state.mode) || refRow;
  document.getElementById('TruthPerfBadge').textContent = `${rawLights.length} evals/px · ${refRow.gpuMs.toFixed(2)} ms (${refRow.fps} fps)`;
  document.getElementById('ApproxPerfBadge').textContent = `${curRow.avgEvals.toFixed(1)} evals/px · ${curRow.gpuMs.toFixed(2)} ms (${curRow.fps} fps)`;
  document.getElementById('FootRmse').textContent = `${curRow.rmsePct.toFixed(1)}%`;

  document.getElementById('BenchFootNote').textContent =
    `${vpApprox.glCanvas.width}×${vpApprox.glCanvas.height} · G-Buffer+HDR: ${refRow.baseRtMb.toFixed(2)} MB · 1 CubeGrid Pod: ${ONE_CUBE_GRID_POD_KB} KB · ${vpApprox.timerLabel}`;

  // Push latest RMSE% into convergence graph
  if (state.errorHistory.length === 0 || state.errorHistory[state.errorHistory.length - 1].key !== `${state.mode}:${state.refineStep}`) {
    state.errorHistory.push({ key: `${state.mode}:${state.refineStep}`, rmsePct: curRow.rmsePct });
    if (state.errorHistory.length > 16) state.errorHistory.shift();
    DrawErrorChart();
  }
}

function DrawErrorChart() {
  const c = document.getElementById('ErrorChartCanvas');
  const ctx = c.getContext('2d');
  const w = c.width, h = c.height;
  ctx.clearRect(0, 0, w, h);
  ctx.strokeStyle = 'rgba(255,255,255,0.06)';
  for (let i = 1; i <= 3; i++) {
    ctx.beginPath(); ctx.moveTo(0, (h / 4) * i); ctx.lineTo(w, (h / 4) * i); ctx.stroke();
  }
  const hist = state.errorHistory;
  if (hist.length === 0) return;
  const maxR = Math.max(15.0, ...hist.map((p) => p.rmsePct));
  ctx.strokeStyle = '#5bdb87';
  ctx.lineWidth = 2.0;
  ctx.beginPath();
  for (let i = 0; i < hist.length; i++) {
    const x = hist.length === 1 ? w * 0.5 : 14 + (i / (hist.length - 1)) * (w - 28);
    const y = h - 12 - (hist[i].rmsePct / maxR) * (h - 26);
    if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();
  for (let i = 0; i < hist.length; i++) {
    const x = hist.length === 1 ? w * 0.5 : 14 + (i / (hist.length - 1)) * (w - 28);
    const y = h - 12 - (hist[i].rmsePct / maxR) * (h - 26);
    ctx.fillStyle = i === hist.length - 1 ? '#ffb454' : '#5bdb87';
    ctx.beginPath(); ctx.arc(x, y, 3.0, 0, Math.PI * 2); ctx.fill();
  }
  const latest = hist[hist.length - 1];
  ctx.fillStyle = '#aeb4c2';
  ctx.font = '500 9.5px "DM Sans", sans-serif';
  ctx.fillText(`RELATIVE RMSE % CONVERGENCE (NOW: ${latest.rmsePct.toFixed(1)}%)`, 8, 13);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  PROJECT ZERO OUTLINER & LIGHT COMPONENT INSPECTOR
//------------------------------------------------------------------------------------------------------------------------

function RenderOutliner() {
  const wrap = document.getElementById('OutlinerRows');
  wrap.innerHTML = '';

  const groups = [
    { title: 'Dynamic System · Cube-Grid Pods & Nanite BVH', items: state.components.filter((c) => c.mobility === 'dynamic') },
    { title: 'Static System · Auto-Baked Toroidal Clipmap',  items: state.components.filter((c) => c.mobility === 'static') },
  ];

  for (const grp of groups) {
    const gh = document.createElement('div');
    gh.className = 'outliner-group';
    gh.textContent = `${grp.title} (${grp.items.length})`;
    wrap.appendChild(gh);

    for (const comp of grp.items) {
      const row = document.createElement('div');
      row.className = `outliner-row${state.selectedComponentId === comp.id ? ' selected' : ''}${comp.enabled ? '' : ' disabled-row'}`;
      const colCss = `rgb(${Math.round(comp.color[0] * 255)},${Math.round(comp.color[1] * 255)},${Math.round(comp.color[2] * 255)})`;
      row.innerHTML = `
        <span class="row-dot" style="background:${colCss};box-shadow:0 0 6px ${colCss};"></span>
        <div class="row-text">
          <span class="row-name">${comp.name}</span>
          <span class="row-sub">${comp.kind} &middot; ${comp.subCount} emitters &middot; ${comp.dimmer.toFixed(2)}&times;</span>
        </div>
        <button class="mob-chip ${comp.mobility}" title="Click to toggle Static ↔ Dynamic">${comp.mobility}</button>
      `;
      row.addEventListener('click', (e) => {
        if (e.target.classList.contains('mob-chip')) {
          e.stopPropagation();
          ToggleComponentMobility(comp.id);
          return;
        }
        state.selectedComponentId = comp.id;
        SelectPage(1); // Open Project Zero Light Component Inspector!
        RenderOutliner();
        SyncComponentInspector();
      });
      wrap.appendChild(row);
    }
  }
}

function ToggleComponentMobility(compId, forcedMobility = null) {
  const comp = state.components.find((c) => c.id === compId);
  if (!comp) return;
  comp.mobility = forcedMobility || (comp.mobility === 'static' ? 'dynamic' : 'static');
  staticDirty = true; // Auto-triggers Static Toroidal Clipmap re-bake!
  SyncLightingSystems(false);
  RenderOutliner();
  SyncComponentInspector();
  ScheduleGpuBenchmark(120);
}

function SyncComponentInspector() {
  const comp = state.components.find((c) => c.id === state.selectedComponentId) || state.components[0];
  document.getElementById('CompCardTitle').innerHTML = `${comp.name} <small id="CompCardKind">${comp.kind}</small>`;
  document.getElementById('BtnCompStatic').classList.toggle('active', comp.mobility === 'static');
  document.getElementById('BtnCompDynamic').classList.toggle('active', comp.mobility === 'dynamic');

  const enTile = document.getElementById('TileCompEnabled');
  enTile.classList.toggle('on', comp.enabled);
  enTile.classList.toggle('off', !comp.enabled);
  document.getElementById('TxtCompEnabled').textContent = comp.enabled ? 'ON' : 'OFF';

  SyncSliderUI('RangeCompDimmer', 'OutCompDimmer', comp.dimmer, (v) => v.toFixed(2));
  SyncSliderUI('RangeCompCone', 'OutCompCone', comp.coneDeg, (v) => String(Math.round(v)));
  SyncSliderUI('RangeCompKelvin', 'OutCompKelvin', comp.temperatureK, (v) => String(Math.round(v)));

  document.getElementById('CompMobilityNote').textContent = comp.mobility === 'static'
    ? `STATIC: Auto-baked into the 2-Cascade Toroidal Clipmap (${state.fineRes}×${state.fineRes} Fine + Coarse) -> O(1) lookup per pixel!`
    : `DYNAMIC: Evaluated via 1 Cube-Grid Pod (${ONE_CUBE_GRID_POD_KB} KB) + Nanite Distance Light BVH -> zero static re-bake when moving!`;
}

function UpdateHeadlightDiagnostic() {
  const banner = document.getElementById('HeadlightStatusBanner');
  const title = document.getElementById('HeadlightBannerTitle');
  const body = document.getElementById('HeadlightBannerBody');

  if (state.mode === 'naive') {
    banner.className = 'status-banner warn';
    title.textContent = '⚠ Naive Point Merge: Headlights Averaged @ Center!';
    body.textContent = 'Left (5) + Right (5) collapsed into 1 grille point -> wrong shadows behind bollards & huge RMSE %.';
    return;
  }
  if (state.mode === 'pods') {
    banner.className = 'status-banner';
    title.textContent = `✓ 1 Cube-Grid/Pod (${allPods.length} Pods · ${(allPods.length * ONE_CUBE_GRID_POD_KB / 1024).toFixed(2)} MB)`;
    body.textContent = 'Each tight light fixture (5 LEDs -> 1 Pod) bakes its directional cone into a 95.1 KB Cube-Grid Pod without merging opposing lights.';
    return;
  }
  const hlLeft = state.components.find((c) => c.id === 'comp-hl-left');
  const hlRight = state.components.find((c) => c.id === 'comp-hl-right');
  const bothStatic = hlLeft.mobility === 'static' && hlRight.mobility === 'static';
  if (bothStatic) {
    banner.className = 'status-banner';
    title.textContent = '✓ Headlights Routed to STATIC Toroidal Clipmap!';
    body.textContent = `Auto-baked into Fine/Coarse toroidal cascades (${staticClipmap?.bakeMs.toFixed(1) || '0.0'} ms). Switch back to Dynamic in Outliner for moving headlights!`;
    return;
  }

  let mergedLR = false;
  for (const n of dynCut) {
    if (n.members.some((m) => m.subGroup === 'LeftHeadlight') && n.members.some((m) => m.subGroup === 'RightHeadlight')) mergedLR = true;
  }
  const distToCar = Math.hypot(state.probePos[0], state.probePos[1] + 2.1);
  banner.className = 'status-banner';
  if (state.mode === 'unified' && mergedLR) {
    title.textContent = `✓ Unified All-3: Far Headlights MERGED (${distToCar.toFixed(1)}m)`;
    body.textContent = `Static lights sampled from Toroidal Clipmap (1×) while distant Dynamic Headlights safely merged into 1 flux-summed node!`;
  } else {
    title.textContent = `✓ Unified All-3: Static Clipmap + Split Dynamic Headlights (${distToCar.toFixed(1)}m)`;
    body.textContent = `Static (${staticLights.length}) in Toroidal Clipmap · Dynamic (${dynamicLights.length}) in Cube-Grid Pods/Nanite BVH with L+R Headlights SPLIT!`;
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  NAVIGATION, SCENE SCALE & INPUT WIRING
//------------------------------------------------------------------------------------------------------------------------

function SelectPage(idx) {
  state.activePage = Math.max(0, Math.min(3, idx));
  document.querySelectorAll('#PageCarouselRail .pc-pill').forEach((p) => {
    p.classList.toggle('open', parseInt(p.dataset.page, 10) === state.activePage);
  });
  document.querySelectorAll('.pc-page').forEach((pg) => {
    pg.classList.toggle('open', parseInt(pg.dataset.page, 10) === state.activePage);
  });
  document.getElementById('BtnPagePrev').disabled = state.activePage === 0;
  document.getElementById('BtnPageNext').disabled = state.activePage === 3;
}

function SelectMode(modeKey) {
  state.mode = modeKey;
  const meta = MODE_META[modeKey];
  document.getElementById('ApproxTitle').textContent = meta.title;
  document.getElementById('ApproxSub').textContent = meta.sub;
  document.getElementById('ApproxDot').className = `dot ${meta.dot}`;
  RenderBenchmarkTable();
  UpdateHeadlightDiagnostic();
}

function SelectDebugView(dbgIdx) {
  state.debugView = dbgIdx;
  document.querySelectorAll('#DebugViewGroup .dbg-pill').forEach((b) => {
    b.classList.toggle('active', parseInt(b.dataset.dbg, 10) === dbgIdx);
  });
  document.getElementById('BtnBenchErrorToggle').classList.toggle('active', dbgIdx === 1);
}

function ApplySceneScale(scaleKey) {
  state.sceneScale = scaleKey;
  const cfg = SCENE_SCALES[scaleKey];
  state.fineRes = cfg.fineRes;
  state.lodThreshold = cfg.defaultLodBudget;
  state.hybridErrorGate = cfg.defaultHybridGate;
  document.querySelectorAll('#SceneScaleGroup button').forEach((b) => {
    b.classList.toggle('active', b.dataset.scale === scaleKey);
  });
  document.getElementById('VpScaleCaption').textContent = cfg.label;
  document.getElementById('ScaleDescNote').textContent = cfg.desc;
  SyncSliderUI('RangeFineRes', 'OutFineRes', state.fineRes, (v) => `${Math.round(v)}×${Math.round(v)}`);
  SyncSliderUI('RangeLodThreshold', 'OutLodThreshold', state.lodThreshold, (v) => v.toFixed(2));
  SyncSliderUI('RangeHybridGate', 'OutHybridGate', state.hybridErrorGate, (v) => v.toFixed(2));
  staticDirty = true;
  SyncLightingSystems(true);
  ScheduleGpuBenchmark(120);
}

function UpdateRangeFill(el) {
  const min = parseFloat(el.min), max = parseFloat(el.max), val = parseFloat(el.value);
  el.style.setProperty('--fill', `${((val - min) / Math.max(1e-6, max - min)) * 100}%`);
}
function SyncSliderUI(id, outId, val, fmt) {
  const el = document.getElementById(id);
  el.value = String(val);
  document.getElementById(outId).textContent = fmt(val);
  UpdateRangeFill(el);
}

function SyncQuickTiles() {
  const setT = (id, txtId, on) => {
    const el = document.getElementById(id);
    if (!el) return;
    el.classList.toggle('on', on);
    el.classList.toggle('off', !on);
    document.getElementById(txtId).textContent = on ? 'ON' : 'OFF';
  };
  setT('TileAutoBake', 'TxtAutoBake', state.autoBake);
  setT('TileShowGrid', 'TxtShowGrid', state.showGrid);
  setT('TileShowLights', 'TxtShowLights', state.showLights);
  setT('TileAnimateDrone', 'TxtAnimateDrone', state.animateDrone);
  document.getElementById('FootBakeState').textContent = state.autoBake ? 'ON' : 'MANUAL';
  document.getElementById('BakeAutoBadge').textContent = state.autoBake ? 'AUTO ON' : 'MANUAL';
}

function InitUI() {
  document.querySelectorAll('input[type="range"]').forEach(UpdateRangeFill);

  // PageCarousel
  document.querySelectorAll('#PageCarouselRail .pc-pill').forEach((p) => {
    p.addEventListener('click', () => SelectPage(parseInt(p.dataset.page, 10)));
  });
  document.getElementById('BtnPagePrev').addEventListener('click', () => SelectPage(state.activePage - 1));
  document.getElementById('BtnPageNext').addEventListener('click', () => SelectPage(state.activePage + 1));

  // Viewport Header: Scene Scale + Debug Views + Camera Presets
  document.querySelectorAll('#SceneScaleGroup button').forEach((b) => {
    b.addEventListener('click', () => ApplySceneScale(b.dataset.scale));
  });
  document.querySelectorAll('#DebugViewGroup .dbg-pill').forEach((b) => {
    b.addEventListener('click', () => SelectDebugView(parseInt(b.dataset.dbg, 10)));
  });
  document.getElementById('BtnBenchErrorToggle').addEventListener('click', () => {
    SelectDebugView(state.debugView === 1 ? 0 : 1);
  });
  document.getElementById('BtnReMeasureGpu').addEventListener('click', () => { benchPending = true; });

  const camPresets = {
    angle:      { yaw: 0.42, pitch: 0.68, dist: 20.5, target: [0.0, 0.4, 0.2] },
    top:        { yaw: 0.0,  pitch: 1.54, dist: 25.0, target: [0.0, 0.0, 0.0] },
    headlights: { yaw: 0.22, pitch: 0.42, dist: 10.5, target: [0.0, 0.5, 0.4] },
    neon:       { yaw: 0.85, pitch: 0.54, dist: 12.0, target: [5.2, 0.7, -1.8] },
  };
  document.querySelectorAll('#CamPresetGroup button').forEach((b) => {
    b.addEventListener('click', () => {
      document.querySelectorAll('#CamPresetGroup button').forEach((x) => x.classList.remove('active'));
      b.classList.add('active');
      const p = camPresets[b.dataset.cam];
      state.yaw = p.yaw; state.pitch = p.pitch; state.dist = p.dist; state.target = [...p.target];
    });
  });

  // Error-Guided Auto-Refine
  document.getElementById('BtnAutoRefine').addEventListener('click', () => {
    state.refineStep++;
    document.getElementById('RefineIterLabel').textContent = `Step ${state.refineStep}`;
    if (state.mode === 'naive') { SelectMode('clipmap'); ScheduleGpuBenchmark(80); return; }
    if (state.mode === 'clipmap') { SelectMode('unified'); ScheduleGpuBenchmark(80); return; }
    state.hybridErrorGate = Math.max(0.03, +(state.hybridErrorGate * 0.72).toFixed(2));
    state.lodThreshold = Math.max(0.06, +(state.lodThreshold * 0.78).toFixed(2));
    state.fineRes = Math.min(24, state.fineRes + 2);
    SyncSliderUI('RangeFineRes', 'OutFineRes', state.fineRes, (v) => `${Math.round(v)}×${Math.round(v)}`);
    SyncSliderUI('RangeLodThreshold', 'OutLodThreshold', state.lodThreshold, (v) => v.toFixed(2));
    SyncSliderUI('RangeHybridGate', 'OutHybridGate', state.hybridErrorGate, (v) => v.toFixed(2));
    staticDirty = true;
    SyncLightingSystems(true);
    ScheduleGpuBenchmark(100);
  });
  document.getElementById('BtnResetRefine').addEventListener('click', () => {
    state.refineStep = 0;
    state.errorHistory = [];
    document.getElementById('RefineIterLabel').textContent = 'Step 0';
    ApplySceneScale(state.sceneScale);
    SelectMode('naive');
  });

  // Page 2: Project Zero Light Component Controls
  document.getElementById('BtnCompStatic').addEventListener('click', () => ToggleComponentMobility(state.selectedComponentId, 'static'));
  document.getElementById('BtnCompDynamic').addEventListener('click', () => ToggleComponentMobility(state.selectedComponentId, 'dynamic'));
  document.getElementById('TileCompEnabled').addEventListener('click', () => {
    const c = state.components.find((x) => x.id === state.selectedComponentId);
    if (!c) return;
    c.enabled = !c.enabled;
    if (c.mobility === 'static') staticDirty = true;
    SyncLightingSystems(false);
    RenderOutliner();
    SyncComponentInspector();
    ScheduleGpuBenchmark(140);
  });

  const bindRange = (id, outId, fmt, fn) => {
    const el = document.getElementById(id);
    const out = document.getElementById(outId);
    el.addEventListener('input', () => {
      UpdateRangeFill(el);
      const v = parseFloat(el.value);
      out.textContent = fmt(v);
      fn(v);
    });
  };
  bindRange('RangeCompDimmer', 'OutCompDimmer', (v) => v.toFixed(2), (v) => {
    const c = state.components.find((x) => x.id === state.selectedComponentId);
    if (!c) return;
    c.dimmer = v;
    if (c.mobility === 'static') staticDirty = true;
    SyncLightingSystems(false);
    RenderOutliner();
    ScheduleGpuBenchmark(240);
  });
  bindRange('RangeCompCone', 'OutCompCone', (v) => String(Math.round(v)), (v) => {
    const c = state.components.find((x) => x.id === state.selectedComponentId);
    if (!c) return;
    c.coneDeg = v;
    if (c.mobility === 'static') staticDirty = true;
    SyncLightingSystems(false);
    ScheduleGpuBenchmark(240);
  });
  bindRange('RangeCompKelvin', 'OutCompKelvin', (v) => String(Math.round(v)), (v) => {
    const c = state.components.find((x) => x.id === state.selectedComponentId);
    if (!c) return;
    c.temperatureK = v;
    c.color = KelvinToTintRgb(v);
    if (c.mobility === 'static') staticDirty = true;
    SyncLightingSystems(false);
    RenderOutliner();
    ScheduleGpuBenchmark(240);
  });

  // Page 3 & 4 Controls
  document.getElementById('TileAutoBake').addEventListener('click', () => { state.autoBake = !state.autoBake; SyncQuickTiles(); });
  document.getElementById('TileShowGrid').addEventListener('click', () => { state.showGrid = !state.showGrid; SyncQuickTiles(); });
  document.getElementById('TileShowLights').addEventListener('click', () => { state.showLights = !state.showLights; SyncQuickTiles(); });
  document.getElementById('TileAnimateDrone').addEventListener('click', () => { state.animateDrone = !state.animateDrone; SyncQuickTiles(); });
  document.getElementById('BtnForceBake').addEventListener('click', () => { staticDirty = true; SyncLightingSystems(true); ScheduleGpuBenchmark(100); });

  bindRange('RangeFineRes', 'OutFineRes', (v) => `${Math.round(v)}×${Math.round(v)}`, (v) => { state.fineRes = Math.round(v); staticDirty = true; SyncLightingSystems(true); ScheduleGpuBenchmark(); });
  bindRange('RangeLodThreshold', 'OutLodThreshold', (v) => v.toFixed(2), (v) => { state.lodThreshold = v; SyncLightingSystems(false); ScheduleGpuBenchmark(); });
  bindRange('RangeHybridGate', 'OutHybridGate', (v) => v.toFixed(2), (v) => { state.hybridErrorGate = v; ScheduleGpuBenchmark(); });
  bindRange('RangeErrorGain', 'OutErrorGain', (v) => v.toFixed(1), (v) => { state.errorGain = v; });
  bindRange('RangeRoughness', 'OutRoughness', (v) => v.toFixed(2), (v) => { state.floorRoughness = v; });

  // Synchronized Orbit + Click-Floor-to-Scroll Toroidal Fine Clipmap & Distance Probe
  for (const vp of allViewports) {
    let dragging = false, moved = false, lastX = 0, lastY = 0;
    vp.overlayCanvas.addEventListener('pointerdown', (e) => {
      dragging = true; moved = false; lastX = e.clientX; lastY = e.clientY;
      vp.overlayCanvas.setPointerCapture(e.pointerId);
    });
    vp.overlayCanvas.addEventListener('pointermove', (e) => {
      if (!dragging) return;
      const dx = e.clientX - lastX, dy = e.clientY - lastY;
      if (Math.hypot(dx, dy) > 2) moved = true;
      lastX = e.clientX; lastY = e.clientY;
      state.yaw -= dx * 0.007;
      state.pitch = Math.max(0.16, Math.min(1.55, state.pitch + dy * 0.007));
    });
    vp.overlayCanvas.addEventListener('pointerup', (e) => {
      if (dragging && !moved) {
        const hit = vp.unprojectToFloor(BuildCameraFrame(), e.clientX, e.clientY);
        if (hit) {
          state.probePos = hit;
          SyncLightingSystems(false);
          ScheduleGpuBenchmark(160);
        }
      }
      dragging = false;
    });
    vp.overlayCanvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      state.dist = Math.max(6.5, Math.min(36.0, state.dist * (1.0 + e.deltaY * 0.001)));
    }, { passive: false });
  }
}

function Tick(nowMs) {
  requestAnimationFrame(Tick);
  const dt = Math.min(0.05, Math.max(0.001, (nowMs - lastFrameMs) / 1000.0));
  lastFrameMs = nowMs;
  if (state.animateDrone) state.timeSec += dt;
  fpsSmooth = fpsSmooth * 0.9 + (1.0 / dt) * 0.1;
  document.getElementById('FootFps').textContent = String(Math.round(fpsSmooth));

  SyncLightingSystems(false);
  const cam = BuildCameraFrame();
  const modeId = MODE_META[state.mode].id;
  const approxList = GetApproxLightArrayForMode(state.mode);

  const commonParams = {
    errorGain: state.errorGain,
    floorRoughness: state.floorRoughness,
    hybridErrorGate: state.hybridErrorGate,
    showGrid: state.showGrid,
  };

  if (benchPending) {
    benchPending = false;
    RunSideBySideGpuBenchmark(cam, commonParams);
  }

  // Left: Exact LOD 0 Reference | Right: Selected Pipeline with active Project Zero Debug View | Bottom: Live Error x8 Heatmap
  vpTruth.render(cam, rawLights, rawLights, staticPromoCut, { ...commonParams, lightMode: 0, debugView: 0 });
  vpApprox.render(cam, rawLights, approxList, staticPromoCut, { ...commonParams, lightMode: modeId, debugView: state.debugView });
  vpError.render(cam, rawLights, approxList, staticPromoCut, { ...commonParams, lightMode: modeId, debugView: 1 });

  const overlayBase = {
    rawLights,
    approxLights: approxList,
    clipmap: staticClipmap,
    showGrid: state.showGrid,
    showLights: state.showLights,
    debugView: state.debugView,
    probePos: state.probePos,
  };
  vpTruth.drawDebugOverlay(cam, { ...overlayBase, panelRole: 'truth' });
  vpApprox.drawDebugOverlay(cam, { ...overlayBase, panelRole: 'approx' });
  vpError.drawDebugOverlay(cam, { ...overlayBase, panelRole: 'error', showLights: false });
}

InitUI();
SyncQuickTiles();
RenderOutliner();
SyncComponentInspector();
ApplySceneScale('medium');
SelectMode('unified');
SelectPage(0);
requestAnimationFrame(Tick);
