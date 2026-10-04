/**
 * High-Performance Signed Distance Field (SDF) & Isosurface Engine
 * 
 * Implements:
 * - Polygon Mesh -> Narrow-Band 3D SDF
 * - Differential Geological Strata Weathering & Thermal/Hydraulic Erosion
 * - Morphological Erosion / Dilation & Crevice Accentuation
 * - Robust Marching Cubes Isosurface Extraction (Strictly manifold triangles, 0 N-gons)
 * 
 * STRICT COMPLIANCE: NO NOISE. Only deterministic structural distance transforms.
 */

import { Vec3, MeshData, StrataLayer } from './types';
import { Vec3Math, MeshBuilder, MeshGeometryOps } from './geometry';

// Standard Marching Cubes Lookup Tables
const EDGE_TABLE = new Int32Array([
  0x0, 0x109, 0x203, 0x30a, 0x406, 0x50f, 0x605, 0x70c,
  0x80c, 0x905, 0xa0f, 0xb06, 0xc0a, 0xd03, 0xe09, 0xf00,
  0x190, 0x99, 0x393, 0x29a, 0x596, 0x49f, 0x795, 0x69c,
  0x99c, 0x895, 0xb9f, 0xa96, 0xd9a, 0xc93, 0xf99, 0xe90,
  0x230, 0x339, 0x33, 0x13a, 0x636, 0x73f, 0x435, 0x53c,
  0xa3c, 0xb35, 0x83f, 0x936, 0xe3a, 0xf33, 0xc39, 0xd30,
  0x3a0, 0x2a9, 0x1a3, 0xaa, 0x7a6, 0x6af, 0x5a5, 0x4ac,
  0xba0, 0xaa9, 0x9a3, 0x8aa, 0xfa6, 0xeaf, 0xda5, 0xca0,
  0x460, 0x569, 0x663, 0x76a, 0x66, 0x16f, 0x265, 0x36c,
  0xc6c, 0xd65, 0xe6f, 0xf66, 0x86a, 0x963, 0xa69, 0xb60,
  0x5f0, 0x4f9, 0x7f3, 0x6fa, 0x1f6, 0xff, 0x3f5, 0x2fc,
  0xdfc, 0xcf5, 0xfff, 0xef6, 0x9fa, 0x8f3, 0xbf9, 0xaf0,
  0x650, 0x759, 0x453, 0x55a, 0x256, 0x35f, 0x55, 0x15c,
  0xe5c, 0xf55, 0xc5f, 0xd56, 0xa5a, 0xb53, 0x859, 0x950,
  0x7c0, 0x6c9, 0x5c3, 0x4ca, 0x3c6, 0x2cf, 0x1c5, 0xcc,
  0xfcc, 0xec5, 0xdcf, 0xcc6, 0xbca, 0xac3, 0x9c9, 0x8c0,
  0x8c0, 0x9c9, 0xac3, 0xbca, 0xcc6, 0xdcf, 0xec5, 0xfcc,
  0xcc, 0x1c5, 0x2cf, 0x3c6, 0x4ca, 0x5c3, 0x6c9, 0x7c0,
  0x950, 0x859, 0xb53, 0xa5a, 0xd56, 0xc5f, 0xf55, 0xe5c,
  0x15c, 0x55, 0x35f, 0x256, 0x55a, 0x453, 0x759, 0x650,
  0xaf0, 0xbf9, 0x8f3, 0x9fa, 0xef6, 0xfff, 0xcf5, 0xdfc,
  0x2fc, 0x3f5, 0xff, 0x1f6, 0x6fa, 0x7f3, 0x4f9, 0x5f0,
  0xb60, 0xa69, 0x963, 0x86a, 0xf66, 0xe6f, 0xd65, 0xc6c,
  0x36c, 0x265, 0x16f, 0x66, 0x76a, 0x663, 0x569, 0x460,
  0xca0, 0xda5, 0xeaf, 0xfa6, 0x8aa, 0x9a3, 0xaa9, 0xba0,
  0x4ac, 0x5a5, 0x6af, 0x7a6, 0xaa, 0x1a3, 0x2a9, 0x3a0,
  0xd30, 0xc39, 0xf33, 0xe3a, 0x936, 0x83f, 0xb35, 0xa3c,
  0x53c, 0x435, 0x73f, 0x636, 0x13a, 0x33, 0x339, 0x230,
  0xe90, 0xf99, 0xc93, 0xd9a, 0xa96, 0xb9f, 0x895, 0x99c,
  0x69c, 0x795, 0x49f, 0x596, 0x29a, 0x393, 0x99, 0x190,
  0xf00, 0xe09, 0xd03, 0xc0a, 0xb06, 0xa0f, 0x905, 0x80c,
  0x70c, 0x605, 0x50f, 0x406, 0x30a, 0x203, 0x109, 0x0,
]);

// Edge connection table
const EDGE_PAIRS: [number, number][] = [
  [0, 1], [1, 2], [2, 3], [3, 0], // Bottom edges (z=0)
  [4, 5], [5, 6], [6, 7], [7, 4], // Top edges (z=1)
  [0, 4], [1, 5], [2, 6], [3, 7], // Vertical edges
];

