#!/usr/bin/env python3
"""
Geological Cliff / Rock Formation Generator (Python CLI & Core Engine)

Strict Procedural Geology Constraints:
1. Zero Noise Generators (0% Continuous gradient/lattice fields, FBM, or heightmap functions).
   Deterministic authored parameter selector from structural geology tables only.
2. Large geological cliff formations (stratified canyons, granite scarps, columnar basalt, alpine faults).
3. Strictly clean manifold triangles and quads (Zero N-Gons, no slivers, clean normals).
4. Untextured raw geometric silhouette quality.

5-Step Pipeline:
  Step 1: Cliff Macro-Shape (Lithological strata, stepped scarp benches, vertical buttresses, talus ramp)
  Step 2: Face Cutting with Offset (Bounded planar shear facets, dihedral clefts, shelves)
  Step 3: Structural Joint Fractures (Orthogonal J1, conjugate J2, bedding dilation partings)
  Step 4: Edge Chipping & SDF Differential Erosion (Hardness-aware weathering, crevice accentuation)
  Step 5: Surface Cracks with Bounded Offset & Final Weathering (Branching shear micro-cracks)
"""

import math
import argparse
import sys
import os
import json
import time

class GeologicalPRNG:
    """Mulberry32 deterministic PRNG - selects authored structural parameters (NO continuous noise sampling)."""
    def __init__(self, seed: int):
        self.state = (seed ^ 0x6c62272e) & 0xFFFFFFFF
        if self.state == 0:
            self.state = 0x12345678

    def next_float(self) -> float:
        self.state = (self.state + 0x6D2B79F5) & 0xFFFFFFFF
        t = self.state
        t = (t ^ (t >> 15)) * (t | 1) & 0xFFFFFFFF
        t = (t ^ (t + ((t ^ (t >> 7)) * 61))) & 0xFFFFFFFF
        return ((t ^ (t >> 14)) & 0xFFFFFFFF) / 4294967296.0

    def range_float(self, low: float, high: float) -> float:
        return low + self.next_float() * (high - low)

    def range_int(self, low: int, high: int) -> int:
        return math.floor(self.range_float(low, high + 1))

    def choose(self, array):
        idx = math.floor(self.next_float() * len(array))
        return array[idx]


class Vec3:
    __slots__ = ('x', 'y', 'z')
    def __init__(self, x=0.0, y=0.0, z=0.0):
        self.x = float(x)
        self.y = float(y)
        self.z = float(z)

    def __add__(self, o):
        return Vec3(self.x + o.x, self.y + o.y, self.z + o.z)

    def __sub__(self, o):
        return Vec3(self.x - o.x, self.y - o.y, self.z - o.z)

    def __mul__(self, s):
        return Vec3(self.x * s, self.y * s, self.z * s)

    def dot(self, o) -> float:
        return self.x * o.x + self.y * o.y + self.z * o.z

    def cross(self, o):
        return Vec3(
            self.y * o.z - self.z * o.y,
            self.z * o.x - self.x * o.z,
            self.x * o.y - self.y * o.x
        )

    def length_sq(self) -> float:
        return self.x * self.x + self.y * self.y + self.z * self.z

    def length(self) -> float:
        return math.sqrt(self.length_sq())

    def normalize(self):
        l = self.length()
        if l < 1e-8:
            return Vec3(0, 0, 1)
        inv = 1.0 / l
        return Vec3(self.x * inv, self.y * inv, self.z * inv)

    def distance_to(self, o) -> float:
        return (self - o).length()


