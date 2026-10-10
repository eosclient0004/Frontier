import './style.css';

const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const clone = (value) => JSON.parse(JSON.stringify(value));
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));

const terrainTemplates = [
  {
    id: 'continent', kind: 'continent', name: 'Continental base', type: 'Macro elevation · seeded', thumb: 'continent', impact: 'Affects global elevation',
    settings: { scale: 6.4, amplitude: 740, warp: 42, preserveBasins: true },
    defaults: { scale: 6.4, amplitude: 740, warp: 42, preserveBasins: true },
    controls: [
      { key: 'scale', label: 'Landscape scale', min: 2, max: 12, step: .1, unit: ' km', low: 'Localized', high: 'Continental' },
      { key: 'amplitude', label: 'Elevation span', min: 100, max: 1600, step: 10, unit: ' m', low: 'Low relief', high: 'Extreme relief' },
      { key: 'warp', label: 'Flow warp', min: 0, max: 100, step: 1, unit: '%', low: 'Structured', high: 'Organic' },
      { key: 'preserveBasins', label: 'Preserve drainage basins', hint: 'Maintain macro catchments', type: 'toggle' },
    ],
  },
  {
    id: 'ridges', kind: 'ridges', name: 'Alpine ridges', type: 'Folded strata · slope aware', thumb: 'ridges', impact: 'Affects mountain silhouettes',
    settings: { scale: 1.35, strength: 82, sharpness: 68, orientation: 'Diagonal' },
    defaults: { scale: 1.35, strength: 82, sharpness: 68, orientation: 'Diagonal' },
    controls: [
      { key: 'scale', label: 'Feature scale', min: .25, max: 3, step: .05, unit: ' km', low: 'Fine folds', high: 'Broad ranges' },
      { key: 'strength', label: 'Ridge prominence', min: 0, max: 100, step: 1, unit: '%', low: 'Subtle', high: 'Dominant' },
      { key: 'sharpness', label: 'Crest sharpness', min: 0, max: 100, step: 1, unit: '%', low: 'Weathered', high: 'Knife edge' },
      { key: 'orientation', label: 'Primary strike', type: 'segment', options: ['Diagonal', 'North / south', 'East / west'] },
    ],
  },
  {
    id: 'erosion', kind: 'erosion', name: 'Hydraulic erosion', type: 'Runoff · channel accumulation', thumb: 'erosion', impact: 'Carves runoff channels',
    settings: { intensity: 64, rainfall: 72, passes: 180, enabledFlow: true },
    defaults: { intensity: 64, rainfall: 72, passes: 180, enabledFlow: true },
    controls: [
      { key: 'intensity', label: 'Erosion intensity', min: 0, max: 100, step: 1, unit: '%', low: 'Pristine', high: 'Deeply cut' },
      { key: 'rainfall', label: 'Annual runoff', min: 0, max: 100, step: 1, unit: '%', low: 'Arid', high: 'Pluvial' },
      { key: 'passes', label: 'Solver passes', min: 20, max: 320, step: 10, unit: '', low: 'Fast', high: 'Refined' },
      { key: 'enabledFlow', label: 'Accumulate flow paths', hint: 'Preserve dendritic drainage', type: 'toggle' },
    ],
  },
  {
    id: 'talus', kind: 'talus', name: 'Talus & sediment', type: 'Gravity deposition · material mask', thumb: 'talus', impact: 'Softens unstable slopes',
    settings: { coverage: 52, repose: 34, grain: 38, mode: 'Balanced' },
    defaults: { coverage: 52, repose: 34, grain: 38, mode: 'Balanced' },
    controls: [
      { key: 'coverage', label: 'Deposit coverage', min: 0, max: 100, step: 1, unit: '%', low: 'Sparse', high: 'Blanketed' },
      { key: 'repose', label: 'Angle of repose', min: 20, max: 48, step: 1, unit: '°', low: 'Loose', high: 'Compacted' },
      { key: 'grain', label: 'Grain breakup', min: 0, max: 100, step: 1, unit: '%', low: 'Boulders', high: 'Fine debris' },
      { key: 'mode', label: 'Deposition profile', type: 'segment', options: ['Light', 'Balanced', 'Heavy'] },
    ],
  },
];

const surfaceTemplates = [
  {
    id: 'bedrock', kind: 'bedrock', name: 'Exposed bedrock', type: 'Slope & elevation mask', thumb: 'bedrock', impact: 'Controls cliff material',
    settings: { coverage: 66, contrast: 58, roughness: 83, triplanar: true },
    defaults: { coverage: 66, contrast: 58, roughness: 83, triplanar: true },
    controls: [
      { key: 'coverage', label: 'Rock exposure', min: 0, max: 100, step: 1, unit: '%', low: 'Covered', high: 'Exposed' },
      { key: 'contrast', label: 'Mask contrast', min: 0, max: 100, step: 1, unit: '%', low: 'Soft blend', high: 'Hard break' },
      { key: 'roughness', label: 'Surface roughness', min: 0, max: 100, step: 1, unit: '%', low: 'Polished', high: 'Fractured' },
      { key: 'triplanar', label: 'Triplanar projection', hint: 'Eliminate cliff stretching', type: 'toggle' },
    ],
  },
  {
    id: 'meadow', kind: 'meadow', name: 'Alpine meadow', type: 'Altitude & moisture blend', thumb: 'meadow', impact: 'Controls vegetated slopes',
    settings: { coverage: 62, moisture: 56, variation: 47, season: 'Late summer' },
    defaults: { coverage: 62, moisture: 56, variation: 47, season: 'Late summer' },
    controls: [
      { key: 'coverage', label: 'Ground cover', min: 0, max: 100, step: 1, unit: '%', low: 'Sparse', high: 'Lush' },
      { key: 'moisture', label: 'Root-zone moisture', min: 0, max: 100, step: 1, unit: '%', low: 'Dry', high: 'Saturated' },
      { key: 'variation', label: 'Color variation', min: 0, max: 100, step: 1, unit: '%', low: 'Even', high: 'Mottled' },
      { key: 'season', label: 'Seasonal palette', type: 'segment', options: ['Spring', 'Late summer', 'Autumn'] },
    ],
  },
  {
    id: 'scree', kind: 'scree', name: 'Scree fields', type: 'Talus linked · micro breakup', thumb: 'scree', impact: 'Accents unstable faces',
    settings: { coverage: 48, scale: 36, brightness: 53, followTalus: true },
    defaults: { coverage: 48, scale: 36, brightness: 53, followTalus: true },
    controls: [
      { key: 'coverage', label: 'Scree coverage', min: 0, max: 100, step: 1, unit: '%', low: 'Trace', high: 'Dominant' },
      { key: 'scale', label: 'Aggregate scale', min: 0, max: 100, step: 1, unit: '%', low: 'Fine', high: 'Coarse' },
      { key: 'brightness', label: 'Albedo lift', min: 0, max: 100, step: 1, unit: '%', low: 'Dark', high: 'Pale' },
      { key: 'followTalus', label: 'Link talus deposition', hint: 'Inherit terrain sediment mask', type: 'toggle' },
    ],
  },
  {
    id: 'snow', kind: 'snow', name: 'Windblown snow', type: 'Altitude · aspect · cavity mask', thumb: 'snow', impact: 'Adds high-altitude snowpack',
    settings: { altitude: 1560, coverage: 76, wind: 44, melt: 'Natural' },
    defaults: { altitude: 1560, coverage: 76, wind: 44, melt: 'Natural' },
    controls: [
      { key: 'altitude', label: 'Snow line', min: 450, max: 2600, step: 10, unit: ' m', low: 'Low valleys', high: 'Summits' },
      { key: 'coverage', label: 'Pack density', min: 0, max: 100, step: 1, unit: '%', low: 'Patchy', high: 'Deep pack' },
      { key: 'wind', label: 'Wind redistribution', min: 0, max: 100, step: 1, unit: '%', low: 'Even', high: 'Leeward' },
      { key: 'melt', label: 'Melt pattern', type: 'segment', options: ['Clean', 'Natural', 'Dirty'] },
    ],
  },
  {
    id: 'wetness', kind: 'wetness', name: 'Runoff wetness', type: 'Flow accumulation · darkening', thumb: 'wetness', impact: 'Darkens drainage pathways',
    settings: { coverage: 28, flow: 67, darkness: 42, streambed: true },
    defaults: { coverage: 28, flow: 67, darkness: 42, streambed: true },
    controls: [
      { key: 'coverage', label: 'Wetness coverage', min: 0, max: 100, step: 1, unit: '%', low: 'Dry', high: 'Pervasive' },
      { key: 'flow', label: 'Flow threshold', min: 0, max: 100, step: 1, unit: '%', low: 'Diffuse', high: 'Channels only' },
      { key: 'darkness', label: 'Albedo darkening', min: 0, max: 100, step: 1, unit: '%', low: 'Subtle', high: 'Saturated' },
      { key: 'streambed', label: 'Expose streambeds', hint: 'Accentuate major drainage', type: 'toggle' },
    ],
  },
];