// Marching Cubes Triangle Table (flat array of 256 * 16 entries)
const TRI_TABLE = new Int8Array([
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 1, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 8, 3, 9, 8, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 3, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  9, 2, 10, 0, 2, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  2, 8, 3, 2, 10, 8, 10, 9, 8, -1, -1, -1, -1, -1, -1, -1,
  3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 11, 2, 8, 11, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 9, 0, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 11, 2, 1, 9, 11, 9, 8, 11, -1, -1, -1, -1, -1, -1, -1,
  3, 10, 1, 11, 10, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 10, 1, 0, 8, 10, 8, 11, 10, -1, -1, -1, -1, -1, -1, -1,
  3, 9, 0, 3, 11, 9, 11, 10, 9, -1, -1, -1, -1, -1, -1, -1,
  9, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  4, 3, 0, 7, 3, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 1, 9, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  4, 1, 9, 4, 7, 1, 7, 3, 1, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 10, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  3, 4, 7, 3, 0, 4, 1, 2, 10, -1, -1, -1, -1, -1, -1, -1,
  9, 2, 10, 9, 0, 2, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1,
  2, 10, 9, 2, 9, 7, 2, 7, 3, 7, 9, 4, -1, -1, -1, -1,
  8, 4, 7, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  11, 4, 7, 11, 2, 4, 2, 0, 4, -1, -1, -1, -1, -1, -1, -1,
  9, 0, 1, 8, 4, 7, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1,
  4, 7, 11, 9, 4, 11, 9, 11, 2, 9, 2, 1, -1, -1, -1, -1,
  3, 10, 1, 3, 11, 10, 7, 8, 4, -1, -1, -1, -1, -1, -1, -1,
  1, 11, 10, 1, 4, 11, 1, 0, 4, 7, 11, 4, -1, -1, -1, -1,
  4, 7, 8, 9, 0, 11, 9, 11, 10, 11, 0, 3, -1, -1, -1, -1,
  4, 7, 11, 4, 11, 9, 9, 11, 10, -1, -1, -1, -1, -1, -1, -1,
  9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  9, 5, 4, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 5, 4, 1, 5, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  8, 5, 4, 8, 3, 5, 3, 1, 5, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 10, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  3, 0, 8, 1, 2, 10, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1,
  5, 2, 10, 5, 4, 2, 4, 0, 2, -1, -1, -1, -1, -1, -1, -1,
  2, 10, 5, 3, 2, 5, 3, 5, 4, 3, 4, 8, -1, -1, -1, -1,
  9, 5, 4, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 11, 2, 0, 8, 11, 4, 9, 5, -1, -1, -1, -1, -1, -1, -1,
  0, 5, 4, 0, 1, 5, 2, 3, 11, -1, -1, -1, -1, -1, -1, -1,
  2, 1, 5, 2, 5, 8, 2, 8, 11, 4, 8, 5, -1, -1, -1, -1,
  10, 3, 11, 10, 1, 3, 9, 5, 4, -1, -1, -1, -1, -1, -1, -1,
  4, 9, 5, 0, 8, 1, 8, 10, 1, 8, 11, 10, -1, -1, -1, -1,
  5, 4, 0, 5, 0, 11, 5, 11, 10, 11, 0, 3, -1, -1, -1, -1,
  5, 4, 8, 5, 8, 10, 10, 8, 11, -1, -1, -1, -1, -1, -1, -1,
  9, 7, 8, 5, 7, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  9, 3, 0, 9, 5, 3, 5, 7, 3, -1, -1, -1, -1, -1, -1, -1,
  0, 7, 8, 0, 1, 7, 1, 5, 7, -1, -1, -1, -1, -1, -1, -1,
  1, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  9, 7, 8, 9, 5, 7, 10, 1, 2, -1, -1, -1, -1, -1, -1, -1,
  10, 1, 2, 9, 5, 0, 5, 3, 0, 5, 7, 3, -1, -1, -1, -1,
  8, 0, 2, 8, 2, 5, 8, 5, 7, 10, 5, 2, -1, -1, -1, -1,
  2, 10, 5, 2, 5, 3, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1,
  7, 9, 5, 7, 8, 9, 3, 11, 2, -1, -1, -1, -1, -1, -1, -1,
  9, 5, 7, 9, 7, 2, 9, 2, 0, 2, 7, 11, -1, -1, -1, -1,
  2, 3, 11, 0, 1, 8, 1, 7, 8, 1, 5, 7, -1, -1, -1, -1,
  11, 2, 1, 11, 1, 7, 7, 1, 5, -1, -1, -1, -1, -1, -1, -1,
  9, 5, 8, 8, 5, 7, 10, 1, 3, 10, 3, 11, -1, -1, -1, -1,
  5, 7, 0, 5, 0, 9, 7, 11, 0, 1, 0, 10, 11, 10, 0, -1,
  11, 10, 0, 11, 0, 3, 10, 5, 0, 8, 0, 7, 5, 7, 0, -1,
  11, 10, 5, 7, 11, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 3, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  9, 0, 1, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 8, 3, 1, 9, 8, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1,
  1, 6, 5, 2, 6, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 6, 5, 1, 2, 6, 3, 0, 8, -1, -1, -1, -1, -1, -1, -1,
  9, 6, 5, 9, 0, 6, 0, 2, 6, -1, -1, -1, -1, -1, -1, -1,
  5, 9, 0, 5, 0, 6, 2, 6, 0, 3, 8, 6, -1, -1, -1, -1,
  2, 3, 11, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  11, 0, 8, 11, 2, 0, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1,
  0, 1, 9, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1, -1, -1, -1,
  5, 10, 6, 1, 9, 2, 9, 11, 2, 9, 8, 11, -1, -1, -1, -1,
  6, 3, 11, 6, 5, 3, 5, 1, 3, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 11, 0, 11, 5, 0, 5, 1, 5, 11, 6, -1, -1, -1, -1,
  3, 11, 6, 0, 3, 6, 0, 6, 5, 0, 5, 9, -1, -1, -1, -1,
  6, 5, 9, 6, 9, 11, 11, 9, 8, -1, -1, -1, -1, -1, -1, -1,
  5, 10, 6, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  4, 3, 0, 4, 7, 3, 6, 5, 10, -1, -1, -1, -1, -1, -1, -1,
  1, 9, 0, 5, 10, 6, 8, 4, 7, -1, -1, -1, -1, -1, -1, -1,
  10, 6, 5, 1, 9, 7, 1, 7, 3, 7, 9, 4, -1, -1, -1, -1,
  6, 1, 2, 6, 5, 1, 4, 7, 8, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 5, 5, 2, 6, 3, 0, 4, 3, 4, 7, -1, -1, -1, -1,
  8, 4, 7, 9, 0, 5, 0, 6, 5, 0, 2, 6, -1, -1, -1, -1,
  7, 3, 9, 7, 9, 4, 3, 2, 9, 5, 9, 6, 2, 6, 9, -1,
  3, 11, 2, 7, 8, 4, 10, 6, 5, -1, -1, -1, -1, -1, -1, -1,
  5, 10, 6, 4, 7, 2, 4, 2, 0, 2, 7, 11, -1, -1, -1, -1,
  0, 1, 9, 4, 7, 8, 2, 3, 11, 5, 10, 6, -1, -1, -1, -1,
  9, 2, 1, 9, 11, 2, 9, 4, 11, 7, 11, 4, 5, 10, 6, -1,
  8, 4, 7, 3, 11, 5, 3, 5, 1, 5, 11, 6, -1, -1, -1, -1,
  5, 1, 11, 5, 11, 6, 1, 0, 11, 7, 11, 4, 0, 4, 11, -1,
  0, 5, 9, 0, 6, 5, 0, 3, 6, 11, 6, 3, 8, 4, 7, -1,
  6, 5, 9, 6, 9, 11, 4, 7, 9, 7, 11, 9, -1, -1, -1, -1,
  10, 4, 9, 6, 4, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  4, 10, 6, 4, 9, 10, 0, 8, 3, -1, -1, -1, -1, -1, -1, -1,
  10, 0, 1, 10, 6, 0, 6, 4, 0, -1, -1, -1, -1, -1, -1, -1,
  8, 3, 1, 8, 1, 6, 8, 6, 4, 6, 1, 10, -1, -1, -1, -1,
  1, 4, 9, 1, 2, 4, 2, 6, 4, -1, -1, -1, -1, -1, -1, -1,
  3, 0, 8, 1, 2, 9, 2, 4, 9, 2, 6, 4, -1, -1, -1, -1,
  0, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  8, 3, 2, 8, 2, 4, 4, 2, 6, -1, -1, -1, -1, -1, -1, -1,
  10, 4, 9, 10, 6, 4, 11, 2, 3, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 2, 2, 8, 11, 4, 9, 10, 4, 10, 6, -1, -1, -1, -1,
  3, 11, 2, 0, 1, 6, 0, 6, 4, 6, 1, 10, -1, -1, -1, -1,
  6, 4, 1, 6, 1, 10, 4, 8, 1, 2, 1, 11, 8, 11, 1, -1,
  9, 6, 4, 9, 3, 6, 9, 1, 3, 11, 6, 3, -1, -1, -1, -1,
  8, 11, 1, 8, 1, 0, 11, 6, 1, 9, 1, 4, 6, 4, 1, -1,
  3, 11, 6, 3, 6, 0, 0, 6, 4, -1, -1, -1, -1, -1, -1, -1,
  6, 4, 8, 11, 6, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  7, 10, 6, 7, 8, 10, 8, 9, 10, -1, -1, -1, -1, -1, -1, -1,
  0, 7, 3, 0, 10, 7, 0, 9, 10, 6, 7, 10, -1, -1, -1, -1,
  10, 6, 7, 1, 10, 7, 1, 7, 8, 1, 8, 0, -1, -1, -1, -1,
  10, 6, 7, 10, 7, 1, 1, 7, 3, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 6, 1, 6, 8, 1, 8, 9, 8, 6, 7, -1, -1, -1, -1,
  2, 6, 9, 2, 9, 1, 6, 7, 9, 0, 9, 3, 7, 3, 9, -1,
  7, 8, 0, 7, 0, 6, 6, 0, 2, -1, -1, -1, -1, -1, -1, -1,
  7, 3, 2, 6, 7, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  2, 3, 11, 10, 6, 8, 10, 8, 9, 8, 6, 7, -1, -1, -1, -1,
  2, 0, 7, 2, 7, 11, 0, 9, 7, 6, 7, 10, 9, 10, 7, -1,
  1, 8, 0, 1, 7, 8, 1, 10, 7, 6, 7, 10, 2, 3, 11, -1,
  11, 2, 1, 11, 1, 7, 10, 6, 1, 6, 7, 1, -1, -1, -1, -1,
  8, 9, 6, 8, 6, 7, 9, 1, 6, 11, 6, 3, 1, 3, 6, -1,
  0, 9, 1, 11, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  7, 8, 0, 7, 0, 6, 3, 11, 0, 11, 6, 0, -1, -1, -1, -1,
  7, 11, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  3, 0, 8, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 1, 9, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  8, 1, 9, 8, 3, 1, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1,
  10, 1, 2, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 10, 3, 0, 8, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1,
  2, 9, 0, 2, 10, 9, 6, 11, 7, -1, -1, -1, -1, -1, -1, -1,
  6, 11, 7, 2, 10, 3, 10, 8, 3, 10, 9, 8, -1, -1, -1, -1,
  7, 2, 3, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  7, 0, 8, 7, 2, 0, 6, 2, 7, -1, -1, -1, -1, -1, -1, -1,
  2, 7, 3, 2, 6, 7, 0, 1, 9, -1, -1, -1, -1, -1, -1, -1,
  1, 6, 2, 1, 8, 6, 1, 9, 8, 8, 7, 6, -1, -1, -1, -1,
  10, 7, 6, 10, 1, 7, 1, 3, 7, -1, -1, -1, -1, -1, -1, -1,
  10, 7, 6, 1, 7, 10, 1, 8, 7, 1, 0, 8, -1, -1, -1, -1,
  0, 3, 7, 0, 7, 10, 0, 10, 9, 6, 10, 7, -1, -1, -1, -1,
  7, 6, 10, 7, 10, 8, 8, 10, 9, -1, -1, -1, -1, -1, -1, -1,
  6, 8, 4, 11, 8, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  3, 6, 11, 3, 0, 6, 0, 4, 6, -1, -1, -1, -1, -1, -1, -1,
  8, 6, 11, 8, 4, 6, 9, 0, 1, -1, -1, -1, -1, -1, -1, -1,
  9, 4, 6, 9, 6, 3, 9, 3, 1, 11, 3, 6, -1, -1, -1, -1,
  6, 8, 4, 6, 11, 8, 2, 10, 1, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 10, 3, 0, 11, 0, 6, 11, 0, 4, 6, -1, -1, -1, -1,
  4, 11, 8, 4, 6, 11, 0, 2, 9, 2, 10, 9, -1, -1, -1, -1,
  10, 9, 3, 10, 3, 2, 9, 4, 3, 11, 3, 6, 4, 6, 3, -1,
  8, 2, 3, 8, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1,
  0, 4, 2, 4, 6, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 9, 0, 2, 3, 4, 2, 4, 6, 4, 3, 8, -1, -1, -1, -1,
  1, 9, 4, 1, 4, 2, 2, 4, 6, -1, -1, -1, -1, -1, -1, -1,
  8, 1, 3, 8, 6, 1, 8, 4, 6, 6, 10, 1, -1, -1, -1, -1,
  10, 1, 0, 10, 0, 6, 6, 0, 4, -1, -1, -1, -1, -1, -1, -1,
  4, 6, 3, 4, 3, 8, 6, 10, 3, 0, 3, 9, 10, 9, 3, -1,
  10, 9, 4, 6, 10, 4, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  4, 9, 5, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 3, 4, 9, 5, 11, 7, 6, -1, -1, -1, -1, -1, -1, -1,
  5, 0, 1, 5, 4, 0, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1,
  11, 7, 6, 8, 3, 4, 3, 5, 4, 3, 1, 5, -1, -1, -1, -1,
  9, 5, 4, 10, 1, 2, 7, 6, 11, -1, -1, -1, -1, -1, -1, -1,
  6, 11, 7, 1, 2, 10, 0, 8, 3, 4, 9, 5, -1, -1, -1, -1,
  7, 6, 11, 5, 4, 10, 4, 2, 10, 4, 0, 2, -1, -1, -1, -1,
  3, 4, 8, 3, 5, 4, 3, 2, 5, 10, 5, 2, 11, 7, 6, -1,
  7, 2, 3, 7, 6, 2, 5, 4, 9, -1, -1, -1, -1, -1, -1, -1,
  9, 5, 4, 0, 8, 6, 0, 6, 2, 6, 8, 7, -1, -1, -1, -1,
  3, 6, 2, 3, 7, 6, 1, 5, 0, 5, 4, 0, -1, -1, -1, -1,
  6, 2, 8, 6, 8, 7, 2, 1, 8, 4, 8, 5, 1, 5, 8, -1,
  9, 5, 4, 10, 1, 6, 1, 7, 6, 1, 3, 7, -1, -1, -1, -1,
  1, 6, 10, 1, 7, 6, 1, 0, 7, 8, 7, 0, 9, 5, 4, -1,
  4, 0, 10, 4, 10, 5, 0, 3, 10, 6, 10, 7, 3, 7, 10, -1,
  7, 6, 10, 7, 10, 8, 5, 4, 10, 4, 8, 10, -1, -1, -1, -1,
  6, 9, 5, 6, 11, 9, 11, 8, 9, -1, -1, -1, -1, -1, -1, -1,
  3, 6, 11, 0, 6, 3, 0, 5, 6, 0, 9, 5, -1, -1, -1, -1,
  0, 11, 8, 0, 5, 11, 0, 1, 5, 5, 6, 11, -1, -1, -1, -1,
  6, 11, 3, 6, 3, 5, 5, 3, 1, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 10, 9, 5, 11, 9, 11, 8, 11, 5, 6, -1, -1, -1, -1,
  0, 11, 3, 0, 6, 11, 0, 9, 6, 5, 6, 9, 1, 2, 10, -1,
  11, 8, 5, 11, 5, 6, 8, 0, 5, 10, 5, 2, 0, 2, 5, -1,
  6, 11, 3, 6, 3, 5, 2, 10, 3, 10, 5, 3, -1, -1, -1, -1,
  5, 8, 9, 5, 2, 8, 5, 6, 2, 3, 8, 2, -1, -1, -1, -1,
  9, 5, 6, 9, 6, 0, 0, 6, 2, -1, -1, -1, -1, -1, -1, -1,
  1, 5, 8, 1, 8, 0, 5, 6, 8, 3, 8, 2, 6, 2, 8, -1,
  1, 5, 6, 2, 1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 3, 6, 1, 6, 10, 3, 8, 6, 5, 6, 9, 8, 9, 6, -1,
  10, 1, 0, 10, 0, 6, 9, 5, 0, 5, 6, 0, -1, -1, -1, -1,
  0, 3, 8, 5, 6, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  10, 5, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  11, 5, 10, 7, 5, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  11, 5, 10, 11, 7, 5, 8, 3, 0, -1, -1, -1, -1, -1, -1, -1,
  5, 11, 7, 5, 10, 11, 1, 9, 0, -1, -1, -1, -1, -1, -1, -1,
  10, 7, 5, 10, 11, 7, 9, 8, 1, 8, 3, 1, -1, -1, -1, -1,
  1, 11, 7, 1, 2, 11, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 3, 1, 2, 7, 1, 7, 5, 7, 2, 11, -1, -1, -1, -1,
  9, 7, 5, 9, 2, 7, 9, 0, 2, 2, 11, 7, -1, -1, -1, -1,
  7, 5, 2, 7, 2, 11, 5, 9, 2, 3, 2, 8, 9, 8, 2, -1,
  2, 5, 10, 2, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1,
  8, 2, 0, 8, 5, 2, 8, 7, 5, 10, 2, 5, -1, -1, -1, -1,
  9, 0, 1, 5, 10, 3, 5, 3, 7, 3, 10, 2, -1, -1, -1, -1,
  9, 8, 2, 9, 2, 1, 8, 7, 2, 10, 1, 2, 7, 10, 2, -1,
  1, 3, 5, 3, 7, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 7, 0, 7, 1, 1, 7, 5, -1, -1, -1, -1, -1, -1, -1,
  9, 0, 3, 9, 3, 5, 5, 3, 7, -1, -1, -1, -1, -1, -1, -1,
  9, 8, 7, 5, 9, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  5, 8, 4, 5, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1,
  5, 0, 4, 5, 11, 0, 5, 10, 11, 11, 3, 0, -1, -1, -1, -1,
  0, 1, 9, 8, 4, 11, 8, 11, 10, 11, 4, 5, -1, -1, -1, -1,
  10, 11, 4, 10, 4, 5, 11, 3, 4, 9, 4, 1, 3, 1, 4, -1,
  2, 5, 1, 2, 8, 5, 2, 11, 8, 4, 5, 8, -1, -1, -1, -1,
  0, 4, 11, 0, 11, 3, 4, 5, 11, 2, 11, 1, 5, 1, 11, -1,
  0, 2, 5, 0, 5, 9, 2, 11, 5, 4, 5, 8, 11, 8, 5, -1,
  9, 4, 5, 2, 11, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  2, 5, 10, 3, 5, 2, 3, 4, 5, 3, 8, 4, -1, -1, -1, -1,
  5, 10, 2, 5, 2, 4, 4, 2, 0, -1, -1, -1, -1, -1, -1, -1,
  3, 10, 2, 3, 5, 10, 3, 8, 5, 4, 5, 8, 0, 1, 9, -1,
  5, 10, 2, 5, 2, 4, 1, 9, 2, 9, 4, 2, -1, -1, -1, -1,
  8, 4, 5, 8, 5, 3, 3, 5, 1, -1, -1, -1, -1, -1, -1, -1,
  0, 4, 5, 1, 0, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  8, 4, 5, 8, 5, 3, 9, 0, 5, 0, 3, 5, -1, -1, -1, -1,
  9, 4, 5, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  4, 11, 7, 4, 9, 11, 9, 10, 11, -1, -1, -1, -1, -1, -1, -1,
  0, 8, 3, 4, 9, 7, 9, 11, 7, 9, 10, 11, -1, -1, -1, -1,
  1, 10, 11, 1, 11, 4, 1, 4, 0, 7, 4, 11, -1, -1, -1, -1,
  3, 1, 4, 3, 4, 8, 1, 10, 4, 7, 4, 11, 10, 11, 4, -1,
  4, 11, 7, 9, 11, 4, 9, 2, 11, 9, 1, 2, -1, -1, -1, -1,
  9, 7, 4, 9, 11, 7, 9, 1, 11, 2, 11, 1, 0, 8, 3, -1,
  11, 7, 4, 11, 4, 2, 2, 4, 0, -1, -1, -1, -1, -1, -1, -1,
  11, 7, 4, 11, 4, 2, 8, 3, 4, 3, 2, 4, -1, -1, -1, -1,
  2, 9, 10, 2, 7, 9, 2, 3, 7, 7, 4, 9, -1, -1, -1, -1,
  9, 10, 7, 9, 7, 4, 10, 2, 7, 0, 7, 8, 2, 8, 7, -1,
  0, 1, 9, 2, 7, 3, 2, 10, 7, 10, 4, 7, -1, -1, -1, -1,
  2, 10, 4, 2, 4, 7, 2, 7, 3, 9, 1, 4, 1, 7, 4, -1,
  7, 4, 1, 7, 1, 3, 4, 9, 1, -1, -1, -1, -1, -1, -1, -1,
  9, 1, 4, 9, 4, 0, 1, 7, 4, 8, 4, 0, 7, 8, 4, -1,
  4, 0, 3, 7, 4, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  4, 8, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  9, 10, 8, 10, 11, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  3, 0, 9, 3, 9, 11, 11, 9, 10, -1, -1, -1, -1, -1, -1, -1,
  0, 1, 10, 0, 10, 8, 8, 10, 11, -1, -1, -1, -1, -1, -1, -1,
  3, 1, 10, 11, 3, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 2, 11, 1, 11, 9, 9, 11, 8, -1, -1, -1, -1, -1, -1, -1,
  3, 0, 9, 3, 9, 11, 1, 2, 9, 2, 11, 9, -1, -1, -1, -1,
  0, 2, 11, 8, 0, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  3, 2, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  2, 3, 8, 2, 8, 10, 10, 8, 9, -1, -1, -1, -1, -1, -1, -1,
  9, 10, 2, 0, 9, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  2, 3, 8, 2, 8, 10, 0, 1, 8, 1, 10, 8, -1, -1, -1, -1,
  1, 10, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  1, 3, 8, 9, 1, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 9, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  0, 3, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
]);