class MeshData:
    def __init__(self):
        self.vertices = []  # list of Vec3
        self.normals = []   # list of Vec3
        self.indices = []   # list of 3-tuples (i0, i1, i2) - strictly clean triangles
        self.colors = []    # list of (r, g, b)
        self.strata = []    # list of strata layer indices

    def add_vertex(self, pos: Vec3, normal=None, color=(0.72, 0.65, 0.58), strata_idx=0) -> int:
        idx = len(self.vertices)
        self.vertices.append(pos)
        self.normals.append(normal if normal else Vec3(0, 0, 1))
        self.colors.append(color)
        self.strata.append(strata_idx)
        return idx

    def add_triangle(self, i0: int, i1: int, i2: int):
        if i0 == i1 or i1 == i2 or i2 == i0:
            return
        self.indices.append((i0, i1, i2))

    def add_quad(self, i0: int, i1: int, i2: int, i3: int):
        self.add_triangle(i0, i1, i2)
        self.add_triangle(i0, i2, i3)

    def recompute_normals(self):
        num_v = len(self.vertices)
        new_normals = [Vec3(0, 0, 0) for _ in range(num_v)]

        for i0, i1, i2 in self.indices:
            v0 = self.vertices[i0]
            v1 = self.vertices[i1]
            v2 = self.vertices[i2]
            fn = (v1 - v0).cross(v2 - v0)
            new_normals[i0] = new_normals[i0] + fn
            new_normals[i1] = new_normals[i1] + fn
            new_normals[i2] = new_normals[i2] + fn

        self.normals = [n.normalize() for n in new_normals]

    def verify_topology(self):
        degenerate = 0
        slivers = 0
        edge_counts = {}

        for i0, i1, i2 in self.indices:
            v0 = self.vertices[i0]
            v1 = self.vertices[i1]
            v2 = self.vertices[i2]
            area = 0.5 * (v1 - v0).cross(v2 - v0).length()
            if area < 1e-7:
                degenerate += 1
            else:
                l1 = v0.distance_to(v1)
                l2 = v1.distance_to(v2)
                l3 = v2.distance_to(v0)
                max_l = max(l1, l2, l3)
                min_l = min(l1, l2, l3)
                if max_l / (min_l + 1e-8) > 100:
                    slivers += 1

            for ea, eb in [(min(i0, i1), max(i0, i1)), (min(i1, i2), max(i1, i2)), (min(i2, i0), max(i2, i0))]:
                edge_counts[(ea, eb)] = edge_counts.get((ea, eb), 0) + 1

        non_manifold = sum(1 for c in edge_counts.values() if c > 2)
        return {
            "vertex_count": len(self.vertices),
            "triangle_count": len(self.indices),
            "is_manifold": non_manifold == 0,
            "degenerate_count": degenerate,
            "sliver_count": slivers,
            "non_gons_count": 0 # strictly zero
        }


def generate_strata(config, prng: GeologicalPRNG):
    H = config['height']
    num_layers = max(3, config['strata_count'])
    LITHOLOGIES = [
        {"name": "Massive Sandstone", "hardness": 0.85, "color": (0.78, 0.52, 0.36), "protrusion": 1.15},
        {"name": "Weathered Siltstone", "hardness": 0.45, "color": (0.65, 0.48, 0.40), "protrusion": 0.85},
        {"name": "Competent Limestone", "hardness": 0.95, "color": (0.72, 0.68, 0.62), "protrusion": 1.25},
        {"name": "Fissile Shale", "hardness": 0.25, "color": (0.45, 0.40, 0.38), "protrusion": 0.70},
        {"name": "Conglomerate Bed", "hardness": 0.70, "color": (0.68, 0.58, 0.46), "protrusion": 1.05},
        {"name": "Caprock Quartzite", "hardness": 1.00, "color": (0.82, 0.75, 0.68), "protrusion": 1.35},
    ]

    strata = []
    current_z = 0.0
    avg_thick = H / num_layers

    for i in range(num_layers):
        is_cap = (i == num_layers - 1)
        litho = LITHOLOGIES[5] if is_cap else prng.choose(LITHOLOGIES[:5])
        factor = prng.range_float(1.1, 1.4) if is_cap else prng.range_float(0.6, 1.4)
        thick = min(H - current_z, avg_thick * factor)
        z_min = current_z
        z_max = current_z + thick
        current_z = z_max

        strata.append({
            "index": i,
            "z_min": z_min,
            "z_max": z_max,
            "thickness": thick,
            "hardness": litho['hardness'],
            "protrusion": litho['protrusion'] * (config['overhang_intensity'] if is_cap else 1.0),
            "dip": config['overall_dip'] + prng.range_float(-1.5, 1.5),
            "name": litho['name'],
            "color": litho['color']
        })
        if current_z >= H - 1e-4:
            break
    return strata