const extraTemplates = {
  terrain: [
    { id: 'terraces', kind: 'terraces', name: 'Terrace incision', type: 'Structural contour detail', thumb: 'erosion', impact: 'Adds stepped drainage detail', settings: { amount: 36, spacing: 42 }, defaults: { amount: 36, spacing: 42 }, controls: [ {key:'amount',label:'Incision strength',min:0,max:100,step:1,unit:'%',low:'Subtle',high:'Defined'}, {key:'spacing',label:'Terrace interval',min:10,max:90,step:1,unit:' m',low:'Fine',high:'Broad'} ] },
    { id: 'glacial', kind: 'glacial', name: 'Glacial carving', type: 'U-valley profile', thumb: 'ridges', impact: 'Broadens high-altitude valleys', settings: { amount: 42, headwall: 56 }, defaults: { amount: 42, headwall: 56 }, controls: [ {key:'amount',label:'Carve amount',min:0,max:100,step:1,unit:'%',low:'Light',high:'Deep'}, {key:'headwall',label:'Headwall steepness',min:0,max:100,step:1,unit:'%',low:'Soft',high:'Steep'} ] },
  ],
  surface: [
    { id: 'lichen', kind: 'lichen', name: 'Lichen patina', type: 'Moisture-gated micro color', thumb: 'meadow', impact: 'Adds cool damp-surface variation', settings: { coverage: 34, hue: 52 }, defaults: { coverage: 34, hue: 52 }, controls: [ {key:'coverage',label:'Patina coverage',min:0,max:100,step:1,unit:'%',low:'Sparse',high:'Dense'}, {key:'hue',label:'Cool hue shift',min:0,max:100,step:1,unit:'%',low:'Neutral',high:'Cool'} ] },
    { id: 'ash', kind: 'ash', name: 'Aeolian dusting', type: 'Windward sediment veil', thumb: 'talus', impact: 'Adds light exposed sediment', settings: { coverage: 23, scale: 40 }, defaults: { coverage: 23, scale: 40 }, controls: [ {key:'coverage',label:'Dust coverage',min:0,max:100,step:1,unit:'%',low:'Trace',high:'Heavy'}, {key:'scale',label:'Pattern scale',min:0,max:100,step:1,unit:'%',low:'Fine',high:'Broad'} ] },
  ],
};

const presets = [
  {
    id: 'alpine', title: 'Alpine watershed', description: 'Glacial ridges · high runoff', seed: 48192,
    values: { continent: { scale: 6.4, amplitude: 740, warp: 42 }, ridges: { strength: 82, sharpness: 68, scale: 1.35 }, erosion: { intensity: 64, rainfall: 72 }, talus: { coverage: 52 }, snow: { altitude: 1560, coverage: 76 }, meadow: { coverage: 62, moisture: 56 } },
  },
  {
    id: 'badlands', title: 'Oxide badlands', description: 'Dry spires · exposed strata', seed: 23017,
    values: { continent: { scale: 5.2, amplitude: 610, warp: 25 }, ridges: { strength: 55, sharpness: 82, scale: .72 }, erosion: { intensity: 86, rainfall: 24 }, talus: { coverage: 31 }, snow: { altitude: 2440, coverage: 4 }, meadow: { coverage: 13, moisture: 10 }, bedrock: { coverage: 87 } },
  },
  {
    id: 'volcanic', title: 'Volcanic caldera', description: 'Broken ridges · mineral scree', seed: 73904,
    values: { continent: { scale: 7.8, amplitude: 920, warp: 63 }, ridges: { strength: 88, sharpness: 71, scale: 2.1 }, erosion: { intensity: 35, rainfall: 38 }, talus: { coverage: 78 }, snow: { altitude: 2020, coverage: 26 }, meadow: { coverage: 26, moisture: 24 }, scree: { coverage: 78 } },
  },
  {
    id: 'coastal', title: 'Coastal headlands', description: 'Wind-cut cliffs · wet meadow', seed: 65211,
    values: { continent: { scale: 4.2, amplitude: 360, warp: 75 }, ridges: { strength: 46, sharpness: 51, scale: .9 }, erosion: { intensity: 72, rainfall: 84 }, talus: { coverage: 42 }, snow: { altitude: 2600, coverage: 0 }, meadow: { coverage: 85, moisture: 80 }, wetness: { coverage: 54, flow: 43 } },
  },
];

function buildLayers(templates) {
  return templates.map((template) => ({ ...clone(template), uid: template.id, enabled: true }));
}

const state = {
  activeDomain: 'terrain',
  selectedId: 'ridges',
  layers: { terrain: buildLayers(terrainTemplates), surface: buildLayers(surfaceTemplates) },
  seed: presets[0].seed,
  presetId: presets[0].id,
  viewMode: 'rendered',
  atmosphere: true,
  detail: true,
  grid: false,
  fov: 48,
  qualityIndex: 0,
  camera: { yaw: -0.66, pitch: 0.42, distance: 115 },
  projectName: 'Crownfall Basin',
  history: [],
  future: [],
  saveTimer: null,
};

let renderer = null;
let dragState = null;
let toastTimer = null;
let savedAt = null;

function layerFor(id = state.selectedId) {
  for (const domain of ['terrain', 'surface']) {
    const found = state.layers[domain].find((layer) => layer.uid === id);
    if (found) return found;
  }
  return null;
}
function layerForKind(kind) {
  for (const domain of ['terrain', 'surface']) {
    const found = state.layers[domain].find((layer) => layer.kind === kind);
    if (found) return found;
  }
  return null;
}
function layerValue(kind, key, fallback) {
  const layer = layerForKind(kind);
  return layer && layer.enabled && layer.settings[key] !== undefined ? layer.settings[key] : fallback;
}
function visibleLayers(domain) { return state.layers[domain].filter((layer) => layer.enabled); }
function currentLayer() { return layerFor(state.selectedId) || state.layers[state.activeDomain][0]; }
function activeLayerDomain() {
  return state.layers.terrain.some((layer) => layer.uid === state.selectedId) ? 'terrain' : 'surface';
}

function captureState() {
  return clone({
    activeDomain: state.activeDomain,
    selectedId: state.selectedId,
    layers: state.layers,
    seed: state.seed,
    presetId: state.presetId,
    viewMode: state.viewMode,
    atmosphere: state.atmosphere,
    detail: state.detail,
    grid: state.grid,
    fov: state.fov,
  });
}
function restoreState(snapshot) {
  state.activeDomain = snapshot.activeDomain;
  state.selectedId = snapshot.selectedId;
  state.layers = clone(snapshot.layers);
  state.seed = snapshot.seed;
  state.presetId = snapshot.presetId;
  state.viewMode = snapshot.viewMode;
  state.atmosphere = snapshot.atmosphere;
  state.detail = snapshot.detail;
  state.grid = snapshot.grid;
  state.fov = snapshot.fov;
  syncAll();
  markDirty();
}
function pushHistory(before) {
  if (!before) return;
  state.history.push(before);
  if (state.history.length > 40) state.history.shift();
  state.future = [];
  updateHistoryButtons();
}
function mutate(mutator, { history = true, render = true, stack = false, inspector = false } = {}) {
  const before = history ? captureState() : null;
  mutator();
  if (history) pushHistory(before);
  if (stack) renderStack();
  if (inspector) renderInspector();
  updateReadouts();
  markDirty();
  if (render) renderer?.requestRender();
}

function updateHistoryButtons() {
  $('#undo-button').disabled = state.history.length === 0;
  $('#redo-button').disabled = state.future.length === 0;
}
function undo() {
  if (!state.history.length) return;
  const previous = state.history.pop();
  state.future.push(captureState());
  restoreState(previous);
  updateHistoryButtons();
  toast('Restored previous edit');
}
function redo() {
  if (!state.future.length) return;
  const next = state.future.pop();
  state.history.push(captureState());
  restoreState(next);
  updateHistoryButtons();
  toast('Reapplied edit');
}