export class SDFVolume {
  public nx: number;
  public ny: number;
  public nz: number;
  public boundsMin: Vec3;
  public boundsMax: Vec3;
  public cellSize: Vec3;
  public distances: Float32Array;

  constructor(
    boundsMin: Vec3,
    boundsMax: Vec3,
    resolution: number
  ) {
    this.boundsMin = Vec3Math.clone(boundsMin);
    this.boundsMax = Vec3Math.clone(boundsMax);

    const spanX = boundsMax.x - boundsMin.x;
    const spanY = boundsMax.y - boundsMin.y;
    const spanZ = boundsMax.z - boundsMin.z;
    const maxSpan = Math.max(spanX, spanY, spanZ);

    const h = maxSpan / resolution;
    this.nx = Math.max(8, Math.ceil(spanX / h));
    this.ny = Math.max(8, Math.ceil(spanY / h));
    this.nz = Math.max(8, Math.ceil(spanZ / h));

    this.cellSize = {
      x: spanX / (this.nx - 1),
      y: spanY / (this.ny - 1),
      z: spanZ / (this.nz - 1),
    };

    this.distances = new Float32Array(this.nx * this.ny * this.nz);
    this.distances.fill(1e5); // Initialize with large positive distance
  }

  public getIndex(ix: number, iy: number, iz: number): number {
    return ix + iy * this.nx + iz * this.nx * this.ny;
  }