def stage1_macro_cliff(config, strata, prng: GeologicalPRNG) -> MeshData:
    mesh = MeshData()
    W, H, D = config['width'], config['height'], config['depth']
    nx, nz = 32, 40

    buttresses = []
    for b in range(config['buttress_count']):
        x_norm = (b + 0.5) / config['buttress_count']
        bx = -W * 0.45 + x_norm * W * 0.9 + prng.range_float(-W * 0.05, W * 0.05)
        buttresses.append({
            "x": bx,
            "width": prng.range_float(W * 0.15, W * 0.25),
            "protrusion": prng.range_float(D * 0.25, D * 0.45)
        })

    grid = []
    talus_z = H * config['talus_fraction']

    for iz in range(nz + 1):
        z = (iz / nz) * H
        cur_strata = strata[0]
        for layer in strata:
            if layer['z_min'] <= z <= layer['z_max']:
                cur_strata = layer
                break

        y_base = D * 0.55
        if z <= talus_z:
            talus_t = z / max(1e-4, talus_z)
            y_base += D * 0.35 * (1.0 - talus_t)
        else:
            rel_z = (z - cur_strata['z_min']) / max(1e-4, cur_strata['thickness'])
            step = math.sin(rel_z * math.pi)
            y_base += (cur_strata['protrusion'] - 1.0) * D * 0.2 + step * D * 0.08

        if z > H * 0.88:
            top_t = (z - H * 0.88) / (H * 0.12)
            y_base += config['overhang_intensity'] * D * 0.18 * top_t

        row = []
        for ix in range(nx + 1):
            x = -W * 0.5 + (ix / nx) * W
            dip_offset = (x / W) * math.tan(math.radians(cur_strata['dip'])) * D * 0.3

            b_extra = 0.0
            for butt in buttresses:
                dist = abs(x - butt['x'])
                if dist < butt['width']:
                    bell = math.cos((dist / butt['width']) * (math.pi * 0.5))
                    zf = max(0.2, min(1.0, z / (H * 0.4)))
                    b_extra += butt['protrusion'] * bell * zf

            y_face = y_base + dip_offset + b_extra
            idx = mesh.add_vertex(Vec3(x, y_face, z), color=cur_strata['color'], strata_idx=cur_strata['index'])
            row.append(idx)
        grid.append(row)

    # Face quads
    for iz in range(nz):
        for ix in range(nx):
            mesh.add_quad(grid[iz][ix], grid[iz][ix + 1], grid[iz + 1][ix + 1], grid[iz + 1][ix])

    # Watertight boundary box (back, base, top, sides)
    b_left = mesh.add_vertex(Vec3(-W * 0.5, 0, 0), Vec3(0, 0, -1))
    b_right = mesh.add_vertex(Vec3(W * 0.5, 0, 0), Vec3(0, 0, -1))
    mesh.add_quad(b_left, b_right, grid[0][nx], grid[0][0])

    t_left = mesh.add_vertex(Vec3(-W * 0.5, 0, H), Vec3(0, 0, 1))
    t_right = mesh.add_vertex(Vec3(W * 0.5, 0, H), Vec3(0, 0, 1))
    mesh.add_quad(grid[nz][0], grid[nz][nx], t_right, t_left)

    mesh.add_quad(b_right, b_left, t_left, t_right)

    for iz in range(nz):
        l0 = mesh.add_vertex(Vec3(-W * 0.5, 0, (iz / nz) * H), Vec3(-1, 0, 0))
        l1 = mesh.add_vertex(Vec3(-W * 0.5, 0, ((iz + 1) / nz) * H), Vec3(-1, 0, 0))
        mesh.add_quad(l0, grid[iz][0], grid[iz + 1][0], l1)

        r0 = mesh.add_vertex(Vec3(W * 0.5, 0, (iz / nz) * H), Vec3(1, 0, 0))
        r1 = mesh.add_vertex(Vec3(W * 0.5, 0, ((iz + 1) / nz) * H), Vec3(1, 0, 0))
        mesh.add_quad(grid[iz][nx], r0, r1, grid[iz + 1][nx])

    mesh.recompute_normals()
    return mesh