function formatValue(control, value) {
  if (typeof value !== 'number') return value;
  let digits = 0;
  if (control.step && control.step < 1) digits = control.step < .1 ? 2 : 1;
  const formatted = value.toLocaleString(undefined, { maximumFractionDigits: digits, minimumFractionDigits: digits });
  return `${formatted}${control.unit || ''}`;
}
function sliderProgress(control, value) {
  return clamp(((value - control.min) / (control.max - control.min)) * 100, 0, 100);
}
function estimatePeak() {
  const base = layerValue('continent', 'amplitude', 600);
  const ridge = layerValue('ridges', 'strength', 50);
  const erosion = layerValue('erosion', 'intensity', 50);
  return Math.round((base * 1.35 + ridge * 11 - erosion * 1.4 + 420) / 5) * 5;
}
function renderSettings() {
  const continent = layerForKind('continent');
  const ridges = layerForKind('ridges');
  const erosion = layerForKind('erosion');
  const talus = layerForKind('talus');
  const bedrock = layerForKind('bedrock');
  const meadow = layerForKind('meadow');
  const scree = layerForKind('scree');
  const snow = layerForKind('snow');
  const wetness = layerForKind('wetness');
  const terraces = layerForKind('terraces');
  const glacial = layerForKind('glacial');
  const lichen = layerForKind('lichen');
  const ash = layerForKind('ash');
  const active = (layer, key, fallback) => layer?.enabled ? (layer.settings[key] ?? fallback) : 0;
  const continentScale = active(continent, 'scale', 6.4);
  const continentWarp = active(continent, 'warp', 42);
  const extent = continentScale * 10;
  const snowAltitude = active(snow, 'altitude', 1560);
  return {
    extent,
    baseHeight: active(continent, 'amplitude', 740) / 100,
    macroFrequency: .016 + (12 - clamp(continentScale, 2, 12)) * .0032 + continentWarp * .000012,
    warp: continentWarp / 100,
    ridgeStrength: active(ridges, 'strength', 82) / 100,
    ridgeScale: active(ridges, 'scale', 1.35),
    sharpness: active(ridges, 'sharpness', 68) / 100,
    erosion: clamp(active(erosion, 'intensity', 64) / 100 * (.48 + active(erosion, 'rainfall', 72) / 190) + active(glacial, 'amount', 0) / 320, 0, 1),
    rainfall: active(erosion, 'rainfall', 72) / 100,
    talus: clamp(active(talus, 'coverage', 52) / 100 + active(terraces, 'amount', 0) / 430, 0, 1),
    snowline: clamp((snowAltitude - 420) / 2250, .16, .96),
    snow: active(snow, 'coverage', 76) / 100,
    wind: active(snow, 'wind', 44) / 100,
    wetness: clamp(active(wetness, 'coverage', 28) / 100 + active(lichen, 'coverage', 0) / 500, 0, 1),
    bedrock: active(bedrock, 'coverage', 66) / 100,
    meadow: active(meadow, 'coverage', 62) / 100,
    scree: clamp(active(scree, 'coverage', 48) / 100 + active(talus, 'coverage', 52) / 520 + active(ash, 'coverage', 0) / 360, 0, 1),
    viewMode: state.viewMode === 'height' ? 1 : state.viewMode === 'mask' ? 2 : 0,
  };
}

function markDirty() {
  const element = $('#save-status');
  element.textContent = 'Saving…';
  element.style.color = '#a7b477';
  clearTimeout(state.saveTimer);
  state.saveTimer = setTimeout(() => {
    try { localStorage.setItem('frontier-strata-session', JSON.stringify(captureState())); } catch { /* local state is optional */ }
    savedAt = new Date();
    element.textContent = 'All changes saved';
    element.style.color = '';
  }, 460);
}
function toast(message) {
  const element = $('#toast');
  element.textContent = message;
  element.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => element.classList.remove('show'), 2100);
}

function renderStack() {
  const domain = state.activeDomain;
  const list = $('#stack-list');
  const layers = state.layers[domain];
  $('#stack-title').textContent = domain === 'terrain' ? 'Terrain build' : 'Surface response';
  $('#terrain-count').textContent = String(state.layers.terrain.length).padStart(2, '0');
  $('#surface-count').textContent = String(state.layers.surface.length).padStart(2, '0');
  $('#add-layer-label').textContent = domain === 'terrain' ? 'terrain layer' : 'surface layer';
  $$('.stack-tab').forEach((tab) => tab.classList.toggle('active', tab.dataset.domain === domain));

  list.innerHTML = layers.map((layer, index) => `
    <div class="layer-row ${layer.uid === state.selectedId ? 'selected' : ''} ${layer.enabled ? '' : 'disabled'}" data-id="${layer.uid}">
      <button class="drag-handle" data-action="move" data-direction="${index === 0 ? 'down' : 'up'}" title="Move layer ${index === 0 ? 'down' : 'up'}"></button>
      <button class="layer-info" data-action="select" title="Inspect ${layer.name}">
        <span class="layer-thumb ${layer.thumb}"></span>
        <span><b>${layer.name}</b><small>${layer.type}</small></span>
      </button>
      <div class="layer-actions">
        <button class="layer-action ${layer.enabled ? 'eye-on' : ''}" data-action="visibility" title="${layer.enabled ? 'Hide' : 'Show'} layer"><span class="icon icon-eye"></span></button>
        <button class="layer-action" data-action="move" data-direction="${index === 0 ? 'down' : 'up'}" title="Move layer ${index === 0 ? 'down' : 'up'}">${index === 0 ? '↓' : '↑'}</button>
      </div>
    </div>`).join('');

  $$('.layer-row', list).forEach((row) => {
    row.addEventListener('click', (event) => {
      const action = event.target.closest('[data-action]');
      const id = row.dataset.id;
      if (!action || action.dataset.action === 'select') {
        selectLayer(id);
        return;
      }
      if (action.dataset.action === 'visibility') toggleLayerVisibility(id);
      if (action.dataset.action === 'move') moveLayer(id, action.dataset.direction);
    });
  });
}

function renderInspector() {
  const layer = currentLayer();
  if (!layer) return;
  const domain = activeLayerDomain();
  const summary = $('#selection-summary');
  $('#inspector-name').textContent = layer.name;
  $('#layer-impact').textContent = layer.impact;
  $('#selected-visibility').classList.toggle('selected-off', !layer.enabled);
  $('#selected-visibility').innerHTML = `<span class="icon icon-eye"></span>`;
  summary.innerHTML = `
    <span class="summary-glyph layer-thumb ${layer.thumb}"></span>
    <span class="summary-copy"><b>${layer.type}</b><small>${layer.enabled ? 'Enabled in the active ' + (domain === 'terrain' ? 'build' : 'surface stack') : 'Layer excluded from the active build'}</small></span>
    <span class="summary-chip">${layer.enabled ? 'LIVE' : 'MUTED'}</span>`;

  const content = $('#inspector-content');
  content.innerHTML = '';
  for (const control of layer.controls) {
    const group = document.createElement('section');
    group.className = 'control-group';
    if (control.type === 'toggle') {
      const checked = !!layer.settings[control.key];
      group.innerHTML = `<div class="check-control"><span>${control.label}<small>${control.hint || ''}</small></span><button class="mini-toggle ${checked ? 'on' : ''}" aria-pressed="${checked}" title="Toggle ${control.label}"><i></i></button></div>`;
      $('button', group).addEventListener('click', () => {
        mutate(() => { layer.settings[control.key] = !layer.settings[control.key]; }, { inspector: true });
      });
      content.appendChild(group);
      continue;
    }
    if (control.type === 'segment') {
      const active = layer.settings[control.key];
      group.innerHTML = `<div class="control-label"><span>${control.label}</span><small>${active}</small></div><div class="segment-control">${control.options.map((option) => `<button class="${option === active ? 'active' : ''}" data-value="${option}">${option.replace(' / ', '·')}</button>`).join('')}</div>`;
      $$('button', group).forEach((button) => button.addEventListener('click', () => {
        if (button.dataset.value === layer.settings[control.key]) return;
        mutate(() => { layer.settings[control.key] = button.dataset.value; }, { inspector: true });
      }));
      content.appendChild(group);
      continue;
    }
    const value = layer.settings[control.key];
    const progress = sliderProgress(control, value);
    group.innerHTML = `
      <div class="control-label"><span>${control.label}</span><input class="value-input" type="text" value="${formatValue(control, value)}" aria-label="${control.label}" /></div>
      <div class="range-shell"><input type="range" min="${control.min}" max="${control.max}" step="${control.step}" value="${value}" style="--progress:${progress}%" aria-label="${control.label}" /></div>
      <div class="range-note"><span>${control.low || control.min}</span><span>${control.high || control.max}</span></div>${control.graphic ? '<div class="mini-value-graphic"></div>' : ''}`;
    const range = $('input[type="range"]', group);
    const text = $('.value-input', group);
    let before = null;
    const applyRange = (raw, commit = false) => {
      const next = Number(raw);
      if (!Number.isFinite(next)) return;
      layer.settings[control.key] = clamp(next, control.min, control.max);
      const exact = layer.settings[control.key];
      range.value = exact;
      range.style.setProperty('--progress', `${sliderProgress(control, exact)}%`);
      text.value = formatValue(control, exact);
      updateReadouts();
      markDirty();
      renderer?.requestRender();
      if (commit && before) { pushHistory(before); before = null; }
    };
    range.addEventListener('pointerdown', () => { before = captureState(); });
    range.addEventListener('keydown', () => { if (!before) before = captureState(); });
    range.addEventListener('input', () => { if (!before) before = captureState(); applyRange(range.value); });
    range.addEventListener('change', () => applyRange(range.value, true));
    text.addEventListener('focus', () => { text.value = layer.settings[control.key]; });
    text.addEventListener('change', () => {
      const parsed = Number(String(text.value).replace(/[^\d.-]/g, ''));
      before = captureState();
      applyRange(parsed, true);
    });
    text.addEventListener('blur', () => { text.value = formatValue(control, layer.settings[control.key]); });
    content.appendChild(group);
  }
}

