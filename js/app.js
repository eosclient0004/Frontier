//============================================================================================================================================
//  APP.JS — FRONTIER WEBGPU TERRAIN & SATMAP TEXTURE LAYER-STACK STUDIO
//  UI Design Language: Experimental/ProjectZeroEditor (Editor.css, WorkspaceCards.css, MaterialPanel.css)
//  Layout: [3D/2D WebGPU Viewport (Left/Main) | Layer Stack Dock (Right-Inner) | Inspector Dock (Right-Outer)]
//============================================================================================================================================

import {
  SATMAPS,
  BLEND_MODES,
  TEX_BLEND_MODES,
  TERRAIN_LAYER_TYPES,
  TEXTURE_LAYER_TEMPLATES,
  WORLD_PRESETS,
  createTerrainLayer,
  createTextureLayer,
} from "./presets.js";
import {
  TerrainStudioEngine,
  mat4Mul,
  mat4LookAt,
  mat4Perspective,
  mat4Invert,
} from "./engine.js";
import {
  buildElevationHistogramSVG,
  buildMaskWindowSVG,
  buildReposeAngleSVG,
  buildSunOrbitSVG,
} from "./diagrams.js";
import {
  exportHeightmap16Bit,
  exportAlbedoPNG,
  exportNormalMapPNG,
  exportSplatMapPNG,
  exportWavefrontOBJ,
  exportProjectJSON,
} from "./export.js";

const $ = (sel) => document.querySelector(sel);

function el(tag, attrs = {}, ...kids) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === undefined || v === null || v === false) continue;
    if (k === "class") node.className = v;
    else if (k === "text") node.textContent = v;
    else if (k === "html") node.innerHTML = v;
    else if (k === "style" && typeof v === "string") node.setAttribute("style", v);
    else if (k.startsWith("on") && typeof v === "function") {
      node.addEventListener(k.slice(2).toLowerCase(), v);
    } else if (k === "value") node.value = v;
    else if (k === "checked") node.checked = !!v;
    else node.setAttribute(k, v === true ? "" : String(v));
  }
  for (const kid of kids.flat()) {
    if (kid === null || kid === undefined || kid === false) continue;
    node.append(kid.nodeType ? kid : document.createTextNode(String(kid)));
  }
  return node;
}

const clamp = (v, a, b) => Math.min(b, Math.max(a, v));

// SVG Checkmark for ProjectZeroEditor .status-disc / .row-status-btn
function checkMarkSVG() {
  const span = document.createElement("span");
  span.style.display = "inline-flex";
  span.innerHTML = `<svg width="11" height="11" viewBox="0 0 12 12" fill="none"><path d="M2.5 6.2L5 8.7L9.6 3.5" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
  return span.firstElementChild;
}

function quickIconSVG(kind) {
  const span = document.createElement("span");
  span.style.display = "inline-flex";
  const paths = {
    power: `<path d="M12 3v9m5.66-5.66a8 8 0 1 1-11.31 0" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"/>`,
    warp: `<path d="M3 8c4-4 6 4 10 0s4-4 8 0M3 16c4-4 6 4 10 0s4-4 8 0" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" fill="none"/>`,
    invert: `<circle cx="12" cy="12" r="8" stroke="currentColor" stroke-width="1.7" fill="none"/><path d="M12 4a8 8 0 0 1 0 16V4z" fill="currentColor"/>`,
    solo: `<polygon points="12,3 14.8,8.8 21,9.7 16.5,14.1 17.6,20.3 12,17.3 6.4,20.3 7.5,14.1 3,9.7 9.2,8.8" stroke="currentColor" stroke-width="1.6" fill="none"/>`,
    satmap: `<rect x="3" y="5" width="18" height="14" rx="3" stroke="currentColor" stroke-width="1.7" fill="none"/><path d="M3 15l5-4 4 3 5-5 4 4" stroke="currentColor" stroke-width="1.6" fill="none"/>`,
    triplanar: `<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9L12 3zM12 12l8-4.5M12 12v9M12 12L4 7.5" stroke="currentColor" stroke-width="1.6" fill="none"/>`,
    mask: `<circle cx="12" cy="12" r="8" stroke="currentColor" stroke-width="1.7" stroke-dasharray="3 2" fill="none"/><circle cx="12" cy="12" r="3.5" fill="currentColor"/>`,
    erode: `<path d="M12 3c-3.5 5-6 8-6 11a6 6 0 0 0 12 0c0-3-2.5-6-6-11z" stroke="currentColor" stroke-width="1.7" fill="none"/>`,
  };
  span.innerHTML = `<svg width="20" height="20" viewBox="0 0 24 24" fill="none">${paths[kind] || paths.power}</svg>`;
  return span.firstElementChild;
}

//============================================================================================================================================
//  APPLICATION STATE
//============================================================================================================================================
const initialPreset = WORLD_PRESETS.matterhorn;

const state = {
  presetId: "matterhorn",
  activeStack: "terrain", // "terrain" | "texture" | "world" | "all"
  inspectorTab: "inspector", // "inspector" | "masks" | "export"
  viewportLayout: "3d", // "3d" | "split" | "map" | "profile"
  shadingMode: 0, // 0=Lit PBR, 1=Clay, 2=Elevation, 3=Slope, 4=Flow, 5=Sediment, 6=Snow, 7=Curvature, 8=LayerMask, 9=Albedo
  autoErode: false,
  turntable: false,
  searchQuery: "",
  categoryFilter: "All",

  env: structuredClone(initialPreset.env),
  terrainStack: structuredClone(initialPreset.terrainStack),
  textureStack: structuredClone(initialPreset.textureStack),

  selection: {
    kind: "terrain", // "terrain" | "texture" | "world"
    id: initialPreset.terrainStack[0].id,
    worldSection: "sun", // "sun" | "sky" | "fog" | "water"
  },

  cam: {
    yaw: 0.68,
    pitch: 0.42,
    dist: 11.2,
    target: [0, 0.95, 0],
    fovDeg: 45,
  },

  cursorTelemetry: null,
  constructModal: {
    open: false,
    targetStack: "terrain", // "terrain" | "texture"
    category: "All",
    query: "",
    selectedKey: "mountain_range",
  },
};

let engine = null;

//============================================================================================================================================
//  PROJECT-ZERO INSPECTOR WIDGET BUILDERS (.slider-pill, .split-value, .switch-row, .quick-tile, .property-card)
//============================================================================================================================================
function setSliderFill(inputEl) {
  const min = parseFloat(inputEl.min) || 0;
  const max = parseFloat(inputEl.max) || 100;
  const val = parseFloat(inputEl.value) || 0;
  const pct = clamp(((val - min) / (max - min || 1)) * 100, 0, 100);
  inputEl.style.setProperty("--fill", `${pct.toFixed(1)}%`);
}

function sliderField({ label, hint, value, min, max, step = 1, unit = "", digits = null, onChange }) {
  const fmtVal = (v) => {
    if (digits !== null) return Number(v).toFixed(digits);
    if (step < 0.01) return Number(v).toFixed(3);
    if (step < 0.1) return Number(v).toFixed(2);
    if (step < 1) return Number(v).toFixed(1);
    return String(Math.round(v));
  };

  const numInput = el("input", {
    type: "number",
    value: fmtVal(value),
    min,
    max,
    step,
  });

  const rangeInput = el("input", {
    type: "range",
    value,
    min,
    max,
    step,
  });
  setSliderFill(rangeInput);

  const commit = (raw, fromRange) => {
    const v = clamp(parseFloat(raw) || 0, min, max);
    if (fromRange) {
      numInput.value = fmtVal(v);
    } else {
      rangeInput.value = v;
    }
    setSliderFill(rangeInput);
    onChange(v);
  };

  rangeInput.addEventListener("input", () => commit(rangeInput.value, true));
  numInput.addEventListener("change", () => commit(numInput.value, false));

  return el(
    "div",
    { class: "field" },
    el(
      "div",
      { class: "field-head" },
      el("span", { text: label }),
      hint ? el("small", { text: hint }) : null
    ),
    el(
      "div",
      { class: "slider-pill" },
      el("div", { class: "split-value" }, numInput, el("small", { text: unit })),
      rangeInput
    )
  );
}

function switchField(label, checked, onChange) {
  const btn = el(
    "button",
    {
      type: "button",
      class: `toggle ${checked ? "on" : ""}`,
      "aria-pressed": checked ? "true" : "false",
      onClick: () => {
        const next = !btn.classList.contains("on");
        btn.classList.toggle("on", next);
        btn.setAttribute("aria-pressed", next ? "true" : "false");
        onChange(next);
      },
    },
    el("i")
  );
  return el("div", { class: "switch-row" }, el("span", { text: label }), btn);
}

function colorField(label, hexValue, onChange) {
  const codeEl = el("code", { text: String(hexValue).toUpperCase() });
  const input = el("input", {
    type: "color",
    value: hexValue,
    onInput: (e) => {
      codeEl.textContent = e.target.value.toUpperCase();
      onChange(e.target.value);
    },
  });
  return el("div", { class: "colour-field" }, el("span", { text: label }), codeEl, input);
}

function selectField(label, currentVal, options, onChange) {
  const sel = el(
    "select",
    {
      onChange: (e) => onChange(e.target.value),
    },
    options.map((o) =>
      el("option", {
        value: o.value,
        text: o.label,
        selected: String(o.value) === String(currentVal),
      })
    )
  );
  return el(
    "div",
    { class: "field" },
    el("div", { class: "field-head" }, el("span", { text: label })),
    sel
  );
}

function quickTile({ label, on, icon, statusText, onClick }) {
  return el(
    "button",
    {
      type: "button",
      class: `quick-tile ${on ? "on" : "off"}`,
      onClick,
    },
    el("span", { class: "quick-icon-seat" }, quickIconSVG(icon)),
    el("span", { class: "quick-label", text: label }),
    el("span", { class: "quick-status", text: statusText ?? (on ? "ON" : "OFF") })
  );
}

//============================================================================================================================================
//  PRESET & WORKFLOW ACTIONS
//============================================================================================================================================
function applyWorldPreset(presetId) {
  const preset = WORLD_PRESETS[presetId];
  if (!preset) return;
  state.presetId = presetId;
  state.env = structuredClone(preset.env);
  state.terrainStack = structuredClone(preset.terrainStack);
  state.textureStack = structuredClone(preset.textureStack);
  if (engine) engine.sculptOffsets.fill(0);

  if (state.activeStack === "texture" && state.textureStack[0]) {
    state.selection = { kind: "texture", id: state.textureStack[0].id };
  } else if (state.terrainStack[0]) {
    state.selection = { kind: "terrain", id: state.terrainStack[0].id };
  }

  rebuildTerrain({ fullRebuild: true });
  renderAllUI();
  showToast(`Loaded World Preset: ${preset.name}`);
}

let rebuildPending = false;
function rebuildTerrain(options = { fullRebuild: true }) {
  if (!engine) return;
  if (rebuildPending) return;
  rebuildPending = true;
  requestAnimationFrame(() => {
    rebuildPending = false;
    engine.evaluateStack(state, options);
    updateTelemetryUI();
    // Refresh live SVG diagrams in the Inspector if open
    refreshLiveInspectorDiagrams();
  });
}

function showToast(msg) {
  const existing = $(".toast");
  if (existing) existing.remove();
  const node = el("div", { class: "toast" }, el("i"), el("span", { text: msg }));
  document.body.appendChild(node);
  setTimeout(() => node.remove(), 2600);
}