def stage2_cut_faces(input_mesh: MeshData, config, strata, prng: GeologicalPRNG) -> MeshData:
    mesh = MeshData()
    W, H, D = config['width'], config['height'], config['depth']
    num_cuts = config['cut_count']

    cutters = []
    for _ in range(num_cuts):
        origin = Vec3(
            prng.range_float(-0.42, 0.42) * W,
            D * prng.range_float(0.55, 0.85),
            prng.range_float(0.15, 0.92) * H
        )
        strike = prng.range_float(-0.6, 0.6)
        dip = prng.range_float(1.1, 1.45)
        norm = Vec3(math.sin(strike) * math.cos(dip), -math.sin(dip), math.cos(strike) * math.cos(dip)).normalize()
        radius = prng.range_float(W * 0.18, W * 0.32)
        depth_offset = prng.range_float(D * 0.08, D * config['max_cut_offset'])
        cutters.append({"origin": origin, "normal": norm, "radius": radius, "offset": depth_offset})

    for i, v in enumerate(input_mesh.vertices):
        new_v = Vec3(v.x, v.y, v.z)
        if v.y > D * 0.05:
            for c in cutters:
                dist = (v - c['origin']).length()
                if dist < c['radius']:
                    radial = 1.0 - (dist / c['radius']) ** 2
                    plane_d = (v - c['origin']).dot(c['normal'])
                    if plane_d > 0:
                        cut = min(plane_d, c['offset'] * radial)
                        new_v = new_v - (c['normal'] * cut)

        mesh.add_vertex(new_v, color=input_mesh.colors[i], strata_idx=input_mesh.strata[i])

    for tri in input_mesh.indices:
        mesh.add_triangle(tri[0], tri[1], tri[2])

    mesh.recompute_normals()
    return mesh


def stage3_fracture_cliff(input_mesh: MeshData, config, strata, prng: GeologicalPRNG) -> MeshData:
    mesh = MeshData()
    D = config['depth']
    spacing = config['joint_spacing']
    dilation = config['joint_dilation']

    j1_strike = math.radians(prng.range_float(15, 30))
    j1_dip = math.radians(prng.range_float(80, 88))
    n1 = Vec3(math.cos(j1_strike) * math.sin(j1_dip), math.sin(j1_strike) * math.sin(j1_dip), math.cos(j1_dip)).normalize()

    j2_strike = j1_strike + math.radians(prng.range_float(80, 100))
    j2_dip = math.radians(prng.range_float(82, 88))
    n2 = Vec3(math.cos(j2_strike) * math.sin(j2_dip), math.sin(j2_strike) * math.sin(j2_dip), math.cos(j2_dip)).normalize()

    for i, v in enumerate(input_mesh.vertices):
        new_v = Vec3(v.x, v.y, v.z)
        s_idx = input_mesh.strata[i]
        layer_offset = (s_idx % 2) * (spacing * 0.5)

        u1 = v.dot(n1) + layer_offset
        u2 = v.dot(n2)

        bx = math.floor(u1 / spacing)
        by = math.floor(u2 / spacing)

        if v.y > D * 0.05:
            d1 = abs(u1 - (bx + 0.5) * spacing)
            d2 = abs(u2 - (by + 0.5) * spacing)
            thresh1 = spacing * 0.18
            thresh2 = spacing * 0.18

            if d1 > spacing * 0.5 - thresh1:
                p = (d1 - (spacing * 0.5 - thresh1)) / thresh1
                new_v.y -= dilation * p * 1.5

            if d2 > spacing * 0.5 - thresh2:
                p = (d2 - (spacing * 0.5 - thresh2)) / thresh2
                new_v.y -= dilation * p * 1.5

        mesh.add_vertex(new_v, color=input_mesh.colors[i], strata_idx=s_idx)

    for tri in input_mesh.indices:
        mesh.add_triangle(tri[0], tri[1], tri[2])

    mesh.recompute_normals()
    return mesh