function updateReadouts() {
  const settings = renderSettings();
  $('#peak-readout').textContent = `${estimatePeak().toLocaleString()} m`;
  $('#extent-readout').textContent = `${settings.extent.toFixed(1)} × ${settings.extent.toFixed(1)} km`;
  $('#build-readout').textContent = `${visibleLayers('terrain').length} terrain layers`;
  const resolution = state.detail ? '4,096²' : '2,048²';
  $('#resolution-readout').textContent = resolution;
  $('#projection-button').innerHTML = `Perspective <span>${state.fov}°</span><i>⌄</i>`;
  $('#quality-label').textContent = ['Ultra', 'Balanced', 'Fast'][state.qualityIndex];
  $('#atmosphere-toggle').checked = state.atmosphere;
  $('#detail-toggle').checked = state.detail;
  $$('.view-mode').forEach((button) => button.classList.toggle('active', button.dataset.view === state.viewMode));
  $$('.mode-entry').forEach((button) => button.classList.toggle('active', (button.dataset.mode === 'terrain' && state.activeDomain === 'terrain') || (button.dataset.mode === 'surface' && state.activeDomain === 'surface')));
  const preset = presets.find((item) => item.id === state.presetId) || presets[0];
  $('#preset-title').textContent = preset.title;
  $('#preset-description').textContent = preset.description;
}

function selectLayer(id) {
  const layer = layerFor(id);
  if (!layer) return;
  state.selectedId = id;
  state.activeDomain = activeLayerDomain();
  renderStack();
  renderInspector();
}
function switchDomain(domain) {
  if (state.activeDomain === domain) return;
  state.activeDomain = domain;
  const current = layerFor(state.selectedId);
  if (!current || activeLayerDomain() !== domain) state.selectedId = state.layers[domain][0]?.uid;
  renderStack();
  renderInspector();
  updateReadouts();
}
function toggleLayerVisibility(id) {
  const layer = layerFor(id);
  if (!layer) return;
  mutate(() => { layer.enabled = !layer.enabled; }, { stack: true, inspector: id === state.selectedId });
}
function moveLayer(id, direction) {
  const domain = activeLayerDomain();
  const layers = state.layers[domain];
  const index = layers.findIndex((layer) => layer.uid === id);
  const next = direction === 'up' ? index - 1 : index + 1;
  if (index < 0 || next < 0 || next >= layers.length) return;
  mutate(() => { [layers[index], layers[next]] = [layers[next], layers[index]]; }, { stack: true });
}
function resetLayer() {
  const layer = currentLayer();
  if (!layer) return;
  mutate(() => { layer.settings = clone(layer.defaults); layer.enabled = true; }, { inspector: true, stack: true });
  toast(`${layer.name} reset to defaults`);
}
function addLayer(template) {
  const domain = state.activeDomain;
  const same = state.layers[domain].filter((item) => item.kind === template.kind).length;
  const uid = same ? `${template.id}-${Date.now().toString().slice(-4)}` : template.id;
  mutate(() => {
    state.layers[domain].push({ ...clone(template), uid, enabled: true });
    state.selectedId = uid;
  }, { stack: true, inspector: true });
  closePopovers();
  toast(`${template.name} added to ${domain} stack`);
}

function openPopover(id, anchor) {
  closePopovers(id);
  const popover = $(`#${id}`);
  popover.hidden = false;
  const rect = anchor.getBoundingClientRect();
  const popoverRect = popover.getBoundingClientRect();
  let left = rect.left;
  let top = rect.bottom + 7;
  if (id === 'add-popover' || id === 'menu-popover') left = rect.right - popoverRect.width;
  left = clamp(left, 7, window.innerWidth - popoverRect.width - 7);
  top = clamp(top, 7, window.innerHeight - popoverRect.height - 7);
  popover.style.left = `${left}px`;
  popover.style.top = `${top}px`;
}
function closePopovers(except = '') { $$('.popover').forEach((popover) => { if (popover.id !== except) popover.hidden = true; }); }
function renderPresetOptions() {
  $('#preset-options').innerHTML = presets.map((preset) => `<button class="preset-option ${preset.id === state.presetId ? 'active' : ''}" data-preset="${preset.id}"><span class="preset-art"><i></i><i></i><i></i></span><span><b>${preset.title}</b><small>${preset.description}</small></span></button>`).join('');
  $$('.preset-option').forEach((button) => button.addEventListener('click', () => applyPreset(button.dataset.preset)));
}
function renderAddOptions() {
  const candidates = extraTemplates[state.activeDomain];
  $('#add-options').innerHTML = candidates.map((template) => `<button class="add-option" data-template="${template.id}"><span>+</span>${template.name}<small>${template.type}</small></button>`).join('');
  $$('.add-option').forEach((button) => button.addEventListener('click', () => {
    const template = candidates.find((candidate) => candidate.id === button.dataset.template);
    if (template) addLayer(template);
  }));
}
function applyPreset(id) {
  const preset = presets.find((item) => item.id === id);
  if (!preset) return;
  mutate(() => {
    state.layers = { terrain: buildLayers(terrainTemplates), surface: buildLayers(surfaceTemplates) };
    for (const [kind, settings] of Object.entries(preset.values)) {
      const layer = layerForKind(kind);
      if (layer) Object.assign(layer.settings, settings);
    }
    state.seed = preset.seed;
    state.presetId = preset.id;
    state.activeDomain = 'terrain';
    state.selectedId = 'ridges';
  }, { stack: true, inspector: true });
  updateReadouts();
  closePopovers();
  flashGeneration();
  toast(`${preset.title} applied`);
}
function cyclePreset() {
  const index = Math.max(0, presets.findIndex((preset) => preset.id === state.presetId));
  applyPreset(presets[(index + 1) % presets.length].id);
}
function generateTerrain() {
  mutate(() => {
    const random = new Uint32Array(1);
    if (globalThis.crypto?.getRandomValues) crypto.getRandomValues(random); else random[0] = Math.floor(Math.random() * 99999);
    state.seed = 10000 + (random[0] % 89999);
  }, { history: true });
  flashGeneration();
  toast(`Seed ${state.seed} generated from the active stack`);
}
function flashGeneration() {
  const flash = $('#generation-flash');
  flash.classList.add('active');
  setTimeout(() => flash.classList.remove('active'), 920);
}
function frameTerrain() {
  state.camera.yaw = -0.66;
  state.camera.pitch = .42;
  state.camera.distance = 115;
  renderer?.requestRender();
  toast('Terrain framed');
}