//============================================================================================================================================
//  RENDER RIGHT-INNER DOCK: LAYER STACK (TERRAIN STACK / TEXTURE STACK / WORLD / ALL)
//============================================================================================================================================
function renderLayerStackDock() {
  const tabsEl = $("#StackTabs");
  if (tabsEl) {
    tabsEl.innerHTML = "";
    const tabs = [
      { id: "terrain", label: "01 Terrain", badge: state.terrainStack.length },
      { id: "texture", label: "02 Texture", badge: state.textureStack.length },
      { id: "world", label: "03 World", badge: 4 },
      { id: "all", label: "All", badge: state.terrainStack.length + state.textureStack.length },
    ];
    for (const t of tabs) {
      tabsEl.append(
        el(
          "button",
          {
            type: "button",
            class: `document-tab ${state.activeStack === t.id ? "active" : ""}`,
            onClick: () => switchWorkflowStage(t.id),
          },
          el("span", { text: t.label }),
          el("span", { class: "tab-badge", text: String(t.badge) })
        )
      );
    }
    tabsEl.append(
      el(
        "div",
        { class: "tab-strip-right" },
        el("button", {
          type: "button",
          class: "tab-add",
          title: "Add New Layer to Active Stack (A)",
          text: "+",
          onClick: () => openConstructModal(state.activeStack === "texture" ? "texture" : "terrain"),
        })
      )
    );
  }

  // Update Top Bar Workflow Stage Pills too
  document.querySelectorAll(".workflow-pill").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.stage === state.activeStack);
  });

  // Heading
  const titleEl = $("#StackTitle");
  const subEl = $("#StackSubtitle");
  if (titleEl && subEl) {
    if (state.activeStack === "terrain") {
      titleEl.textContent = "Terrain Layer Stack";
      subEl.textContent = `${state.terrainStack.filter((l) => l.enabled).length} of ${state.terrainStack.length} active`;
    } else if (state.activeStack === "texture") {
      titleEl.textContent = "Texture Layer Stack";
      subEl.textContent = `${state.textureStack.filter((l) => l.enabled).length} of ${state.textureStack.length} active`;
    } else if (state.activeStack === "world") {
      titleEl.textContent = "World & Atmosphere";
      subEl.textContent = "Sun · Sky · Fog · Water";
    } else {
      titleEl.textContent = "Unified Layer Pipeline";
      subEl.textContent = `${state.terrainStack.length} Terrain + ${state.textureStack.length} Texture`;
    }
  }

  // Stats Tiles (ProjectZeroEditor .stats 64px tiles)
  const statsBox = $("#StackStats");
  if (statsBox) {
    statsBox.innerHTML = "";
    if (state.activeStack === "texture") {
      const activeTex = state.textureStack.filter((l) => l.enabled).length;
      statsBox.append(
        el(
          "div",
          { class: "stat-tile" },
          el("span", { class: "status-disc" }, checkMarkSVG()),
          el("small", { text: "Texture Layers" }),
          el("strong", { text: String(activeTex) })
        ),
        el(
          "div",
          { class: "stat-tile" },
          el("span", { class: "muted-disc" }, checkMarkSVG()),
          el("small", { text: "SatMap Ramps" }),
          el("strong", { text: String(SATMAPS.length) })
        )
      );
    } else {
      const activeTer = state.terrainStack.filter((l) => l.enabled).length;
      const cycles = engine ? engine.timings.erosionCycles : 0;
      statsBox.append(
        el(
          "div",
          { class: "stat-tile" },
          el("span", { class: "status-disc" }, checkMarkSVG()),
          el("small", { text: "Active Layers" }),
          el("strong", { text: String(activeTer) })
        ),
        el(
          "div",
          { class: "stat-tile" },
          el("span", { class: "status-disc" }, checkMarkSVG()),
          el("small", { text: "Erosion Steps" }),
          el("strong", { id: "StatErosionCycles", text: String(cycles) })
        )
      );
    }
  }

  // Filter Chips
  const chipsEl = $("#StackFilterChips");
  if (chipsEl) {
    chipsEl.innerHTML = "";
    const cats =
      state.activeStack === "texture"
        ? ["All", "Bedrock", "Cliffs", "Strata", "Sediment", "Hydrology", "Biome", "Snow/Ice"]
        : ["All", "Base Landforms", "Geological Structure", "Physical Erosion", "Surface Modifiers"];
    for (const c of cats) {
      chipsEl.append(
        el("button", {
          type: "button",
          class: state.categoryFilter === c ? "active" : "",
          text: c,
          onClick: () => {
            state.categoryFilter = c;
            renderLayerStackDock();
          },
        })
      );
    }
  }

  // Layer Stack Rows
  const listEl = $("#StackRows");
  if (!listEl) return;
  listEl.innerHTML = "";

  const q = state.searchQuery.trim().toLowerCase();

  const appendTerrainRows = () => {
    listEl.append(
      el(
        "div",
        { class: "stack-section-banner" },
        el("span", { text: "Stage 01 · Terrain Generation & Erosion" }),
        el("span", { text: "Bottom → Top" })
      )
    );
    state.terrainStack.forEach((layer, idx) => {
      if (state.categoryFilter !== "All" && layer.category !== state.categoryFilter) return;
      if (q && !layer.name.toLowerCase().includes(q) && !layer.type.toLowerCase().includes(q)) return;
      listEl.append(buildTerrainLayerRow(layer, idx));
    });
  };

  const appendTextureRows = () => {
    listEl.append(
      el(
        "div",
        { class: "stack-section-banner" },
        el("span", { text: "Stage 02 · SatMap & PBR Texture Splatting" }),
        el("span", { text: "Base → Top" })
      )
    );
    state.textureStack.forEach((layer, idx) => {
      if (state.categoryFilter !== "All" && layer.category !== state.categoryFilter) return;
      if (q && !layer.name.toLowerCase().includes(q) && !layer.category.toLowerCase().includes(q)) return;
      listEl.append(buildTextureLayerRow(layer, idx));
    });
  };

  const appendWorldRows = () => {
    listEl.append(
      el(
        "div",
        { class: "stack-section-banner" },
        el("span", { text: "Stage 03 · World, Atmosphere & Hydrology" }),
        el("span", { text: "Environment" })
      )
    );
    const items = [
      { id: "sun", name: "Directional Sun & Shadows", sub: `${state.env.sunAzimuth}° Az · ${state.env.sunElevation}° El · ${state.env.sunIlluminanceKlux} klux`, icon: "sun.svg", enabled: true },
      { id: "sky", name: "Rayleigh-Mie Sky & Aerial Haze", sub: `Turbidity ${state.env.mieTurbidity} · Rayleigh ${state.env.rayleigh}`, icon: "sky-scattering.svg", enabled: state.env.skyEnabled },
      { id: "fog", name: "Exponential Height Fog & Mist", sub: `Density ${Math.round(state.env.fogDensity * 100)}% · Valley Mist ${Math.round(state.env.fogValleyMist * 100)}%`, icon: "fog.svg", enabled: state.env.fogEnabled },
      { id: "water", name: "Water Table / Alpine Lake", sub: `Level ${state.env.waterLevelM} m · Waves ${Math.round(state.env.waterWaveStrength * 100)}%`, icon: "fluid.svg", enabled: state.env.waterEnabled },
    ];
    items.forEach((item, idx) => {
      const selected = state.selection.kind === "world" && state.selection.worldSection === item.id;
      listEl.append(
        el(
          "div",
          {
            class: `outliner-row ${selected ? "selected" : ""} ${!item.enabled ? "hidden-row" : ""}`,
            onClick: () => {
              state.selection = { kind: "world", id: item.id, worldSection: item.id };
              renderLayerStackDock();
              renderInspectorDock();
            },
          },
          el("span", { class: "layer-index", text: `0${idx + 1}` }),
          el(
            "div",
            { class: "layer-swatch" },
            el("img", { src: `./assets/icons/${item.icon}`, width: 15, height: 15, class: "native-icon" })
          ),
          el(
            "div",
            { class: "row-identity" },
            el("span", { class: "row-name", text: item.name }),
            el("small", { text: item.sub })
          ),
          el(
            "button",
            {
              type: "button",
              class: `row-status-btn ${item.enabled ? "" : "off"}`,
              title: "Toggle Enabled",
              onClick: (e) => {
                e.stopPropagation();
                if (item.id === "sky") state.env.skyEnabled = !state.env.skyEnabled;
                if (item.id === "fog") state.env.fogEnabled = !state.env.fogEnabled;
                if (item.id === "water") state.env.waterEnabled = !state.env.waterEnabled;
                renderLayerStackDock();
                renderInspectorDock();
              },
            },
            checkMarkSVG()
          )
        )
      );
    });
  };

  if (state.activeStack === "terrain") {
    appendTerrainRows();
  } else if (state.activeStack === "texture") {
    appendTextureRows();
  } else if (state.activeStack === "world") {
    appendWorldRows();
  } else {
    appendTerrainRows();
    appendTextureRows();
    appendWorldRows();
  }
}

function buildTerrainLayerRow(layer, idx) {
  const schema = TERRAIN_LAYER_TYPES[layer.type] || TERRAIN_LAYER_TYPES.mountain_range;
  const selected = state.selection.kind === "terrain" && state.selection.id === layer.id;
  const blendObj = BLEND_MODES.find((b) => b.id === layer.blendMode) || BLEND_MODES[0];

  let summary = `${Math.round((layer.opacity ?? 1) * 100)}%`;
  if (layer.type === "hydraulic_erosion") summary = `${layer.iterations} iters · Rain ${layer.rainRate}`;
  else if (layer.type === "thermal_erosion") summary = `${layer.iterations} iters · θc ${layer.reposeAngle}°`;
  else if (layer.type === "snowfall") summary = `Snowline ${layer.snowlineAlt} m`;
  else if (layer.elevation !== undefined) summary = `${layer.elevation} m · Scale ${layer.scale ?? 2.0}`;

  return el(
    "div",
    {
      class: `outliner-row ${selected ? "selected" : ""} ${!layer.enabled ? "hidden-row" : ""} ${layer.solo ? "solo-row" : ""}`,
      onClick: () => {
        state.selection = { kind: "terrain", id: layer.id };
        rebuildTerrain({ fullRebuild: false });
        renderLayerStackDock();
        renderInspectorDock();
      },
    },
    el("span", { class: "layer-index", text: String(idx + 1).padStart(2, "0") }),
    el(
      "div",
      {
        class: "layer-swatch",
        style: `background: radial-gradient(circle at 35% 30%, ${schema.color}44, #1b1b1b); border-color: ${schema.color}66;`,
      },
      el("img", { src: `./assets/icons/${schema.icon}`, width: 14, height: 14, class: "native-icon" })
    ),
    el(
      "div",
      { class: "row-identity" },
      el("span", { class: "row-name", text: layer.name }),
      el(
        "small",
        {},
        el("span", { class: "blend-tag", text: schema.isSimulation ? "SIM" : blendObj.short }),
        el("span", { text: summary })
      )
    ),
    el(
      "div",
      { class: "row-controls" },
      el("button", {
        type: "button",
        class: `row-mini-btn ${layer.solo ? "active" : ""}`,
        title: "Solo Layer (S)",
        text: "S",
        onClick: (e) => {
          e.stopPropagation();
          layer.solo = !layer.solo;
          rebuildTerrain({ fullRebuild: true });
          renderLayerStackDock();
          renderInspectorDock();
        },
      }),
      el("button", {
        type: "button",
        class: "row-mini-btn",
        title: "Move Layer Up",
        text: "↑",
        onClick: (e) => {
          e.stopPropagation();
          moveLayerInArray(state.terrainStack, idx, -1);
          rebuildTerrain({ fullRebuild: true });
          renderLayerStackDock();
        },
      }),
      el("button", {
        type: "button",
        class: "row-mini-btn",
        title: "Move Layer Down",
        text: "↓",
        onClick: (e) => {
          e.stopPropagation();
          moveLayerInArray(state.terrainStack, idx, 1);
          rebuildTerrain({ fullRebuild: true });
          renderLayerStackDock();
        },
      }),
      el(
        "button",
        {
          type: "button",
          class: `row-status-btn ${layer.enabled ? "" : "off"}`,
          title: layer.enabled ? "Disable Layer" : "Enable Layer",
          onClick: (e) => {
            e.stopPropagation();
            layer.enabled = !layer.enabled;
            rebuildTerrain({ fullRebuild: true });
            renderLayerStackDock();
            renderInspectorDock();
          },
        },
        checkMarkSVG()
      )
    )
  );
}

function buildTextureLayerRow(layer, idx) {
  const selected = state.selection.kind === "texture" && state.selection.id === layer.id;
  const sat = SATMAPS.find((s) => s.id === layer.satmapId) || SATMAPS[0];
  const blendObj = TEX_BLEND_MODES.find((b) => b.id === layer.blendMode) || TEX_BLEND_MODES[0];
  const gradCSS = `linear-gradient(135deg, ${sat.stops.join(", ")})`;

  const masksActive = [];
  if (layer.useAltMask) masksActive.push("Alt");
  if (layer.useSlopeMask) masksActive.push("Slope");
  if (layer.useCurvatureMask) masksActive.push(layer.curvatureMode === 0 ? "Ridge" : "Cavity");
  if (layer.useFlowMask) masksActive.push("Flow");
  if (layer.useSedimentMask) masksActive.push("Sed/Talus");
  if (layer.useSnowMask) masksActive.push("Snow");
  const maskText = masksActive.length ? masksActive.join(" · ") : "Base Fill";

  return el(
    "div",
    {
      class: `outliner-row ${selected ? "selected" : ""} ${!layer.enabled ? "hidden-row" : ""} ${layer.solo ? "solo-row" : ""}`,
      onClick: () => {
        state.selection = { kind: "texture", id: layer.id };
        rebuildTerrain({ fullRebuild: false });
        renderLayerStackDock();
        renderInspectorDock();
      },
    },
    el("span", { class: "layer-index", text: String(idx + 1).padStart(2, "0") }),
    el("div", {
      class: "layer-swatch",
      style: `background: ${layer.useSatmap ? gradCSS : `linear-gradient(135deg, ${layer.colSecondary}, ${layer.colPrimary})`};`,
    }),
    el(
      "div",
      { class: "row-identity" },
      el("span", { class: "row-name", text: layer.name }),
      el(
        "small",
        {},
        el("span", { class: "blend-tag", text: blendObj.short }),
        el("span", { text: `${Math.round(layer.opacity * 100)}% · ${maskText}` })
      )
    ),
    el(
      "div",
      { class: "row-controls" },
      el("button", {
        type: "button",
        class: `row-mini-btn ${layer.solo ? "active" : ""}`,
        title: "Solo Texture Layer",
        text: "S",
        onClick: (e) => {
          e.stopPropagation();
          layer.solo = !layer.solo;
          rebuildTerrain({ fullRebuild: false });
          renderLayerStackDock();
          renderInspectorDock();
        },
      }),
      el("button", {
        type: "button",
        class: "row-mini-btn",
        title: "Move Up",
        text: "↑",
        onClick: (e) => {
          e.stopPropagation();
          moveLayerInArray(state.textureStack, idx, -1);
          rebuildTerrain({ fullRebuild: false });
          renderLayerStackDock();
        },
      }),
      el("button", {
        type: "button",
        class: "row-mini-btn",
        title: "Move Down",
        text: "↓",
        onClick: (e) => {
          e.stopPropagation();
          moveLayerInArray(state.textureStack, idx, 1);
          rebuildTerrain({ fullRebuild: false });
          renderLayerStackDock();
        },
      }),
      el(
        "button",
        {
          type: "button",
          class: `row-status-btn ${layer.enabled ? "" : "off"}`,
          title: layer.enabled ? "Disable Texture Layer" : "Enable Texture Layer",
          onClick: (e) => {
            e.stopPropagation();
            layer.enabled = !layer.enabled;
            rebuildTerrain({ fullRebuild: false });
            renderLayerStackDock();
            renderInspectorDock();
          },
        },
        checkMarkSVG()
      )
    )
  );
}

