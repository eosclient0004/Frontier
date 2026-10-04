/**
 * Step 4: Convex Edge Chipping & SDF Differential Geological Erosion
 * 
 * Implements the hybrid Polygon/SDF stage:
 * - Geometric Edge Chipping: Detects sharp convex edges on rock blocks and subtracts localized chip wedges
 * - Signed Distance Field (SDF) Conversion: Converts polygon mesh into distance field
 * - Differential Geological Strata Weathering: Soft strata erode deeper, hard competent beds stand proud
 * - Crevice Accentuation: Joint seams and block contacts are deepened by erosion
 * - Clean Isosurface Extraction: Polygonizes back into 100% manifold triangles (zero N-gons)
 * 
 * STRICT COMPLIANCE: ZERO NOISE GENERATOR. Deterministic geometric chipping and physical SDF transforms.
 */

import { CliffConfig, StrataLayer, MeshData, EdgeChip, Vec3 } from './types';
import { GeologicalPRNG } from './prng';
import { Vec3Math, MeshGeometryOps } from './geometry';
import { SDFVolume } from './sdf';

export class Stage4EdgeChippingErosion {
  /**
   * Author deterministic edge chips along sharp convex rock edges
   */
  public static generateEdgeChips(
    inputMesh: MeshData,
    config: CliffConfig,
    strata: StrataLayer[],
    prng: GeologicalPRNG
  ): EdgeChip[] {
    const chips: EdgeChip[] = [];
    const convexEdges = MeshGeometryOps.extractConvexEdges(inputMesh, 25);

    if (convexEdges.length === 0) return chips;

    const numChips = Math.min(config.edgeChipCount, convexEdges.length * 2);

    for (let i = 0; i < numChips; i++) {
      const edge = prng.choose(convexEdges);
      const t = prng.range(0.15, 0.85);
      const pos = Vec3Math.lerp(edge.p1, edge.p2, t);

      // Only chip front exposed faces (y > 0.1)
      if (pos.y < config.depth * 0.05) continue;

      const edgeDir = Vec3Math.normalize(Vec3Math.sub(edge.p2, edge.p1));
      const edgeLength = Vec3Math.distance(edge.p1, edge.p2);

      const chipSize = prng.range(config.edgeChipDepth * 0.6, config.edgeChipDepth * 1.4);
      const size: Vec3 = {
        x: Math.min(edgeLength * 0.4, chipSize * 1.2),
        y: chipSize * 0.9,
        z: chipSize * 0.8,
      };

      const apex = Vec3Math.sub(pos, Vec3Math.scale(edge.normal, chipSize * 0.7));

      chips.push({
        position: pos,
        edgeDir,
        normal: edge.normal,
        size,
        tetrahedralApex: apex,
      });
    }

    return chips;
  }

  /**
   * Execute Hybrid Edge Chipping and SDF Differential Erosion
   */
  public static processChippingAndErosion(
    inputMesh: MeshData,
    chips: EdgeChip[],
    strata: StrataLayer[],
    config: CliffConfig
  ): MeshData {
    // 1. Convert input mesh to SDF Grid
    const sdfRes = Math.max(32, Math.min(128, config.sdfResolution));
    const sdf = SDFVolume.fromMesh(inputMesh, sdfRes, 0.08);

    // 2. Subtract discrete 3D edge chip wedge volumes from the distance field
    for (const chip of chips) {
      sdf.subtractWedgeVolume(
        chip.tetrahedralApex,
        chip.position,
        chip.size.x
      );
    }

    // 3. Apply Differential Strata Hardness Erosion
    // Softer strata erode deeper; competent hard beds stand proud
    const baseErosion = (config.erosionStrength * config.depth) / sdfRes;
    sdf.applyStrataDifferentialErosion(
      strata,
      baseErosion,
      config.strataHardnessContrast
    );

    // 4. Extract Isosurface via Marching Cubes
    // Generates 100% clean manifold triangles, 0 n-gons
    const erodedMesh = sdf.extractMesh(0.0, strata);

    return erodedMesh;
  }
}
