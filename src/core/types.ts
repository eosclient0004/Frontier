/**
 * Core Types for Geological Cliff / Rock Formation Generator
 * Strict Non-Noise Procedural Geology Engine
 */

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface Triangle {
  v0: Vec3;
  v1: Vec3;
  v2: Vec3;
  normal?: Vec3;
  strataIndex?: number;
  rockBlockId?: number;
}

export interface MeshData {
  vertices: Float32Array; // [x0, y0, z0, x1, y1, z1, ...]
  indices: Uint32Array;   // [i0, i1, i2, ...] (clean triangles only, no n-gons)
  normals: Float32Array;  // [nx0, ny0, nz0, ...]
  strataIndices?: Uint8Array; // Strata layer index per vertex/face
  blockIds?: Uint32Array;    // Rock block ID per face
  colors?: Float32Array;     // [r0, g0, b0, ...] for geological visualization
  bounds: {
    min: Vec3;
    max: Vec3;
  };
  triangleCount: number;
  vertexCount: number;
}

export interface StrataLayer {
  index: number;
  zMin: number;
  zMax: number;
  thickness: number;
  hardness: number;       // 0.1 (soft shale) to 1.0 (hard quartz/granite/limestone)
  protrusion: number;     // How far this layer projects outward
  dipAngle: number;       // Layer inclination in degrees
  strikeAngle: number;    // Layer compass strike in degrees
  jointSpacing: number;   // Spacing of vertical joints in this layer (proportional to thickness & hardness)
  name: string;           // E.g., "Caprock Sandstone", "Upper Siltstone", "Massive Limestone"
  color: [number, number, number]; // [r, g, b] 0..1 for strata inspection
}

export interface JointSet {
  id: string;
  name: string;
  strike: number;         // Strike angle in degrees (0 - 360)
  dip: number;            // Dip angle in degrees from horizontal (0 - 90)
  spacing: number;        // Spacing between parallel joint planes
  persistence: number;    // Probability (0..1) of joint continuing through strata
  dilation: number;       // Physical gap / offset opening (0.01 - 0.2)
  isBeddingParallel?: boolean;
}

export interface MacroCutter {
  origin: Vec3;
  normal: Vec3;
  radius: number;         // Spatial boundary (does not cut infinitely)
  depthOffset: number;    // Maximum penetration offset into the cliff face
  facetAngle: number;
  type: 'shear_facet' | 'dihedral_notch' | 'scarp_shelf' | 'overhang_cleft';
}

export interface EdgeChip {
  position: Vec3;
  edgeDir: Vec3;
  normal: Vec3;
  size: Vec3;
  tetrahedralApex: Vec3;
}

export interface SurfaceCrack {
  points: Vec3[];         // Polyline path
  depth: number;          // Depth offset (strictly bounded to shallow surface)
  width: number;          // Surface opening width
  branches: SurfaceCrack[];
}

export interface CliffConfig {
  seed: number;
  formationType: 'sedimentary_canyon' | 'granite_scarp' | 'columnar_basalt' | 'alpine_fault' | 'coastal_bluff';
  
  // Dimensions
  width: number;          // Along X
  height: number;         // Along Z (elevation)
  depth: number;          // Along Y (cliff face to back wall)
  
  // Stage 1: Macro Cliff Base
  strataCount: number;
  overallDip: number;     // General strata dip
  talusHeightFraction: number; // Height fraction occupied by base talus ramp (0..0.35)
  buttressCount: number;  // Number of protruding vertical ribs
  overhangIntensity: number; // Caprock overhang protrusion
  
  // Stage 2: Face Cutting with Offset
  cutCount: number;
  maxCutOffset: number;   // Depth bounding offset (so it doesn't cut everything)
  shearFacetSharpness: number;
  
  // Stage 3: Structural Joint Fracturing
  jointSetsCount: number;
  jointSpacing: number;
  jointDilation: number;  // Spacing offset between fractured rock blocks
  staggerJoints: boolean; // Offset vertical joints across strata
  
  // Stage 4: Edge Chipping & SDF Differential Erosion
  edgeChipCount: number;
  edgeChipDepth: number;
  sdfResolution: number;  // Voxel grid resolution (e.g., 64, 96, 128)
  erosionStrength: number;// Differential weathering rate
  strataHardnessContrast: number; // How much soft layers erode vs hard layers
  smoothMinRadius: number;// SDF smin blending radius
  
  // Stage 5: Realistic Surface Cracks with Offset & Final Detail
  surfaceCrackDensity: number;
  crackMaxDepthOffset: number; // Bounded shallow crack penetration
  crackBranching: boolean;
  crackChippingIntensity: number;
  finalLaplacianSmoothing: number; // Topological triangle conditioning
}

export interface PipelineStageResult {
  stageNumber: number;
  stageName: string;
  description: string;
  mesh: MeshData;
  debugFeatures?: {
    strataPlanes?: { z: number; name: string }[];
    jointPlanes?: { origin: Vec3; normal: Vec3; size: number }[];
    cutters?: MacroCutter[];
    crackPolylines?: Vec3[][];
    chipPositions?: Vec3[];
  };
  metrics: {
    vertexCount: number;
    triangleCount: number;
    isManifold: boolean;
    nonGonsCount: number;   // Should always be 0
    processingTimeMs: number;
    boundingBox: { min: Vec3; max: Vec3 };
  };
}

export interface PipelineResult {
  config: CliffConfig;
  strata: StrataLayer[];
  jointSets: JointSet[];
  stages: PipelineStageResult[];
  finalMesh: MeshData;
  totalTimeMs: number;
}
