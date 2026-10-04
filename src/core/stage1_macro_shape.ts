/**
 * Step 1: Geological Cliff Macro-Shape Construction
 * 
 * Creates a majestic geological cliff formation without any noise:
 * - Stratified bedding layers (alternating competent & incompetent strata)
 * - Stepped scarp terraces and caprock overhangs
 * - Vertical buttresses and promontories
 * - Basal talus scree apron
 * 
 * STRICT COMPLIANCE: ZERO NOISE GENERATION. Pure structural geometry.
 */

import { CliffConfig, StrataLayer, MeshData, Vec3 } from './types';
import { GeologicalPRNG } from './prng';
import { MeshBuilder, Vec3Math } from './geometry';

export class Stage1MacroShape {
  /**
   * Author deterministic geological strata stack
   */
  public static generateStrata(config: CliffConfig, prng: GeologicalPRNG): StrataLayer[] {
    const strata: StrataLayer[] = [];
    const H = config.height;
    const numLayers = Math.max(3, config.strataCount);

    // Geological lithology templates with typical hardness & joint spacing ratios
    const LITHOLOGIES = [
      { name: 'Massive Sandstone', hardness: 0.85, color: [0.78, 0.52, 0.36] as [number, number, number], protrusion: 1.15 },
      { name: 'Weathered Siltstone', hardness: 0.45, color: [0.65, 0.48, 0.40] as [number, number, number], protrusion: 0.85 },
      { name: 'Competent Limestone', hardness: 0.95, color: [0.72, 0.68, 0.62] as [number, number, number], protrusion: 1.25 },
      { name: 'Fissile Shale', hardness: 0.25, color: [0.45, 0.40, 0.38] as [number, number, number], protrusion: 0.70 },
      { name: 'Conglomerate Bed', hardness: 0.70, color: [0.68, 0.58, 0.46] as [number, number, number], protrusion: 1.05 },
      { name: 'Caprock Quartzite', hardness: 1.00, color: [0.82, 0.75, 0.68] as [number, number, number], protrusion: 1.35 },
    ];

    let currentZ = 0;
    const targetThicknessAvg = H / numLayers;

    for (let i = 0; i < numLayers; i++) {
      const isTopCaprock = i === numLayers - 1;
      const litho = isTopCaprock 
        ? LITHOLOGIES[5] // Caprock for the top
        : prng.choose(LITHOLOGIES.slice(0, 5));

      // Authored thickness variance
      const thicknessFactor = isTopCaprock 
        ? prng.range(1.1, 1.4) 
        : prng.range(0.6, 1.4);
      
      const layerThickness = Math.min(H - currentZ, targetThicknessAvg * thicknessFactor);
      const zMin = currentZ;
      const zMax = currentZ + layerThickness;
      currentZ = zMax;

      const dip = config.overallDip + prng.range(-2.0, 2.0);
      const strike = prng.range(0, 360);
      const jointSpacing = layerThickness * prng.range(0.8, 1.6) * litho.hardness;

      strata.push({
        index: i,
        zMin,
        zMax,
        thickness: layerThickness,
        hardness: litho.hardness,
        protrusion: isTopCaprock ? litho.protrusion * config.overhangIntensity : litho.protrusion,
        dipAngle: dip,
        strikeAngle: strike,
        jointSpacing,
        name: litho.name,
        color: litho.color,
      });

      if (currentZ >= H - 1e-4) break;
    }

    return strata;
  }

