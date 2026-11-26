/*============================================================================================================================================
                                                         BOTANICALWORKBENCH.JS
============================================================================================================================================*/
// 🧩 Project-Zero UI Botanical Workbench: 3 core species (+37 Slate), 1 shared WindField, 2-LUT VAT, 128-tri Clusters, UV, Fracture & Wind-SDF

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

import { SpeciesGroups, SpeciesPresets, FindSpeciesPreset } from './Engine/SpeciesPresets.js';
import { GenerateBotanicalSpecimen } from './Engine/WeberPennGenerator.js';
import {
  CreateDefaultWindParameters,
  SampleUnifiedWind,
  UNIFIED_WIND_GLSL_UNIFORMS,
  UNIFIED_WIND_GLSL_FUNCTIONS,
} from './Engine/UnifiedWindField.js';
import {
  BuildClusteredBuffers,
  SplitSpecimenForFracture,
  ConstructBarkTextureCanvas,
  ConstructUvCheckerCanvas,
  ConstructLeafTextureCanvas,
  BakeCanonicalLeafSdf3D,
  BuildCanonicalLeafSdfMeshData,
  RenderCanonicalLeafSdfPreview,
  EvaluateWindDeformedSdfPrimitives,
  RenderGdfCrossSection,
} from './Engine/ClusterFractureSdf.js';

const CORE_SPECIES = ['English Oak', 'Scots Pine', 'Quaking Aspen'];
const PAGE_COUNT = 6;
const EMISSIVE_TINTS = {
  emerald: [0.24, 0.96, 0.56],
  amber:   [1.00, 0.68, 0.22],
  cyan:    [0.24, 0.84, 1.00],
};

const workbench = {
  presetName: 'English Oak',
  seed: 1337,
  scaleMul: 1.0,
  levelsOverride: null,
  leafScaleMul: 1.0,
  flareOverride: null,
  tropismOverride: null,
  lodTier: 0,
  uvScale: 1.0,
  renderMode: 'pbr',
  forestCount: 1,
  leafShuffleMode: 1, // 1 = 2-Channel VAT (Direction LUT + Scaled Rustle VAT) by default
  windEnabled: true,
  showClusterSpheres: false,
  showSdfGizmos: false,
  includeLeafSdf: true,
  leafThicknessNorm: 0.048,
  emissiveStrength: 0.0,
  emissiveTint: 'emerald',
  showWindArrows: false,
  cutHeightNorm: 0.28,
  snapImpulse: 1.0,
  wind: CreateDefaultWindParameters(),
  activePage: 0,
  selectedOutlinerId: 'specimen',
  outlinerFilterMode: 'all', // all | visible | hidden
  outlinerQuery: '',
  visibility: {
    sky: true,
    wind: true,
    bark: true,
    foliage: true,
    clusters: true,
    sdf: false,
    forest: true,
  },
};

const runtime = {
  specimen: null,
  bakedLeafSdf: null,
  clusteredBark: null,
  clusteredFoliage: null,
  barkTex: null,
  leafTex: null,
  uvCheckerTex: null,
  heroGroup: null,
  barkMesh: null,
  leafMesh: null,
  forestGroup: null,
  clusterSphereGroup: null,
  sdfGizmoGroup: null,
  sdfCapsuleMeshes: [],
  sdfLeafInstMesh: null,
  sdfLeafInstMat: null,
  windArrowGroup: null,
  windArrows: [],
  fractureActive: false,
  fractureGroup: null,
  fractureCrownPivot: null,
  fractureCutY: 0,
  fractureTilt: 0,
  fractureVel: 0,
  fractureAxis: new THREE.Vector3(0, 0, 1),
  fractureSplinters: [],
  timeSec: 0,
  lastFrameMs: performance.now(),
  fpsSmooth: 60,
  frameTick: 0,
  streamParticles: Array.from({ length: 72 }, (_, i) => ({
    x: ((i * 53) % 300),
    y: ((i * 37) % 150),
    life: (i % 40) / 40,
  })),
  sharedUniforms: {
    uTime: { value: 0 },
    uWindDirXZ: { value: new THREE.Vector2(Math.cos(35 * Math.PI / 180), Math.sin(35 * Math.PI / 180)) },
    uWindSpeed: { value: 6.5 },
    uWindGust: { value: 0.45 },
    uWindFreq: { value: 0.55 },
    uWindWavelength: { value: 28.0 },
    uWindTurb: { value: 0.35 },
    uVortexEnabled: { value: 0.0 },
    uVortexRadius: { value: 14.0 },
    uVortexStrength: { value: 0.65 },
    uSpecimenHeight: { value: 16.0 },
    uTrunkFlex: { value: 1.0 },
    uLimbFlex: { value: 1.0 },
    uDetailFlex: { value: 1.0 },
    uLeafShuffleMode: { value: 1.0 },
    uRenderMode: { value: 0.0 },
    uSunDir: { value: new THREE.Vector3(0.55, 0.72, 0.42).normalize() },
    uEmissiveColor: { value: new THREE.Vector3(0.24, 0.96, 0.56) },
    uEmissiveStrength: { value: 0.0 },
  },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THREE.JS VIEWPORT
//------------------------------------------------------------------------------------------------------------------------

const canvas = document.getElementById('ViewportCanvas');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.08;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;

const scene = new THREE.Scene();
scene.background = new THREE.Color('#0c1017');
scene.fog = new THREE.FogExp2('#0d121b', 0.0042);

const camera = new THREE.PerspectiveCamera(45, 16 / 9, 0.15, 900);
camera.position.set(16, 11, 20);

const controls = new OrbitControls(camera, canvas);
controls.enableDamping = true;
controls.dampingFactor = 0.07;
controls.maxPolarAngle = Math.PI * 0.495;
controls.target.set(0, 6.5, 0);

const skyMesh = new THREE.Mesh(
  new THREE.SphereGeometry(420, 32, 20),
  new THREE.ShaderMaterial({
    side: THREE.BackSide,
    depthWrite: false,
    uniforms: { uSunDir: runtime.sharedUniforms.uSunDir },
    vertexShader: `
      varying vec3 vDir;
      void main() { vDir = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }
    `,
    fragmentShader: `
      uniform vec3 uSunDir;
      varying vec3 vDir;
      void main() {
        vec3 d = normalize(vDir);
        float h = clamp(d.y, 0.0, 1.0);
        vec3 col = mix(vec3(0.18, 0.24, 0.34), vec3(0.05, 0.09, 0.17), pow(h, 0.55));
        float sunAmt = max(dot(d, normalize(uSunDir)), 0.0);
        col += vec3(1.0, 0.85, 0.62) * pow(sunAmt, 64.0) * 0.85;
        col += vec3(0.55, 0.48, 0.38) * pow(sunAmt, 6.0) * 0.25;
        gl_FragColor = vec4(col, 1.0);
      }
    `,
  })
);
scene.add(skyMesh);

const groundMesh = new THREE.Mesh(
  new THREE.CircleGeometry(360, 64),
  new THREE.MeshStandardMaterial({ color: '#161b18', roughness: 0.94, metalness: 0.0 })
);
groundMesh.rotation.x = -Math.PI * 0.5;
groundMesh.receiveShadow = true;
scene.add(groundMesh);

const gridHelper = new THREE.GridHelper(120, 40, 0x33404f, 0x1b2129);
gridHelper.position.y = 0.01;
scene.add(gridHelper);

const sunLight = new THREE.DirectionalLight('#fff2dc', 2.4);
sunLight.position.set(28, 38, 22);
sunLight.castShadow = true;
sunLight.shadow.mapSize.set(1024, 1024);
sunLight.shadow.camera.near = 2;
sunLight.shadow.camera.far = 120;
const sc = 24;
sunLight.shadow.camera.left = -sc;
sunLight.shadow.camera.right = sc;
sunLight.shadow.camera.top = sc;
sunLight.shadow.camera.bottom = -sc;
scene.add(sunLight);
scene.add(new THREE.HemisphereLight('#7fa4d4', '#2a241d', 0.85));

// Dynamic Radiosity Emissive Bounce Light (demonstrates EmissiveRadiancePad lighting ground & surroundings!)
const canopyGlowLight = new THREE.PointLight('#3df58f', 0.0, 42.0, 1.4);
canopyGlowLight.position.set(0, 9.0, 0);
scene.add(canopyGlowLight);

runtime.uvCheckerTex = new THREE.CanvasTexture(ConstructUvCheckerCanvas());
runtime.uvCheckerTex.wrapS = THREE.RepeatWrapping;
runtime.uvCheckerTex.wrapT = THREE.RepeatWrapping;
runtime.uvCheckerTex.colorSpace = THREE.SRGBColorSpace;

//------------------------------------------------------------------------------------------------------------------------
//                                           CUSTOM GPU WIND SHADER MATERIALS
//------------------------------------------------------------------------------------------------------------------------

function CreateBarkMaterial(barkTex) {
  return new THREE.ShaderMaterial({
    uniforms: {
      ...runtime.sharedUniforms,
      uBarkMap: { value: barkTex },
      uUvCheckerMap: { value: runtime.uvCheckerTex },
    },
    vertexShader: `
      attribute vec4 aWind;
      attribute vec3 aPivot;
      attribute vec3 aClusterColor;
      ${UNIFIED_WIND_GLSL_UNIFORMS}
      varying vec2 vUv;
      varying vec3 vNormalW;
      varying vec3 vClusterColor;
      varying float vHeightNorm;
      ${UNIFIED_WIND_GLSL_FUNCTIONS}
      void main() {
        vUv = uv;
        vClusterColor = aClusterColor;
        vHeightNorm = aWind.x;
        vec3 worldOrigin = vec3(0.0);
        mat4 instMat = mat4(1.0);
        #ifdef USE_INSTANCING
          instMat = instanceMatrix;
          worldOrigin = vec3(instanceMatrix[3][0], instanceMatrix[3][1], instanceMatrix[3][2]);
        #endif
        vec3 displaced = frontierApplyUnifiedWind(position, worldOrigin, aWind, aPivot);
        vec4 worldPos = modelMatrix * instMat * vec4(displaced, 1.0);
        vNormalW = normalize(mat3(modelMatrix) * mat3(instMat) * normal);
        gl_Position = projectionMatrix * viewMatrix * worldPos;
      }
    `,
    fragmentShader: `
      uniform sampler2D uBarkMap;
      uniform sampler2D uUvCheckerMap;
      uniform float uRenderMode;
      uniform vec3 uSunDir;
      uniform vec3 uEmissiveColor;
      uniform float uEmissiveStrength;
      varying vec2 vUv;
      varying vec3 vNormalW;
      varying vec3 vClusterColor;
      varying float vHeightNorm;
      void main() {
        vec3 N = normalize(vNormalW);
        vec3 L = normalize(uSunDir);
        float ndl = max(dot(N, L), 0.0);
        vec3 lit = vec3(0.24, 0.28, 0.34) * (0.5 + 0.5 * N.y) + vec3(1.12, 1.04, 0.92) * ndl;
        // Indirect radiosity bounce onto bark from emissive canopy leaves above
        float canopyBounce = (0.35 + 0.65 * max(N.y, 0.0)) * (0.25 + 0.75 * vHeightNorm);
        lit += uEmissiveColor * uEmissiveStrength * 0.42 * canopyBounce;
        vec3 baseCol = uRenderMode > 0.5 && uRenderMode < 1.5
          ? vClusterColor
          : (uRenderMode >= 1.5 ? texture2D(uUvCheckerMap, vUv).rgb : texture2D(uBarkMap, vUv).rgb * (0.72 + 0.28 * vHeightNorm));
        gl_FragColor = vec4(baseCol * lit, 1.0);
      }
    `,
  });
}

function CreateFoliageMaterial(leafTex) {
  return new THREE.ShaderMaterial({
    side: THREE.DoubleSide,
    uniforms: {
      ...runtime.sharedUniforms,
      uLeafMap: { value: leafTex },
      uUvCheckerMap: { value: runtime.uvCheckerTex },
    },
    vertexShader: `
      attribute vec4 aWind;
      attribute vec3 aPivot;
      attribute vec3 aClusterColor;
      attribute float aTintVar;
      ${UNIFIED_WIND_GLSL_UNIFORMS}
      varying vec2 vUv;
      varying vec3 vNormalW;
      varying vec3 vClusterColor;
      varying float vTintVar;
      varying float vHeightNorm;
      ${UNIFIED_WIND_GLSL_FUNCTIONS}
      void main() {
        vUv = uv;
        vClusterColor = aClusterColor;
        vTintVar = aTintVar;
        vHeightNorm = aWind.x;
        vec3 worldOrigin = vec3(0.0);
        mat4 instMat = mat4(1.0);
        #ifdef USE_INSTANCING
          instMat = instanceMatrix;
          worldOrigin = vec3(instanceMatrix[3][0], instanceMatrix[3][1], instanceMatrix[3][2]);
        #endif
        vec3 displaced = frontierApplyUnifiedWind(position, worldOrigin, aWind, aPivot);
        vec4 worldPos = modelMatrix * instMat * vec4(displaced, 1.0);
        vNormalW = normalize(mat3(modelMatrix) * mat3(instMat) * normal);
        gl_Position = projectionMatrix * viewMatrix * worldPos;
      }
    `,
    fragmentShader: `
      uniform sampler2D uLeafMap;
      uniform sampler2D uUvCheckerMap;
      uniform float uRenderMode;
      uniform vec3 uSunDir;
      uniform vec3 uEmissiveColor;
      uniform float uEmissiveStrength;
      varying vec2 vUv;
      varying vec3 vNormalW;
      varying vec3 vClusterColor;
      varying float vTintVar;
      varying float vHeightNorm;
      void main() {
        vec4 tex = texture2D(uLeafMap, vUv);
        if (tex.a < 0.32 && uRenderMode < 1.5) discard;
        vec3 N = normalize(gl_FrontFacing ? vNormalW : -vNormalW);
        vec3 L = normalize(uSunDir);
        float ndl = max(dot(N, L), 0.0);
        float sss = pow(max(dot(-N, L), 0.0), 2.0) * 0.42;
        vec3 lit = vec3(0.24, 0.30, 0.36) * (0.5 + 0.5 * N.y) + vec3(1.15, 1.06, 0.90) * (ndl + sss);
        vec3 baseCol = uRenderMode > 0.5 && uRenderMode < 1.5
          ? vClusterColor
          : (uRenderMode >= 1.5 ? texture2D(uUvCheckerMap, vUv).rgb : tex.rgb * (0.86 + 0.28 * vTintVar) * (0.68 + 0.38 * vHeightNorm));
        vec3 emissiveTerm = uRenderMode < 0.5 ? (uEmissiveColor * uEmissiveStrength * (0.45 + 0.55 * vTintVar)) : vec3(0.0);
        gl_FragColor = vec4(baseCol * lit + emissiveTerm, 1.0);
      }
    `,
  });
}

function BuildThreeGeometry(clustered, includeTint = false) {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(clustered.positions, 3));
  geo.setAttribute('normal', new THREE.BufferAttribute(clustered.normals, 3));
  geo.setAttribute('uv', new THREE.BufferAttribute(clustered.uvs, 2));
  geo.setAttribute('aWind', new THREE.BufferAttribute(clustered.wind, 4));
  geo.setAttribute('aPivot', new THREE.BufferAttribute(clustered.pivots, 3));
  geo.setAttribute('aClusterColor', new THREE.BufferAttribute(clustered.clusterColors, 3));
  if (includeTint && clustered.tint) geo.setAttribute('aTintVar', new THREE.BufferAttribute(clustered.tint, 1));
  geo.computeBoundingSphere();
  return geo;
}