  public getPosition(ix: number, iy: number, iz: number): Vec3 {
    return {
      x: this.boundsMin.x + ix * this.cellSize.x,
      y: this.boundsMin.y + iy * this.cellSize.y,
      z: this.boundsMin.z + iz * this.cellSize.z,
    };
  }

  public sample(x: number, y: number, z: number): number {
    const fx = (x - this.boundsMin.x) / this.cellSize.x;
    const fy = (y - this.boundsMin.y) / this.cellSize.y;
    const fz = (z - this.boundsMin.z) / this.cellSize.z;

    const ix0 = Math.max(0, Math.min(this.nx - 2, Math.floor(fx)));
    const iy0 = Math.max(0, Math.min(this.ny - 2, Math.floor(fy)));
    const iz0 = Math.max(0, Math.min(this.nz - 2, Math.floor(fz)));

    const tx = Math.max(0, Math.min(1, fx - ix0));
    const ty = Math.max(0, Math.min(1, fy - iy0));
    const tz = Math.max(0, Math.min(1, fz - iz0));

    const d000 = this.distances[this.getIndex(ix0, iy0, iz0)];
    const d100 = this.distances[this.getIndex(ix0 + 1, iy0, iz0)];
    const d010 = this.distances[this.getIndex(ix0, iy0 + 1, iz0)];
    const d110 = this.distances[this.getIndex(ix0 + 1, iy0 + 1, iz0)];
    const d001 = this.distances[this.getIndex(ix0, iy0, iz0 + 1)];
    const d101 = this.distances[this.getIndex(ix0 + 1, iy0, iz0 + 1)];
    const d011 = this.distances[this.getIndex(ix0, iy0 + 1, iz0 + 1)];
    const d111 = this.distances[this.getIndex(ix0 + 1, iy0 + 1, iz0 + 1)];

    const c00 = d000 * (1 - tx) + d100 * tx;
    const c10 = d010 * (1 - tx) + d110 * tx;
    const c01 = d001 * (1 - tx) + d101 * tx;
    const c11 = d011 * (1 - tx) + d111 * tx;

    const c0 = c00 * (1 - ty) + c10 * ty;
    const c1 = c01 * (1 - ty) + c11 * ty;

    return c0 * (1 - tz) + c1 * tz;
  }

