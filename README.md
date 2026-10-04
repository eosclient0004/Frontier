# Geological Cliff & Rock Formation Generator

A procedural geological rock formation engine combining **discrete structural geology**, **bounded polyhedral planar carving**, **tectonic joint dilation**, and **Signed Distance Field (SDF) differential erosion**.

---

## 🏛️ Strict Constraint Compliance

1. **Zero Noise Generators**
   - **0%** Perlin, Simplex, Worley, Voronoi, FBM, value noise, curl noise, domain warping, random displacement textures, or disguised noise functions.
   - **No heightmap noise** at any stage.
   - Deterministic seeded PRNG selects and arranges authored geological primitives only (lithology layer thicknesses, joint dip/strike angles, cutter spatial bounds).

2. **Geological Massif Formation**
   - The generated mesh reads as a large-scale geological cliff (canyon wall, granite batholith scarp, columnar basalt tier, alpine fault scarp, coastal bluff), not a collection of distorted primitives or scattered rocks.

3. **Zero N-Gons & Manifold Topology**
   - Geometry contains **only strictly well-formed triangles**.
   - No non-manifold edges, flipped normals, zero-area faces, or high aspect-ratio slivers.

4. **Untextured Geometry Silhouette Quality**
   - The untextured silhouette, strata steps, planar shear facets, edge chips, and crack fissures look realistic in clay/matcap mode with 0 texture maps.

---

## 🔬 5-Stage Geological Pipeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ 1. Macro Cliff Base Shape                                                   │
│    - Stacked lithological strata (sandstone, siltstone, limestone, shale)    │
│    - Stepped scarp terraces, vertical buttresses, caprock overhang, talus    │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 2. Bounded Planar & Wedge Face Carving                                      │
│    - Tectonic shear planes & dihedral clefts with spatial radius falloff    │
│    - Bounded maximum depth offset (does NOT cut through entire formation)   │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 3. Structural Joint Fracturing & Block Dilation                             │
│    - Systematic orthogonal joint set J1 & conjugate cross-joint set J2      │
│    - Lithology-dependent staggered brickwork joint spacing                  │
│    - Physical joint dilation gaps (opening fissures and block seams)        │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 4. Edge Chipping & SDF Differential Erosion                                 │
│    - Convex edge detection & localized 3D chip wedge subtraction            │
│    - SDF voxelization + hardness-aware differential strata weathering       │
│    - Crevice and joint contact deepening                                   │
└──────────────────────────────────────┬──────────────────────────────────────┘
                                       ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ 5. Surface Cracks with Bounded Offset & Final Polish                        │
│    - Branching stress-relief surface micro-cracks (30°, 45°, 60°, 90°)      │
│    - Strictly shallow depth offset (never cuts through rock blocks)         │
│    - Combined final weathering & tangential Laplacian triangle conditioning │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🚀 Interactive Web Studio & Live 3D Preview

The repository includes a real-time WebGL studio powered by Three.js and Vite:
- **Live 3D Viewport**: OrbitControls, cinematic grazing sunlight with soft PCF contact shadows, ground shadow plane.
- **Render Modes**:
  - `Clay (Untextured)`: Demonstrates pure geometric relief without textures.
  - `Strata Layers`: Color-codes individual geological lithologies.
  - `Matcap Curvature`: High-contrast surface curvature inspection.
  - `Normals`: Geometric surface normal vectors.
  - `Tri Wireframe`: Verifies clean manifold triangles with zero n-gons.
- **Pipeline Scrubber**: Instant inspection of Step 1, 2, 3, 4, or 5.
- **Presets**:
  - *Sedimentary Canyon Wall* (Grand Canyon / Zion style)
  - *Massive Granite Batholith Scarp* (Yosemite / El Capitan style)
  - *Columnar Basalt Formation* (Giant's Causeway / Fingal's Cave style)
  - *Alpine Tectonic Fault Scarp* (Dolomites / Rocky Mountain style)
  - *Coastal Wave-Cut Sea Cliff* (Marine bluff / chalk scarp style)
- **Exporting**: Export OBJ and PLY directly to disk.

### Running the Web Studio
```bash
npm install
npm run dev
# Live preview binds to http://0.0.0.0:5173
```

---

## 🐍 Python CLI Tool

Generate rock formations directly from the command line:

```bash
# Generate Sedimentary Canyon (Stage 5 OBJ)
python3 generate_cliff.py --preset sedimentary_canyon --stage 5 --output canyon.obj

# Generate Granite Batholith Scarp
python3 generate_cliff.py --preset granite_scarp --stage 5 --output granite.obj

# Inspect JSON metrics
python3 generate_cliff.py --preset columnar_basalt --stage 5 --json
```

### Running Test Suite
```bash
python3 -m unittest discover tests
```