function DisposeObject3D(obj) {
  if (!obj) return;
  obj.traverse((c) => {
    if (c.geometry) c.geometry.dispose();
    if (c.material) {
      if (Array.isArray(c.material)) c.material.forEach((m) => m.dispose());
      else c.material.dispose();
    }
  });
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SPECIMEN, FOREST & FRACTURE BUILDERS
//------------------------------------------------------------------------------------------------------------------------

function BuildActivePresetDescriptor() {
  const clone = JSON.parse(JSON.stringify(FindSpeciesPreset(workbench.presetName)));
  clone.botany.scale *= workbench.scaleMul;
  clone.botany.leafScale *= workbench.leafScaleMul;
  if (workbench.levelsOverride !== null) clone.botany.levels = workbench.levelsOverride;
  if (workbench.flareOverride !== null) clone.botany.flare = workbench.flareOverride;
  if (workbench.tropismOverride !== null) clone.botany.attractionUp = workbench.tropismOverride;
  return clone;
}

function RebuildWindVectorGrid() {
  if (runtime.windArrowGroup) {
    scene.remove(runtime.windArrowGroup);
    DisposeObject3D(runtime.windArrowGroup);
  }
  runtime.windArrowGroup = new THREE.Group();
  runtime.windArrows = [];
  const spacing = 5.5;
  const hY = Math.max(4.5, (runtime.specimen?.height || 14) * 0.65);

  const shaftGeo = new THREE.CylinderGeometry(0.05, 0.05, 1.0, 6);
  shaftGeo.translate(0, 0.5, 0);
  shaftGeo.rotateX(Math.PI * 0.5);
  const headGeo = new THREE.ConeGeometry(0.16, 0.42, 8);
  headGeo.translate(0, 1.15, 0);
  headGeo.rotateX(Math.PI * 0.5);

  for (let gx = -3; gx <= 3; gx++) {
    for (let gz = -3; gz <= 3; gz++) {
      const grp = new THREE.Group();
      const mat = new THREE.MeshBasicMaterial({ color: '#58a56e', transparent: true, opacity: 0.78 });
      grp.add(new THREE.Mesh(shaftGeo, mat));
      grp.add(new THREE.Mesh(headGeo, mat));
      grp.position.set(gx * spacing, hY, gz * spacing);
      runtime.windArrowGroup.add(grp);
      runtime.windArrows.push({ group: grp, mat, wx: gx * spacing, wy: hY, wz: gz * spacing });
    }
  }
  runtime.windArrowGroup.visible = workbench.showWindArrows || workbench.renderMode === 'wind';
  scene.add(runtime.windArrowGroup);
}

function RebakeCanonicalLeafSdfAndPreview() {
  if (!runtime.specimen) return;
  runtime.bakedLeafSdf = BakeCanonicalLeafSdf3D(runtime.specimen.leafShape, workbench.leafThicknessNorm);
  const tint = EMISSIVE_TINTS[workbench.emissiveTint] || EMISSIVE_TINTS.emerald;
  const protoCanvas = document.getElementById('LeafSdfProtoCanvas');
  if (protoCanvas) {
    RenderCanonicalLeafSdfPreview(protoCanvas, runtime.bakedLeafSdf, tint, workbench.emissiveStrength);
  }
  const memEl = document.getElementById('MetricLeafSdfMem');
  if (memEl) memEl.innerHTML = `${Math.round(runtime.bakedLeafSdf.bytesR16F / 1024)}<small>KB</small>`;
  const instEl = document.getElementById('MetricLeafSdfInst');
  if (instEl) instEl.textContent = (runtime.specimen.leafCount || 0).toLocaleString();
}

function RebuildSdfGizmos() {
  if (runtime.sdfGizmoGroup) {
    scene.remove(runtime.sdfGizmoGroup);
    DisposeObject3D(runtime.sdfGizmoGroup);
  }
  RebakeCanonicalLeafSdfAndPreview();

  runtime.sdfGizmoGroup = new THREE.Group();
  runtime.sdfCapsuleMeshes = [];
  runtime.sdfLeafInstMesh = null;

  // 1) Proper Tapered Capsules for Trunk + Primary/Secondary Branches
  const capGeo = new THREE.CylinderGeometry(1, 1, 1, 10, 1, false);
  const capMat = new THREE.MeshBasicMaterial({ color: '#38bdf8', wireframe: true, transparent: true, opacity: 0.78 });
  for (let i = 0; i < runtime.specimen.sdfCapsules.length; i++) {
    const m = new THREE.Mesh(capGeo, capMat);
    runtime.sdfGizmoGroup.add(m);
    runtime.sdfCapsuleMeshes.push(m);
  }

  // 2) ONE Single Thick Canonical Leaf SDF Geometry Instanced Across Canopy (ZERO Blobby Spheres!)
  const maxInst = Math.min(640, (runtime.specimen.leafSdfInstances || []).length);
  if (maxInst > 0) {
    const meshData = BuildCanonicalLeafSdfMeshData(runtime.specimen.leafShape, workbench.leafThicknessNorm);
    const leafProtoGeo = new THREE.BufferGeometry();
    leafProtoGeo.setAttribute('position', new THREE.BufferAttribute(meshData.positions, 3));
    leafProtoGeo.setAttribute('normal', new THREE.BufferAttribute(meshData.normals, 3));
    const tint = EMISSIVE_TINTS[workbench.emissiveTint] || EMISSIVE_TINTS.emerald;
    runtime.sdfLeafInstMat = new THREE.MeshStandardMaterial({
      color: '#4ade80',
      emissive: new THREE.Color(tint[0], tint[1], tint[2]),
      emissiveIntensity: workbench.emissiveStrength * 0.55,
      roughness: 0.45,
      metalness: 0.05,
      transparent: true,
      opacity: 0.84,
    });
    runtime.sdfLeafInstMesh = new THREE.InstancedMesh(leafProtoGeo, runtime.sdfLeafInstMat, maxInst);
    runtime.sdfLeafInstMesh.visible = workbench.includeLeafSdf;
    runtime.sdfGizmoGroup.add(runtime.sdfLeafInstMesh);
  }

  runtime.sdfGizmoGroup.visible = workbench.showSdfGizmos || workbench.renderMode === 'sdf';
  scene.add(runtime.sdfGizmoGroup);
}

function RebuildClusterSpheres() {
  if (runtime.clusterSphereGroup) {
    scene.remove(runtime.clusterSphereGroup);
    DisposeObject3D(runtime.clusterSphereGroup);
  }
  runtime.clusterSphereGroup = new THREE.Group();
  const geo = new THREE.IcosahedronGeometry(1, 1);
  const spheres = [
    ...runtime.clusteredBark.clusterSpheres,
    ...runtime.clusteredFoliage.clusterSpheres.slice(0, 64),
  ];
  for (const sp of spheres) {
    const mat = new THREE.MeshBasicMaterial({ color: '#a78bfa', wireframe: true, transparent: true, opacity: 0.24 });
    const m = new THREE.Mesh(geo, mat);
    m.position.set(sp.center[0], sp.center[1], sp.center[2]);
    m.scale.setScalar(sp.radius);
    runtime.clusterSphereGroup.add(m);
  }
  runtime.clusterSphereGroup.visible = workbench.showClusterSpheres;
  scene.add(runtime.clusterSphereGroup);
}

function RebuildForestInstances() {
  if (runtime.forestGroup) {
    scene.remove(runtime.forestGroup);
    DisposeObject3D(runtime.forestGroup);
    runtime.forestGroup = null;
  }
  if (workbench.forestCount <= 1 || !workbench.visibility.forest) {
    if (runtime.heroGroup) runtime.heroGroup.visible = !runtime.fractureActive;
    SyncAllUI();
    return;
  }

  const desc = BuildActivePresetDescriptor();
  const proto = GenerateBotanicalSpecimen(desc, {
    seed: workbench.seed + 77,
    lodTier: workbench.forestCount > 500 ? 3 : 2,
    uvScale: workbench.uvScale,
  });
  const cBark = BuildClusteredBuffers(proto.bark, 0, false);
  const cLeaf = BuildClusteredBuffers(proto.foliage, cBark.clusterCount, true);
  const count = workbench.forestCount - 1;
  runtime.forestGroup = new THREE.Group();

  const instBark = new THREE.InstancedMesh(BuildThreeGeometry(cBark, false), CreateBarkMaterial(runtime.barkTex), count);
  const instLeaf = cLeaf.triangleCount > 0
    ? new THREE.InstancedMesh(BuildThreeGeometry(cLeaf, true), CreateFoliageMaterial(runtime.leafTex), count)
    : null;

  const dummy = new THREE.Object3D();
  const spacing = workbench.forestCount > 500 ? 4.4 : 9.5;
  const gridSide = Math.ceil(Math.sqrt(count));
  const half = (gridSide * spacing) * 0.5;
  let placed = 0;

  for (let gx = 0; gx < gridSide && placed < count; gx++) {
    for (let gz = 0; gz < gridSide && placed < count; gz++) {
      const h = Math.sin(gx * 12.9898 + gz * 78.233) * 43758.5453;
      const fA = h - Math.floor(h);
      const fB = (h * 1.37) - Math.floor(h * 1.37);
      const wx = gx * spacing - half + (fA - 0.5) * spacing * 0.72;
      const wz = gz * spacing - half + (fB - 0.5) * spacing * 0.72;
      if (Math.hypot(wx, wz) < Math.max(7.5, runtime.specimen.height * 0.65)) continue;
      dummy.position.set(wx, 0, wz);
      dummy.rotation.set(0, fB * Math.PI * 2.0, 0);
      dummy.scale.setScalar(0.72 + fA * 0.58);
      dummy.updateMatrix();
      instBark.setMatrixAt(placed, dummy.matrix);
      if (instLeaf) instLeaf.setMatrixAt(placed, dummy.matrix);
      placed++;
    }
  }
  instBark.count = placed;
  instBark.instanceMatrix.needsUpdate = true;
  runtime.forestGroup.add(instBark);
  if (instLeaf) {
    instLeaf.count = placed;
    instLeaf.instanceMatrix.needsUpdate = true;
    runtime.forestGroup.add(instLeaf);
  }
  scene.add(runtime.forestGroup);
  SyncAllUI();
}

function ResetFracture() {
  runtime.fractureActive = false;
  if (runtime.fractureGroup) {
    scene.remove(runtime.fractureGroup);
    DisposeObject3D(runtime.fractureGroup);
    runtime.fractureGroup = null;
    runtime.fractureCrownPivot = null;
    runtime.fractureSplinters = [];
  }
  if (runtime.heroGroup) runtime.heroGroup.visible = true;
  document.getElementById('MetricStumpTris').textContent = '-';
  document.getElementById('MetricCrownTilt').textContent = '0.0°';
  SyncQuickTiles();
  RenderOutliner();
}

function TriggerFracture() {
  ResetFracture();
  if (!runtime.clusteredBark || !runtime.clusteredFoliage) return;
  const split = SplitSpecimenForFracture(
    runtime.clusteredBark,
    runtime.clusteredFoliage,
    runtime.specimen.height,
    workbench.cutHeightNorm
  );
  runtime.fractureActive = true;
  runtime.fractureCutY = split.cutY;
  runtime.fractureTilt = 0;
  runtime.fractureVel = 0.35 * workbench.snapImpulse;

  const rad = (workbench.wind.directionDeg * Math.PI) / 180.0;
  runtime.fractureAxis.set(-Math.sin(rad), 0, Math.cos(rad)).normalize();

  runtime.fractureGroup = new THREE.Group();
  const barkMat = CreateBarkMaterial(runtime.barkTex);
  const leafMat = CreateFoliageMaterial(runtime.leafTex);

  if (split.bark.lower.triangleCount > 0) runtime.fractureGroup.add(new THREE.Mesh(BuildThreeGeometry(split.bark.lower, false), barkMat));
  if (split.foliage.lower.triangleCount > 0) runtime.fractureGroup.add(new THREE.Mesh(BuildThreeGeometry(split.foliage.lower, true), leafMat));

  const stumpCap = new THREE.Mesh(
    new THREE.CylinderGeometry(runtime.specimen.trunkRadius * 0.72, runtime.specimen.trunkRadius * 0.85, 0.16, 10),
    new THREE.MeshStandardMaterial({ color: '#c9a675', roughness: 0.85 })
  );
  stumpCap.position.y = split.cutY - 0.04;
  runtime.fractureGroup.add(stumpCap);

  runtime.fractureCrownPivot = new THREE.Group();
  runtime.fractureCrownPivot.position.set(0, split.cutY, 0);
  const inner = new THREE.Group();
  inner.position.set(0, -split.cutY, 0);
  if (split.bark.upper.triangleCount > 0) inner.add(new THREE.Mesh(BuildThreeGeometry(split.bark.upper, false), barkMat));
  if (split.foliage.upper.triangleCount > 0) inner.add(new THREE.Mesh(BuildThreeGeometry(split.foliage.upper, true), leafMat));
  runtime.fractureCrownPivot.add(inner);
  runtime.fractureGroup.add(runtime.fractureCrownPivot);

  runtime.fractureSplinters = [];
  const shardGeo = new THREE.BoxGeometry(0.08, 0.36, 0.05);
  const shardMat = new THREE.MeshStandardMaterial({ color: '#be9a68', roughness: 0.88 });
  for (let i = 0; i < 24; i++) {
    const ang = (i / 24) * Math.PI * 2.0 + Math.random() * 0.3;
    const m = new THREE.Mesh(shardGeo, shardMat);
    m.position.set(Math.cos(ang) * 0.25, split.cutY + (Math.random() - 0.5) * 0.2, Math.sin(ang) * 0.25);
    runtime.fractureGroup.add(m);
    const spd = (2.2 + Math.random() * 3.8) * workbench.snapImpulse;
    runtime.fractureSplinters.push({
      mesh: m,
      vel: new THREE.Vector3(Math.cos(ang) * spd + Math.cos(rad) * 2.5, 1.8 + Math.random() * 3.2, Math.sin(ang) * spd + Math.sin(rad) * 2.5),
      rotVel: new THREE.Vector3((Math.random() - 0.5) * 9, (Math.random() - 0.5) * 9, (Math.random() - 0.5) * 9),
    });
  }

  if (runtime.heroGroup) runtime.heroGroup.visible = false;
  scene.add(runtime.fractureGroup);
  document.getElementById('MetricStumpTris').textContent = split.bark.lower.triangleCount.toLocaleString();
  SyncQuickTiles();
  RenderOutliner();
}

function RebuildHeroSpecimen() {
  ResetFracture();
  const desc = BuildActivePresetDescriptor();
  const sp = GenerateBotanicalSpecimen(desc, {
    seed: workbench.seed,
    lodTier: workbench.lodTier,
    uvScale: workbench.uvScale,
  });
  runtime.specimen = sp;
  runtime.sharedUniforms.uSpecimenHeight.value = sp.height;

  const cBark = BuildClusteredBuffers(sp.bark, 0, false);
  const cLeaf = BuildClusteredBuffers(sp.foliage, cBark.clusterCount, true);
  runtime.clusteredBark = cBark;
  runtime.clusteredFoliage = cLeaf;

  const barkCanvas = ConstructBarkTextureCanvas(desc.visual);
  const leafCanvas = ConstructLeafTextureCanvas(desc.visual);
  if (runtime.barkTex) runtime.barkTex.dispose();
  if (runtime.leafTex) runtime.leafTex.dispose();
  runtime.barkTex = new THREE.CanvasTexture(barkCanvas);
  runtime.barkTex.wrapS = THREE.RepeatWrapping;
  runtime.barkTex.wrapT = THREE.RepeatWrapping;
  runtime.barkTex.colorSpace = THREE.SRGBColorSpace;
  runtime.leafTex = new THREE.CanvasTexture(leafCanvas);
  runtime.leafTex.colorSpace = THREE.SRGBColorSpace;

  const swBark = document.getElementById('SwatchBark').getContext('2d');
  swBark.clearRect(0, 0, 128, 128);
  swBark.drawImage(barkCanvas, 0, 0, 128, 128);
  const swLeaf = document.getElementById('SwatchLeaf').getContext('2d');
  swLeaf.clearRect(0, 0, 128, 128);
  swLeaf.drawImage(leafCanvas, 0, 0, 128, 128);

  if (runtime.heroGroup) {
    scene.remove(runtime.heroGroup);
    DisposeObject3D(runtime.heroGroup);
  }
  runtime.heroGroup = new THREE.Group();

  runtime.barkMesh = new THREE.Mesh(BuildThreeGeometry(cBark, false), CreateBarkMaterial(runtime.barkTex));
  runtime.barkMesh.castShadow = true;
  runtime.barkMesh.receiveShadow = true;
  runtime.barkMesh.visible = workbench.visibility.bark;
  runtime.heroGroup.add(runtime.barkMesh);

  if (cLeaf.triangleCount > 0) {
    runtime.leafMesh = new THREE.Mesh(BuildThreeGeometry(cLeaf, true), CreateFoliageMaterial(runtime.leafTex));
    runtime.leafMesh.castShadow = true;
    runtime.leafMesh.visible = workbench.visibility.foliage;
    runtime.heroGroup.add(runtime.leafMesh);
  } else {
    runtime.leafMesh = null;
  }

  scene.add(runtime.heroGroup);
  RebuildClusterSpheres();
  RebuildSdfGizmos();
  RebuildWindVectorGrid();
  if (workbench.forestCount > 1) RebuildForestInstances();

  controls.target.set(0, sp.height * 0.44, 0);
  SyncAllUI();
}

//------------------------------------------------------------------------------------------------------------------------
//                                      EXACT PROJECT ZERO OUTLINER & INSPECTOR SYNC
//------------------------------------------------------------------------------------------------------------------------

function AzimuthCompass(deg) {
  const dirs = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
  return dirs[Math.round(((deg % 360) / 22.5)) % 16];
}

const ICONS = {
  folder: `<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#8a8a92" stroke-width="1.6"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></svg>`,
  sun:    `<svg width="15" height="15" viewBox="0 0 24 24" fill="#ffb454"><circle cx="12" cy="12" r="5"/></svg>`,
  wind:   `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#78b7d4" stroke-width="1.8"><path d="M9.6 4.6A2 2 0 1 1 11 8H2m10.6 11.4A2 2 0 1 0 14 16H2m15.7-8.3A2.5 2.5 0 1 1 19.5 12H2"/></svg>`,
  tree:   `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#5bdb87" stroke-width="1.8"><path d="M12 3l-5 8h10l-5-8z"/><path d="M12 9l-6 9h12l-6-9z"/><line x1="12" y1="18" x2="12" y2="22"/></svg>`,
  bark:   `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#c5996e" stroke-width="1.8"><rect x="7" y="3" width="10" height="18" rx="3"/></svg>`,
  leaf:   `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#4ade80" stroke-width="1.8"><path d="M17 8C8 10 5.9 16.17 3.82 21.34L5.71 22l1-2.3A4.49 4.49 0 0 0 8 20c4 0 8.5-3.5 9-12z"/></svg>`,
  clus:   `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#a78bfa" stroke-width="1.8"><polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/></svg>`,
  frac:   `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#f87171" stroke-width="1.8"><polyline points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>`,
  sdf:    `<svg width="15" height="15" viewBox="0 0 24 24" fill="none" stroke="#38bdf8" stroke-width="1.8"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="3"/></svg>`,
};

function GetOutlinerModel() {
  const sp = runtime.specimen;
  const barkT = runtime.clusteredBark?.triangleCount || 0;
  const leafT = runtime.clusteredFoliage?.triangleCount || 0;
  const clus = (runtime.clusteredBark?.clusterCount || 0) + (runtime.clusteredFoliage?.clusterCount || 0);
  const sdfN = (sp?.sdfCapsules.length || 0) + (sp?.sdfCanopyLobes.length || 0);
  return [
    { group: 'Botanical', count: '5 items', items: [
      { id: 'specimen', depth: 0, icon: 'tree', name: workbench.presetName,      sub: `${(sp?.height || 0).toFixed(1)}m · ${clus} clusters`, page: 0 },
      { id: 'bark',     depth: 1, icon: 'bark', name: 'Trunk · Boughs · Roots',  sub: `${barkT.toLocaleString()} tris · UDIM 1001`, page: 3, visKey: 'bark' },
      { id: 'foliage',  depth: 1, icon: 'leaf', name: 'Canopy Foliage Sheet',    sub: `${(sp?.leafCount || 0).toLocaleString()} fronds · 2-LUT VAT`, page: 1, visKey: 'foliage' },
      { id: 'clusters', depth: 1, icon: 'clus', name: '128-Tri Cluster Pyramid', sub: `${clus} clusters · VisBuffer`, page: 3, visKey: 'clusters' },
      { id: 'fracture', depth: 1, icon: 'frac', name: 'Voronoi Fracture Seam',   sub: runtime.fractureActive ? 'Snapped · Rigid Fall' : `Armed @ ${Math.round(workbench.cutHeightNorm * 100)}% H`, page: 4 },
    ]},
    { group: 'Simulation & World', count: '4 items', items: [
      { id: 'wind',     depth: 0, icon: 'wind', name: 'Global WindField',        sub: `${workbench.wind.speedMps.toFixed(1)} m/s · ${Math.round(workbench.wind.directionDeg)}° ${AzimuthCompass(workbench.wind.directionDeg)}`, page: 2, visKey: 'wind' },
      { id: 'sdf',      depth: 0, icon: 'sdf',  name: '1-Leaf SDF · Capsules',   sub: `${sp?.sdfCapsules.length || 0} caps + 1 leaf (32 KB) × ${(sp?.leafCount || 0).toLocaleString()}`, page: 5, visKey: 'sdf' },
      { id: 'forest',   depth: 0, icon: 'tree', name: 'Instanced Forest Field',  sub: `${workbench.forestCount.toLocaleString()} inst · Tier ${workbench.forestCount > 500 ? 2 : workbench.forestCount > 1 ? 1 : 0}`, page: 0, visKey: 'forest' },
      { id: 'sky',      depth: 0, icon: 'sun',  name: 'Sun & Atmosphere',        sub: 'Directional light · 3-LUT Sky', page: 0, visKey: 'sky' },
    ]},
  ];
}

function RenderOutliner() {
  const container = document.getElementById('OutlinerRows');
  const query = workbench.outlinerQuery.trim().toLowerCase();
  const groups = GetOutlinerModel();

  let visCount = 0;
  let hidCount = 0;
  for (const g of groups) {
    for (const it of g.items) {
      const hidden = it.visKey ? !workbench.visibility[it.visKey] : false;
      if (hidden) hidCount++; else visCount++;
    }
  }
  document.getElementById('StatVisibleCount').textContent = String(visCount);
  document.getElementById('StatHiddenCount').textContent = String(hidCount);
  document.getElementById('OutlinerSubtitle').textContent = `Untitled · ${visCount + hidCount} nodes`;

  container.innerHTML = '';
  for (const g of groups) {
    const visibleItems = g.items.filter((it) => {
      const hidden = it.visKey ? !workbench.visibility[it.visKey] : false;
      if (workbench.outlinerFilterMode === 'visible' && hidden) return false;
      if (workbench.outlinerFilterMode === 'hidden' && !hidden) return false;
      if (query && !it.name.toLowerCase().includes(query) && !it.sub.toLowerCase().includes(query)) return false;
      return true;
    });
    if (visibleItems.length === 0) continue;

    const gh = document.createElement('div');
    gh.className = 'outliner-group-row';
    gh.innerHTML = `<span>▾</span>${ICONS.folder}<span>${g.group}</span><small>${visibleItems.length} items</small>`;
    container.appendChild(gh);

    for (const it of visibleItems) {
      const hidden = it.visKey ? !workbench.visibility[it.visKey] : false;
      const row = document.createElement('div');
      row.className = `outliner-row${workbench.selectedOutlinerId === it.id ? ' selected' : ''}${hidden ? ' hidden-row' : ''}`;
      row.style.setProperty('--depth', it.depth);
      row.innerHTML = `
        <span class="row-icon">${ICONS[it.icon] || ICONS.tree}</span>
        <div class="row-text">
          <span class="row-name">${it.name}</span>
          <span class="row-sub">${it.sub}</span>
        </div>
        <span class="row-check">${hidden ? '–' : '✓'}</span>
        ${it.visKey ? `<button class="row-eye" title="Toggle visibility">${hidden ? '✕' : '◉'}</button>` : ''}
      `;
      row.addEventListener('click', (ev) => {
        if (ev.target.classList.contains('row-eye') && it.visKey) {
          ev.stopPropagation();
          workbench.visibility[it.visKey] = !workbench.visibility[it.visKey];
          ApplyVisibility();
          RenderOutliner();
          return;
        }
        workbench.selectedOutlinerId = it.id;
        SelectPage(it.page);
        RenderOutliner();
      });
      container.appendChild(row);
    }
  }
}

function ApplyVisibility() {
  if (runtime.barkMesh) runtime.barkMesh.visible = workbench.visibility.bark;
  if (runtime.leafMesh) runtime.leafMesh.visible = workbench.visibility.foliage;
  skyMesh.visible = workbench.visibility.sky;
  workbench.showSdfGizmos = workbench.visibility.sdf;
  if (runtime.sdfGizmoGroup) runtime.sdfGizmoGroup.visible = workbench.showSdfGizmos || workbench.renderMode === 'sdf';
  workbench.showWindArrows = !workbench.visibility.wind ? false : workbench.showWindArrows;
  if (runtime.windArrowGroup) runtime.windArrowGroup.visible = workbench.showWindArrows || workbench.renderMode === 'wind';
  if (runtime.forestGroup) runtime.forestGroup.visible = workbench.visibility.forest;
  SyncQuickTiles();
}

function SelectPage(pageIdx) {
  workbench.activePage = Math.max(0, Math.min(PAGE_COUNT - 1, pageIdx));
  document.querySelectorAll('#PageCarouselRail .pc-pill').forEach((p) => {
    const active = parseInt(p.dataset.page, 10) === workbench.activePage;
    p.classList.toggle('open', active);
    if (active) p.scrollIntoView({ behavior: 'smooth', inline: 'center', block: 'nearest' });
  });
  document.querySelectorAll('.pc-page').forEach((pg) => {
    pg.classList.toggle('open', parseInt(pg.dataset.page, 10) === workbench.activePage);
  });
  document.getElementById('PageCountLabel').textContent = `${workbench.activePage + 1} / ${PAGE_COUNT}`;
  document.getElementById('BtnPagePrev').disabled = workbench.activePage === 0;
  document.getElementById('BtnPageNext').disabled = workbench.activePage === PAGE_COUNT - 1;
}

function SyncQuickTiles() {
  const setTile = (id, statusId, isOn, onTxt = 'ON', offTxt = 'OFF') => {
    const el = document.getElementById(id);
    if (!el) return;
    el.classList.toggle('on', isOn);
    el.classList.toggle('off', !isOn);
    document.getElementById(statusId).textContent = isOn ? onTxt : offTxt;
  };
  setTile('TileToggleWind', 'StatusTileWind', workbench.windEnabled);
  setTile('TileToggleClusters', 'StatusTileClusters', workbench.renderMode === 'clusters');
  setTile('TileToggleUv', 'StatusTileUv', workbench.renderMode === 'uv');
  setTile('TileToggleSdf', 'StatusTileSdf', workbench.showSdfGizmos || workbench.renderMode === 'sdf');
  setTile('TileToggleForest', 'StatusTileForest', workbench.forestCount >= 10000);
  document.getElementById('StatusTileFracture').textContent = runtime.fractureActive ? 'SNAPPED' : 'ARMED';

  document.getElementById('BtnVpClusters').classList.toggle('active', workbench.renderMode === 'clusters');
  document.getElementById('BtnVpUv').classList.toggle('active', workbench.renderMode === 'uv');
  document.getElementById('BtnVpSdf').classList.toggle('active', workbench.showSdfGizmos || workbench.renderMode === 'sdf');
  document.getElementById('BtnVpWind').classList.toggle('active', workbench.showWindArrows || workbench.renderMode === 'wind');

  document.getElementById('BtnToggleSdfGizmos').classList.toggle('on', workbench.showSdfGizmos);
  document.getElementById('BtnToggleLeafSdfOnly').classList.toggle('on', workbench.includeLeafSdf);
  document.getElementById('BtnToggleWindArrows').classList.toggle('on', workbench.showWindArrows);
  document.getElementById('BtnToggleVortex').classList.toggle('on', workbench.wind.vortexEnabled);
  document.getElementById('BtnToggleClusterSpheres').classList.toggle('on', workbench.showClusterSpheres);
}

function SyncEmissiveUniforms() {
  const tint = EMISSIVE_TINTS[workbench.emissiveTint] || EMISSIVE_TINTS.emerald;
  runtime.sharedUniforms.uEmissiveColor.value.set(tint[0], tint[1], tint[2]);
  runtime.sharedUniforms.uEmissiveStrength.value = workbench.emissiveStrength;
  canopyGlowLight.color.setRGB(tint[0], tint[1], tint[2]);
  canopyGlowLight.intensity = workbench.emissiveStrength * 32.0;
  canopyGlowLight.position.set(0, (runtime.specimen?.height || 14) * 0.62, 0);
  if (runtime.sdfLeafInstMat) {
    runtime.sdfLeafInstMat.emissive.setRGB(tint[0], tint[1], tint[2]);
    runtime.sdfLeafInstMat.emissiveIntensity = workbench.emissiveStrength * 0.55;
  }
  document.querySelectorAll('#EmissiveTintChoices button').forEach((b) => {
    b.classList.toggle('on', b.dataset.emissive === workbench.emissiveTint);
  });
  RebakeCanonicalLeafSdfAndPreview();
}

function SyncAllUI() {
  const sp = runtime.specimen;
  const grp = SpeciesGroups.find((g) => g.names.includes(workbench.presetName));
  const barkC = runtime.clusteredBark?.clusterCount || 0;
  const leafC = runtime.clusteredFoliage?.clusterCount || 0;
  const totalC = barkC + leafC;
  const totalT = (runtime.clusteredBark?.triangleCount || 0) + (runtime.clusteredFoliage?.triangleCount || 0);
  const capN = sp?.sdfCapsules.length || 0;
  const comp = AzimuthCompass(workbench.wind.directionDeg);

  document.getElementById('HeroTitle').textContent = workbench.presetName;
  document.getElementById('HeroSubtitle').textContent = `Procedural Tree · Weber-Penn · ${grp ? grp.label : 'Botanical'}`;
  document.getElementById('BannerLeft').textContent = `${(sp?.height || 0).toFixed(1)}m · ${totalC} CLUSTERS`;
  document.getElementById('BannerRight').textContent = `${workbench.wind.speedMps.toFixed(1)} m/s · ${Math.round(workbench.wind.directionDeg)}° ${comp}`;

  document.getElementById('TileHeight').innerHTML = `${(sp?.height || 0).toFixed(1)}<small>m</small>`;
  document.getElementById('TileClusters').innerHTML = `${totalC}<small>×128t</small>`;
  document.getElementById('TileWind').innerHTML = `${workbench.wind.speedMps.toFixed(1)}<small>m/s</small>`;
  document.getElementById('TileWindSub').textContent = `WIND · ${Math.round(workbench.wind.directionDeg)}° ${comp}`;
  document.getElementById('TileSdf').innerHTML = `${capN}<small>+1 leaf</small>`;

  document.getElementById('SubSpecSummary').textContent = `${workbench.presetName} · 2-LUT VAT (Direction + Rustle)`;
  document.getElementById('SubWindSummary').textContent = `${workbench.wind.speedMps.toFixed(1)} m/s · ${Math.round(workbench.wind.directionDeg)}° · Wave ${workbench.wind.wavelengthMeters}m`;
  document.getElementById('SubSdfSummary').textContent = `${capN} Capsules + 1 Leaf SDF (32 KB) × ${(sp?.leafCount || 0).toLocaleString()} · ContactPCSS`;

  document.getElementById('MetricBarkClusters').textContent = String(barkC);
  document.getElementById('MetricLeafClusters').textContent = String(leafC);
  document.getElementById('FooterClusters').textContent = String(totalC);
  document.getElementById('FooterWind').textContent = `${workbench.wind.speedMps.toFixed(1)} m/s`;
  document.getElementById('FooterForest').textContent = workbench.forestCount.toLocaleString();
  document.getElementById('VpHeaderSub').textContent = `${workbench.forestCount.toLocaleString()} specimen${workbench.forestCount > 1 ? 's' : ''} · 1 shared WindField`;
  document.getElementById('VpFooterTris').textContent = totalT.toLocaleString();
  document.getElementById('VpFooterLeaves').textContent = (sp?.leafCount || 0).toLocaleString();
  document.getElementById('VpFooterGenMs').textContent = `${(sp?.generationMs || 0).toFixed(1)} ms`;
  document.getElementById('VpFooterDraws').textContent = String(workbench.forestCount > 1 ? 6 : 4);

  document.querySelectorAll('#CoreSpeciesChoices button').forEach((b) => {
    b.classList.toggle('on', b.dataset.species === workbench.presetName);
  });
  document.querySelectorAll('#ForestScaleGroup button').forEach((b) => {
    b.classList.toggle('active', parseInt(b.dataset.forest, 10) === workbench.forestCount);
  });

  SyncQuickTiles();
  RenderOutliner();
}

function SetRenderMode(mode) {
  workbench.renderMode = mode;
  document.getElementById('SelectRenderMode').value = mode;
  runtime.sharedUniforms.uRenderMode.value = mode === 'clusters' ? 1.0 : mode === 'uv' ? 2.0 : 0.0;
  if (runtime.sdfGizmoGroup) runtime.sdfGizmoGroup.visible = workbench.showSdfGizmos || mode === 'sdf';
  if (runtime.windArrowGroup) runtime.windArrowGroup.visible = workbench.showWindArrows || mode === 'wind';
  const cap = {
    pbr: 'OPENPBR DIRECT · 1 SHARED WINDFIELD · ANALYTICAL SDF SYNCED',
    clusters: '128-TRI CLUSTER PYRAMID · VISBUFFER HW/SW PARTITION',
    uv: 'CYLINDRICAL UDIM 1001 BARK UV + UDIM 1002 FOLIAGE SHEET',
    sdf: 'WIND-DISPLACED ANALYTICAL SDF CAPSULES + CANOPY SPHERE LOBES',
    wind: 'UNIFIED GLOBAL WINDFIELD · TRAVELING GUST WAVEFRONT',
    fracture: 'VORONOI TRUNK FRACTURE · RIGID FALL + WIND TORQUE',
  };
  document.getElementById('VpCornerCaption').textContent = cap[mode] || cap.pbr;
  SyncQuickTiles();
}

function SetForestCount(count) {
  workbench.forestCount = count;
  RebuildForestInstances();
  if (count > 500) camera.position.set(55, 34, 68);
  else if (count > 1) camera.position.set(28, 18, 34);
  else camera.position.set(16, 11, 20);
}

function SelectSpecies(name) {
  workbench.presetName = name;
  document.getElementById('SelectSpecies').value = name;
  const p = FindSpeciesPreset(name);
  workbench.levelsOverride = null;
  workbench.flareOverride = null;
  workbench.tropismOverride = null;
  const syncSlider = (id, outId, val, fmt) => {
    const el = document.getElementById(id);
    el.value = String(val);
    document.getElementById(outId).textContent = fmt(val);
    UpdateRangeFill(el);
  };
  syncSlider('RangeLevels', 'OutLevels', p.botany.levels, (v) => String(v));
  syncSlider('RangeFlare', 'OutFlare', p.botany.flare, (v) => Number(v).toFixed(2));
  syncSlider('RangeTropism', 'OutTropism', p.botany.attractionUp, (v) => Number(v).toFixed(2));
  RebuildHeroSpecimen();
}

function UpdateRangeFill(input) {
  const min = parseFloat(input.min);
  const max = parseFloat(input.max);
  const val = parseFloat(input.value);
  const pct = max > min ? ((val - min) / (max - min)) * 100 : 0;
  input.style.setProperty('--fill', `${pct.toFixed(1)}%`);
}

function SyncWindUniforms() {
  const rad = (workbench.wind.directionDeg * Math.PI) / 180.0;
  runtime.sharedUniforms.uWindDirXZ.value.set(Math.cos(rad), Math.sin(rad));
  runtime.sharedUniforms.uWindSpeed.value = workbench.windEnabled ? workbench.wind.speedMps : 0.0;
  runtime.sharedUniforms.uWindGust.value = workbench.wind.gustStrength;
  runtime.sharedUniforms.uWindWavelength.value = workbench.wind.wavelengthMeters;
  runtime.sharedUniforms.uVortexEnabled.value = workbench.wind.vortexEnabled ? 1.0 : 0.0;
  runtime.sharedUniforms.uTrunkFlex.value = workbench.wind.trunkFlex;
  runtime.sharedUniforms.uLimbFlex.value = workbench.wind.limbFlex;
  runtime.sharedUniforms.uDetailFlex.value = workbench.wind.detailFlex;
  SyncAllUI();
}

// Project Zero WindPanel-style live streamline canvas
const _streamVec = new Float32Array(3);
function DrawWindStreamlines() {
  const c = document.getElementById('WindStreamCanvas');
  if (!c) return;
  const ctx = c.getContext('2d');
  ctx.fillStyle = 'rgba(13, 17, 18, 0.24)';
  ctx.fillRect(0, 0, c.width, c.height);

  for (const pt of runtime.streamParticles) {
    const wx = ((pt.x / c.width) - 0.5) * 44;
    const wz = ((pt.y / c.height) - 0.5) * 28;
    SampleUnifiedWind(wx, 8.0, wz, runtime.timeSec, workbench.wind, _streamVec);
    const spd = Math.hypot(_streamVec[0], _streamVec[2]);
    const nx = pt.x + _streamVec[0] * 0.28;
    const ny = pt.y + _streamVec[2] * 0.28;
    const t = Math.min(1.0, spd / 18.0);
    // Project Zero WindPanel legend colors (#306e7c -> #58a56e -> #d3b565)
    const r = Math.round(48 + 163 * t);
    const g = Math.round(110 + 71 * Math.sin(t * Math.PI * 0.7));
    const b = Math.round(124 - 23 * t);
    ctx.strokeStyle = `rgb(${r},${g},${b})`;
    ctx.lineWidth = 1.6;
    ctx.beginPath();
    ctx.moveTo(pt.x, pt.y);
    ctx.lineTo(nx, ny);
    ctx.stroke();

    pt.x = nx;
    pt.y = ny;
    pt.life += 0.015;
    if (pt.x < 0 || pt.x > c.width || pt.y < 0 || pt.y > c.height || pt.life > 1.0) {
      pt.x = Math.random() * c.width;
      pt.y = Math.random() * c.height;
      pt.life = 0;
    }
  }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EVENT WIRING
//------------------------------------------------------------------------------------------------------------------------

function InitControls() {
  // Populate all 37 Slate presets grouped
  const sel = document.getElementById('SelectSpecies');
  for (const grp of SpeciesGroups) {
    const og = document.createElement('optgroup');
    og.label = grp.label;
    for (const name of grp.names) {
      const opt = document.createElement('option');
      opt.value = name;
      opt.textContent = name;
      og.appendChild(opt);
    }
    sel.appendChild(og);
  }
  sel.value = workbench.presetName;
  sel.addEventListener('change', () => SelectSpecies(sel.value));

  document.querySelectorAll('input[type="range"]').forEach(UpdateRangeFill);

  // PageCarousel strip
  document.querySelectorAll('#PageCarouselRail .pc-pill').forEach((p) => {
    p.addEventListener('click', () => SelectPage(parseInt(p.dataset.page, 10)));
  });
  document.getElementById('BtnPagePrev').addEventListener('click', () => SelectPage(workbench.activePage - 1));
  document.getElementById('BtnPageNext').addEventListener('click', () => SelectPage(workbench.activePage + 1));
  document.querySelectorAll('[data-jump-page]').forEach((b) => {
    b.addEventListener('click', () => SelectPage(parseInt(b.dataset.jumpPage, 10)));
  });

  // Core 3 species buttons + bottom notch cycle
  document.querySelectorAll('#CoreSpeciesChoices button').forEach((b) => {
    b.addEventListener('click', () => SelectSpecies(b.dataset.species));
  });
  document.getElementById('BtnCycleCoreSpecies').addEventListener('click', () => {
    const idx = CORE_SPECIES.indexOf(workbench.presetName);
    SelectSpecies(CORE_SPECIES[(idx + 1) % CORE_SPECIES.length]);
  });
  document.getElementById('BtnConstructNext').addEventListener('click', () => {
    const idx = SpeciesPresets.findIndex((p) => p.name === workbench.presetName);
    SelectSpecies(SpeciesPresets[(idx + 1) % SpeciesPresets.length].name);
  });

  // Seed buttons
  const bumpSeed = () => {
    workbench.seed = (workbench.seed * 1664525 + 1013904223) >>> 0;
    RebuildHeroSpecimen();
  };
  document.getElementById('BtnNewSeed').addEventListener('click', bumpSeed);
  document.getElementById('BtnOutlinerNewSeed').addEventListener('click', bumpSeed);

  // Overview Quick Tiles
  document.getElementById('TileToggleWind').addEventListener('click', () => {
    workbench.windEnabled = !workbench.windEnabled;
    SyncWindUniforms();
  });
  document.getElementById('TileToggleClusters').addEventListener('click', () => {
    SetRenderMode(workbench.renderMode === 'clusters' ? 'pbr' : 'clusters');
  });
  document.getElementById('TileToggleUv').addEventListener('click', () => {
    SetRenderMode(workbench.renderMode === 'uv' ? 'pbr' : 'uv');
  });
  document.getElementById('TileToggleSdf').addEventListener('click', () => {
    workbench.showSdfGizmos = !workbench.showSdfGizmos;
    workbench.visibility.sdf = workbench.showSdfGizmos;
    if (runtime.sdfGizmoGroup) runtime.sdfGizmoGroup.visible = workbench.showSdfGizmos;
    SyncQuickTiles();
    RenderOutliner();
  });
  document.getElementById('TileToggleForest').addEventListener('click', () => {
    SetForestCount(workbench.forestCount >= 10000 ? 1 : 10240);
  });
  document.getElementById('TileTriggerFracture').addEventListener('click', () => {
    if (runtime.fractureActive) ResetFracture();
    else TriggerFracture();
  });
  document.getElementById('BtnResetAll').addEventListener('click', () => {
    workbench.wind = CreateDefaultWindParameters();
    workbench.windEnabled = true;
    workbench.scaleMul = 1.0;
    workbench.leafScaleMul = 1.0;
    workbench.lodTier = 0;
    SetRenderMode('pbr');
    SetForestCount(1);
    SelectSpecies('English Oak');
    SyncWindUniforms();
  });

  // Viewport top chrome
  document.getElementById('SelectRenderMode').addEventListener('change', (e) => SetRenderMode(e.target.value));
  document.getElementById('BtnVpClusters').addEventListener('click', () => SetRenderMode(workbench.renderMode === 'clusters' ? 'pbr' : 'clusters'));
  document.getElementById('BtnVpUv').addEventListener('click', () => SetRenderMode(workbench.renderMode === 'uv' ? 'pbr' : 'uv'));
  document.getElementById('BtnVpSdf').addEventListener('click', () => {
    workbench.showSdfGizmos = !workbench.showSdfGizmos;
    workbench.visibility.sdf = workbench.showSdfGizmos;
    if (runtime.sdfGizmoGroup) runtime.sdfGizmoGroup.visible = workbench.showSdfGizmos;
    SyncQuickTiles();
    RenderOutliner();
  });
  document.getElementById('BtnVpWind').addEventListener('click', () => {
    workbench.showWindArrows = !workbench.showWindArrows;
    if (runtime.windArrowGroup) runtime.windArrowGroup.visible = workbench.showWindArrows;
    SyncQuickTiles();
  });

  document.querySelectorAll('#ForestScaleGroup button').forEach((b) => {
    b.addEventListener('click', () => SetForestCount(parseInt(b.dataset.forest, 10)));
  });

  // Leaf solver 3-choice buttons
  document.querySelectorAll('#LeafModeGroup button').forEach((b) => {
    b.addEventListener('click', () => {
      document.querySelectorAll('#LeafModeGroup button').forEach((x) => x.classList.remove('on'));
      b.classList.add('on');
      workbench.leafShuffleMode = parseInt(b.dataset.leafmode, 10);
      runtime.sharedUniforms.uLeafShuffleMode.value = workbench.leafShuffleMode;
    });
  });

  // Outliner search & stat filter cards
  document.getElementById('OutlinerSearchInput').addEventListener('input', (e) => {
    workbench.outlinerQuery = e.target.value;
    RenderOutliner();
  });
  document.getElementById('BtnFilterVisible').addEventListener('click', () => {
    workbench.outlinerFilterMode = workbench.outlinerFilterMode === 'visible' ? 'all' : 'visible';
    RenderOutliner();
  });
  document.getElementById('BtnFilterHidden').addEventListener('click', () => {
    workbench.outlinerFilterMode = workbench.outlinerFilterMode === 'hidden' ? 'all' : 'hidden';
    RenderOutliner();
  });
  document.getElementById('BtnOutlinerFilter').addEventListener('click', () => {
    const next = workbench.outlinerFilterMode === 'all' ? 'visible' : workbench.outlinerFilterMode === 'visible' ? 'hidden' : 'all';
    workbench.outlinerFilterMode = next;
    document.getElementById('BtnOutlinerFilter').classList.toggle('active', next !== 'all');
    RenderOutliner();
  });
  document.getElementById('BtnOutlinerExpand').addEventListener('click', () => {
    for (const k of Object.keys(workbench.visibility)) workbench.visibility[k] = true;
    ApplyVisibility();
    RenderOutliner();
  });

  // Range sliders helper
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
  bindRange('RangeScaleMul', 'OutScaleMul', (v) => v.toFixed(2), (v) => { workbench.scaleMul = v; RebuildHeroSpecimen(); });
  bindRange('RangeLevels', 'OutLevels', (v) => String(Math.round(v)), (v) => { workbench.levelsOverride = Math.round(v); RebuildHeroSpecimen(); });
  bindRange('RangeLeafScale', 'OutLeafScale', (v) => v.toFixed(2), (v) => { workbench.leafScaleMul = v; RebuildHeroSpecimen(); });
  bindRange('RangeFlare', 'OutFlare', (v) => v.toFixed(2), (v) => { workbench.flareOverride = v; RebuildHeroSpecimen(); });
  bindRange('RangeTropism', 'OutTropism', (v) => v.toFixed(2), (v) => { workbench.tropismOverride = v; RebuildHeroSpecimen(); });

  bindRange('RangeWindDir', 'OutWindDir', (v) => String(Math.round(v)), (v) => { workbench.wind.directionDeg = v; SyncWindUniforms(); });
  bindRange('RangeWindSpeed', 'OutWindSpeed', (v) => v.toFixed(1), (v) => { workbench.wind.speedMps = v; SyncWindUniforms(); });
  bindRange('RangeWindGust', 'OutWindGust', (v) => v.toFixed(2), (v) => { workbench.wind.gustStrength = v; SyncWindUniforms(); });
  bindRange('RangeWindWave', 'OutWindWave', (v) => String(Math.round(v)), (v) => { workbench.wind.wavelengthMeters = v; SyncWindUniforms(); });
  bindRange('RangeTrunkFlex', 'OutTrunkFlex', (v) => v.toFixed(2), (v) => { workbench.wind.trunkFlex = v; SyncWindUniforms(); });
  bindRange('RangeLimbFlex', 'OutLimbFlex', (v) => v.toFixed(2), (v) => { workbench.wind.limbFlex = v; SyncWindUniforms(); });
  bindRange('RangeDetailFlex', 'OutDetailFlex', (v) => v.toFixed(2), (v) => { workbench.wind.detailFlex = v; SyncWindUniforms(); });

  bindRange('RangeLodTier', 'OutLodTier', (v) => `LOD ${Math.round(v)}`, (v) => { workbench.lodTier = Math.round(v); RebuildHeroSpecimen(); });
  bindRange('RangeUvScale', 'OutUvScale', (v) => v.toFixed(1), (v) => { workbench.uvScale = v; RebuildHeroSpecimen(); });
  bindRange('RangeCutHeight', 'OutCutHeight', (v) => String(Math.round(v * 100)), (v) => { workbench.cutHeightNorm = v; RenderOutliner(); });
  bindRange('RangeSnapImpulse', 'OutSnapImpulse', (v) => v.toFixed(1), (v) => { workbench.snapImpulse = v; });
  bindRange('RangeLeafThickness', 'OutLeafThickness', (v) => (v * 100).toFixed(1), (v) => {
    workbench.leafThicknessNorm = v;
    RebuildSdfGizmos();
  });
  bindRange('RangeEmissive', 'OutEmissive', (v) => v.toFixed(1), (v) => {
    workbench.emissiveStrength = v;
    SyncEmissiveUniforms();
  });

  document.querySelectorAll('#EmissiveTintChoices button').forEach((b) => {
    b.addEventListener('click', () => {
      workbench.emissiveTint = b.dataset.emissive;
      SyncEmissiveUniforms();
    });
  });

  // Toggle switches
  document.getElementById('RowToggleVortex').addEventListener('click', () => {
    workbench.wind.vortexEnabled = !workbench.wind.vortexEnabled;
    SyncWindUniforms();
  });
  document.getElementById('RowToggleWindArrows').addEventListener('click', () => {
    workbench.showWindArrows = !workbench.showWindArrows;
    if (runtime.windArrowGroup) runtime.windArrowGroup.visible = workbench.showWindArrows || workbench.renderMode === 'wind';
    SyncQuickTiles();
  });
  document.getElementById('RowToggleClusterSpheres').addEventListener('click', () => {
    workbench.showClusterSpheres = !workbench.showClusterSpheres;
    if (runtime.clusterSphereGroup) runtime.clusterSphereGroup.visible = workbench.showClusterSpheres;
    SyncQuickTiles();
  });
  document.getElementById('RowToggleSdfGizmos').addEventListener('click', () => {
    workbench.showSdfGizmos = !workbench.showSdfGizmos;
    workbench.visibility.sdf = workbench.showSdfGizmos;
    if (runtime.sdfGizmoGroup) runtime.sdfGizmoGroup.visible = workbench.showSdfGizmos || workbench.renderMode === 'sdf';
    SyncQuickTiles();
    RenderOutliner();
  });
  document.getElementById('RowToggleLeafSdfOnly').addEventListener('click', () => {
    workbench.includeLeafSdf = !workbench.includeLeafSdf;
    if (runtime.sdfLeafInstMesh) runtime.sdfLeafInstMesh.visible = workbench.includeLeafSdf;
    SyncQuickTiles();
  });

  document.getElementById('BtnToggleUvMode').addEventListener('click', () => SetRenderMode(workbench.renderMode === 'uv' ? 'pbr' : 'uv'));
  document.getElementById('BtnTriggerFracture').addEventListener('click', TriggerFracture);
  document.getElementById('BtnResetFracture').addEventListener('click', ResetFracture);

  SelectPage(0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ANIMATION LOOP
//------------------------------------------------------------------------------------------------------------------------

const _tmpVec = new Float32Array(3);
const _upY = new THREE.Vector3(0, 1, 0);
const _vA = new THREE.Vector3();
const _vB = new THREE.Vector3();
const _dirAB = new THREE.Vector3();
const _midAB = new THREE.Vector3();
const _quat = new THREE.Quaternion();
const _instMat = new THREE.Matrix4();

function ResizeViewport() {
  const wrap = canvas.parentElement;
  const w = Math.max(1, wrap.clientWidth);
  const h = Math.max(1, wrap.clientHeight);
  renderer.setSize(w, h, false);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
window.addEventListener('resize', ResizeViewport);

function TickAnimation(nowMs) {
  requestAnimationFrame(TickAnimation);
  const dt = Math.min(0.05, Math.max(0.001, (nowMs - runtime.lastFrameMs) / 1000.0));
  runtime.lastFrameMs = nowMs;
  runtime.timeSec += dt;
  runtime.sharedUniforms.uTime.value = runtime.timeSec;

  runtime.fpsSmooth = runtime.fpsSmooth * 0.92 + (1.0 / dt) * 0.08;
  if ((runtime.frameTick & 7) === 0) {
    document.getElementById('FooterFps').textContent = `${Math.round(runtime.fpsSmooth)} fps`;
  }

  // Update wind-deformed Wood Capsules + Instanced 1-Leaf SDF (3D shells + Hero Banner + Page 6 GDF Section)
  if (runtime.specimen) {
    const activeWind = workbench.windEnabled ? workbench.wind : { ...workbench.wind, speedMps: 0.0 };
    const needSdf = runtime.sdfGizmoGroup?.visible || (runtime.frameTick % 4 === 0);
    if (needSdf) {
      const deformed = EvaluateWindDeformedSdfPrimitives(runtime.specimen, runtime.timeSec, activeWind, 640);
      if (runtime.sdfGizmoGroup?.visible) {
        for (let i = 0; i < deformed.capsules.length && i < runtime.sdfCapsuleMeshes.length; i++) {
          const c = deformed.capsules[i];
          const m = runtime.sdfCapsuleMeshes[i];
          _vA.set(c.ax, c.ay, c.az);
          _vB.set(c.bx, c.by, c.bz);
          _dirAB.subVectors(_vB, _vA);
          const len = Math.max(0.05, _dirAB.length());
          _midAB.addVectors(_vA, _vB).multiplyScalar(0.5);
          m.position.copy(_midAB);
          _quat.setFromUnitVectors(_upY, _dirAB.normalize());
          m.quaternion.copy(_quat);
          const avgR = (c.rA + c.rB) * 0.5;
          m.scale.set(avgR, len, avgR);
        }
        if (runtime.sdfLeafInstMesh && runtime.sdfLeafInstMesh.visible) {
          const count = Math.min(deformed.leafInstances.length, runtime.sdfLeafInstMesh.count);
          for (let i = 0; i < count; i++) {
            const lf = deformed.leafInstances[i];
            const sx = lf.scaleX, sy = lf.scaleY;
            // Column-major 4x4 basis: X = side*sx, Y = dir*sy, Z = normal*sy, T = pos
            _instMat.set(
              lf.side[0] * sx, lf.dir[0] * sy, lf.normal[0] * sy, lf.px,
              lf.side[1] * sx, lf.dir[1] * sy, lf.normal[1] * sy, lf.py,
              lf.side[2] * sx, lf.dir[2] * sy, lf.normal[2] * sy, lf.pz,
              0, 0, 0, 1
            );
            runtime.sdfLeafInstMesh.setMatrixAt(i, _instMat);
          }
          runtime.sdfLeafInstMesh.instanceMatrix.needsUpdate = true;
        }
      }
      if (runtime.frameTick % 4 === 0) {
        const gdfOpts = {
          emissiveStrength: workbench.emissiveStrength,
          emissiveRgb: EMISSIVE_TINTS[workbench.emissiveTint] || EMISSIVE_TINTS.emerald,
          leafSdfEnabled: workbench.includeLeafSdf,
        };
        RenderGdfCrossSection(document.getElementById('HeroBannerCanvas'), deformed, runtime.specimen.height, runtime.bakedLeafSdf, gdfOpts);
        if (workbench.activePage === 5) {
          RenderGdfCrossSection(document.getElementById('GdfSectionCanvas'), deformed, runtime.specimen.height, runtime.bakedLeafSdf, gdfOpts);
        }
      }
    }
  }

  if (workbench.activePage === 2) {
    DrawWindStreamlines();
  }

  if (runtime.windArrowGroup?.visible) {
    for (const ar of runtime.windArrows) {
      SampleUnifiedWind(ar.wx, ar.wy, ar.wz, runtime.timeSec, workbench.wind, _tmpVec);
      const spd = Math.hypot(_tmpVec[0], _tmpVec[1], _tmpVec[2]);
      ar.group.scale.set(1, 1, Math.max(0.3, Math.min(3.6, spd * 0.22)));
      ar.group.lookAt(ar.wx + _tmpVec[0], ar.wy + _tmpVec[1], ar.wz + _tmpVec[2]);
      const t = Math.min(1.0, spd / 18.0);
      ar.mat.color.setRGB(0.19 + 0.64 * t, 0.43 + 0.28 * Math.sin(t * Math.PI * 0.7), 0.49 - 0.1 * t);
    }
  }

  if (runtime.fractureActive && runtime.fractureCrownPivot) {
    const maxTilt = Math.PI * 0.47;
    if (runtime.fractureTilt < maxTilt) {
      const gravityTorque = 2.4 * Math.sin(Math.max(0.14, runtime.fractureTilt)) + 0.08 * workbench.wind.speedMps;
      runtime.fractureVel += gravityTorque * dt;
      runtime.fractureTilt = Math.min(maxTilt, runtime.fractureTilt + runtime.fractureVel * dt);
      if (runtime.fractureTilt >= maxTilt) runtime.fractureVel *= -0.18;
      runtime.fractureCrownPivot.setRotationFromAxisAngle(runtime.fractureAxis, runtime.fractureTilt);
      document.getElementById('MetricCrownTilt').textContent = `${(runtime.fractureTilt * 180 / Math.PI).toFixed(1)}°`;
    }
    for (const s of runtime.fractureSplinters) {
      if (s.mesh.position.y > 0.05 || s.vel.y > 0) {
        s.vel.y -= 9.81 * dt;
        s.mesh.position.addScaledVector(s.vel, dt);
        s.mesh.rotation.x += s.rotVel.x * dt;
        s.mesh.rotation.y += s.rotVel.y * dt;
        s.mesh.rotation.z += s.rotVel.z * dt;
        if (s.mesh.position.y < 0.05) {
          s.mesh.position.y = 0.05;
          s.vel.y *= -0.25;
          s.vel.x *= 0.65;
          s.vel.z *= 0.65;
        }
      }
    }
  }

  runtime.frameTick++;
  controls.update();
  renderer.render(scene, camera);
}

InitControls();
ResizeViewport();
RebuildHeroSpecimen();
requestAnimationFrame(TickAnimation);