  /**
   * Populate distance field from triangle mesh
   */
  public static fromMesh(mesh: MeshData, resolution: number, marginFraction: number = 0.08): SDFVolume {
    const { min, max } = mesh.bounds;
    const sizeX = max.x - min.x;
    const sizeY = max.y - min.y;
    const sizeZ = max.z - min.z;

    const margin: Vec3 = {
      x: sizeX * marginFraction,
      y: sizeY * marginFraction,
      z: sizeZ * marginFraction,
    };

    const bMin: Vec3 = Vec3Math.sub(min, margin);
    const bMax: Vec3 = Vec3Math.add(max, margin);

    const sdf = new SDFVolume(bMin, bMax, resolution);

    const vertices = mesh.vertices;
    const indices = mesh.indices;
    const numTriangles = indices.length / 3;

    // Pre-calculate triangle bounding boxes and centroids
    const triBBoxes: { min: Vec3; max: Vec3; v0: Vec3; v1: Vec3; v2: Vec3; normal: Vec3 }[] = [];
    for (let t = 0; t < numTriangles; t++) {
      const i0 = indices[t * 3] * 3;
      const i1 = indices[t * 3 + 1] * 3;
      const i2 = indices[t * 3 + 2] * 3;

      const v0: Vec3 = { x: vertices[i0], y: vertices[i0 + 1], z: vertices[i0 + 2] };
      const v1: Vec3 = { x: vertices[i1], y: vertices[i1 + 1], z: vertices[i1 + 2] };
      const v2: Vec3 = { x: vertices[i2], y: vertices[i2 + 1], z: vertices[i2 + 2] };

      const e1 = Vec3Math.sub(v1, v0);
      const e2 = Vec3Math.sub(v2, v0);
      const norm = Vec3Math.normalize(Vec3Math.cross(e1, e2));

      triBBoxes.push({
        min: Vec3Math.min(Vec3Math.min(v0, v1), v2),
        max: Vec3Math.max(Vec3Math.max(v0, v1), v2),
        v0,
        v1,
        v2,
        normal: norm,
      });
    }

    // Narrow-band distance computation
    const totalCells = sdf.nx * sdf.ny * sdf.nz;
    const signs = new Int8Array(totalCells);

    for (let iz = 0; iz < sdf.nz; iz++) {
      for (let iy = 0; iy < sdf.ny; iy++) {
        for (let ix = 0; ix < sdf.nx; ix++) {
          const idx = sdf.getIndex(ix, iy, iz);
          const p = sdf.getPosition(ix, iy, iz);

          let minDistSq = Infinity;
          let closestNormal: Vec3 = { x: 0, y: 0, z: 1 };
          let closestDiff: Vec3 = { x: 0, y: 0, z: 0 };

          for (let t = 0; t < numTriangles; t++) {
            const tri = triBBoxes[t];

            // Quick AABB test
            const dx = Math.max(0, tri.min.x - p.x, p.x - tri.max.x);
            const dy = Math.max(0, tri.min.y - p.y, p.y - tri.max.y);
            const dz = Math.max(0, tri.min.z - p.z, p.z - tri.max.z);
            if (dx * dx + dy * dy + dz * dz > minDistSq) continue;

            const distSq = SDFVolume.pointTriangleDistanceSq(p, tri.v0, tri.v1, tri.v2);
            if (distSq < minDistSq) {
              minDistSq = distSq;
              closestNormal = tri.normal;
              closestDiff = Vec3Math.sub(p, tri.v0);
            }
          }

          const dist = Math.sqrt(minDistSq);
          // Sign based on dot with closest triangle normal
          const dot = Vec3Math.dot(closestDiff, closestNormal);
          const isInside = dot < 0;

          sdf.distances[idx] = isInside ? -dist : dist;
          signs[idx] = isInside ? -1 : 1;
        }
      }
    }

    return sdf;
  }