const shader = /* wgsl */ `
struct Uniforms {
  viewProjection: mat4x4<f32>,
  camera: vec4<f32>,
  terrain: vec4<f32>,
  shape: vec4<f32>,
  surfaceA: vec4<f32>,
  surfaceB: vec4<f32>,
  lighting: vec4<f32>,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

fn sat(v: f32) -> f32 { return clamp(v, 0.0, 1.0); }
fn hash12(p: vec2<f32>) -> f32 { return fract(sin(dot(p, vec2<f32>(127.1, 311.7))) * 43758.5453123); }
fn noise(p: vec2<f32>) -> f32 {
  let i = floor(p);
  let f = fract(p);
  let w = f * f * (3.0 - 2.0 * f);
  let a = hash12(i);
  let b = hash12(i + vec2<f32>(1.0, 0.0));
  let c = hash12(i + vec2<f32>(0.0, 1.0));
  let d = hash12(i + vec2<f32>(1.0, 1.0));
  return mix(mix(a, b, w.x), mix(c, d, w.x), w.y);
}
fn fbm(pIn: vec2<f32>) -> f32 {
  var p = pIn;
  var sum = 0.0;
  var amp = 0.5;
  for (var i = 0; i < 5; i = i + 1) {
    sum = sum + noise(p) * amp;
    p = vec2<f32>(p.x * 1.63 - p.y * 1.11, p.x * 1.11 + p.y * 1.63) + vec2<f32>(17.17, -13.73);
    amp = amp * 0.5;
  }
  return sum / 0.96875;
}
fn ridged(pIn: vec2<f32>) -> f32 {
  var p = pIn;
  var sum = 0.0;
  var amp = 0.5;
  for (var i = 0; i < 4; i = i + 1) {
    let n = 1.0 - abs(noise(p) * 2.0 - 1.0);
    sum = sum + n * amp;
    p = vec2<f32>(p.x * 1.71 + p.y * 1.02, -p.x * 1.02 + p.y * 1.71) + vec2<f32>(-9.41, 21.33);
    amp = amp * 0.5;
  }
  return sum / 0.9375;
}
fn terrainHeight(p: vec2<f32>) -> f32 {
  let seed = vec2<f32>(u.terrain.z, u.terrain.z * 1.618);
  let broadFrequency = u.shape.x;
  let macro = fbm(p * broadFrequency + seed);
  let continent = (macro - 0.50) * u.terrain.x * 3.35;
  let massif = smoothstep(0.34, 0.76, fbm(p * broadFrequency * 0.48 + seed * 0.59 + 8.0));
  let ridgeField = pow(max(0.0, ridged(p * u.shape.w + seed * 2.31)), 1.55 + (1.0 - u.shape.z) * 1.7);
  let ridges = ridgeField * u.terrain.w * (9.0 + 18.0 * massif);
  let drainage = pow(max(0.0, 1.0 - abs(fbm(p * 0.39 + seed * 3.7) * 2.0 - 1.0)), 8.0);
  let cut = drainage * u.shape.y * (1.1 + 3.8 * massif);
  let strata = sin((p.x * 0.29 + p.y * 0.21) + fbm(p * 0.12) * 2.0) * u.shape.z * 0.65;
  let grain = (fbm(p * 1.35 + seed * 5.2) - 0.5) * u.shape.z * 0.7;
  return continent + ridges - cut + strata + grain - u.surfaceA.z * drainage * 0.35;
}

struct VertexOut {
  @builtin(position) position: vec4<f32>,
  @location(0) world: vec3<f32>,
  @location(1) normal: vec3<f32>,
  @location(2) elevation: f32,
};

@vertex
fn vsMain(@location(0) grid: vec2<f32>) -> VertexOut {
  let p = grid * u.terrain.y;
  let h = terrainHeight(p);
  let e = 0.15;
  let hx0 = terrainHeight(p - vec2<f32>(e, 0.0));
  let hx1 = terrainHeight(p + vec2<f32>(e, 0.0));
  let hz0 = terrainHeight(p - vec2<f32>(0.0, e));
  let hz1 = terrainHeight(p + vec2<f32>(0.0, e));
  let normal = normalize(vec3<f32>(hx0 - hx1, 2.0 * e, hz0 - hz1));
  let world = vec3<f32>(p.x, h, p.y);
  var out: VertexOut;
  out.position = u.viewProjection * vec4<f32>(world, 1.0);
  out.world = world;
  out.normal = normal;
  out.elevation = h;
  return out;
}

@fragment
fn fsMain(in: VertexOut) -> @location(0) vec4<f32> {
  let n = normalize(in.normal);
  let elevation = sat((in.elevation + 12.0) / 44.0);
  let slope = sat(1.0 - n.y);
  let patch = fbm(in.world.xz * 0.17 + vec2<f32>(u.terrain.z * 0.31, 4.7));
  let smallPatch = fbm(in.world.xz * 0.78 + vec2<f32>(2.1, u.terrain.z * 0.13));
  let rock = smoothstep(0.17, 0.68, slope * (1.2 + u.surfaceA.w * 0.4) + elevation * 0.22 + (patch - 0.5) * 0.16) * u.surfaceB.y;
  let scree = smoothstep(0.25, 0.72, slope + (0.5 - elevation) * 0.24) * (1.0 - rock * 0.72) * u.surfaceB.z;
  let snowline = u.surfaceA.x;
  let snow = smoothstep(snowline - 0.10, snowline + 0.09, elevation + (patch - 0.5) * 0.12 - slope * (0.17 + u.surfaceA.y * 0.09)) * u.surfaceA.y;
  let meadow = (1.0 - rock) * (1.0 - snow) * (1.0 - scree * 0.62) * u.surfaceB.x;
  let channel = pow(max(0.0, 1.0 - abs(fbm(in.world.xz * 0.39 + u.terrain.z * 3.7) * 2.0 - 1.0)), 8.0);
  let wet = channel * u.surfaceA.z * (1.0 - snow);

  let meadowColor = mix(vec3<f32>(0.15, 0.23, 0.12), vec3<f32>(0.33, 0.43, 0.19), patch * 0.72 + smallPatch * 0.28);
  let rockColor = mix(vec3<f32>(0.20, 0.205, 0.18), vec3<f32>(0.47, 0.405, 0.31), patch * 0.62 + elevation * 0.20);
  let screeColor = mix(vec3<f32>(0.27, 0.255, 0.20), vec3<f32>(0.50, 0.425, 0.32), smallPatch);
  let snowColor = mix(vec3<f32>(0.67, 0.75, 0.72), vec3<f32>(0.95, 0.985, 0.94), sat(0.5 + patch));
  var albedo = meadowColor;
  albedo = mix(albedo, screeColor, scree);
  albedo = mix(albedo, rockColor, rock);
  albedo = mix(albedo, snowColor, snow);
  albedo = mix(albedo, albedo * vec3<f32>(0.47, 0.61, 0.57), wet);

  let azimuth = u.lighting.x;
  let elevationAngle = u.lighting.y;
  let sun = normalize(vec3<f32>(cos(azimuth) * cos(elevationAngle), sin(elevationAngle), sin(azimuth) * cos(elevationAngle)));
  let diffuse = max(dot(n, sun), 0.0);
  let viewDir = normalize(u.camera.xyz - in.world);
  let halfDir = normalize(sun + viewDir);
  let specular = pow(max(dot(n, halfDir), 0.0), 38.0) * (0.08 + snow * 0.22 + wet * 0.13);
  let ambient = vec3<f32>(0.18, 0.26, 0.27) * (0.72 + 0.28 * n.y);
  var color = albedo * (ambient + vec3<f32>(1.0, 0.92, 0.76) * diffuse * 0.96) + vec3<f32>(1.0, 0.94, 0.82) * specular;

  if (u.surfaceB.w > 0.5 && u.surfaceB.w < 1.5) {
    color = mix(vec3<f32>(0.055, 0.12, 0.14), vec3<f32>(0.89, 0.80, 0.42), elevation);
  }
  if (u.surfaceB.w > 1.5) {
    color = rock * vec3<f32>(0.62, 0.41, 0.25) + meadow * vec3<f32>(0.24, 0.63, 0.31) + scree * vec3<f32>(0.73, 0.55, 0.30) + snow * vec3<f32>(0.82, 0.90, 0.98) + wet * vec3<f32>(0.10, 0.47, 0.54);
  }

  let worldDistance = length(u.camera.xyz - in.world);
  let fog = 1.0 - exp(-worldDistance * 0.008 * u.lighting.z);
  color = mix(color, vec3<f32>(0.29, 0.51, 0.55), fog * 0.79);
  if (u.lighting.w > 0.5 && u.surfaceB.w < 0.5) {
    let gx = abs(fract(in.world.x * 0.10) - 0.5);
    let gz = abs(fract(in.world.z * 0.10) - 0.5);
    let grid = 1.0 - smoothstep(0.46, 0.5, max(gx, gz));
    color = mix(color, vec3<f32>(0.58, 0.72, 0.67), grid * 0.13);
  }
  color = pow(max(color, vec3<f32>(0.0)), vec3<f32>(0.4545));
  return vec4<f32>(color, 1.0);
}`;