function moveLayerInArray(arr, index, delta) {
  const target = index + delta;
  if (target < 0 || target >= arr.length) return;
  const [item] = arr.splice(index, 1);
  arr.splice(target, 0, item);
}

function switchWorkflowStage(stageId) {
  state.activeStack = stageId;
  state.categoryFilter = "All";
  if (stageId === "terrain" && state.selection.kind !== "terrain" && state.terrainStack[0]) {
    state.selection = { kind: "terrain", id: state.terrainStack[0].id };
  } else if (stageId === "texture" && state.selection.kind !== "texture" && state.textureStack[0]) {
    state.selection = { kind: "texture", id: state.textureStack[0].id };
  } else if (stageId === "world" && state.selection.kind !== "world") {
    state.selection = { kind: "world", id: "sun", worldSection: "sun" };
  }
  renderLayerStackDock();
  renderInspectorDock();
}

//============================================================================================================================================
//  RENDER RIGHT-OUTER DOCK: SPECIALIZED PROJECT-ZERO INSPECTOR
//============================================================================================================================================
function renderInspectorDock() {
  const tabsEl = $("#InspectorTabs");
  if (tabsEl) {
    tabsEl.innerHTML = "";
    const tabs = [
      { id: "inspector", label: "Inspector" },
      { id: "masks", label: "Histogram & Masks" },
      { id: "export", label: "Export & Bake" },
    ];
    for (const t of tabs) {
      tabsEl.append(
        el(
          "button",
          {
            type: "button",
            class: `document-tab ${state.inspectorTab === t.id ? "active" : ""}`,
            onClick: () => {
              state.inspectorTab = t.id;
              renderInspectorDock();
            },
          },
          el("span", { text: t.label })
        )
      );
    }
  }

  const host = $("#InspectorContent");
  if (!host) return;
  host.innerHTML = "";

  if (state.inspectorTab === "masks") {
    host.append(buildHistogramAndMasksPanel());
    return;
  }
  if (state.inspectorTab === "export") {
    host.append(buildExportAndBakePanel());
    return;
  }

  if (state.selection.kind === "texture") {
    const texLayer = state.textureStack.find((l) => l.id === state.selection.id) || state.textureStack[0];
    if (texLayer) {
      host.append(buildTextureLayerInspector(texLayer));
      return;
    }
  }

  if (state.selection.kind === "world") {
    host.append(buildWorldEnvironmentInspector(state.selection.worldSection || "sun"));
    return;
  }

  const terLayer = state.terrainStack.find((l) => l.id === state.selection.id) || state.terrainStack[0];
  if (terLayer) {
    host.append(buildTerrainLayerInspector(terLayer));
  }
}