  /**
   * Build the volumetric polygonal mesh of the macro cliff
   */
  public static buildMacroCliff(
    config: CliffConfig,
    strata: StrataLayer[],
    prng: GeologicalPRNG
  ): MeshData {
    const builder = new MeshBuilder();

    const W = config.width;
    const H = config.height;
    const D = config.depth;

    const numXSteps = 32;
    const numZSteps = 40;

    // Define buttress locations along X (authored geological structural promontories)
    const buttressPositions: { x: number; width: number; protrusion: number }[] = [];
    const numButtresses = config.buttressCount;
    for (let b = 0; b < numButtresses; b++) {
      const xNorm = (b + 0.5) / numButtresses;
      const xPos = -W * 0.45 + xNorm * W * 0.9 + prng.range(-W * 0.05, W * 0.05);
      buttressPositions.push({
        x: xPos,
        width: prng.range(W * 0.15, W * 0.25),
        protrusion: prng.range(D * 0.25, D * 0.45),
      });
    }

    // Grid of cliff face profile points [ix][iz]
    const grid: number[][] = []; // stores vertex indices for the front cliff face

    const talusMaxZ = H * config.talusHeightFraction;

    for (let iz = 0; iz <= numZSteps; iz++) {
      const zNorm = iz / numZSteps;
      const z = zNorm * H;

      // Find current strata layer
      let currentStrata = strata[0];
      for (const layer of strata) {
        if (z >= layer.zMin && z <= layer.zMax) {
          currentStrata = layer;
          break;
        }
      }

      // Base Y profile curve (cliff wall distance from back plane Y=0 to front face)
      // Bottom talus ramp slopes out; middle has stepped scarp terraces; top has caprock overhang
      let yBase = D * 0.55;

      if (z <= talusMaxZ) {
        // Talus scree ramp at base (angled natural repose slope)
        const talusT = z / Math.max(1e-4, talusMaxZ);
        yBase += D * 0.35 * (1.0 - talusT);
      } else {
        // Stratified scarp terrace profile
        const layerRelZ = (z - currentStrata.zMin) / Math.max(1e-4, currentStrata.thickness);
        // Stepped profile: hard layers form vertical cliffs; transitions form benches
        const step = Math.sin(layerRelZ * Math.PI);
        yBase += (currentStrata.protrusion - 1.0) * D * 0.2 + step * D * 0.08;
      }

      // Overhang at top cliff crest
      if (z > H * 0.88) {
        const topT = (z - H * 0.88) / (H * 0.12);
        yBase += config.overhangIntensity * D * 0.18 * topT;
      }

      const row: number[] = [];

      for (let ix = 0; ix <= numXSteps; ix++) {
        const xNorm = ix / numXSteps;
        const x = -W * 0.5 + xNorm * W;

        // Strata dip effect (tilt along X and Y based on geological dip)
        const dipOffset = (x / W) * Math.tan((currentStrata.dipAngle * Math.PI) / 180) * D * 0.3;

        // Calculate buttress promontory outward protrusion
        let buttressExtraY = 0;
        for (const butt of buttressPositions) {
          const distToButt = Math.abs(x - butt.x);
          if (distToButt < butt.width) {
            const bell = Math.cos((distToButt / butt.width) * (Math.PI * 0.5));
            // Buttresses taper down into the talus
            const zFactor = Math.max(0.2, Math.min(1.0, z / (H * 0.4)));
            buttressExtraY += butt.protrusion * bell * zFactor;
          }
        }

        const yFace = yBase + dipOffset + buttressExtraY;

        const pos: Vec3 = { x, y: yFace, z };
        const vIdx = builder.addVertex(
          pos,
          undefined,
          currentStrata.color,
          currentStrata.index,
          0
        );
        row.push(vIdx);
      }
      grid.push(row);
    }

    // Triangulate front cliff face with clean quads (decomposed into 2 clean triangles)
    for (let iz = 0; iz < numZSteps; iz++) {
      for (let ix = 0; ix < numXSteps; ix++) {
        const i00 = grid[iz][ix];
        const i10 = grid[iz][ix + 1];
        const i11 = grid[iz + 1][ix + 1];
        const i01 = grid[iz + 1][ix];

        builder.addQuad(i00, i10, i11, i01);
      }
    }

    // Add Solid Enclosing Bounding Volume (Back Wall, Base, Top, Side Walls)
    // to form a completely solid 3D watertight manifold rock formation
    const backY = 0;
    const baseZ = 0;
    const topZ = H;

    // Bottom Base Cap
    const botFrontLeft = grid[0][0];
    const botFrontRight = grid[0][numXSteps];
    const botBackLeft = builder.addVertex({ x: -W * 0.5, y: backY, z: baseZ }, { x: 0, y: 0, z: -1 });
    const botBackRight = builder.addVertex({ x: W * 0.5, y: backY, z: baseZ }, { x: 0, y: 0, z: -1 });
    builder.addQuad(botBackLeft, botBackRight, botFrontRight, botFrontLeft);

    // Top Plateau Cap
    const topFrontLeft = grid[numZSteps][0];
    const topFrontRight = grid[numZSteps][numXSteps];
    const topBackLeft = builder.addVertex({ x: -W * 0.5, y: backY, z: topZ }, { x: 0, y: 0, z: 1 });
    const topBackRight = builder.addVertex({ x: W * 0.5, y: backY, z: topZ }, { x: 0, y: 0, z: 1 });
    builder.addQuad(topFrontLeft, topFrontRight, topBackRight, topBackLeft);

    // Back Vertical Wall
    builder.addQuad(botBackRight, botBackLeft, topBackLeft, topBackRight);

    // Left Side Wall
    for (let iz = 0; iz < numZSteps; iz++) {
      const f0 = grid[iz][0];
      const f1 = grid[iz + 1][0];
      const b0 = builder.addVertex({ x: -W * 0.5, y: backY, z: (iz / numZSteps) * H }, { x: -1, y: 0, z: 0 });
      const b1 = builder.addVertex({ x: -W * 0.5, y: backY, z: ((iz + 1) / numZSteps) * H }, { x: -1, y: 0, z: 0 });
      builder.addQuad(b0, f0, f1, b1);
    }

    // Right Side Wall
    for (let iz = 0; iz < numZSteps; iz++) {
      const f0 = grid[iz][numXSteps];
      const f1 = grid[iz + 1][numXSteps];
      const b0 = builder.addVertex({ x: W * 0.5, y: backY, z: (iz / numZSteps) * H }, { x: 1, y: 0, z: 0 });
      const b1 = builder.addVertex({ x: W * 0.5, y: backY, z: ((iz + 1) / numZSteps) * H }, { x: 1, y: 0, z: 0 });
      builder.addQuad(f0, b0, b1, f1);
    }

    return builder.build();
  }
}
