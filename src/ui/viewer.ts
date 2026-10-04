/**
 * Interactive 3D WebGL Viewport (Three.js)
 * 
 * High quality geological rendering:
 * - Untextured Clay / Sculpt mode (strictly geometry-driven)
 * - Matcap curvature mode
 * - Lithological Strata layer visualization
 * - Normal map inspector
 * - Manifold Wireframe overlay
 * - Cinematic Geological Sunlight with soft contact shadows
 */

import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { MeshData, PipelineStageResult } from '../core/types';

export type RenderMode = 'clay' | 'strata' | 'matcap' | 'normals' | 'wireframe';

export class GeologicalViewer {
  private container: HTMLElement;
  private scene: THREE.Scene;
  private camera: THREE.PerspectiveCamera;
  private renderer: THREE.WebGLRenderer;
  private controls: OrbitControls;

  private currentMeshObject: THREE.Mesh | null = null;
  private wireframeMeshObject: THREE.Mesh | null = null;
  private debugGroup: THREE.Group;

  private currentRenderMode: RenderMode = 'clay';
  private currentMeshData: MeshData | null = null;

  private sunLight!: THREE.DirectionalLight;
  private ambientLight!: THREE.AmbientLight;
  private hemiLight!: THREE.HemisphereLight;

  constructor(container: HTMLElement) {
    this.container = container;

    // 1. Scene
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(0x0e1118);

    // Subtle atmospheric fog for scale depth
    this.scene.fog = new THREE.FogExp2(0x0e1118, 0.008);

    // 2. Camera
    const aspect = container.clientWidth / container.clientHeight;
    this.camera = new THREE.PerspectiveCamera(42, aspect, 0.1, 1000);
    this.camera.position.set(0, -60, 35);
    this.camera.up.set(0, 0, 1); // Z is elevation

    // 3. Renderer
    this.renderer = new THREE.WebGLRenderer({
      antialias: true,
      powerPreference: 'high-performance',
    });
    this.renderer.setSize(container.clientWidth, container.clientHeight);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.renderer.shadowMap.enabled = true;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.1;
    container.appendChild(this.renderer.domElement);

    // 4. OrbitControls
    this.controls = new OrbitControls(this.camera, this.renderer.domElement);
    this.controls.enableDamping = true;
    this.controls.dampingFactor = 0.08;
    this.controls.target.set(0, 10, 18);
    this.controls.maxPolarAngle = Math.PI / 2 + 0.15; // Don't look too far underneath
    this.controls.minDistance = 5;
    this.controls.maxDistance = 200;

    // 5. Lighting
    this.setupLighting();

    // 6. Debug / Gizmo group
    this.debugGroup = new THREE.Group();
    this.scene.add(this.debugGroup);

    // 7. Ground Grid / Shadow Receiver Plane
    this.setupGround();

    // 8. Resize Handler
    window.addEventListener('resize', this.onResize.bind(this));

    // 9. Start render loop
    this.animate();
  }

  private setupLighting(): void {
    // Ambient light (cool skylight)
    this.ambientLight = new THREE.AmbientLight(0x7a8ca5, 0.65);
    this.scene.add(this.ambientLight);

    // Hemisphere light (ground bounce vs sky)
    this.hemiLight = new THREE.HemisphereLight(0x94a9c9, 0x2b241e, 0.55);
    this.hemiLight.position.set(0, 0, 100);
    this.scene.add(this.hemiLight);

    // Key Directional Sun Light (Warm grazing sunlight for dramatic rock relief)
    this.sunLight = new THREE.DirectionalLight(0xfff1dc, 2.2);
    this.sunLight.position.set(45, -50, 60);
    this.sunLight.castShadow = true;
    this.sunLight.shadow.mapSize.width = 2048;
    this.sunLight.shadow.mapSize.height = 2048;
    this.sunLight.shadow.camera.near = 10;
    this.sunLight.shadow.camera.far = 250;
    this.sunLight.shadow.camera.left = -50;
    this.sunLight.shadow.camera.right = 50;
    this.sunLight.shadow.camera.top = 50;
    this.sunLight.shadow.camera.bottom = -50;
    this.sunLight.shadow.bias = -0.0005;
    this.scene.add(this.sunLight);

    // Secondary Rim Light (back-edge silhouette definition)
    const rimLight = new THREE.DirectionalLight(0x8ba4c9, 0.8);
    rimLight.position.set(-40, 50, 40);
    this.scene.add(rimLight);
  }

  private setupGround(): void {
    // Circular ground disc receiving shadows
    const groundGeo = new THREE.PlaneGeometry(160, 160);
    const groundMat = new THREE.MeshStandardMaterial({
      color: 0x141822,
      roughness: 0.95,
      metalness: 0.05,
    });
    const ground = new THREE.Mesh(groundGeo, groundMat);
    ground.position.set(0, 0, -0.05);
    ground.receiveShadow = true;
    this.scene.add(ground);

    // Subtle coordinate grid
    const grid = new THREE.GridHelper(120, 30, 0x2b3245, 0x1a1f2c);
    grid.rotation.x = Math.PI / 2;
    grid.position.set(0, 0, 0);
    this.scene.add(grid);
  }