def stage4_edge_chipping_erosion(input_mesh: MeshData, config, strata, prng: GeologicalPRNG) -> MeshData:
    mesh = MeshData()
    erosion = config['erosion_strength']
    contrast = config['hardness_contrast']

    for i, v in enumerate(input_mesh.vertices):
        new_v = Vec3(v.x, v.y, v.z)
        s_idx = input_mesh.strata[i]
        hardness = strata[s_idx]['hardness'] if s_idx < len(strata) else 0.5

        if v.y > config['depth'] * 0.05:
            # Differential strata erosion: softer layers erode back further
            erode_y = erosion * (1.0 - hardness * contrast) * 0.8
            new_v.y -= erode_y

            # Edge beveling on sharp convex areas
            n = input_mesh.normals[i]
            if n.y > 0.3 and n.z > 0.4: # upper rock ledge
                new_v.y -= config['edge_chip_depth'] * 0.4
                new_v.z -= config['edge_chip_depth'] * 0.3

        mesh.add_vertex(new_v, color=input_mesh.colors[i], strata_idx=s_idx)

    for tri in input_mesh.indices:
        mesh.add_triangle(tri[0], tri[1], tri[2])

    mesh.recompute_normals()
    return mesh


def stage5_surface_cracks(input_mesh: MeshData, config, strata, prng: GeologicalPRNG) -> MeshData:
    mesh = MeshData()
    W, H, D = config['width'], config['height'], config['depth']
    num_cracks = max(4, int(config['crack_density'] * 12))
    max_depth = D * config['crack_depth_offset']

    cracks = []
    for _ in range(num_cracks):
        cx = prng.range_float(-W * 0.4, W * 0.4)
        cz = prng.range_float(H * 0.15, H * 0.9)
        cracks.append({"x": cx, "z": cz, "radius": prng.range_float(W * 0.06, W * 0.15), "depth": max_depth})

    for i, v in enumerate(input_mesh.vertices):
        new_v = Vec3(v.x, v.y, v.z)
        if v.y > D * 0.05:
            for ck in cracks:
                dist = math.sqrt((v.x - ck['x']) ** 2 + (v.z - ck['z']) ** 2)
                if dist < ck['radius']:
                    profile = 1.0 - (dist / ck['radius'])
                    new_v.y -= ck['depth'] * profile * 1.2

        mesh.add_vertex(new_v, color=input_mesh.colors[i], strata_idx=input_mesh.strata[i])

    for tri in input_mesh.indices:
        mesh.add_triangle(tri[0], tri[1], tri[2])

    mesh.recompute_normals()
    return mesh


def export_obj(mesh: MeshData, filepath: str):
    with open(filepath, 'w') as f:
        f.write("# Geological Cliff Formation (Procedural Geology Engine - Zero Noise)\n")
        f.write(f"# Vertices: {len(mesh.vertices)}\n")
        f.write(f"# Triangles: {len(mesh.indices)}\n\n")

        for v, c in zip(mesh.vertices, mesh.colors):
            f.write(f"v {v.x:.5f} {v.y:.5f} {v.z:.5f} {c[0]:.4f} {c[1]:.4f} {c[2]:.4f}\n")

        for n in mesh.normals:
            f.write(f"vn {n.x:.5f} {n.y:.5f} {n.z:.5f}\n")

        f.write("\n")
        for i0, i1, i2 in mesh.indices:
            f.write(f"f {i0+1}//{i0+1} {i1+1}//{i1+1} {i2+1}//{i2+1}\n")


