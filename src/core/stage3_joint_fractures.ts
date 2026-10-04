/**
 * Step 3: Structural Joint Fractures & Tectonic Rock Block Segmentation
 * 
 * Transforms the solid cliff massif into an assembly of interlocking geological rock blocks:
 * - Systematic orthogonal and conjugate joint sets (J1, J2, J3 bedding planes)
 * - Strata-staggered joint spacing (lithology-dependent block sizes)
 * - Tectonic joint dilation (opening physical fissures and seams between blocks)
 * 
 * STRICT COMPLIANCE: ZERO NOISE GENERATION. Authored geological joint mechanics only.
 */

import { CliffConfig, StrataLayer, JointSet, MeshData, Vec3 } from './types';
import { GeologicalPRNG } from './prng';
import { Vec3Math, MeshBuilder } from './geometry';

export class Stage3JointFractures {
  /**
   * Author deterministic geological joint sets
   */
  public static generateJointSets(
    config: CliffConfig,
    strata: StrataLayer[],
    prng: GeologicalPRNG
  ): JointSet[] {
    const jointSets: JointSet[] = [];

    // Joint Set 1: Primary Vertical Tectonic Joint Set (orthogonal strike)
    const j1Strike = prng.range(10, 35);
    const j1Dip = prng.range(78, 88);
    jointSets.push({
      id: 'J1',
      name: 'Primary Systematic Joint Set (J1)',
      strike: j1Strike,
      dip: j1Dip,
      spacing: config.jointSpacing * prng.range(0.9, 1.2),
      persistence: 0.95,
      dilation: config.jointDilation,
      isBeddingParallel: false,
    });

    // Joint Set 2: Cross Joint Set (Conjugate or Orthogonal to J1)
    const j2Strike = j1Strike + prng.range(75, 105);
    const j2Dip = prng.range(80, 90);
    jointSets.push({
      id: 'J2',
      name: 'Conjugate Cross Joint Set (J2)',
      strike: j2Strike % 360,
      dip: j2Dip,
      spacing: config.jointSpacing * prng.range(1.1, 1.5),
      persistence: 0.85,
      dilation: config.jointDilation * 0.9,
      isBeddingParallel: false,
    });

    // Joint Set 3: Bedding Plane Partings (parallel to strata dip)
    jointSets.push({
      id: 'J3',
      name: 'Bedding Plane Parting Set (J3)',
      strike: config.overallDip > 0 ? 90 : 0,
      dip: config.overallDip,
      spacing: config.height / Math.max(3, config.strataCount),
      persistence: 1.0,
      dilation: config.jointDilation * 1.2,
      isBeddingParallel: true,
    });

    return jointSets;
  }

  /**
   * Apply joint fracturing, block partitioning, and joint dilation
   */
  public static fractureCliff(
    inputMesh: MeshData,
    jointSets: JointSet[],
    strata: StrataLayer[],
    config: CliffConfig,
    prng: GeologicalPRNG
  ): MeshData {
    const builder = new MeshBuilder();
    const { vertices, indices, strataIndices, colors } = inputMesh;
    const numVertices = vertices.length / 3;

    const j1 = jointSets[0];
    const j2 = jointSets[1];

    // Compute joint plane normal vectors
    const j1StrikeRad = (j1.strike * Math.PI) / 180;
    const j1DipRad = (j1.dip * Math.PI) / 180;
    const n1 = Vec3Math.normalize({
      x: Math.cos(j1StrikeRad) * Math.sin(j1DipRad),
      y: Math.sin(j1StrikeRad) * Math.sin(j1DipRad),
      z: Math.cos(j1DipRad),
    });

    const j2StrikeRad = (j2.strike * Math.PI) / 180;
    const j2DipRad = (j2.dip * Math.PI) / 180;
    const n2 = Vec3Math.normalize({
      x: Math.cos(j2StrikeRad) * Math.sin(j2DipRad),
      y: Math.sin(j2StrikeRad) * Math.sin(j2DipRad),
      z: Math.cos(j2DipRad),
    });

    // Partition vertices into structural rock blocks
    const modifiedPositions: Vec3[] = [];
    const blockIds: number[] = [];

    // Pre-generate layer offsets for staggered brickwork jointing
    const layerStaggerOffsets = strata.map((_, idx) => (config.staggerJoints ? (idx % 2) * (j1.spacing * 0.5) : 0));

    for (let i = 0; i < numVertices; i++) {
      const v: Vec3 = {
        x: vertices[i * 3],
        y: vertices[i * 3 + 1],
        z: vertices[i * 3 + 2],
      };

      const sIdx = strataIndices ? strataIndices[i] : 0;
      const layerOffset = layerStaggerOffsets[sIdx] || 0;

      // Joint coordinate projections
      const u1 = Vec3Math.dot(v, n1) + layerOffset;
      const u2 = Vec3Math.dot(v, n2);

      // Block grid cell indices
      const blockX = Math.floor(u1 / Math.max(0.5, j1.spacing));
      const blockY = Math.floor(u2 / Math.max(0.5, j2.spacing));
      const blockZ = sIdx;

      // Unique discrete block ID
      const blockId = ((blockZ * 1000 + blockY) * 1000 + blockX) >>> 0;
      blockIds.push(blockId);

      // Only dilate joints on the exposed rock faces (y > 0.1)
      if (v.y > config.depth * 0.05) {
        // Distance to nearest joint plane
        const distToJ1 = Math.abs(u1 - (blockX + 0.5) * j1.spacing);
        const distToJ2 = Math.abs(u2 - (blockY + 0.5) * j2.spacing);

        // Joint dilation gap along joint seams
        const edgeThreshold1 = j1.spacing * 0.18;
        const edgeThreshold2 = j2.spacing * 0.18;

        if (distToJ1 > j1.spacing * 0.5 - edgeThreshold1) {
          const seamProximity = (distToJ1 - (j1.spacing * 0.5 - edgeThreshold1)) / edgeThreshold1;
          const dilationAmount = j1.dilation * seamProximity * 0.6;
          // Pull vertex inward into the joint seam to open physical fissure
          v.y -= dilationAmount * 1.5;
        }

        if (distToJ2 > j2.spacing * 0.5 - edgeThreshold2) {
          const seamProximity = (distToJ2 - (j2.spacing * 0.5 - edgeThreshold2)) / edgeThreshold2;
          const dilationAmount = j2.dilation * seamProximity * 0.6;
          v.y -= dilationAmount * 1.5;
        }

        // Stratified bedding plane parting: deepen seam at strata contact
        const currentStrata = strata[sIdx] || strata[0];
        const distToBedding = Math.min(
          Math.abs(v.z - currentStrata.zMin),
          Math.abs(v.z - currentStrata.zMax)
        );
        const beddingThreshold = currentStrata.thickness * 0.12;
        if (distToBedding < beddingThreshold) {
          const beddingProximity = 1.0 - (distToBedding / beddingThreshold);
          v.y -= config.jointDilation * 1.8 * beddingProximity * (1.1 - currentStrata.hardness * 0.5);
        }
      }

      modifiedPositions.push(v);
    }

    // Reconstruct mesh
    for (let i = 0; i < numVertices; i++) {
      const sIdx = strataIndices ? strataIndices[i] : 0;
      const color: [number, number, number] = colors
        ? [colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]]
        : [0.72, 0.65, 0.58];

      builder.addVertex(modifiedPositions[i], undefined, color, sIdx, blockIds[i]);
    }

    for (let i = 0; i < indices.length; i += 3) {
      builder.addTriangle(indices[i], indices[i + 1], indices[i + 2]);
    }

    return builder.build();
  }
}