//--------------------------------------------------------------------------------------------------------------------------------------------
//  1. TERRAIN LAYER INSPECTOR
//--------------------------------------------------------------------------------------------------------------------------------------------
function buildTerrainLayerInspector(layer) {
  const schema = TERRAIN_LAYER_TYPES[layer.type] || TERRAIN_LAYER_TYPES.mountain_range;
  const wrap = el("div", { class: "specialized-inspector" });

  // Top Header
  wrap.append(
    el(
      "div",
      { class: "inspector-top-header" },
      el(
        "div",
        { class: "breadcrumbs" },
        "INSPECTOR / TERRAIN STACK / ",
        el("span", { text: schema.category.toUpperCase() })
      ),
      el(
        "div",
        { class: "inspector-title-row" },
        el("h1", { text: layer.name }),
        el(
          "div",
          { class: "inspector-title-actions" },
          el("button", {
            type: "button",
            title: "Preview this layer's mask in viewport",
            text: state.shadingMode === 8 ? "Exit Mask View" : "Inspect Mask",
            onClick: () => {
              state.shadingMode = state.shadingMode === 8 ? 0 : 8;
              rebuildTerrain({ fullRebuild: false });
              renderViewportToolbar();
              renderInspectorDock();
            },
          }),
          el("button", {
            type: "button",
            title: "Duplicate Layer",
            text: "Duplicate",
            onClick: () => {
              const copy = structuredClone(layer);
              copy.id = "ter_" + Math.random().toString(36).slice(2, 8);
              copy.name = layer.name + " Copy";
              const idx = state.terrainStack.indexOf(layer);
              state.terrainStack.splice(idx + 1, 0, copy);
              state.selection = { kind: "terrain", id: copy.id };
              rebuildTerrain({ fullRebuild: true });
              renderLayerStackDock();
              renderInspectorDock();
            },
          }),
          state.terrainStack.length > 1
            ? el("button", {
                type: "button",
                title: "Delete Layer",
                text: "Delete",
                onClick: () => {
                  const idx = state.terrainStack.indexOf(layer);
                  if (idx >= 0) state.terrainStack.splice(idx, 1);
                  state.selection = { kind: "terrain", id: state.terrainStack[0]?.id };
                  rebuildTerrain({ fullRebuild: true });
                  renderLayerStackDock();
                  renderInspectorDock();
                },
              })
            : null
        )
      ),
      el("p", { class: "inspector-subtitle", text: schema.description })
    )
  );

  // Quick Tiles Row (.tiles & .quick-tile)
  wrap.append(
    el("div", { class: "section-caption" }, el("span", { text: "Quick Actions" }), el("small", { text: schema.name })),
    el(
      "div",
      { class: "tiles" },
      quickTile({
        label: "Layer Active",
        on: layer.enabled,
        icon: "power",
        onClick: () => {
          layer.enabled = !layer.enabled;
          rebuildTerrain({ fullRebuild: true });
          renderLayerStackDock();
          renderInspectorDock();
        },
      }),
      quickTile({
        label: "Solo Layer",
        on: layer.solo,
        icon: "solo",
        onClick: () => {
          layer.solo = !layer.solo;
          rebuildTerrain({ fullRebuild: true });
          renderLayerStackDock();
          renderInspectorDock();
        },
      }),
      quickTile({
        label: "Domain Warp",
        on: !!layer.domainWarp,
        icon: "warp",
        onClick: () => {
          layer.domainWarp = !layer.domainWarp;
          rebuildTerrain({ fullRebuild: true });
          renderInspectorDock();
        },
      }),
      quickTile({
        label: schema.isSimulation ? "Step Sim +20" : "Invert Relief",
        on: schema.isSimulation ? true : !!layer.invert,
        icon: schema.isSimulation ? "erode" : "invert",
        statusText: schema.isSimulation ? "RUN" : layer.invert ? "ON" : "OFF",
        onClick: () => {
          if (schema.isSimulation && engine) {
            engine.stepLiveErosion(state, 20);
            updateTelemetryUI();
            refreshLiveInspectorDiagrams();
          } else {
            layer.invert = !layer.invert;
            rebuildTerrain({ fullRebuild: true });
            renderInspectorDock();
          }
        },
      })
    )
  );

  // Card 1: Layer Blending & Elevation Profile Card
  const cardBlend = el(
    "div",
    { class: "property-card" },
    el(
      "h3",
      {},
      el("span", { text: "Layer Compositing & Relief Distribution" }),
      el("span", { class: "card-pill", text: schema.isSimulation ? "GPU Solver" : "Procedural" })
    ),
    el(
      "div",
      { class: "metric" },
      String(engine ? engine.stats.maxElev : 2380),
      el("small", { text: "m peak elevation" })
    ),
    el("div", { class: "card-diagram", id: "LiveHistDiagram" }, engine ? buildElevationHistogramSVG(engine.stats, state.env) : null)
  );

  if (!schema.isSimulation) {
    cardBlend.append(
      selectField(
        "Blend Operator",
        layer.blendMode,
        BLEND_MODES.map((b) => ({ value: b.id, label: b.name })),
        (v) => {
          layer.blendMode = parseInt(v, 10);
          rebuildTerrain({ fullRebuild: true });
          renderLayerStackDock();
        }
      )
    );
  }

  cardBlend.append(
    sliderField({
      label: "Layer Opacity / Weight",
      hint: "0–100%",
      value: Math.round((layer.opacity ?? 1) * 100),
      min: 0,
      max: 100,
      step: 1,
      unit: "%",
      onChange: (v) => {
        layer.opacity = v / 100;
        rebuildTerrain({ fullRebuild: true });
        renderLayerStackDock();
      },
    })
  );
  wrap.append(cardBlend);

  // Card 2: Type-Specific Geological / Erosion Parameters
  const cardParams = el(
    "div",
    { class: "property-card" },
    el(
      "h3",
      {},
      el("span", { text: `${schema.name} Parameters` }),
      layer.seed !== undefined
        ? el("button", {
            type: "button",
            class: "card-pill",
            text: `Reseed (${layer.seed})`,
            onClick: () => {
              layer.seed = Math.floor(100 + Math.random() * 899);
              rebuildTerrain({ fullRebuild: true });
              renderInspectorDock();
            },
          })
        : null
    )
  );

  const onParam = (key, val) => {
    layer[key] = val;
    rebuildTerrain({ fullRebuild: true });
    renderLayerStackDock();
  };

  if (layer.type === "mountain_range") {
    cardParams.append(
      sliderField({ label: "Relief Elevation", value: layer.elevation, min: 100, max: 3200, step: 10, unit: "m", onChange: (v) => onParam("elevation", v) }),
      sliderField({ label: "Feature Scale", value: layer.scale, min: 0.5, max: 6.0, step: 0.05, unit: "x", onChange: (v) => onParam("scale", v) }),
      sliderField({ label: "Ridge Sharpness (Arête)", value: layer.ridgeSharpness, min: 0.6, max: 2.6, step: 0.05, unit: "γ", onChange: (v) => onParam("ridgeSharpness", v) }),
      sliderField({ label: "Tectonic Strike Angle", value: layer.strikeAngle, min: 0, max: 180, step: 1, unit: "deg", onChange: (v) => onParam("strikeAngle", v) }),
      sliderField({ label: "Range Anisotropy", value: Math.round(layer.anisotropy * 100), min: 0, max: 85, step: 1, unit: "%", onChange: (v) => onParam("anisotropy", v / 100) }),
      sliderField({ label: "Glaciated Valley Widening", value: Math.round(layer.valleyFloor * 100), min: 0, max: 85, step: 1, unit: "%", onChange: (v) => onParam("valleyFloor", v / 100) }),
      sliderField({ label: "Domain Warp Intensity", value: Math.round(layer.warpStrength * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onParam("warpStrength", v / 100) }),
      sliderField({ label: "Fractal Octaves", value: layer.octaves, min: 3, max: 8, step: 1, unit: "oct", onChange: (v) => onParam("octaves", v) })
    );
  } else if (layer.type === "alpine_peak") {
    cardParams.append(
      sliderField({ label: "Summit Elevation", value: layer.elevation, min: 300, max: 3500, step: 10, unit: "m", onChange: (v) => onParam("elevation", v) }),
      sliderField({ label: "Horn Footprint Radius", value: layer.radiusKm, min: 0.8, max: 4.0, step: 0.05, unit: "km", onChange: (v) => onParam("radiusKm", v) }),
      sliderField({ label: "Radiating Arête Ridges", value: layer.areteCount, min: 3, max: 7, step: 1, unit: "arêtes", onChange: (v) => onParam("areteCount", v) }),
      sliderField({ label: "Glacial Cirque Carving", value: Math.round(layer.cirqueCarving * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onParam("cirqueCarving", v / 100) }),
      sliderField({ label: "Crag Ridge Sharpness", value: layer.ridgeSharpness, min: 0.8, max: 2.5, step: 0.05, unit: "γ", onChange: (v) => onParam("ridgeSharpness", v) }),
      sliderField({ label: "Domain Warp", value: Math.round(layer.warpStrength * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onParam("warpStrength", v / 100) })
    );
  } else if (layer.type === "canyon_mesa") {
    cardParams.append(
      sliderField({ label: "Plateau Rim Elevation", value: layer.elevation, min: 300, max: 2800, step: 10, unit: "m", onChange: (v) => onParam("elevation", v) }),
      sliderField({ label: "Gorge Incision Depth", value: Math.round(layer.canyonDepth * 100), min: 15, max: 98, step: 1, unit: "%", onChange: (v) => onParam("canyonDepth", v / 100) }),
      sliderField({ label: "Canyon Gorge Width", value: Math.round(layer.canyonWidth * 100), min: 10, max: 75, step: 1, unit: "%", onChange: (v) => onParam("canyonWidth", v / 100) }),
      sliderField({ label: "River Meander Sinuosity", value: layer.meanderFreq, min: 0.5, max: 3.5, step: 0.05, unit: "x", onChange: (v) => onParam("meanderFreq", v) }),
      sliderField({ label: "Sandstone Bench Steps", value: layer.stepCount, min: 2, max: 14, step: 1, unit: "steps", onChange: (v) => onParam("stepCount", v) }),
      sliderField({ label: "Caprock Cliff Sharpness", value: Math.round(layer.stepSharpness * 100), min: 10, max: 95, step: 1, unit: "%", onChange: (v) => onParam("stepSharpness", v / 100) })
    );
  } else if (layer.type === "volcano") {
    cardParams.append(
      sliderField({ label: "Cone Summit Elevation", value: layer.elevation, min: 400, max: 3400, step: 10, unit: "m", onChange: (v) => onParam("elevation", v) }),
      sliderField({ label: "Cone Base Radius", value: layer.radiusKm, min: 0.9, max: 4.0, step: 0.05, unit: "km", onChange: (v) => onParam("radiusKm", v) }),
      sliderField({ label: "Caldera Rim Radius", value: Math.round(layer.calderaRadius * 100), min: 12, max: 65, step: 1, unit: "%", onChange: (v) => onParam("calderaRadius", v / 100) }),
      sliderField({ label: "Caldera Collapse Depth", value: Math.round(layer.calderaDepth * 100), min: 10, max: 110, step: 1, unit: "%", onChange: (v) => onParam("calderaDepth", v / 100) }),
      sliderField({ label: "Pyroclastic Radial Gullies", value: layer.radialGullies, min: 6, max: 32, step: 1, unit: "gullies", onChange: (v) => onParam("radialGullies", v) }),
      sliderField({ label: "Lahar Gully Incision", value: Math.round(layer.gullyDepth * 100), min: 0, max: 95, step: 1, unit: "%", onChange: (v) => onParam("gullyDepth", v / 100) })
    );
  } else if (layer.type === "fault_scarp") {
    cardParams.append(
      sliderField({ label: "Fault Throw Elevation", value: layer.elevation, min: 50, max: 1500, step: 10, unit: "m", onChange: (v) => onParam("elevation", v) }),
      sliderField({ label: "Fault Strike Azimuth", value: layer.strikeAngle, min: 0, max: 180, step: 1, unit: "deg", onChange: (v) => onParam("strikeAngle", v) }),
      sliderField({ label: "Escarpment Sharpness", value: Math.round(layer.faultSharpness * 100), min: 15, max: 96, step: 1, unit: "%", onChange: (v) => onParam("faultSharpness", v / 100) }),
      sliderField({ label: "Tectonic Block Scale", value: layer.blockScale, min: 0.8, max: 6.0, step: 0.1, unit: "x", onChange: (v) => onParam("blockScale", v) }),
      sliderField({ label: "Block Tilt & Graben", value: Math.round(layer.tiltAmount * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onParam("tiltAmount", v / 100) })
    );
  } else if (layer.type === "strata") {
    cardParams.append(
      sliderField({ label: "Strata Outcrop Amplitude", value: layer.elevation, min: 20, max: 800, step: 5, unit: "m", onChange: (v) => onParam("elevation", v) }),
      sliderField({ label: "Bedding Plane Frequency", value: layer.frequency, min: 3, max: 35, step: 0.5, unit: "beds", onChange: (v) => onParam("frequency", v) }),
      sliderField({ label: "Geological Dip Angle", value: layer.dipAngle, min: 0, max: 60, step: 1, unit: "deg", onChange: (v) => onParam("dipAngle", v) }),
      sliderField({ label: "Bedding Strike Azimuth", value: layer.strikeAngle, min: 0, max: 180, step: 1, unit: "deg", onChange: (v) => onParam("strikeAngle", v) }),
      sliderField({ label: "Sandstone Ledge Sharpness", value: Math.round(layer.ledgeSharpness * 100), min: 10, max: 96, step: 1, unit: "%", onChange: (v) => onParam("ledgeSharpness", v / 100) }),
      sliderField({ label: "Anticline Fold Warp", value: Math.round(layer.foldWarp * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onParam("foldWarp", v / 100) })
    );
  } else if (layer.type === "swiss_fbm") {
    cardParams.append(
      sliderField({ label: "Crag Relief Amplitude", value: layer.elevation, min: 40, max: 1200, step: 10, unit: "m", onChange: (v) => onParam("elevation", v) }),
      sliderField({ label: "Spatial Frequency", value: layer.scale, min: 1.0, max: 10.0, step: 0.1, unit: "x", onChange: (v) => onParam("scale", v) }),
      sliderField({ label: "Crag Sharpness", value: layer.ridgeSharpness, min: 0.8, max: 2.6, step: 0.05, unit: "γ", onChange: (v) => onParam("ridgeSharpness", v) }),
      sliderField({ label: "Derivative Gradient Damping", value: Math.round(layer.gradientDamping * 100), min: 0, max: 120, step: 1, unit: "%", onChange: (v) => onParam("gradientDamping", v / 100) })
    );
  } else if (layer.type === "terrace") {
    cardParams.append(
      sliderField({ label: "Terrace Step Count", value: layer.stepCount, min: 2, max: 28, step: 1, unit: "steps", onChange: (v) => onParam("stepCount", v) }),
      sliderField({ label: "Riser Cliff Sharpness", value: Math.round(layer.stepSharpness * 100), min: 10, max: 95, step: 1, unit: "%", onChange: (v) => onParam("stepSharpness", v / 100) }),
      sliderField({ label: "Organic Bench Warp", value: Math.round(layer.warpStrength * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onParam("warpStrength", v / 100) })
    );
  } else if (layer.type === "dune_sea") {
    cardParams.append(
      sliderField({ label: "Dune Crest Height", value: layer.elevation, min: 40, max: 900, step: 10, unit: "m", onChange: (v) => onParam("elevation", v) }),
      sliderField({ label: "Dune Wavelength Scale", value: layer.scale, min: 1.5, max: 10.0, step: 0.1, unit: "x", onChange: (v) => onParam("scale", v) }),
      sliderField({ label: "Prevailing Wind Azimuth", value: layer.windAngle, min: 0, max: 180, step: 1, unit: "deg", onChange: (v) => onParam("windAngle", v) }),
      sliderField({ label: "Slip-Face Crest Sharpness", value: layer.crestSharpness, min: 0.9, max: 2.8, step: 0.05, unit: "γ", onChange: (v) => onParam("crestSharpness", v) }),
      sliderField({ label: "Windward / Leeward Asymmetry", value: Math.round(layer.asymmetry * 100), min: 10, max: 85, step: 1, unit: "%", onChange: (v) => onParam("asymmetry", v / 100) })
    );
  } else if (layer.type === "hydraulic_erosion") {
    cardParams.append(
      sliderField({ label: "Simulation Iterations", value: layer.iterations, min: 10, max: 160, step: 5, unit: "steps", onChange: (v) => onParam("iterations", v) }),
      sliderField({ label: "Precipitation / Rain Rate", value: Math.round(layer.rainRate * 1000), min: 4, max: 45, step: 1, unit: "mm", onChange: (v) => onParam("rainRate", v / 1000) }),
      sliderField({ label: "Stream Power Rock Solubility", value: Math.round(layer.erosionRate * 100), min: 10, max: 120, step: 1, unit: "%", onChange: (v) => onParam("erosionRate", v / 100) }),
      sliderField({ label: "Alluvial Fan Deposition Rate", value: Math.round(layer.depositionRate * 100), min: 10, max: 100, step: 1, unit: "%", onChange: (v) => onParam("depositionRate", v / 100) }),
      sliderField({ label: "Sediment Transport Capacity", value: layer.sedimentCapacity, min: 0.4, max: 3.0, step: 0.05, unit: "Kc", onChange: (v) => onParam("sedimentCapacity", v) }),
      sliderField({ label: "Dendritic Channel Focus", value: Math.round(layer.channelSharpness * 100), min: 20, max: 100, step: 1, unit: "%", onChange: (v) => onParam("channelSharpness", v / 100) }),
      sliderField({ label: "Evaporation Rate", value: Math.round(layer.evaporation * 1000), min: 5, max: 90, step: 1, unit: "‰", onChange: (v) => onParam("evaporation", v / 1000) })
    );
  } else if (layer.type === "thermal_erosion") {
    cardParams.append(
      el("div", { class: "card-diagram", id: "ReposeDiagram" }, buildReposeAngleSVG(layer.reposeAngle)),
      sliderField({ label: "Weathering Iterations", value: layer.iterations, min: 5, max: 90, step: 5, unit: "steps", onChange: (v) => onParam("iterations", v) }),
      sliderField({
        label: "Critical Talus Repose Angle",
        value: layer.reposeAngle,
        min: 24,
        max: 55,
        step: 1,
        unit: "deg",
        onChange: (v) => {
          onParam("reposeAngle", v);
          const d = $("#ReposeDiagram");
          if (d) {
            d.innerHTML = "";
            d.append(buildReposeAngleSVG(v));
          }
        },
      }),
      sliderField({ label: "Rock Spalling Rate", value: Math.round(layer.weatheringRate * 100), min: 10, max: 100, step: 1, unit: "%", onChange: (v) => onParam("weatheringRate", v / 100) }),
      sliderField({ label: "Talus Apron Spread", value: Math.round(layer.talusSpread * 100), min: 20, max: 100, step: 1, unit: "%", onChange: (v) => onParam("talusSpread", v / 100) })
    );
  } else if (layer.type === "snowfall") {
    cardParams.append(
      sliderField({ label: "Climatic Snowline Altitude", value: layer.snowlineAlt, min: 100, max: 2600, step: 20, unit: "m", onChange: (v) => onParam("snowlineAlt", v) }),
      sliderField({ label: "Snowpack Accumulation Depth", value: Math.round(layer.snowDepth * 100), min: 10, max: 140, step: 1, unit: "%", onChange: (v) => onParam("snowDepth", v / 100) }),
      sliderField({ label: "Cliff Avalanche Shed Angle", value: layer.reposeAngle, min: 28, max: 68, step: 1, unit: "deg", onChange: (v) => onParam("reposeAngle", v) }),
      sliderField({ label: "Prevailing Wind Azimuth", value: layer.windAzimuth, min: 0, max: 360, step: 2, unit: "deg", onChange: (v) => onParam("windAzimuth", v) }),
      sliderField({ label: "Leeward Cornice Drift", value: Math.round(layer.windDrift * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onParam("windDrift", v / 100) }),
      sliderField({ label: "South-Aspect Solar Melt", value: Math.round(layer.solarMelt * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onParam("solarMelt", v / 100) })
    );
  }

  wrap.append(cardParams);

  // Card 3: Spatial & Topographic Layer Mask
  if (!schema.isSimulation) {
    wrap.append(
      el(
        "div",
        { class: "property-card" },
        el(
          "h3",
          {},
          el("span", { text: "Elevation, Slope & Radial Mask" }),
          el("span", { class: "card-pill", text: "Spatial Filter" })
        ),
        el("div", { class: "card-diagram", id: "MaskTransferDiagram" }, buildMaskWindowSVG(layer)),
        sliderField({
          label: "Minimum Altitude",
          value: layer.maskAltMin ?? -500,
          min: -500,
          max: 3000,
          step: 20,
          unit: "m",
          onChange: (v) => onParam("maskAltMin", v),
        }),
        sliderField({
          label: "Maximum Altitude",
          value: layer.maskAltMax ?? 4500,
          min: 200,
          max: 4500,
          step: 20,
          unit: "m",
          onChange: (v) => onParam("maskAltMax", v),
        }),
        sliderField({
          label: "Minimum Slope Angle",
          value: layer.maskSlopeMin ?? 0,
          min: 0,
          max: 85,
          step: 1,
          unit: "deg",
          onChange: (v) => onParam("maskSlopeMin", v),
        }),
        sliderField({
          label: "Maximum Slope Angle",
          value: layer.maskSlopeMax ?? 90,
          min: 10,
          max: 90,
          step: 1,
          unit: "deg",
          onChange: (v) => onParam("maskSlopeMax", v),
        }),
        sliderField({
          label: "Radial Distance Falloff",
          value: Math.round((layer.maskRadial ?? 0) * 100),
          min: 0,
          max: 100,
          step: 1,
          unit: "%",
          onChange: (v) => onParam("maskRadial", v / 100),
        })
      )
    );
  }

  return wrap;
}

//--------------------------------------------------------------------------------------------------------------------------------------------
//  2. TEXTURE LAYER INSPECTOR (SATMAPS, PBR SPLATTING & PROCEDURAL MASKS)
//--------------------------------------------------------------------------------------------------------------------------------------------
function buildTextureLayerInspector(layer) {
  const wrap = el("div", { class: "specialized-inspector" });
  const sat = SATMAPS.find((s) => s.id === layer.satmapId) || SATMAPS[0];

  const onTexChange = (key, val) => {
    layer[key] = val;
    rebuildTerrain({ fullRebuild: false });
    renderLayerStackDock();
    const diag = $("#MaskTransferDiagram");
    if (diag) {
      diag.innerHTML = "";
      diag.append(buildMaskWindowSVG(layer));
    }
  };

  // Top Header
  wrap.append(
    el(
      "div",
      { class: "inspector-top-header" },
      el(
        "div",
        { class: "breadcrumbs" },
        "INSPECTOR / TEXTURE STACK / ",
        el("span", { text: (layer.category || "MATERIAL").toUpperCase() })
      ),
      el(
        "div",
        { class: "inspector-title-row" },
        el("h1", { text: layer.name }),
        el(
          "div",
          { class: "inspector-title-actions" },
          el("button", {
            type: "button",
            text: state.shadingMode === 8 ? "Exit Mask View" : "Inspect Mask",
            onClick: () => {
              state.shadingMode = state.shadingMode === 8 ? 0 : 8;
              rebuildTerrain({ fullRebuild: false });
              renderViewportToolbar();
              renderInspectorDock();
            },
          }),
          el("button", {
            type: "button",
            text: "Duplicate",
            onClick: () => {
              const copy = structuredClone(layer);
              copy.id = "tex_" + Math.random().toString(36).slice(2, 8);
              copy.name = layer.name + " Copy";
              const idx = state.textureStack.indexOf(layer);
              state.textureStack.splice(idx + 1, 0, copy);
              state.selection = { kind: "texture", id: copy.id };
              rebuildTerrain({ fullRebuild: false });
              renderLayerStackDock();
              renderInspectorDock();
            },
          }),
          state.textureStack.length > 1
            ? el("button", {
                type: "button",
                text: "Delete",
                onClick: () => {
                  const idx = state.textureStack.indexOf(layer);
                  if (idx >= 0) state.textureStack.splice(idx, 1);
                  state.selection = { kind: "texture", id: state.textureStack[0]?.id };
                  rebuildTerrain({ fullRebuild: false });
                  renderLayerStackDock();
                  renderInspectorDock();
                },
              })
            : null
        )
      ),
      el("p", {
        class: "inspector-subtitle",
        text: `SatMap & PBR procedural splatting layer (${sat.name}) with height-contrast rock blending and geomorphological masks.`,
      })
    )
  );

  // Quick Tiles
  wrap.append(
    el("div", { class: "section-caption" }, el("span", { text: "Material Switches" }), el("small", { text: sat.name })),
    el(
      "div",
      { class: "tiles" },
      quickTile({
        label: "Layer Active",
        on: layer.enabled,
        icon: "power",
        onClick: () => {
          layer.enabled = !layer.enabled;
          rebuildTerrain({ fullRebuild: false });
          renderLayerStackDock();
          renderInspectorDock();
        },
      }),
      quickTile({
        label: "Solo Material",
        on: layer.solo,
        icon: "solo",
        onClick: () => {
          layer.solo = !layer.solo;
          rebuildTerrain({ fullRebuild: false });
          renderLayerStackDock();
          renderInspectorDock();
        },
      }),
      quickTile({
        label: "SatMap Ramp",
        on: layer.useSatmap,
        icon: "satmap",
        onClick: () => {
          layer.useSatmap = !layer.useSatmap;
          rebuildTerrain({ fullRebuild: false });
          renderLayerStackDock();
          renderInspectorDock();
        },
      }),
      quickTile({
        label: "Mask Heatmap",
        on: state.shadingMode === 8,
        icon: "mask",
        onClick: () => {
          state.shadingMode = state.shadingMode === 8 ? 0 : 8;
          rebuildTerrain({ fullRebuild: false });
          renderViewportToolbar();
          renderInspectorDock();
        },
      })
    )
  );

  // Card 1: SatMap Library & Mineral Color Palette
  const satGrid = el("div", { class: "satmap-grid" });
  for (const s of SATMAPS) {
    satGrid.append(
      el(
        "button",
        {
          type: "button",
          class: `satmap-btn ${layer.satmapId === s.id ? "selected" : ""}`,
          onClick: () => {
            layer.satmapId = s.id;
            layer.colSecondary = s.stops[0];
            layer.colPrimary = s.stops[2];
            layer.colAccent = s.stops[4];
            rebuildTerrain({ fullRebuild: false });
            renderLayerStackDock();
            renderInspectorDock();
          },
        },
        el("div", {
          class: "satmap-bar",
          style: `background: linear-gradient(90deg, ${s.stops.join(", ")});`,
        }),
        el("span", { text: s.name })
      )
    );
  }

  wrap.append(
    el(
      "div",
      { class: "property-card" },
      el(
        "h3",
        {},
        el("span", { text: "SatMap Gradient & Mineral Albedo" }),
        el("span", { class: "card-pill", text: sat.category })
      ),
      satGrid,
      colorField("Primary High Tone", layer.colPrimary, (v) => onTexChange("colPrimary", v)),
      colorField("Secondary Crevice Tone", layer.colSecondary, (v) => onTexChange("colSecondary", v)),
      colorField("Strata & Vein Accent", layer.colAccent, (v) => onTexChange("colAccent", v))
    )
  );

  // Card 2: PBR Surface & Height-Blend Compositing
  wrap.append(
    el(
      "div",
      { class: "property-card" },
      el(
        "h3",
        {},
        el("span", { text: "PBR Microsurface & Height Blending" }),
        el("span", { class: "card-pill", text: "Triplanar" })
      ),
      selectField(
        "Layer Blend Mode",
        layer.blendMode,
        TEX_BLEND_MODES.map((b) => ({ value: b.id, label: b.name })),
        (v) => onTexChange("blendMode", parseInt(v, 10))
      ),
      sliderField({
        label: "Layer Opacity",
        value: Math.round(layer.opacity * 100),
        min: 0,
        max: 100,
        step: 1,
        unit: "%",
        onChange: (v) => onTexChange("opacity", v / 100),
      }),
      sliderField({
        label: "Height-Blend Rock Protrusion",
        hint: "Sharpens transition around rock crevices",
        value: Math.round(layer.heightContrast * 100),
        min: 5,
        max: 98,
        step: 1,
        unit: "%",
        onChange: (v) => onTexChange("heightContrast", v / 100),
      }),
      sliderField({
        label: "Surface Roughness",
        value: Math.round(layer.roughness * 100),
        min: 5,
        max: 98,
        step: 1,
        unit: "%",
        onChange: (v) => onTexChange("roughness", v / 100),
      }),
      sliderField({
        label: "Specular Reflectance (F0)",
        value: Math.round(layer.specular * 100),
        min: 5,
        max: 100,
        step: 1,
        unit: "%",
        onChange: (v) => onTexChange("specular", v / 100),
      }),
      sliderField({
        label: "Sedimentary Strata Vein Intensity",
        value: Math.round(layer.strataIntensity * 100),
        min: 0,
        max: 100,
        step: 1,
        unit: "%",
        onChange: (v) => onTexChange("strataIntensity", v / 100),
      })
    )
  );

  // Card 3: Geomorphological & Topographic Masks
  const cardMasks = el(
    "div",
    { class: "property-card" },
    el(
      "h3",
      {},
      el("span", { text: "Procedural Geomorphology Masks" }),
      el("span", { class: "card-pill", text: "7 Mask Channels" })
    ),
    el("div", { class: "card-diagram", id: "MaskTransferDiagram" }, buildMaskWindowSVG(layer)),

    // 1. Elevation Mask
    switchField("Elevation Range Mask", layer.useAltMask, (v) => {
      onTexChange("useAltMask", v);
      renderInspectorDock();
    })
  );

  if (layer.useAltMask) {
    cardMasks.append(
      sliderField({ label: "Min Elevation", value: layer.altMin, min: -200, max: 3500, step: 20, unit: "m", onChange: (v) => onTexChange("altMin", v) }),
      sliderField({ label: "Max Elevation", value: layer.altMax, min: 0, max: 4500, step: 20, unit: "m", onChange: (v) => onTexChange("altMax", v) }),
      sliderField({ label: "Elevation Feather", value: layer.altFeather, min: 20, max: 600, step: 10, unit: "m", onChange: (v) => onTexChange("altFeather", v) })
    );
  }

  // 2. Slope Angle Mask
  cardMasks.append(
    switchField("Slope Angle Mask", layer.useSlopeMask, (v) => {
      onTexChange("useSlopeMask", v);
      renderInspectorDock();
    })
  );
  if (layer.useSlopeMask) {
    cardMasks.append(
      sliderField({ label: "Min Slope Angle", value: layer.slopeMin, min: 0, max: 85, step: 1, unit: "deg", onChange: (v) => onTexChange("slopeMin", v) }),
      sliderField({ label: "Max Slope Angle", value: layer.slopeMax, min: 5, max: 90, step: 1, unit: "deg", onChange: (v) => onTexChange("slopeMax", v) }),
      sliderField({ label: "Slope Transition Feather", value: layer.slopeFeather, min: 1, max: 25, step: 1, unit: "deg", onChange: (v) => onTexChange("slopeFeather", v) })
    );
  }

  // 3. Curvature / Ridge & Cavity Mask
  cardMasks.append(
    switchField("Curvature (Ridge / Cavity) Mask", layer.useCurvatureMask, (v) => {
      onTexChange("useCurvatureMask", v);
      renderInspectorDock();
    })
  );
  if (layer.useCurvatureMask) {
    cardMasks.append(
      selectField(
        "Curvature Target",
        layer.curvatureMode,
        [
          { value: 0, label: "Convex Ridges & Arêtes" },
          { value: 1, label: "Concave Gullies & Crevices" },
        ],
        (v) => onTexChange("curvatureMode", parseInt(v, 10))
      ),
      sliderField({ label: "Curvature Sensitivity", value: Math.round(layer.curvatureStrength * 100), min: 10, max: 100, step: 1, unit: "%", onChange: (v) => onTexChange("curvatureStrength", v / 100) })
    );
  }

  // 4. Hydrology / Flow Mask
  cardMasks.append(
    switchField("Hydrology / Drainage Flow Mask", layer.useFlowMask, (v) => {
      onTexChange("useFlowMask", v);
      renderInspectorDock();
    })
  );
  if (layer.useFlowMask) {
    cardMasks.append(
      sliderField({ label: "Stream Channel Focus", value: Math.round(layer.flowStrength * 100), min: 10, max: 100, step: 1, unit: "%", onChange: (v) => onTexChange("flowStrength", v / 100) })
    );
  }

  // 5. Sediment & Talus Mask
  cardMasks.append(
    switchField("Eroded Sediment & Talus Mask", layer.useSedimentMask, (v) => {
      onTexChange("useSedimentMask", v);
      renderInspectorDock();
    })
  );
  if (layer.useSedimentMask) {
    cardMasks.append(
      sliderField({ label: "Alluvial Sediment Weight", value: Math.round(layer.sedimentWeight * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onTexChange("sedimentWeight", v / 100) }),
      sliderField({ label: "Thermal Talus Weight", value: Math.round(layer.talusWeight * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => onTexChange("talusWeight", v / 100) })
    );
  }

  // 6. Snowpack Mask
  cardMasks.append(
    switchField("Simulated Snowpack Mask", layer.useSnowMask, (v) => {
      onTexChange("useSnowMask", v);
      renderInspectorDock();
    })
  );
  if (layer.useSnowMask) {
    cardMasks.append(
      sliderField({ label: "Snowpack Binding Weight", value: Math.round(layer.snowWeight * 100), min: 10, max: 100, step: 1, unit: "%", onChange: (v) => onTexChange("snowWeight", v / 100) })
    );
  }

  // 7. Fractal Noise Breakup Mask
  cardMasks.append(
    switchField("Fractal Noise Breakup Mask", layer.useNoiseMask, (v) => {
      onTexChange("useNoiseMask", v);
      renderInspectorDock();
    })
  );
  if (layer.useNoiseMask) {
    cardMasks.append(
      sliderField({ label: "Noise Frequency Scale", value: layer.noiseScale, min: 1, max: 18, step: 0.5, unit: "x", onChange: (v) => onTexChange("noiseScale", v) }),
      sliderField({ label: "Breakup Modulation", value: Math.round(layer.noiseAmount * 100), min: 5, max: 100, step: 1, unit: "%", onChange: (v) => onTexChange("noiseAmount", v / 100) })
    );
  }

  wrap.append(cardMasks);
  return wrap;
}

//--------------------------------------------------------------------------------------------------------------------------------------------
//  3. WORLD & ENVIRONMENT INSPECTOR (SUN, SKY, FOG, WATER TABLE)
//--------------------------------------------------------------------------------------------------------------------------------------------
function buildWorldEnvironmentInspector(section = "sun") {
  const wrap = el("div", { class: "specialized-inspector" });
  const env = state.env;

  const setEnv = (k, v) => {
    env[k] = v;
    renderLayerStackDock();
  };

  wrap.append(
    el(
      "div",
      { class: "inspector-top-header" },
      el("div", { class: "breadcrumbs" }, "INSPECTOR / WORLD & ATMOSPHERE / ", el("span", { text: section.toUpperCase() })),
      el(
        "div",
        { class: "inspector-title-row" },
        el("h1", { text: "World & Sky Lighting" }),
        el(
          "div",
          { class: "inspector-title-actions" },
          ["sun", "sky", "fog", "water"].map((s) =>
            el("button", {
              type: "button",
              text: s.charAt(0).toUpperCase() + s.slice(1),
              style: section === s ? "background:#2e2e2e;color:#ffb454;border-color:#ffb45455;" : "",
              onClick: () => {
                state.selection = { kind: "world", id: s, worldSection: s };
                renderLayerStackDock();
                renderInspectorDock();
              },
            })
          )
        )
      ),
      el("p", {
        class: "inspector-subtitle",
        text: "Physical Rayleigh-Mie sky scattering, heightfield raymarched soft shadows, valley mist, and Beer-Lambert water table.",
      })
    )
  );

  // Sun & Shadows Card
  wrap.append(
    el(
      "div",
      { class: "property-card" },
      el("h3", {}, el("span", { text: "Directional Sun & Celestial Orbit" }), el("span", { class: "card-pill", text: "Alt+Drag in 3D" })),
      el("div", { class: "metric", id: "SunMetricText" }, `${env.sunElevation}°`, el("small", { text: `Elev · ${env.sunAzimuth}° Azimuth` })),
      el(
        "div",
        { class: "card-diagram", id: "SunOrbitBox" },
        buildSunOrbitSVG(env.sunAzimuth, env.sunElevation, (az, elv) => {
          env.sunAzimuth = az;
          env.sunElevation = elv;
          renderInspectorDock();
          renderLayerStackDock();
        })
      ),
      sliderField({ label: "Solar Azimuth", value: env.sunAzimuth, min: 0, max: 360, step: 1, unit: "deg", onChange: (v) => { setEnv("sunAzimuth", v); renderInspectorDock(); } }),
      sliderField({ label: "Solar Elevation", value: env.sunElevation, min: 2, max: 88, step: 1, unit: "deg", onChange: (v) => { setEnv("sunElevation", v); renderInspectorDock(); } }),
      sliderField({ label: "Solar Illuminance", value: env.sunIlluminanceKlux, min: 20, max: 160, step: 1, unit: "klux", onChange: (v) => setEnv("sunIlluminanceKlux", v) }),
      sliderField({ label: "Blackbody Temperature", value: env.sunTemperatureK, min: 2800, max: 8500, step: 50, unit: "K", onChange: (v) => setEnv("sunTemperatureK", v) }),
      switchField("Heightfield Raymarched Soft Shadows", env.softShadows, (v) => setEnv("softShadows", v)),
      switchField("Volumetric Drifting Cloud Shadows", env.cloudShadows, (v) => setEnv("cloudShadows", v))
    )
  );

  // Water Table / Ocean / Lake Card
  wrap.append(
    el(
      "div",
      { class: "property-card" },
      el("h3", {}, el("span", { text: "Water Table / Alpine Lake / Ocean" }), el("span", { class: "card-pill", text: "Beer-Lambert" })),
      switchField("Enable Water Table Plane", env.waterEnabled, (v) => { setEnv("waterEnabled", v); renderViewportToolbar(); }),
      sliderField({ label: "Water Surface Elevation", value: env.waterLevelM, min: -50, max: 1400, step: 5, unit: "m", onChange: (v) => setEnv("waterLevelM", v) }),
      colorField("Shallow Turquoise Tint", env.waterShallowColor, (v) => setEnv("waterShallowColor", v)),
      colorField("Deep Abyssal Absorb Color", env.waterDeepColor, (v) => setEnv("waterDeepColor", v)),
      sliderField({ label: "Capillary Wave Ripples", value: Math.round(env.waterWaveStrength * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => setEnv("waterWaveStrength", v / 100) })
    )
  );

  // Atmosphere, Fog & Scale Card
  wrap.append(
    el(
      "div",
      { class: "property-card" },
      el("h3", {}, el("span", { text: "Atmosphere, Height Fog & World Scale" }), el("span", { class: "card-pill", text: `${env.domainSizeKm} km` })),
      switchField("Exponential Height Fog & Valley Mist", env.fogEnabled, (v) => { setEnv("fogEnabled", v); renderViewportToolbar(); }),
      sliderField({ label: "Fog Optical Density", value: Math.round(env.fogDensity * 100), min: 0, max: 80, step: 1, unit: "%", onChange: (v) => setEnv("fogDensity", v / 100) }),
      sliderField({ label: "Valley Mist Accumulation", value: Math.round(env.fogValleyMist * 100), min: 0, max: 100, step: 1, unit: "%", onChange: (v) => setEnv("fogValleyMist", v / 100) }),
      sliderField({ label: "Rayleigh Scattering", value: env.rayleigh, min: 0.3, max: 2.2, step: 0.05, unit: "x", onChange: (v) => setEnv("rayleigh", v) }),
      sliderField({ label: "Mie Aerosol Turbidity", value: env.mieTurbidity, min: 1.0, max: 6.0, step: 0.1, unit: "T", onChange: (v) => setEnv("mieTurbidity", v) }),
      colorField("Horizon Fog Scattering Tint", env.fogColor, (v) => setEnv("fogColor", v)),
      sliderField({ label: "Vertical Relief Exaggeration", value: env.verticalExaggeration, min: 0.5, max: 2.2, step: 0.05, unit: "x", onChange: (v) => setEnv("verticalExaggeration", v) }),
      sliderField({ label: "ACES Exposure Bias", value: env.exposureEv, min: -1.5, max: 1.5, step: 0.05, unit: "EV", onChange: (v) => setEnv("exposureEv", v) }),
      switchField("Geological Pedestal Block Skirt", env.pedestalEnabled, (v) => setEnv("pedestalEnabled", v))
    )
  );

  return wrap;
}

//--------------------------------------------------------------------------------------------------------------------------------------------
//  4. HISTOGRAM & DIAGNOSTIC MASKS TAB
//--------------------------------------------------------------------------------------------------------------------------------------------
function buildHistogramAndMasksPanel() {
  const stats = engine ? engine.stats : { minElev: 0, maxElev: 2200, meanElev: 800, meanSlope: 28, snowCoveragePct: 24, erodedVolumeM3: 14.2 };
  return el(
    "div",
    { class: "specialized-inspector" },
    el(
      "div",
      { class: "inspector-top-header" },
      el("div", { class: "breadcrumbs" }, "INSPECTOR / ", el("span", { text: "GEOMORPHOLOGY TELEMETRY" })),
      el("div", { class: "inspector-title-row" }, el("h1", { text: "Hypsometry & Data Maps" })),
      el("p", { class: "inspector-subtitle", text: "Real-time elevation distribution, slope analysis, and diagnostic GPU field inspection." })
    ),
    el(
      "div",
      { class: "property-card" },
      el("h3", {}, el("span", { text: "Elevation Hypsometric Curve" }), el("span", { class: "card-pill", text: "64 Bins" })),
      el("div", { class: "metric" }, `${stats.maxElev - stats.minElev}`, el("small", { text: "m total vertical relief" })),
      el("div", { class: "card-diagram", id: "LiveHistDiagram" }, engine ? buildElevationHistogramSVG(stats, state.env) : null),
      el("p", { text: `Min: ${stats.minElev} m · Mean: ${stats.meanElev} m · Peak: ${stats.maxElev} m · Mean Slope: ${stats.meanSlope}° · Snow Coverage: ${stats.snowCoveragePct}%` })
    ),
    el(
      "div",
      { class: "property-card" },
      el("h3", {}, el("span", { text: "Diagnostic Viewport Data Channels" }), el("span", { class: "card-pill", text: "10 Modes" })),
      ...[
        { id: 0, name: "01 · Lit PBR (AAA Realism)" },
        { id: 1, name: "02 · Clay / Studio Relief" },
        { id: 2, name: "03 · Elevation Hypsometric Ramp + Contours" },
        { id: 3, name: "04 · Slope Angle Heatmap (0°–90°)" },
        { id: 4, name: "05 · Hydrology / Stream Power Flow" },
        { id: 5, name: "06 · Eroded Sediment & Talus Deposits" },
        { id: 6, name: "07 · Snowpack & Glacial Firn Depth" },
        { id: 7, name: "08 · Mean Curvature (Ridges vs Gullies)" },
        { id: 8, name: "09 · Active Layer Mask Heatmap" },
        { id: 9, name: "10 · Unlit SatMap Albedo" },
      ].map((m) =>
        el(
          "button",
          {
            type: "button",
            style: `width:100%;justify-content:space-between;margin-bottom:6px;padding:8px 14px;background:${state.shadingMode === m.id ? "#2c2820" : "#181818"};border-color:${state.shadingMode === m.id ? "#ffb454" : "#2c2c2c"};`,
            onClick: () => {
              state.shadingMode = m.id;
              renderViewportToolbar();
              renderInspectorDock();
            },
          },
          el("span", { text: m.name }),
          el("small", { text: state.shadingMode === m.id ? "ACTIVE" : "VIEW" })
        )
      )
    )
  );
}

//--------------------------------------------------------------------------------------------------------------------------------------------
//  5. EXPORT & BAKE TAB
//--------------------------------------------------------------------------------------------------------------------------------------------
function buildExportAndBakePanel() {
  return el(
    "div",
    { class: "specialized-inspector" },
    el(
      "div",
      { class: "inspector-top-header" },
      el("div", { class: "breadcrumbs" }, "INSPECTOR / ", el("span", { text: "DCC & ENGINE EXPORT" })),
      el("div", { class: "inspector-title-row" }, el("h1", { text: "Export & Bake Maps" })),
      el("p", {
        class: "inspector-subtitle",
        text: "Export production-ready 16-bit PNG heightmaps, tangent-space normal maps, RGBA splat masks, baked SatMap albedo, and 3D OBJ meshes for Unreal Engine 5, Unity, Blender, or Gaea.",
      })
    ),
    el(
      "div",
      { class: "property-card" },
      el("h3", {}, el("span", { text: "Heightfield & Geometry Exports" }), el("span", { class: "card-pill", text: "16-Bit PNG / OBJ" })),
      el(
        "button",
        {
          type: "button",
          style: "width:100%;height:38px;margin-bottom:8px;background:#dededb;color:#181818;font-weight:400;",
          onClick: () => {
            exportHeightmap16Bit(engine, state.presetId);
            showToast("Exported 16-bit Grayscale PNG Heightmap (65,536 levels)");
          },
        },
        "Download 16-Bit Heightmap (.PNG)"
      ),
      el(
        "button",
        {
          type: "button",
          style: "width:100%;height:36px;margin-bottom:6px;",
          onClick: () => {
            exportWavefrontOBJ(engine, state, state.presetId);
            showToast("Exported 3D Terrain Mesh (.OBJ)");
          },
        },
        "Download 3D Terrain Mesh (.OBJ)"
      )
    ),
    el(
      "div",
      { class: "property-card" },
      el("h3", {}, el("span", { text: "Baked Texture & Splatmap Exports" }), el("span", { class: "card-pill", text: "PBR Maps" })),
      el(
        "button",
        {
          type: "button",
          style: "width:100%;height:36px;margin-bottom:8px;",
          onClick: () => {
            exportAlbedoPNG(engine, state.presetId);
            showToast("Exported Baked SatMap Albedo (.PNG)");
          },
        },
        "Download Baked SatMap Albedo (.PNG)"
      ),
      el(
        "button",
        {
          type: "button",
          style: "width:100%;height:36px;margin-bottom:8px;",
          onClick: () => {
            exportNormalMapPNG(engine, state.presetId);
            showToast("Exported Tangent-Space Normal Map (.PNG)");
          },
        },
        "Download Tangent Normal Map (.PNG)"
      ),
      el(
        "button",
        {
          type: "button",
          style: "width:100%;height:36px;",
          onClick: () => {
            exportSplatMapPNG(engine, state.presetId);
            showToast("Exported RGBA SplatMask (Bedrock/Sediment/Talus/Snow)");
          },
        },
        "Download RGBA Splatmap (.PNG)"
      )
    ),
    el(
      "div",
      { class: "property-card" },
      el("h3", {}, el("span", { text: "Project Recipe JSON" }), el("span", { class: "card-pill", text: "Layer Stacks" })),
      el(
        "button",
        {
          type: "button",
          style: "width:100%;height:36px;",
          onClick: () => {
            exportProjectJSON(state);
            showToast("Saved Terrain + Texture Layer Stack Recipe (.JSON)");
          },
        },
        "Save Layer Stack Recipe (.JSON)"
      )
    )
  );
}

function refreshLiveInspectorDiagrams() {
  const histEl = $("#LiveHistDiagram");
  if (histEl && engine) {
    histEl.innerHTML = "";
    histEl.append(buildElevationHistogramSVG(engine.stats, state.env));
  }
}

//============================================================================================================================================
//  VIEWPORT TOOLBAR & TELEMETRY
//============================================================================================================================================
function renderViewportToolbar() {
  const modesEl = $("#ViewportShadingModes");
  if (modesEl) {
    modesEl.innerHTML = "";
    const modes = [
      { id: 0, label: "Lit PBR" },
      { id: 1, label: "Clay" },
      { id: 2, label: "Height" },
      { id: 3, label: "Slope" },
      { id: 4, label: "Flow" },
      { id: 5, label: "Sediment" },
      { id: 6, label: "Snow" },
      { id: 7, label: "Curvature" },
      { id: 8, label: "Mask" },
    ];
    for (const m of modes) {
      modesEl.append(
        el("button", {
          type: "button",
          class: state.shadingMode === m.id ? "active" : "",
          text: m.label,
          onClick: () => {
            state.shadingMode = m.id;
            renderViewportToolbar();
          },
        })
      );
    }
  }

  // Update Viewport Layout Tabs (3D Viewport, Split 3D/2D, 2D Data Map, Cross-Section Profile)
  document.querySelectorAll("#ViewportTabs .document-tab").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.view === state.viewportLayout);
  });

  const pane3D = $("#Pane3D");
  const paneMap = $("#PaneMap");
  if (pane3D && paneMap) {
    if (state.viewportLayout === "3d") {
      pane3D.style.display = "block";
      paneMap.style.display = "none";
    } else if (state.viewportLayout === "map" || state.viewportLayout === "profile") {
      pane3D.style.display = "none";
      paneMap.style.display = "block";
    } else {
      pane3D.style.display = "block";
      paneMap.style.display = "block";
    }
  }

  // Update chip button states
  const setPressed = (id, val) => {
    const b = document.getElementById(id);
    if (b) b.setAttribute("aria-pressed", val ? "true" : "false");
  };
  setPressed("BtnAutoErode", state.autoErode);
  setPressed("BtnSculptToggle", engine?.brush.enabled);
  setPressed("BtnWaterToggle", state.env.waterEnabled);
  setPressed("BtnFogToggle", state.env.fogEnabled);
  setPressed("BtnTurntable", state.turntable);

  const topAutoBtn = $("#TopAutoErodeBtn");
  if (topAutoBtn) {
    topAutoBtn.classList.toggle("running", state.autoErode);
    topAutoBtn.textContent = state.autoErode ? "Auto-Erode: Running" : "Auto-Erode Sim";
  }

  const sculptBar = $("#SculptFloatingBar");
  if (sculptBar && engine) {
    sculptBar.style.display = engine.brush.enabled ? "flex" : "none";
    sculptBar.querySelectorAll("button[data-bmode]").forEach((b) => {
      b.classList.toggle("active", b.dataset.bmode === engine.brush.mode);
    });
  }
}

function updateTelemetryUI() {
  if (!engine) return;
  const s = engine.stats;
  const t = engine.timings;

  const badge = $("#ViewportPerfBadge");
  if (badge) {
    badge.textContent = `${engine.backend.toUpperCase()} · ${t.totalMs.toFixed(1)} ms`;
  }

  const sub = $("#ViewportSubInfo");
  if (sub) {
    sub.textContent = `${state.env.domainSizeKm.toFixed(1)} × ${state.env.domainSizeKm.toFixed(1)} km · Peak ${s.maxElev} m · Mean Slope ${s.meanSlope}° · Snow ${s.snowCoveragePct}%`;
  }

  const hudElev = $("#HudElevSpan");
  if (hudElev) {
    hudElev.innerHTML = `Relief <strong>${s.minElev}m – ${s.maxElev}m</strong> · Eroded <strong>${s.erodedVolumeM3}M m³</strong> · Cycles <strong>${t.erosionCycles}</strong>`;
  }

  const footTimings = $("#FooterTimings");
  if (footTimings) {
    footTimings.innerHTML = `Gen <b>${t.terrainMs.toFixed(1)}ms</b> · Erosion <b>${t.erosionMs.toFixed(1)}ms</b> · Splat <b>${t.splatMs.toFixed(1)}ms</b>`;
  }

  const statCycles = $("#StatErosionCycles");
  if (statCycles) statCycles.textContent = String(t.erosionCycles);

  const outFootPeak = $("#OutlinerFootPeak");
  if (outFootPeak) outFootPeak.textContent = `${s.maxElev} m`;
  const outFootSlope = $("#OutlinerFootSlope");
  if (outFootSlope) outFootSlope.textContent = `${s.meanSlope}°`;
  const outFootSnow = $("#OutlinerFootSnow");
  if (outFootSnow) outFootSnow.textContent = `${s.snowCoveragePct}%`;
}

//============================================================================================================================================
//  CONSTRUCT MODAL (+ ADD TERRAIN OR TEXTURE LAYER)
//============================================================================================================================================
function openConstructModal(targetStack = "terrain") {
  state.constructModal.open = true;
  state.constructModal.targetStack = targetStack;
  state.constructModal.category = "All";
  state.constructModal.query = "";
  state.constructModal.selectedKey =
    targetStack === "texture" ? TEXTURE_LAYER_TEMPLATES[0].templateId : "mountain_range";
  renderConstructModal();
}

function closeConstructModal() {
  state.constructModal.open = false;
  const host = $("#ModalHost");
  if (host) host.innerHTML = "";
}

function confirmConstructLayer() {
  const m = state.constructModal;
  if (m.targetStack === "texture") {
    const tpl =
      TEXTURE_LAYER_TEMPLATES.find((t) => t.templateId === m.selectedKey) ||
      TEXTURE_LAYER_TEMPLATES[0];
    const newLayer = createTextureLayer(structuredClone(tpl.overrides));
    state.textureStack.push(newLayer);
    state.activeStack = "texture";
    state.selection = { kind: "texture", id: newLayer.id };
    rebuildTerrain({ fullRebuild: false });
    showToast(`Added Texture Layer: ${newLayer.name}`);
  } else {
    const newLayer = createTerrainLayer(m.selectedKey);
    // Insert landform layers before physical erosion passes if adding a landform, or at top if simulation
    const schema = TERRAIN_LAYER_TYPES[m.selectedKey];
    if (schema && !schema.isSimulation) {
      const firstSimIdx = state.terrainStack.findIndex(
        (l) => TERRAIN_LAYER_TYPES[l.type]?.isSimulation
      );
      if (firstSimIdx >= 0) {
        state.terrainStack.splice(firstSimIdx, 0, newLayer);
      } else {
        state.terrainStack.push(newLayer);
      }
    } else {
      state.terrainStack.push(newLayer);
    }
    state.activeStack = "terrain";
    state.selection = { kind: "terrain", id: newLayer.id };
    rebuildTerrain({ fullRebuild: true });
    showToast(`Added Terrain Layer: ${newLayer.name}`);
  }
  closeConstructModal();
  renderLayerStackDock();
  renderInspectorDock();
}

function renderConstructModal() {
  const host = $("#ModalHost");
  if (!host) return;
  host.innerHTML = "";
  if (!state.constructModal.open) return;

  const m = state.constructModal;
  const isTex = m.targetStack === "texture";

  const items = isTex
    ? TEXTURE_LAYER_TEMPLATES.map((t) => ({
        key: t.templateId,
        name: t.name,
        category: t.category,
        description: t.description,
      }))
    : Object.values(TERRAIN_LAYER_TYPES).map((t) => ({
        key: t.type,
        name: t.name,
        category: t.category,
        description: t.description,
      }));

  const categories = ["All", ...new Set(items.map((i) => i.category))];
  const q = m.query.trim().toLowerCase();
  const filtered = items.filter(
    (i) =>
      (m.category === "All" || i.category === m.category) &&
      (!q || i.name.toLowerCase().includes(q) || i.description.toLowerCase().includes(q))
  );

  const dialog = el(
    "div",
    {
      class: "modal-scrim",
      onClick: (e) => {
        if (e.target.classList.contains("modal-scrim")) closeConstructModal();
      },
    },
    el(
      "div",
      { class: "construct-dialog" },
      el(
        "div",
        { class: "construct-titlebar" },
        el("h2", { text: "Construct · Add Layer to Stack" }),
        el("button", { type: "button", text: "✕", onClick: closeConstructModal })
      ),
      el(
        "div",
        { class: "construct-path" },
        el("span", { text: "TARGET STACK:" }),
        el("button", {
          type: "button",
          style: !isTex ? "background:#303030;color:#ffb454;" : "",
          text: "01 · Terrain Layer Stack",
          onClick: () => openConstructModal("terrain"),
        }),
        el("button", {
          type: "button",
          style: isTex ? "background:#303030;color:#ffb454;" : "",
          text: "02 · Texture Layer Stack",
          onClick: () => openConstructModal("texture"),
        }),
        el("kbd", { text: "ESC to close" })
      ),
      el(
        "div",
        { class: "construct-search" },
        el("span", { text: "⌕" }),
        el("input", {
          type: "text",
          placeholder: isTex
            ? "Search SatMap & PBR texture layer templates..."
            : "Search geological landforms, fault scarps, hydraulic & thermal erosion...",
          value: m.query,
          onInput: (e) => {
            m.query = e.target.value;
            renderConstructModal();
          },
        })
      ),
      el(
        "div",
        { class: "construct-catalogue" },
        el(
          "div",
          { class: "construct-categories" },
          categories.map((cat) =>
            el("button", {
              type: "button",
              class: m.category === cat ? "active" : "",
              text: cat,
              onClick: () => {
                m.category = cat;
                renderConstructModal();
              },
            })
          )
        ),
        el(
          "div",
          { class: "construct-results" },
          el(
            "div",
            { class: "construct-matrix" },
            filtered.map((item) =>
              el(
                "button",
                {
                  type: "button",
                  class: `construct-entity ${m.selectedKey === item.key ? "selected" : ""}`,
                  onClick: () => {
                    m.selectedKey = item.key;
                    renderConstructModal();
                  },
                  onDblClick: () => {
                    m.selectedKey = item.key;
                    confirmConstructLayer();
                  },
                },
                el("small", { style: "color:#ffb454;font-size:9px;text-transform:uppercase;", text: item.category }),
                el("strong", { text: item.name }),
                el("small", { text: item.description })
              )
            )
          )
        )
      ),
      el(
        "footer",
        {},
        el("small", {
          text: "Double-click any card or click Construct Layer to insert into the non-destructive stack.",
        }),
        el("button", { type: "button", text: "Cancel", onClick: closeConstructModal }),
        el("button", {
          type: "button",
          class: "top-action-btn primary-construct",
          text: isTex ? "+ Add Texture Layer" : "+ Add Terrain Layer",
          onClick: confirmConstructLayer,
        })
      )
    )
  );

  host.append(dialog);
}

//============================================================================================================================================
//  CAMERA ORBIT, PAN, DOLLY, SUN DRAG & 3D SCULPT INTERACTION
//============================================================================================================================================
function computeCameraMatrices(canvas) {
  const c = state.cam;
  const cp = Math.cos(c.pitch);
  const sp = Math.sin(c.pitch);
  const cy = Math.cos(c.yaw);
  const sy = Math.sin(c.yaw);

  const eye = [
    c.target[0] + c.dist * cp * sy,
    c.target[1] + c.dist * sp,
    c.target[2] + c.dist * cp * cy,
  ];

  const aspect = Math.max(0.2, (canvas.clientWidth || 800) / Math.max(1, canvas.clientHeight || 600));
  const view = mat4LookAt(eye, c.target, [0, 1, 0]);
  const proj = mat4Perspective((c.fovDeg * Math.PI) / 180, aspect, 0.15, 95.0);
  const viewProj = mat4Mul(proj, view);
  const invViewProj = mat4Invert(viewProj);

  return { eye, view, proj, viewProj, invViewProj };
}

function setupViewportInteractions(canvas) {
  let dragMode = null; // "orbit" | "pan" | "sun" | "sculpt"
  let lastX = 0;
  let lastY = 0;

  canvas.addEventListener("contextmenu", (e) => e.preventDefault());

  canvas.addEventListener("pointerdown", (e) => {
    canvas.setPointerCapture(e.pointerId);
    lastX = e.clientX;
    lastY = e.clientY;

    if (e.altKey) {
      dragMode = "sun";
    } else if (e.button === 1 || e.button === 2 || e.shiftKey) {
      dragMode = "pan";
    } else if (engine && engine.brush.enabled && e.button === 0) {
      dragMode = "sculpt";
      const rect = canvas.getBoundingClientRect();
      const ndcX = ((e.clientX - rect.left) / rect.width) * 2 - 1;
      const ndcY = 1 - ((e.clientY - rect.top) / rect.height) * 2;
      const camInfo = computeCameraMatrices(canvas);
      const hit = engine.raycastTerrain(ndcX, ndcY, camInfo.invViewProj, camInfo.eye, state);
      if (hit) {
        engine.applySculptBrush(hit.u, hit.v, state);
        updateTelemetryUI();
      }
    } else {
      dragMode = "orbit";
    }
  });

  canvas.addEventListener("pointermove", (e) => {
    const rect = canvas.getBoundingClientRect();
    const ndcX = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    const ndcY = 1 - ((e.clientY - rect.top) / rect.height) * 2;

    if (!dragMode) {
      // Update cursor coordinates & 3D brush hover ring
      if (engine) {
        const camInfo = computeCameraMatrices(canvas);
        const hit = engine.raycastTerrain(ndcX, ndcY, camInfo.invViewProj, camInfo.eye, state);
        if (hit) {
          engine.brush.worldX = hit.worldX;
          engine.brush.worldZ = hit.worldZ;
          engine.brush.hovering = true;
          const cursorEl = $("#FooterCursorCoords");
          if (cursorEl) {
            cursorEl.innerHTML = `X <b>${hit.xmKm}km</b> · Z <b>${hit.zmKm}km</b> · Elev <b>${hit.elevM}m</b> · Slope <b>${hit.slopeDeg}°</b>`;
          }
        } else {
          engine.brush.hovering = false;
        }
      }
      return;
    }

    const dx = e.clientX - lastX;
    const dy = e.clientY - lastY;
    lastX = e.clientX;
    lastY = e.clientY;

    if (dragMode === "orbit") {
      state.cam.yaw -= dx * 0.0065;
      state.cam.pitch = clamp(state.cam.pitch + dy * 0.0055, 0.06, 1.48);
    } else if (dragMode === "pan") {
      const panScale = state.cam.dist * 0.0014;
      const cy = Math.cos(state.cam.yaw);
      const sy = Math.sin(state.cam.yaw);
      state.cam.target[0] -= (cy * dx - sy * dy * 0.5) * panScale;
      state.cam.target[2] += (sy * dx + cy * dy * 0.5) * panScale;
    } else if (dragMode === "sun") {
      state.env.sunAzimuth = Math.round((state.env.sunAzimuth + dx * 0.45 + 360) % 360);
      state.env.sunElevation = Math.round(clamp(state.env.sunElevation - dy * 0.3, 2, 88));
      if (state.selection.kind === "world") renderInspectorDock();
    } else if (dragMode === "sculpt" && engine) {
      const camInfo = computeCameraMatrices(canvas);
      const hit = engine.raycastTerrain(ndcX, ndcY, camInfo.invViewProj, camInfo.eye, state);
      if (hit) {
        engine.brush.worldX = hit.worldX;
        engine.brush.worldZ = hit.worldZ;
        engine.brush.hovering = true;
        engine.applySculptBrush(hit.u, hit.v, state);
        updateTelemetryUI();
      }
    }
  });

  const endDrag = () => {
    dragMode = null;
  };
  canvas.addEventListener("pointerup", endDrag);
  canvas.addEventListener("pointercancel", endDrag);

  canvas.addEventListener(
    "wheel",
    (e) => {
      e.preventDefault();
      const factor = Math.exp(e.deltaY * 0.001);
      state.cam.dist = clamp(state.cam.dist * factor, 2.2, 28.0);
    },
    { passive: false }
  );
}

//============================================================================================================================================
//  RESIZABLE RIGHT-HAND DOCK DIVIDERS
//============================================================================================================================================
function setupDockDividers() {
  const stackDivider = $("#StackDivider");
  const inspDivider = $("#InspectorDivider");

  const attachResize = (handleEl, cssVar, minW, maxW, getRightNeighborWidth) => {
    if (!handleEl) return;
    handleEl.addEventListener("pointerdown", (e) => {
      e.preventDefault();
      handleEl.setPointerCapture(e.pointerId);
      handleEl.classList.add("active");
      document.body.classList.add("resizing");

      const onMove = (ev) => {
        const rightWidth = getRightNeighborWidth();
        const newW = clamp(window.innerWidth - ev.clientX - rightWidth, minW, maxW);
        document.documentElement.style.setProperty(cssVar, `${Math.round(newW)}px`);
      };
      const onUp = () => {
        handleEl.classList.remove("active");
        document.body.classList.remove("resizing");
        handleEl.removeEventListener("pointermove", onMove);
        handleEl.removeEventListener("pointerup", onUp);
      };
      handleEl.addEventListener("pointermove", onMove);
      handleEl.addEventListener("pointerup", onUp);
    });
  };

  attachResize(stackDivider, "--stack-w", 280, 520, () => {
    const inspDock = $("#InspectorDock");
    return inspDock ? inspDock.getBoundingClientRect().width : 420;
  });

  attachResize(inspDivider, "--insp-w", 320, 620, () => 0);
}

//============================================================================================================================================
//  INITIALIZE APPLICATION & RENDER LOOP
//============================================================================================================================================
function renderAllUI() {
  const presetSel = $("#TopPresetSelect");
  if (presetSel) presetSel.value = state.presetId;

  renderViewportToolbar();
  renderLayerStackDock();
  renderInspectorDock();
  updateTelemetryUI();
}

async function boot() {
  const gpuCanvas = $("#GpuCanvas");
  const mapCanvas = $("#MapCanvas");

  engine = new TerrainStudioEngine(gpuCanvas, mapCanvas);
  const backend = await engine.init();

  const gpuPill = $("#GpuStatusPill");
  if (gpuPill) {
    if (backend === "webgpu") {
      gpuPill.className = "gpu-pill";
      gpuPill.innerHTML = `<i></i><span>WebGPU Compute · Live</span>`;
    } else {
      gpuPill.className = "gpu-pill fallback";
      gpuPill.innerHTML = `<i></i><span>WebGL2 Compute · Active</span>`;
    }
  }

  // Populate World Presets selector
  const presetSel = $("#TopPresetSelect");
  if (presetSel) {
    presetSel.innerHTML = "";
    for (const p of Object.values(WORLD_PRESETS)) {
      presetSel.append(el("option", { value: p.id, text: p.name }));
    }
    presetSel.value = state.presetId;
    presetSel.addEventListener("change", (e) => applyWorldPreset(e.target.value));
  }

  // Resolution selector
  const resSel = $("#TopResSelect");
  if (resSel) {
    resSel.addEventListener("change", async (e) => {
      const n = parseInt(e.target.value, 10) || 512;
      await engine.setResolution(n);
      rebuildTerrain({ fullRebuild: true });
      showToast(`Simulation Grid Resolution: ${n} × ${n}`);
    });
  }

  // Top bar workflow stage buttons
  document.querySelectorAll(".workflow-pill").forEach((btn) => {
    btn.addEventListener("click", () => switchWorkflowStage(btn.dataset.stage));
  });

  // Top bar action buttons
  $("#TopStepErodeBtn")?.addEventListener("click", () => {
    engine.stepLiveErosion(state, 25);
    updateTelemetryUI();
    refreshLiveInspectorDiagrams();
    showToast("Simulated +25 Hydraulic & Thermal Erosion Cycles");
  });

  $("#TopAutoErodeBtn")?.addEventListener("click", () => {
    state.autoErode = !state.autoErode;
    renderViewportToolbar();
  });

  $("#TopAddLayerBtn")?.addEventListener("click", () => {
    openConstructModal(state.activeStack === "texture" ? "texture" : "terrain");
  });

  $("#TopExportBtn")?.addEventListener("click", () => {
    state.inspectorTab = "export";
    renderInspectorDock();
  });

  // Viewport Tabs (3D Viewport, Split, 2D Map, Cross-Section)
  document.querySelectorAll("#ViewportTabs .document-tab").forEach((btn) => {
    btn.addEventListener("click", () => {
      state.viewportLayout = btn.dataset.view;
      renderViewportToolbar();
    });
  });

  // Viewport Action Row Buttons
  $("#BtnConstructLayer")?.addEventListener("click", () => {
    openConstructModal(state.activeStack === "texture" ? "texture" : "terrain");
  });
  $("#BtnStepErode")?.addEventListener("click", () => {
    engine.stepLiveErosion(state, 25);
    updateTelemetryUI();
    refreshLiveInspectorDiagrams();
  });
  $("#BtnAutoErode")?.addEventListener("click", () => {
    state.autoErode = !state.autoErode;
    renderViewportToolbar();
  });
  $("#BtnSculptToggle")?.addEventListener("click", () => {
    engine.brush.enabled = !engine.brush.enabled;
    renderViewportToolbar();
  });
  $("#BtnWaterToggle")?.addEventListener("click", () => {
    state.env.waterEnabled = !state.env.waterEnabled;
    renderViewportToolbar();
    if (state.selection.kind === "world") renderInspectorDock();
  });
  $("#BtnFogToggle")?.addEventListener("click", () => {
    state.env.fogEnabled = !state.env.fogEnabled;
    renderViewportToolbar();
    if (state.selection.kind === "world") renderInspectorDock();
  });
  $("#BtnTurntable")?.addEventListener("click", () => {
    state.turntable = !state.turntable;
    renderViewportToolbar();
  });
  $("#BtnFrameCamera")?.addEventListener("click", () => {
    state.cam = { yaw: 0.68, pitch: 0.42, dist: 11.2, target: [0, 0.95, 0], fovDeg: 45 };
  });

  // Sculpt Floating Bar controls
  document.querySelectorAll("#SculptFloatingBar button[data-bmode]").forEach((btn) => {
    btn.addEventListener("click", () => {
      engine.brush.mode = btn.dataset.bmode;
      renderViewportToolbar();
    });
  });
  $("#BrushRadiusInput")?.addEventListener("input", (e) => {
    engine.brush.radiusKm = parseFloat(e.target.value) || 0.42;
    setSliderFill(e.target);
  });
  $("#BrushStrengthInput")?.addEventListener("input", (e) => {
    engine.brush.strength = parseFloat(e.target.value) || 0.55;
    setSliderFill(e.target);
  });
  $("#BrushClearBtn")?.addEventListener("click", () => {
    engine.sculptOffsets.fill(0);
    rebuildTerrain({ fullRebuild: true });
    showToast("Cleared manual sculpt offsets");
  });

  // 3D Axis Orientation Gizmo clicks
  $("#AxisX")?.addEventListener("click", () => { state.cam.yaw = Math.PI * 0.5; state.cam.pitch = 0.25; });
  $("#AxisY")?.addEventListener("click", () => { state.cam.yaw = 0.0; state.cam.pitch = 1.45; });
  $("#AxisZ")?.addEventListener("click", () => { state.cam.yaw = 0.0; state.cam.pitch = 0.28; });

  // Layer Stack Search & Bottom Strip Buttons
  $("#StackSearchInput")?.addEventListener("input", (e) => {
    state.searchQuery = e.target.value;
    renderLayerStackDock();
  });
  $("#StackAddBtn")?.addEventListener("click", () => {
    openConstructModal(state.activeStack === "texture" ? "texture" : "terrain");
  });
  $("#StackStepErodeBtn")?.addEventListener("click", () => {
    engine.stepLiveErosion(state, 25);
    updateTelemetryUI();
    refreshLiveInspectorDiagrams();
  });
  $("#StackSwitchStageBtn")?.addEventListener("click", () => {
    const next = state.activeStack === "terrain" ? "texture" : "terrain";
    switchWorkflowStage(next);
  });

  // Keyboard shortcuts
  window.addEventListener("keydown", (e) => {
    if (e.target.tagName === "INPUT" || e.target.tagName === "SELECT" || e.target.tagName === "TEXTAREA") {
      if (e.key === "Escape") closeConstructModal();
      return;
    }
    if (e.key === "Escape") closeConstructModal();
    else if (e.key === "a" || e.key === "A") {
      e.preventDefault();
      openConstructModal(state.activeStack === "texture" ? "texture" : "terrain");
    } else if (e.key === "f" || e.key === "F") {
      state.cam = { yaw: 0.68, pitch: 0.42, dist: 11.2, target: [0, 0.95, 0], fovDeg: 45 };
    } else if (e.key === "e" || e.key === "E") {
      engine.stepLiveErosion(state, 20);
      updateTelemetryUI();
      refreshLiveInspectorDiagrams();
    } else if (e.key === "1") switchWorkflowStage("terrain");
    else if (e.key === "2") switchWorkflowStage("texture");
    else if (e.key === "3") switchWorkflowStage("world");
  });

  setupViewportInteractions(gpuCanvas);
  setupDockDividers();

  // Initial Evaluation & UI Render
  engine.evaluateStack(state, { fullRebuild: true });
  renderAllUI();

  // Frame Loop
  let t0 = performance.now();
  let frameCounter = 0;
  function frame(now) {
    const dt = Math.min(0.1, (now - t0) * 0.001);
    t0 = now;
    frameCounter++;

    if (state.turntable) {
      state.cam.yaw += dt * 0.22;
    }
    if (state.autoErode && frameCounter % 3 === 0) {
      engine.stepLiveErosion(state, 4);
      updateTelemetryUI();
    }

    const camInfo = computeCameraMatrices(gpuCanvas);
    engine.renderFrame(state, camInfo, now * 0.001);

    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);
}

boot();