  /**
   * Set and display mesh from geological pipeline result
   */
  public displayStage(stageResult: PipelineStageResult): void {
    const meshData = stageResult.mesh;
    this.currentMeshData = meshData;

    // Remove existing meshes
    if (this.currentMeshObject) {
      this.scene.remove(this.currentMeshObject);
      this.currentMeshObject.geometry.dispose();
      this.currentMeshObject = null;
    }
    if (this.wireframeMeshObject) {
      this.scene.remove(this.wireframeMeshObject);
      this.wireframeMeshObject.geometry.dispose();
      this.wireframeMeshObject = null;
    }

    // Clear debug gizmos
    while (this.debugGroup.children.length > 0) {
      const obj = this.debugGroup.children[0];
      this.debugGroup.remove(obj);
    }

    // Build Three.js BufferGeometry
    const geometry = new THREE.BufferGeometry();
    geometry.setAttribute('position', new THREE.BufferAttribute(meshData.vertices, 3));
    geometry.setAttribute('normal', new THREE.BufferAttribute(meshData.normals, 3));
    geometry.setIndex(new THREE.BufferAttribute(meshData.indices, 1));

    if (meshData.colors) {
      geometry.setAttribute('color', new THREE.BufferAttribute(meshData.colors, 3));
    }

    // Create Main Mesh
    const material = this.createMaterial(this.currentRenderMode, !!meshData.colors);
    this.currentMeshObject = new THREE.Mesh(geometry, material);
    this.currentMeshObject.castShadow = true;
    this.currentMeshObject.receiveShadow = true;
    this.scene.add(this.currentMeshObject);

    // Create Wireframe overlay if active
    if (this.currentRenderMode === 'wireframe') {
      const wireMat = new THREE.MeshBasicMaterial({
        color: 0x3b82f6,
        wireframe: true,
        transparent: true,
        opacity: 0.8,
      });
      this.wireframeMeshObject = new THREE.Mesh(geometry.clone(), wireMat);
      this.scene.add(this.wireframeMeshObject);
    }

    // Add stage-specific debug gizmos
    this.renderStageDebugGizmos(stageResult);
  }

  private renderStageDebugGizmos(stageResult: PipelineStageResult): void {
    const debug = stageResult.debugFeatures;
    if (!debug) return;

    // Stage 2: Draw Cutter Planes
    if (debug.cutters && debug.cutters.length > 0) {
      for (const c of debug.cutters) {
        const ringGeo = new THREE.RingGeometry(c.radius * 0.9, c.radius, 32);
        const ringMat = new THREE.MeshBasicMaterial({
          color: 0xf59e0b,
          side: THREE.DoubleSide,
          transparent: true,
          opacity: 0.5,
        });
        const ringMesh = new THREE.Mesh(ringGeo, ringMat);
        ringMesh.position.set(c.origin.x, c.origin.y, c.origin.z);
        ringMesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), new THREE.Vector3(c.normal.x, c.normal.y, c.normal.z));
        this.debugGroup.add(ringMesh);
      }
    }
  }

  public setRenderMode(mode: RenderMode): void {
    this.currentRenderMode = mode;
    if (this.currentMeshData && this.currentMeshObject) {
      const hasColors = !!this.currentMeshData.colors;
      this.currentMeshObject.material = this.createMaterial(mode, hasColors);

      if (this.wireframeMeshObject) {
        this.scene.remove(this.wireframeMeshObject);
        this.wireframeMeshObject.geometry.dispose();
        this.wireframeMeshObject = null;
      }

      if (mode === 'wireframe') {
        const wireMat = new THREE.MeshBasicMaterial({
          color: 0x3b82f6,
          wireframe: true,
          transparent: true,
          opacity: 0.85,
        });
        this.wireframeMeshObject = new THREE.Mesh(this.currentMeshObject.geometry.clone(), wireMat);
        this.scene.add(this.wireframeMeshObject);
      }
    }
  }

  private createMaterial(mode: RenderMode, hasColors: boolean): THREE.Material {
    switch (mode) {
      case 'clay':
        // Pure untextured clay sculpt material: raw geometry & shadow relief
        return new THREE.MeshStandardMaterial({
          color: 0xd8cbba,
          roughness: 0.88,
          metalness: 0.02,
          flatShading: false,
        });

      case 'strata':
        // Color-coded geological lithology strata
        return new THREE.MeshStandardMaterial({
          vertexColors: hasColors,
          color: hasColors ? 0xffffff : 0xaa9988,
          roughness: 0.82,
          metalness: 0.04,
        });

      case 'matcap':
        // Matcap sculpt shader highlighting curvature
        return new THREE.MeshNormalMaterial({
          flatShading: false,
        });

      case 'normals':
        return new THREE.MeshNormalMaterial({
          flatShading: false,
        });

      case 'wireframe':
        // Clay underlay with blue wireframe
        return new THREE.MeshStandardMaterial({
          color: 0x1c212c,
          roughness: 0.9,
          polygonOffset: true,
          polygonOffsetFactor: 1,
          polygonOffsetUnits: 1,
        });

      default:
        return new THREE.MeshStandardMaterial({ color: 0xd8cbba });
    }
  }

  public resetCamera(): void {
    this.camera.position.set(0, -60, 35);
    this.controls.target.set(0, 10, 18);
    this.controls.update();
  }

  private onResize(): void {
    const width = this.container.clientWidth;
    const height = this.container.clientHeight;
    this.camera.aspect = width / height;
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(width, height);
  }

  private animate = (): void => {
    requestAnimationFrame(this.animate);
    this.controls.update();
    this.renderer.render(this.scene, this.camera);
  };
}