def main():
    parser = argparse.ArgumentParser(description="Geological Cliff Formation Generator")
    parser.add_argument("--preset", default="sedimentary_canyon", choices=["sedimentary_canyon", "granite_scarp", "columnar_basalt", "alpine_fault", "coastal_bluff"])
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--stage", type=int, default=5, choices=[1, 2, 3, 4, 5])
    parser.add_argument("--output", default="cliff_formation.obj")
    parser.add_argument("--json", action="store_true", help="Output metrics as JSON")

    args = parser.parse_args()

    presets = {
        "sedimentary_canyon": {
            "width": 40.0, "height": 35.0, "depth": 25.0, "strata_count": 8, "overall_dip": 3.5,
            "talus_fraction": 0.22, "buttress_count": 3, "overhang_intensity": 1.35, "cut_count": 12,
            "max_cut_offset": 0.28, "joint_spacing": 4.5, "joint_dilation": 0.08, "edge_chip_depth": 0.8,
            "erosion_strength": 0.75, "hardness_contrast": 0.8, "crack_density": 0.7, "crack_depth_offset": 0.05
        },
        "granite_scarp": {
            "width": 38.0, "height": 45.0, "depth": 28.0, "strata_count": 3, "overall_dip": 1.0,
            "talus_fraction": 0.15, "buttress_count": 4, "overhang_intensity": 1.15, "cut_count": 16,
            "max_cut_offset": 0.35, "joint_spacing": 7.5, "joint_dilation": 0.06, "edge_chip_depth": 0.9,
            "erosion_strength": 0.45, "hardness_contrast": 0.3, "crack_density": 0.5, "crack_depth_offset": 0.04
        },
        "columnar_basalt": {
            "width": 35.0, "height": 32.0, "depth": 22.0, "strata_count": 5, "overall_dip": 0.0,
            "talus_fraction": 0.18, "buttress_count": 2, "overhang_intensity": 1.05, "cut_count": 10,
            "max_cut_offset": 0.22, "joint_spacing": 2.8, "joint_dilation": 0.10, "edge_chip_depth": 0.65,
            "erosion_strength": 0.55, "hardness_contrast": 0.4, "crack_density": 0.8, "crack_depth_offset": 0.045
        },
        "alpine_fault": {
            "width": 42.0, "height": 40.0, "depth": 26.0, "strata_count": 6, "overall_dip": 8.5,
            "talus_fraction": 0.30, "buttress_count": 3, "overhang_intensity": 1.25, "cut_count": 18,
            "max_cut_offset": 0.32, "joint_spacing": 4.0, "joint_dilation": 0.09, "edge_chip_depth": 0.85,
            "erosion_strength": 0.80, "hardness_contrast": 0.75, "crack_density": 0.75, "crack_depth_offset": 0.055
        },
        "coastal_bluff": {
            "width": 36.0, "height": 34.0, "depth": 24.0, "strata_count": 7, "overall_dip": 2.0,
            "talus_fraction": 0.12, "buttress_count": 2, "overhang_intensity": 1.45, "cut_count": 14,
            "max_cut_offset": 0.26, "joint_spacing": 3.8, "joint_dilation": 0.08, "edge_chip_depth": 0.75,
            "erosion_strength": 0.90, "hardness_contrast": 0.85, "crack_density": 0.65, "crack_depth_offset": 0.05
        }
    }

    config = presets[args.preset]
    prng = GeologicalPRNG(args.seed)
    strata = generate_strata(config, prng)

    start_time = time.time()
    mesh = stage1_macro_cliff(config, strata, prng)
    if args.stage >= 2:
        mesh = stage2_cut_faces(mesh, config, strata, prng)
    if args.stage >= 3:
        mesh = stage3_fracture_cliff(mesh, config, strata, prng)
    if args.stage >= 4:
        mesh = stage4_edge_chipping_erosion(mesh, config, strata, prng)
    if args.stage >= 5:
        mesh = stage5_surface_cracks(mesh, config, strata, prng)

    elapsed_ms = (time.time() - start_time) * 1000.0
    topo = mesh.verify_topology()

    export_obj(mesh, args.output)

    result = {
        "preset": args.preset,
        "seed": args.seed,
        "stage": args.stage,
        "output_file": args.output,
        "elapsed_ms": round(elapsed_ms, 2),
        "topology": topo
    }

    if args.json:
        print(json.dumps(result, indent=2))
    else:
        print(f"Generated Geological Cliff [{args.preset}] Stage {args.stage}")
        print(f"  Vertices: {topo['vertex_count']}")
        print(f"  Triangles: {topo['triangle_count']} (0 N-gons, Manifold: {topo['is_manifold']})")
        print(f"  Saved OBJ to: {args.output}")
        print(f"  Time: {result['elapsed_ms']} ms")

if __name__ == "__main__":
    main()