function makePerspective(fov, aspect, near, far) {
  const f = 1 / Math.tan(fov / 2);
  const rangeInv = 1 / (near - far);
  return new Float32Array([
    f / aspect, 0, 0, 0,
    0, f, 0, 0,
    0, 0, far * rangeInv, -1,
    0, 0, far * near * rangeInv, 0,
  ]);
}
function normalize3(vector) {
  const length = Math.hypot(vector[0], vector[1], vector[2]) || 1;
  return [vector[0] / length, vector[1] / length, vector[2] / length];
}
function cross3(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
function dot3(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function makeLookAt(eye, target, up = [0, 1, 0]) {
  const z = normalize3([eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]]);
  const x = normalize3(cross3(up, z));
  const y = cross3(z, x);
  return new Float32Array([
    x[0], y[0], z[0], 0,
    x[1], y[1], z[1], 0,
    x[2], y[2], z[2], 0,
    -dot3(x, eye), -dot3(y, eye), -dot3(z, eye), 1,
  ]);
}
function multiplyMat4(a, b) {
  const out = new Float32Array(16);
  for (let col = 0; col < 4; col += 1) {
    for (let row = 0; row < 4; row += 1) {
      out[col * 4 + row] = a[row] * b[col * 4] + a[4 + row] * b[col * 4 + 1] + a[8 + row] * b[col * 4 + 2] + a[12 + row] * b[col * 4 + 3];
    }
  }
  return out;
}
function cameraEye() {
  const { yaw, pitch, distance } = state.camera;
  return [Math.sin(yaw) * Math.cos(pitch) * distance, Math.sin(pitch) * distance + 7, Math.cos(yaw) * Math.cos(pitch) * distance];
}

class WebGpuTerrainRenderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.context = null;
    this.device = null;
    this.pipeline = null;
    this.vertexBuffer = null;
    this.indexBuffer = null;
    this.uniformBuffer = null;
    this.bindGroup = null;
    this.indexCount = 0;
    this.depthTexture = null;
    this.msaaTexture = null;
    this.raf = 0;
    this.lastFrame = performance.now();
    this.resizeObserver = null;
  }
  async init() {
    if (!navigator.gpu) throw new Error('WebGPU is not exposed by this browser.');
    const adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No compatible WebGPU adapter was found.');
    this.device = await adapter.requestDevice();
    this.context = this.canvas.getContext('webgpu');
    if (!this.context) throw new Error('Unable to create a WebGPU canvas context.');
    this.format = navigator.gpu.getPreferredCanvasFormat();
    const module = this.device.createShaderModule({ code: shader });
    if (module.getCompilationInfo) {
      const info = await module.getCompilationInfo();
      const errors = info.messages.filter((message) => message.type === 'error');
      if (errors.length) throw new Error(errors.map((message) => message.message).join('\n'));
    }
    this.pipeline = this.device.createRenderPipeline({
      layout: 'auto',
      vertex: {
        module,
        entryPoint: 'vsMain',
        buffers: [{ arrayStride: 8, attributes: [{ shaderLocation: 0, offset: 0, format: 'float32x2' }] }],
      },
      fragment: { module, entryPoint: 'fsMain', targets: [{ format: this.format }] },
      primitive: { topology: 'triangle-list', cullMode: 'back' },
      depthStencil: { format: 'depth24plus', depthWriteEnabled: true, depthCompare: 'less' },
      multisample: { count: 4 },
    });
    this.createGeometry(state.detail ? 212 : 160);
    this.uniformBuffer = this.device.createBuffer({ size: 160, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    this.bindGroup = this.device.createBindGroup({ layout: this.pipeline.getBindGroupLayout(0), entries: [{ binding: 0, resource: { buffer: this.uniformBuffer } }] });
    this.resizeObserver = new ResizeObserver(() => this.resize());
    this.resizeObserver.observe(this.canvas);
    this.device.lost.then(() => { $('#render-status-text').textContent = 'Renderer paused'; }).catch(() => {});
    this.resize();
  }
  createGeometry(resolution) {
    const vertexCount = (resolution + 1) * (resolution + 1);
    const vertices = new Float32Array(vertexCount * 2);
    let cursor = 0;
    for (let y = 0; y <= resolution; y += 1) {
      for (let x = 0; x <= resolution; x += 1) {
        vertices[cursor++] = x / resolution * 2 - 1;
        vertices[cursor++] = y / resolution * 2 - 1;
      }
    }
    const indices = new Uint32Array(resolution * resolution * 6);
    cursor = 0;
    for (let y = 0; y < resolution; y += 1) {
      for (let x = 0; x < resolution; x += 1) {
        const a = y * (resolution + 1) + x;
        const b = a + 1;
        const c = a + resolution + 1;
        const d = c + 1;
        indices[cursor++] = a; indices[cursor++] = c; indices[cursor++] = b;
        indices[cursor++] = b; indices[cursor++] = c; indices[cursor++] = d;
      }
    }
    this.vertexBuffer = this.device.createBuffer({ size: vertices.byteLength, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST, mappedAtCreation: true });
    new Float32Array(this.vertexBuffer.getMappedRange()).set(vertices); this.vertexBuffer.unmap();
    this.indexBuffer = this.device.createBuffer({ size: indices.byteLength, usage: GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST, mappedAtCreation: true });
    new Uint32Array(this.indexBuffer.getMappedRange()).set(indices); this.indexBuffer.unmap();
    this.indexCount = indices.length;
  }
  resize() {
    if (!this.device) return;
    const ratio = Math.min(window.devicePixelRatio || 1, 1.7);
    const width = Math.max(2, Math.floor(this.canvas.clientWidth * ratio));
    const height = Math.max(2, Math.floor(this.canvas.clientHeight * ratio));
    if (width === this.width && height === this.height) return;
    this.width = width; this.height = height;
    this.canvas.width = width; this.canvas.height = height;
    this.context.configure({ device: this.device, format: this.format, alphaMode: 'opaque' });
    this.depthTexture?.destroy(); this.msaaTexture?.destroy();
    this.depthTexture = this.device.createTexture({ size: [width, height], sampleCount: 4, format: 'depth24plus', usage: GPUTextureUsage.RENDER_ATTACHMENT });
    this.msaaTexture = this.device.createTexture({ size: [width, height], sampleCount: 4, format: this.format, usage: GPUTextureUsage.RENDER_ATTACHMENT });
    this.requestRender();
  }
  writeUniforms() {
    const settings = renderSettings();
    const eye = cameraEye();
    const target = [0, 7, 0];
    const projection = makePerspective(state.fov * Math.PI / 180, this.width / this.height, .3, 430);
    const view = makeLookAt(eye, target);
    const vp = multiplyMat4(projection, view);
    const values = new Float32Array(40);
    values.set(vp, 0);
    values.set([eye[0], eye[1], eye[2], performance.now() * .001], 16);
    values.set([settings.baseHeight, settings.extent, state.seed * .00001, settings.ridgeStrength], 20);
    values.set([settings.macroFrequency, settings.erosion, settings.sharpness, .18 + (3 - settings.ridgeScale) * .09], 24);
    values.set([settings.snowline, settings.snow, settings.wetness, settings.bedrock], 28);
    values.set([settings.meadow, layerForKind('bedrock')?.enabled ? 1 : 0, settings.scree, settings.viewMode], 32);
    values.set([-.82, .69, state.atmosphere ? 1 : 0, state.grid ? 1 : 0], 36);
    this.device.queue.writeBuffer(this.uniformBuffer, 0, values);
  }
  requestRender() {
    if (!this.raf) this.raf = requestAnimationFrame(() => { this.raf = 0; this.render(); });
  }
  render() {
    if (!this.device || !this.depthTexture || !this.msaaTexture) return;
    const started = performance.now();
    try {
      this.writeUniforms();
      const encoder = this.device.createCommandEncoder();
      const pass = encoder.beginRenderPass({
        colorAttachments: [{ view: this.msaaTexture.createView(), resolveTarget: this.context.getCurrentTexture().createView(), clearValue: { r: .018, g: .05, b: .07, a: 1 }, loadOp: 'clear', storeOp: 'discard' }],
        depthStencilAttachment: { view: this.depthTexture.createView(), depthClearValue: 1, depthLoadOp: 'clear', depthStoreOp: 'discard' },
      });
      pass.setPipeline(this.pipeline);
      pass.setBindGroup(0, this.bindGroup);
      pass.setVertexBuffer(0, this.vertexBuffer);
      pass.setIndexBuffer(this.indexBuffer, 'uint32');
      pass.drawIndexed(this.indexCount);
      pass.end();
      this.device.queue.submit([encoder.finish()]);
      const duration = performance.now() - started;
      $('#frame-readout').textContent = `${duration.toFixed(1)} ms`;
      $('#render-status-text').textContent = 'Converged preview';
    } catch (error) {
      console.warn('WebGPU frame failed', error);
      $('#render-status-text').textContent = 'Preview needs refresh';
    }
  }
  destroy() {
    this.resizeObserver?.disconnect();
    cancelAnimationFrame(this.raf);
    this.vertexBuffer?.destroy(); this.indexBuffer?.destroy(); this.uniformBuffer?.destroy(); this.depthTexture?.destroy(); this.msaaTexture?.destroy();
  }
}

function jsHash(x, y) {
  const value = Math.sin(x * 127.1 + y * 311.7) * 43758.5453123;
  return value - Math.floor(value);
}
function jsNoise(x, y) {
  const ix = Math.floor(x), iy = Math.floor(y), fx = x - ix, fy = y - iy;
  const smoothX = fx * fx * (3 - 2 * fx), smoothY = fy * fy * (3 - 2 * fy);
  const a = jsHash(ix, iy), b = jsHash(ix + 1, iy), c = jsHash(ix, iy + 1), d = jsHash(ix + 1, iy + 1);
  return (a * (1 - smoothX) + b * smoothX) * (1 - smoothY) + (c * (1 - smoothX) + d * smoothX) * smoothY;
}
function jsFbm(x, y) {
  let total = 0, amplitude = .5;
  for (let i = 0; i < 5; i += 1) { total += jsNoise(x, y) * amplitude; const nx = x * 1.63 - y * 1.11 + 17.17; y = x * 1.11 + y * 1.63 - 13.73; x = nx; amplitude *= .5; }
  return total / .96875;
}
function jsHeight(x, y) {
  const s = renderSettings();
  const seed = state.seed * .00001;
  const frequency = s.macroFrequency;
  const macro = jsFbm(x * frequency + seed, y * frequency + seed * 1.618);
  let h = (macro - .5) * s.baseHeight * 3.35;
  const massif = clamp((jsFbm(x * frequency * .48 + seed * .59 + 8, y * frequency * .48 + seed * .59 + 8) - .34) / .42, 0, 1);
  let ridge = 0, pX = x * (.18 + (3 - s.ridgeScale) * .09) + seed * 2.31, pY = y * (.18 + (3 - s.ridgeScale) * .09) + seed * 3.74, amp = .5;
  for (let i = 0; i < 4; i += 1) { ridge += (1 - Math.abs(jsNoise(pX, pY) * 2 - 1)) * amp; const nx = pX * 1.71 + pY * 1.02 - 9.41; pY = -pX * 1.02 + pY * 1.71 + 21.33; pX = nx; amp *= .5; }
  ridge /= .9375;
  const sharp = 1.55 + (1 - s.sharpness) * 1.7;
  h += Math.pow(Math.max(0, ridge), sharp) * s.ridgeStrength * (9 + 18 * massif);
  const channel = Math.pow(Math.max(0, 1 - Math.abs(jsFbm(x * .39 + seed * 3.7, y * .39 + seed * 6) * 2 - 1)), 8);
  h -= channel * s.erosion * (1.1 + 3.8 * massif);
  return h;
}

class FallbackTerrainRenderer {
  constructor(canvas) { this.canvas = canvas; this.raf = 0; this.resizeObserver = null; }
  init() {
    this.context = this.canvas.getContext('2d', { alpha: false });
    this.buffer = document.createElement('canvas'); this.buffer.width = 520; this.buffer.height = 300;
    this.bufferContext = this.buffer.getContext('2d', { alpha: false });
    this.resizeObserver = new ResizeObserver(() => this.resize()); this.resizeObserver.observe(this.canvas); this.resize();
  }
  resize() {
    const ratio = Math.min(window.devicePixelRatio || 1, 1.5);
    this.canvas.width = Math.max(2, this.canvas.clientWidth * ratio); this.canvas.height = Math.max(2, this.canvas.clientHeight * ratio); this.requestRender();
  }
  requestRender() { if (!this.raf) this.raf = requestAnimationFrame(() => { this.raf = 0; this.render(); }); }
  render() {
    const ctx = this.bufferContext, w = this.buffer.width, h = this.buffer.height;
    const sky = ctx.createLinearGradient(0, 0, 0, h); sky.addColorStop(0, '#405e69'); sky.addColorStop(.43, '#8aa8aa'); sky.addColorStop(.55, '#76978f'); sky.addColorStop(1, '#18231f'); ctx.fillStyle = sky; ctx.fillRect(0, 0, w, h);
    const glow = ctx.createRadialGradient(w * .72, h * .22, 0, w * .72, h * .22, w * .35); glow.addColorStop(0, 'rgba(255,239,191,.34)'); glow.addColorStop(1, 'rgba(255,239,191,0)'); ctx.fillStyle = glow; ctx.fillRect(0, 0, w, h);
    const image = ctx.getImageData(0, 0, w, h), pixels = image.data, horizon = h * (.45 + state.camera.pitch * .15), visible = new Int32Array(w); visible.fill(h);
    const yaw = state.camera.yaw, cos = Math.cos(yaw), sin = Math.sin(yaw), settings = renderSettings();
    for (let z = 115; z > 1; z -= .68) {
      const fade = clamp(z / 115, 0, 1);
      for (let sx = 0; sx < w; sx += 1) {
        const side = (sx - w * .5) / w * z * 1.48;
        const worldX = side * cos + z * sin;
        const worldZ = z * cos - side * sin;
        const height = jsHeight(worldX, worldZ);
        const screenY = Math.floor(horizon - (height - 1) * (88 / z) + z * .22);
        if (screenY >= visible[sx]) continue;
        const slope = Math.abs(jsHeight(worldX + .5, worldZ) - height) + Math.abs(jsHeight(worldX, worldZ + .5) - height);
        const elev = clamp((height + 12) / 44, 0, 1);
        let r = 54 + elev * 34, g = 78 + elev * 38, b = 47 + elev * 19;
        if (slope > 1.2) { r = 100 + elev * 36; g = 91 + elev * 27; b = 71 + elev * 19; }
        if (elev > settings.snowline && slope < 1.6) { r = 204 + elev * 25; g = 220 + elev * 22; b = 211 + elev * 25; }
        const light = .5 + (1 - fade) * .48;
        const fog = fade * .55;
        r = r * (1 - fog) + 104 * fog; g = g * (1 - fog) + 145 * fog; b = b * (1 - fog) + 150 * fog;
        r *= light; g *= light; b *= light;
        const start = Math.max(0, screenY);
        for (let y = start; y < visible[sx]; y += 1) {
          const idx = (y * w + sx) * 4; pixels[idx] = r; pixels[idx + 1] = g; pixels[idx + 2] = b; pixels[idx + 3] = 255;
        }
        visible[sx] = start;
      }
    }
    ctx.putImageData(image, 0, 0);
    this.context.imageSmoothingEnabled = true;
    this.context.drawImage(this.buffer, 0, 0, this.canvas.width, this.canvas.height);
    $('#frame-readout').textContent = 'CPU fallback'; $('#render-status-text').textContent = 'Static preview';
  }
  destroy() { this.resizeObserver?.disconnect(); cancelAnimationFrame(this.raf); }
}

async function setupRenderer() {
  const canvas = $('#terrain-canvas');
  try {
    renderer = new WebGpuTerrainRenderer(canvas);
    await renderer.init();
    $('#gpu-label').textContent = 'WebGPU terrain device';
    $('#gpu-detail').textContent = 'GPU displacement · 4× MSAA';
    $('#gpu-status').className = 'gpu-status live';
    $('#render-badge').classList.remove('fallback');
    $('#render-badge b').textContent = 'WebGPU';
    $('#render-badge small').textContent = 'real-time material preview';
    $('#render-status-text').textContent = 'Converged preview';
    renderer.requestRender();
  } catch (error) {
    console.warn('WebGPU unavailable; using presentation fallback.', error);
    renderer = new FallbackTerrainRenderer(canvas);
    renderer.init();
    $('#gpu-label').textContent = 'Presentation fallback';
    $('#gpu-detail').textContent = 'WebGPU unavailable in this browser';
    $('#gpu-status').className = 'gpu-status fallback';
    $('#render-badge').classList.add('fallback');
    $('#render-badge b').textContent = 'PREVIEW';
    $('#render-badge small').textContent = 'WebGPU activates in a supported browser';
    toast('WebGPU is unavailable here — showing a terrain preview fallback');
  }
}

function bindEvents() {
  $('#undo-button').addEventListener('click', undo);
  $('#redo-button').addEventListener('click', redo);
  $('#generate-button').addEventListener('click', generateTerrain);
  $('#snapshot-button').addEventListener('click', () => {
    try { localStorage.setItem(`frontier-strata-snapshot-${Date.now()}`, JSON.stringify(captureState())); } catch { /* no storage */ }
    toast('Local terrain snapshot saved');
  });
  $('#project-name').addEventListener('click', () => {
    const next = window.prompt('Terrain project name', state.projectName);
    if (next && next.trim()) { state.projectName = next.trim(); $('#project-name').textContent = state.projectName; markDirty(); }
  });
  $$('.stack-tab').forEach((tab) => tab.addEventListener('click', () => switchDomain(tab.dataset.domain)));
  $$('.mode-entry').forEach((entry) => entry.addEventListener('click', () => {
    if (entry.dataset.mode === 'biome') { switchDomain('surface'); toast('Biome masks are authored through the surface stack'); return; }
    switchDomain(entry.dataset.mode);
  }));
  $$('.view-mode').forEach((button) => button.addEventListener('click', () => {
    if (state.viewMode === button.dataset.view) return;
    mutate(() => { state.viewMode = button.dataset.view; }, { history: false });
    updateReadouts();
  }));
  $('#atmosphere-toggle').addEventListener('change', (event) => mutate(() => { state.atmosphere = event.target.checked; }, { history: false }));
  $('#detail-toggle').addEventListener('change', (event) => {
    mutate(() => { state.detail = event.target.checked; }, { history: false });
    toast(state.detail ? 'Adaptive terrain detail enabled' : 'Adaptive terrain detail reduced');
  });
  $('#preset-card').addEventListener('click', (event) => { renderPresetOptions(); openPopover('preset-popover', event.currentTarget); });
  $('#preset-cycle').addEventListener('click', cyclePreset);
  $('#add-layer-button').addEventListener('click', (event) => { renderAddOptions(); openPopover('add-popover', event.currentTarget); });
  $('#stack-menu').addEventListener('click', (event) => { $('#toggle-all-layers').textContent = `${visibleLayers(state.activeDomain).length ? 'Disable' : 'Enable'} active stack`; openPopover('menu-popover', event.currentTarget); });
  $('#shortcuts-button').addEventListener('click', (event) => openPopover('shortcuts-popover', event.currentTarget));
  $('#selected-visibility').addEventListener('click', () => toggleLayerVisibility(state.selectedId));
  $('#reset-layer-button').addEventListener('click', resetLayer);
  $('#duplicate-stack').addEventListener('click', () => { const saved = clone(state.layers[state.activeDomain]); mutate(() => { state.layers[state.activeDomain] = saved.map((layer, index) => ({ ...layer, uid: `${layer.uid}-copy${index + 1}` })); state.selectedId = state.layers[state.activeDomain][0].uid; }, { stack: true, inspector: true }); closePopovers(); toast('Active stack duplicated'); });
  $('#toggle-all-layers').addEventListener('click', () => { const shouldEnable = !visibleLayers(state.activeDomain).length; mutate(() => state.layers[state.activeDomain].forEach((layer) => { layer.enabled = shouldEnable; }), { stack: true, inspector: true }); closePopovers(); });
  $('#reset-stack').addEventListener('click', () => { applyPreset(state.presetId); closePopovers(); });
  $$('[data-close-popover]').forEach((button) => button.addEventListener('click', () => { $(`#${button.dataset.closePopover}`).hidden = true; }));
  $('#frame-button').addEventListener('click', frameTerrain);
  $('#projection-button').addEventListener('click', () => { state.fov = state.fov === 48 ? 60 : 48; updateReadouts(); renderer?.requestRender(); toast(`${state.fov}° perspective selected`); });
  $('#grid-button').addEventListener('click', (event) => { state.grid = !state.grid; event.currentTarget.classList.toggle('active', state.grid); renderer?.requestRender(); });
  $('#quality-button').addEventListener('click', () => { state.qualityIndex = (state.qualityIndex + 1) % 3; updateReadouts(); toast(`${['Ultra', 'Balanced', 'Fast'][state.qualityIndex]} preview quality`); });
  $('#fullscreen-button').addEventListener('click', async () => { try { if (document.fullscreenElement) await document.exitFullscreen(); else await $('#canvas-stage').requestFullscreen(); } catch { toast('Fullscreen is not available in this preview'); } });

  const stage = $('#canvas-stage');
  const canvas = $('#terrain-canvas');
  canvas.addEventListener('pointerdown', (event) => {
    dragState = { id: event.pointerId, x: event.clientX, y: event.clientY, yaw: state.camera.yaw, pitch: state.camera.pitch };
    canvas.setPointerCapture(event.pointerId); stage.classList.add('dragging'); $('#viewport-instruction').classList.add('dismissed');
  });
  canvas.addEventListener('pointermove', (event) => {
    if (!dragState || dragState.id !== event.pointerId) return;
    state.camera.yaw = dragState.yaw - (event.clientX - dragState.x) * .008;
    state.camera.pitch = clamp(dragState.pitch + (event.clientY - dragState.y) * .006, .08, 1.15);
    renderer?.requestRender();
  });
  const releasePointer = (event) => { if (!dragState || dragState.id !== event.pointerId) return; dragState = null; stage.classList.remove('dragging'); };
  canvas.addEventListener('pointerup', releasePointer); canvas.addEventListener('pointercancel', releasePointer);
  canvas.addEventListener('wheel', (event) => { event.preventDefault(); state.camera.distance = clamp(state.camera.distance + event.deltaY * .075, 60, 180); renderer?.requestRender(); $('#viewport-instruction').classList.add('dismissed'); }, { passive: false });

  document.addEventListener('keydown', (event) => {
    if (event.target.matches('input,textarea')) return;
    if (event.key.toLowerCase() === 'g') { event.preventDefault(); generateTerrain(); }
    if (event.key.toLowerCase() === 'f') { event.preventDefault(); frameTerrain(); }
    if (event.key === '2') { event.preventDefault(); switchDomain('surface'); }
    if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 'z') { event.preventDefault(); event.shiftKey ? redo() : undo(); }
    if (event.key === '?') openPopover('shortcuts-popover', $('#shortcuts-button'));
  });
  document.addEventListener('pointerdown', (event) => {
    if (event.target.closest('.popover, #preset-card, #add-layer-button, #stack-menu, #shortcuts-button')) return;
    closePopovers();
  });
}

function syncAll() {
  renderStack(); renderInspector(); updateReadouts(); updateHistoryButtons(); renderer?.requestRender();
}

function initialize() {
  try {
    const saved = JSON.parse(localStorage.getItem('frontier-strata-session') || 'null');
    if (saved?.layers && saved?.selectedId) {
      state.activeDomain = saved.activeDomain || 'terrain'; state.selectedId = saved.selectedId; state.layers = saved.layers; state.seed = saved.seed || state.seed; state.presetId = saved.presetId || state.presetId; state.viewMode = saved.viewMode || state.viewMode; state.atmosphere = saved.atmosphere !== false; state.detail = saved.detail !== false; state.grid = !!saved.grid; state.fov = saved.fov || state.fov;
    }
  } catch { /* A fresh workspace is perfectly valid. */ }
  bindEvents();
  syncAll();
  setupRenderer();
}

initialize();
