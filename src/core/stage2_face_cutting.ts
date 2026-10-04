/**
 * Step 2: Bounded Planar & Wedge Face Carving (Face Cutting with Offset)
 * 
 * Slices the cliff faces along tectonic shear planes and joint cleavage facets:
 * - Uses spatial radius bounding and maximum penetration depth offsets
 * - Ensures cuts DO NOT cut through the entire formation indiscriminately
 * - Produces sharp planar cliff facets, dihedral clefts, and scarp shelves
 * 
 * STRICT COMPLIANCE: ZERO NOISE GENERATOR. Authored geometric planar carving only.
 */

import { CliffConfig, StrataLayer, MeshData, MacroCutter, Vec3 } from './types';
import { GeologicalPRNG } from './prng';
import { Vec3Math, MeshBuilder } from './geometry';

export class Stage2FaceCutting {
  /**
   * Author deterministic macro cutters
   */
  public static generateCutters(
    config: CliffConfig,
    strata: StrataLayer[],
    prng: GeologicalPRNG
  ): MacroCutter[] {
    const cutters: MacroCutter[] = [];
    const numCuts = config.cutCount;
    const W = config.width;
    const H = config.height;
    const D = config.depth;

    const CUT_TYPES: Array<'shear_facet' | 'dihedral_notch' | 'scarp_shelf' | 'overhang_cleft'> = [
      'shear_facet',
      'dihedral_notch',
      'scarp_shelf',
      'overhang_cleft',
    ];

    for (let i = 0; i < numCuts; i++) {
      const type = prng.choose(CUT_TYPES);

      // Distribute cutters along the front cliff face
      const xNorm = prng.range(-0.42, 0.42);
      const zNorm = prng.range(0.15, 0.92);

      const origin: Vec3 = {
        x: xNorm * W,
        y: D * prng.range(0.55, 0.85),
        z: zNorm * H,
      };

      // Tectonic shear cleavage angles
      let normal: Vec3;
      let radius: number;
      let depthOffset: number;
      let facetAngle: number;

      if (type === 'shear_facet') {
        // Vertical or steeply dipping shear plane
        const strikeRad = prng.range(-0.6, 0.6);
        const dipRad = prng.range(1.1, 1.45); // 65 to 83 degrees
        normal = Vec3Math.normalize({
          x: Math.sin(strikeRad) * Math.cos(dipRad),
          y: -Math.sin(dipRad),
          z: Math.cos(strikeRad) * Math.cos(dipRad),
        });
        radius = prng.range(W * 0.18, W * 0.32);
        depthOffset = prng.range(D * 0.08, D * config.maxCutOffset);
        facetAngle = (dipRad * 180) / Math.PI;
      } else if (type === 'scarp_shelf') {
        // Stepped horizontal-ish bench cutter
        const dipRad = prng.range(0.2, 0.5); // 10 to 30 degrees
        normal = Vec3Math.normalize({
          x: prng.range(-0.2, 0.2),
          y: -0.4,
          z: Math.cos(dipRad),
        });
        radius = prng.range(W * 0.15, W * 0.28);
        depthOffset = prng.range(D * 0.06, D * config.maxCutOffset * 0.8);
        facetAngle = (dipRad * 180) / Math.PI;
      } else if (type === 'dihedral_notch') {
        // Angled cleft / chimney notch
        const sideSign = prng.chance(0.5) ? 1 : -1;
        normal = Vec3Math.normalize({
          x: sideSign * prng.range(0.6, 0.9),
          y: -prng.range(0.4, 0.7),
          z: prng.range(-0.3, 0.3),
        });
        radius = prng.range(W * 0.12, W * 0.22);
        depthOffset = prng.range(D * 0.1, D * config.maxCutOffset * 1.2);
        facetAngle = 75;
      } else {
        // Overhang cleft
        normal = Vec3Math.normalize({
          x: prng.range(-0.3, 0.3),
          y: -0.85,
          z: -prng.range(0.3, 0.6),
        });
        radius = prng.range(W * 0.14, W * 0.25);
        depthOffset = prng.range(D * 0.08, D * config.maxCutOffset * 0.9);
        facetAngle = 110;
      }

      cutters.push({
        origin,
        normal,
        radius,
        depthOffset,
        facetAngle,
        type,
      });
    }

    return cutters;
  }

  /**
   * Apply bounded face cutting to the input mesh
   */
  public static cutFaces(
    inputMesh: MeshData,
    cutters: MacroCutter[],
    strata: StrataLayer[],
    config: CliffConfig
  ): MeshData {
    const builder = new MeshBuilder();
    const { vertices, indices, strataIndices, colors } = inputMesh;
    const numVertices = vertices.length / 3;

    // We process each vertex: if it lies within a cutter's spatial influence sphere and
    // is on the front side of the cutter plane, it gets clipped back to the shear plane
    // constrained strictly by the cutter's maximum depth offset
    const newPositions: Vec3[] = [];

    for (let i = 0; i < numVertices; i++) {
      const v: Vec3 = {
        x: vertices[i * 3],
        y: vertices[i * 3 + 1],
        z: vertices[i * 3 + 2],
      };

      // Only cut front-facing geometry (y > 0.1)
      if (v.y > config.depth * 0.05) {
        for (const cutter of cutters) {
          const distToCenter = Vec3Math.distance(v, cutter.origin);
          if (distToCenter < cutter.radius) {
            // Radial spatial falloff: full cut at center, smooth boundary at radius
            const radialWeight = 1.0 - (distToCenter / cutter.radius) ** 2;

            // Distance from vertex to cutter plane
            const diff = Vec3Math.sub(v, cutter.origin);
            const planeDist = Vec3Math.dot(diff, cutter.normal);

            // If vertex is in front of the cut plane (positive normal direction)
            if (planeDist > 0) {
              // Bounded offset shear: do not cut deeper than depthOffset
              const cutDepth = Math.min(planeDist, cutter.depthOffset * radialWeight);
              // Push vertex back along cut plane normal to form the flat geological facet
              v.x -= cutter.normal.x * cutDepth;
              v.y -= cutter.normal.y * cutDepth;
              v.z -= cutter.normal.z * cutDepth;
            }
          }
        }
      }

      newPositions.push(v);
    }

    // Reconstruct mesh with updated faceted vertices
    for (let i = 0; i < numVertices; i++) {
      const sIdx = strataIndices ? strataIndices[i] : 0;
      const color: [number, number, number] = colors
        ? [colors[i * 3], colors[i * 3 + 1], colors[i * 3 + 2]]
        : [0.72, 0.65, 0.58];

      builder.addVertex(newPositions[i], undefined, color, sIdx, 0);
    }

    // Add all triangles (maintaining exact manifold triangle topology)
    for (let i = 0; i < indices.length; i += 3) {
      builder.addTriangle(indices[i], indices[i + 1], indices[i + 2]);
    }

    return builder.build();
  }
}