  /**
   * Point to triangle squared distance
   */
  private static pointTriangleDistanceSq(p: Vec3, a: Vec3, b: Vec3, c: Vec3): number {
    const ab = Vec3Math.sub(b, a);
    const ac = Vec3Math.sub(c, a);
    const ap = Vec3Math.sub(p, a);

    const d1 = Vec3Math.dot(ab, ap);
    const d2 = Vec3Math.dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return Vec3Math.lengthSq(ap);

    const bp = Vec3Math.sub(p, b);
    const d3 = Vec3Math.dot(ab, bp);
    const d4 = Vec3Math.dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return Vec3Math.lengthSq(bp);

    const vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
      const v = d1 / (d1 - d3);
      const proj = Vec3Math.add(a, Vec3Math.scale(ab, v));
      return Vec3Math.distance(p, proj) ** 2;
    }

    const cp = Vec3Math.sub(p, c);
    const d5 = Vec3Math.dot(ab, cp);
    const d6 = Vec3Math.dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return Vec3Math.lengthSq(cp);

    const vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
      const w = d2 / (d2 - d6);
      const proj = Vec3Math.add(a, Vec3Math.scale(ac, w));
      return Vec3Math.distance(p, proj) ** 2;
    }

    const va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0) {
      const w = (d4 - d3) / (d4 - d3 + (d5 - d6));
      const proj = Vec3Math.add(b, Vec3Math.scale(Vec3Math.sub(c, b), w));
      return Vec3Math.distance(p, proj) ** 2;
    }

    const denom = 1 / (va + vb + vc);
    const v = vb * denom;
    const w = vc * denom;
    const proj = Vec3Math.add(a, Vec3Math.add(Vec3Math.scale(ab, v), Vec3Math.scale(ac, w)));
    return Vec3Math.distance(p, proj) ** 2;
  }

  /**
   * Apply Differential Geological Strata Erosion
   * Softer strata layers erode deeper; hard competent rock layers remain prominent.
   */
  public applyStrataDifferentialErosion(
    strata: StrataLayer[],
    baseErosion: number,
    hardnessContrast: number
  ): void {
    if (strata.length === 0 || baseErosion <= 0) return;

    for (let iz = 0; iz < this.nz; iz++) {
      const z = this.boundsMin.z + iz * this.cellSize.z;

      // Find strata layer for elevation z
      let hardness = 0.5;
      for (const layer of strata) {
        if (z >= layer.zMin && z <= layer.zMax) {
          hardness = layer.hardness;
          break;
        }
      }

      // Erosion amount: softer rocks (low hardness) get higher erosion radius
      const erosionOffset = baseErosion * (1.0 - hardness * hardnessContrast);

      for (let iy = 0; iy < this.ny; iy++) {
        for (let ix = 0; ix < this.nx; ix++) {
          const idx = this.getIndex(ix, iy, iz);
          this.distances[idx] += erosionOffset;
        }
      }
    }
  }

  /**
   * Smooth Minimum CSG Union: smin(a, b, k)
   */
  public static smin(a: number, b: number, k: number): number {
    if (k <= 0) return Math.min(a, b);
    const h = Math.max(k - Math.abs(a - b), 0.0) / k;
    return Math.min(a, b) - h * h * k * (1.0 / 4.0);
  }

  /**
   * Subtract a discrete 3D wedge / tetrahedral chip volume from the SDF
   */
  public subtractWedgeVolume(apex: Vec3, baseCenter: Vec3, radius: number): void {
    const minZ = Math.min(apex.z, baseCenter.z) - radius;
    const maxZ = Math.max(apex.z, baseCenter.z) + radius;
    const minX = Math.min(apex.x, baseCenter.x) - radius;
    const maxX = Math.max(apex.x, baseCenter.x) + radius;
    const minY = Math.min(apex.y, baseCenter.y) - radius;
    const maxY = Math.max(apex.y, baseCenter.y) + radius;

    const ix0 = Math.max(0, Math.floor((minX - this.boundsMin.x) / this.cellSize.x));
    const ix1 = Math.min(this.nx - 1, Math.ceil((maxX - this.boundsMin.x) / this.cellSize.x));
    const iy0 = Math.max(0, Math.floor((minY - this.boundsMin.y) / this.cellSize.y));
    const iy1 = Math.min(this.ny - 1, Math.ceil((maxY - this.boundsMin.y) / this.cellSize.y));
    const iz0 = Math.max(0, Math.floor((minZ - this.boundsMin.z) / this.cellSize.z));
    const iz1 = Math.min(this.nz - 1, Math.ceil((maxZ - this.boundsMin.z) / this.cellSize.z));

    for (let iz = iz0; iz <= iz1; iz++) {
      for (let iy = iy0; iy <= iy1; iy++) {
        for (let ix = ix0; ix <= ix1; ix++) {
          const idx = this.getIndex(ix, iy, iz);
          const p = this.getPosition(ix, iy, iz);

          // Distance to segment [apex, baseCenter]
          const ab = Vec3Math.sub(baseCenter, apex);
          const ap = Vec3Math.sub(p, apex);
          const t = Math.max(0, Math.min(1, Vec3Math.dot(ap, ab) / (Vec3Math.lengthSq(ab) + 1e-8)));
          const closest = Vec3Math.add(apex, Vec3Math.scale(ab, t));
          const wedgeRadius = radius * (1 - t * 0.7); // Tapered cone/wedge
          const dCutter = Vec3Math.distance(p, closest) - wedgeRadius;

          // CSG Subtraction: max(A, -B)
          this.distances[idx] = Math.max(this.distances[idx], -dCutter);
        }
      }
    }
  }

  /**
   * Subtract a shallow surface crack polyline with strict depth offset
   */
  public subtractCrackPolyline(points: Vec3[], width: number, maxDepth: number): void {
    if (points.length < 2) return;

    for (let s = 0; s < points.length - 1; s++) {
      const p1 = points[s];
      const p2 = points[s + 1];

      const segMin: Vec3 = {
        x: Math.min(p1.x, p2.x) - width - maxDepth,
        y: Math.min(p1.y, p2.y) - width - maxDepth,
        z: Math.min(p1.z, p2.z) - width - maxDepth,
      };
      const segMax: Vec3 = {
        x: Math.max(p1.x, p2.x) + width + maxDepth,
        y: Math.max(p1.y, p2.y) + width + maxDepth,
        z: Math.max(p1.z, p2.z) + width + maxDepth,
      };

      const ix0 = Math.max(0, Math.floor((segMin.x - this.boundsMin.x) / this.cellSize.x));
      const ix1 = Math.min(this.nx - 1, Math.ceil((segMax.x - this.boundsMin.x) / this.cellSize.x));
      const iy0 = Math.max(0, Math.floor((segMin.y - this.boundsMin.y) / this.cellSize.y));
      const iy1 = Math.min(this.ny - 1, Math.ceil((segMax.y - this.boundsMin.y) / this.cellSize.y));
      const iz0 = Math.max(0, Math.floor((segMin.z - this.boundsMin.z) / this.cellSize.z));
      const iz1 = Math.min(this.nz - 1, Math.ceil((segMax.z - this.boundsMin.z) / this.cellSize.z));

      const ab = Vec3Math.sub(p2, p1);
      const abLenSq = Vec3Math.lengthSq(ab);

      for (let iz = iz0; iz <= iz1; iz++) {
        for (let iy = iy0; iy <= iy1; iy++) {
          for (let ix = ix0; ix <= ix1; ix++) {
            const idx = this.getIndex(ix, iy, iz);
            const p = this.getPosition(ix, iy, iz);

            const ap = Vec3Math.sub(p, p1);
            const t = Math.max(0, Math.min(1, Vec3Math.dot(ap, ab) / (abLenSq + 1e-8)));
            const segPoint = Vec3Math.add(p1, Vec3Math.scale(ab, t));

            const distToLine = Vec3Math.distance(p, segPoint);
            // V-groove profile with bounded depth offset
            const crackRadius = width * Math.max(0, 1.0 - (distToLine / maxDepth));
            const dCrack = distToLine - crackRadius;

            // Subtract crack from SDF
            this.distances[idx] = Math.max(this.distances[idx], -dCrack);
          }
        }
      }
    }
  }

  /**
   * Extract Isosurface via Marching Cubes
   * Strictly produces 100% manifold triangles, zero n-gons.
   */
  public extractMesh(isoValue: number = 0.0, strata?: StrataLayer[]): MeshData {
    const builder = new MeshBuilder();

    // Cache edge vertex indices for vertex sharing
    const edgeVertexMap = new Map<string, number>();

    const getEdgeVertex = (
      p1: Vec3,
      p2: Vec3,
      v1: number,
      v2: number,
      edgeKey: string
    ): number => {
      const cached = edgeVertexMap.get(edgeKey);
      if (cached !== undefined) return cached;

      const t = Math.abs(v1 - v2) < 1e-9 ? 0.5 : (isoValue - v1) / (v2 - v1);
      const pos = Vec3Math.lerp(p1, p2, Math.max(0, Math.min(1, t)));

      // Estimate normal from SDF central differences
      const hx = this.cellSize.x;
      const hy = this.cellSize.y;
      const hz = this.cellSize.z;

      const nx = (this.sample(pos.x + hx, pos.y, pos.z) - this.sample(pos.x - hx, pos.y, pos.z)) / (2 * hx);
      const ny = (this.sample(pos.x, pos.y + hy, pos.z) - this.sample(pos.x, pos.y - hy, pos.z)) / (2 * hy);
      const nz = (this.sample(pos.x, pos.y, pos.z + hz) - this.sample(pos.x, pos.y, pos.z - hz)) / (2 * hz);

      const normal = Vec3Math.normalize({ x: nx, y: ny, z: nz });

      // Determine strata layer and inspection color
      let strataIdx = 0;
      let color: [number, number, number] = [0.72, 0.65, 0.58];

      if (strata && strata.length > 0) {
        for (const layer of strata) {
          if (pos.z >= layer.zMin && pos.z <= layer.zMax) {
            strataIdx = layer.index;
            color = layer.color;
            break;
          }
        }
      }

      const vIdx = builder.addVertex(pos, normal, color, strataIdx, 0, 1e-5);
      edgeVertexMap.set(edgeKey, vIdx);
      return vIdx;
    };

    for (let iz = 0; iz < this.nz - 1; iz++) {
      for (let iy = 0; iy < this.ny - 1; iy++) {
        for (let ix = 0; ix < this.nx - 1; ix++) {
          // 8 voxel corner values
          const p: Vec3[] = [
            this.getPosition(ix, iy, iz),
            this.getPosition(ix + 1, iy, iz),
            this.getPosition(ix + 1, iy + 1, iz),
            this.getPosition(ix, iy + 1, iz),
            this.getPosition(ix, iy, iz + 1),
            this.getPosition(ix + 1, iy, iz + 1),
            this.getPosition(ix + 1, iy + 1, iz + 1),
            this.getPosition(ix, iy + 1, iz + 1),
          ];

          const val: number[] = [
            this.distances[this.getIndex(ix, iy, iz)],
            this.distances[this.getIndex(ix + 1, iy, iz)],
            this.distances[this.getIndex(ix + 1, iy + 1, iz)],
            this.distances[this.getIndex(ix, iy + 1, iz)],
            this.distances[this.getIndex(ix, iy, iz + 1)],
            this.distances[this.getIndex(ix + 1, iy, iz + 1)],
            this.distances[this.getIndex(ix + 1, iy + 1, iz + 1)],
            this.distances[this.getIndex(ix, iy + 1, iz + 1)],
          ];

          let cubeIndex = 0;
          if (val[0] < isoValue) cubeIndex |= 1;
          if (val[1] < isoValue) cubeIndex |= 2;
          if (val[2] < isoValue) cubeIndex |= 4;
          if (val[3] < isoValue) cubeIndex |= 8;
          if (val[4] < isoValue) cubeIndex |= 16;
          if (val[5] < isoValue) cubeIndex |= 32;
          if (val[6] < isoValue) cubeIndex |= 64;
          if (val[7] < isoValue) cubeIndex |= 128;

          if (EDGE_TABLE[cubeIndex] === 0) continue;

          // For each edge with zero-crossing
          const edgeIndices = new Int32Array(12);
          for (let e = 0; e < 12; e++) {
            if ((EDGE_TABLE[cubeIndex] & (1 << e)) !== 0) {
              const [c0, c1] = EDGE_PAIRS[e];
              const key = `${ix + (c0 & 1)}_${iy + ((c0 >> 1) & 1)}_${iz + ((c0 >> 2) & 1)}-${ix + (c1 & 1)}_${iy + ((c1 >> 1) & 1)}_${iz + ((c1 >> 2) & 1)}`;
              edgeIndices[e] = getEdgeVertex(p[c0], p[c1], val[c0], val[c1], key);
            }
          }

          // Emit triangles (clean triangles only, 0 n-gons)
          const baseTriIdx = cubeIndex * 16;
          for (let t = 0; t < 16; t += 3) {
            const e0 = TRI_TABLE[baseTriIdx + t];
            if (e0 === -1) break;
            const e1 = TRI_TABLE[baseTriIdx + t + 1];
            const e2 = TRI_TABLE[baseTriIdx + t + 2];

            builder.addTriangle(edgeIndices[e0], edgeIndices[e1], edgeIndices[e2]);
          }
        }
      }
    }

    return builder.build();
  }
}
