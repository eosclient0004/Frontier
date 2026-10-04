/**
 * Geological Cliff Generation Pipeline Controller
 * 
 * Executes all 5 stages deterministically:
 * Stage 1: Macro Cliff Base Shape (Strata, terraces, buttresses, talus ramp)
 * Stage 2: Face Cutting with Offset (Bounded shear planes, clefts, shelves)
 * Stage 3: Structural Joint Fracturing (Systematic joint sets, tectonic block dilation)
 * Stage 4: Edge Chipping & SDF Differential Erosion (Hardness-aware weathering, crevice accentuation)
 * Stage 5: Surface Cracks with Bounded Offset & Final Weathering (Shear micro-cracks, manifold polish)
 * 
 * STRICT COMPLIANCE: ZERO NOISE GENERATOR. 100% Manifold Triangles, 0 N-Gons.
 */

import { CliffConfig, PipelineResult, PipelineStageResult, StrataLayer, JointSet } from './types';
import { GeologicalPRNG } from './prng';
import { MeshGeometryOps } from './geometry';
import { Stage1MacroShape } from './stage1_macro_shape';
import { Stage2FaceCutting } from './stage2_face_cutting';
import { Stage3JointFractures } from './stage3_joint_fractures';
import { Stage4EdgeChippingErosion } from './stage4_edge_chipping_erosion';
import { Stage5SurfaceCracks } from './stage5_surface_cracks';

export class CliffPipeline {
  public static execute(config: CliffConfig): PipelineResult {
    const startTime = performance.now();
    const prng = new GeologicalPRNG(config.seed);

    // 1. Author Geological Strata Stack
    const strata = Stage1MacroShape.generateStrata(config, prng);

    const stages: PipelineStageResult[] = [];

    // ==========================================
    // STAGE 1: Macro Cliff Base Shape
    // ==========================================
    const t1 = performance.now();
    const stage1Mesh = Stage1MacroShape.buildMacroCliff(config, strata, prng);
    const topo1 = MeshGeometryOps.verifyTopology(stage1Mesh);
    stages.push({
      stageNumber: 1,
      stageName: '1. Macro Cliff Base Shape',
      description: 'Generates the massive geological formation silhouette: stratified lithological layers, stepped terraces, vertical buttresses, caprock overhangs, and basal talus scree ramp without noise.',
      mesh: stage1Mesh,
      metrics: {
        vertexCount: stage1Mesh.vertexCount,
        triangleCount: stage1Mesh.triangleCount,
        isManifold: topo1.isManifold,
        nonGonsCount: topo1.nonGonsCount,
        processingTimeMs: Math.round(performance.now() - t1),
        boundingBox: stage1Mesh.bounds,
      },
    });

    // ==========================================
    // STAGE 2: Face Cutting with Offset
    // ==========================================
    const t2 = performance.now();
    const cutters = Stage2FaceCutting.generateCutters(config, strata, prng);
    const stage2Mesh = Stage2FaceCutting.cutFaces(stage1Mesh, cutters, strata, config);
    const topo2 = MeshGeometryOps.verifyTopology(stage2Mesh);
    stages.push({
      stageNumber: 2,
      stageName: '2. Bounded Face Carving',
      description: 'Slices and shears cliff faces along tectonic shear planes using strict spatial bounding and penetration depth offsets so it does not destroy the mountain mass.',
      mesh: stage2Mesh,
      debugFeatures: {
        cutters,
      },
      metrics: {
        vertexCount: stage2Mesh.vertexCount,
        triangleCount: stage2Mesh.triangleCount,
        isManifold: topo2.isManifold,
        nonGonsCount: topo2.nonGonsCount,
        processingTimeMs: Math.round(performance.now() - t2),
        boundingBox: stage2Mesh.bounds,
      },
    });

    // ==========================================
    // STAGE 3: Structural Joint Fractures
    // ==========================================
    const t3 = performance.now();
    const jointSets = Stage3JointFractures.generateJointSets(config, strata, prng);
    const stage3Mesh = Stage3JointFractures.fractureCliff(stage2Mesh, jointSets, strata, config, prng);
    const topo3 = MeshGeometryOps.verifyTopology(stage3Mesh);
    stages.push({
      stageNumber: 3,
      stageName: '3. Structural Rock Fracturing',
      description: 'Partitions the cliff massif into individual interlocking rock blocks along systematic orthogonal (J1) and conjugate (J2) joint sets with bedding dilation gaps.',
      mesh: stage3Mesh,
      metrics: {
        vertexCount: stage3Mesh.vertexCount,
        triangleCount: stage3Mesh.triangleCount,
        isManifold: topo3.isManifold,
        nonGonsCount: topo3.nonGonsCount,
        processingTimeMs: Math.round(performance.now() - t3),
        boundingBox: stage3Mesh.bounds,
      },
    });

    // ==========================================
    // STAGE 4: Edge Chipping & SDF Differential Erosion
    // ==========================================
    const t4 = performance.now();
    const chips = Stage4EdgeChippingErosion.generateEdgeChips(stage3Mesh, config, strata, prng);
    const stage4Mesh = Stage4EdgeChippingErosion.processChippingAndErosion(stage3Mesh, chips, strata, config);
    const topo4 = MeshGeometryOps.verifyTopology(stage4Mesh);
    stages.push({
      stageNumber: 4,
      stageName: '4. Edge Chipping & SDF Erosion',
      description: 'Convex rock edges are geometrically chipped by localized wedge subtractions, then converted to SDF for differential strata erosion (softer beds erode deeper, hard beds form overhangs).',
      mesh: stage4Mesh,
      metrics: {
        vertexCount: stage4Mesh.vertexCount,
        triangleCount: stage4Mesh.triangleCount,
        isManifold: topo4.isManifold,
        nonGonsCount: topo4.nonGonsCount,
        processingTimeMs: Math.round(performance.now() - t4),
        boundingBox: stage4Mesh.bounds,
      },
    });

    // ==========================================
    // STAGE 5: Surface Cracks & Final Weathering
    // ==========================================
    const t5 = performance.now();
    const cracks = Stage5SurfaceCracks.generateSurfaceCracks(stage4Mesh, config, strata, prng);
    const stage5Mesh = Stage5SurfaceCracks.applyCracksAndFinalWeathering(stage4Mesh, cracks, strata, config);
    const topo5 = MeshGeometryOps.verifyTopology(stage5Mesh);
    stages.push({
      stageNumber: 5,
      stageName: '5. Surface Cracks & Final Weathering',
      description: 'Authored branching stress fissures with shallow depth offset chip individual rock faces, combined with final weathering to accent deep crevices while refining triangle geometry.',
      mesh: stage5Mesh,
      metrics: {
        vertexCount: stage5Mesh.vertexCount,
        triangleCount: stage5Mesh.triangleCount,
        isManifold: topo5.isManifold,
        nonGonsCount: topo5.nonGonsCount,
        processingTimeMs: Math.round(performance.now() - t5),
        boundingBox: stage5Mesh.bounds,
      },
    });

    const totalTimeMs = Math.round(performance.now() - startTime);

    return {
      config,
      strata,
      jointSets,
      stages,
      finalMesh: stage5Mesh,
      totalTimeMs,
    };
  }
}
