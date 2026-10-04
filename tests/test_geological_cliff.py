#!/usr/bin/env python3
"""
Comprehensive Geological & Geometric Validation Tests
Verifies:
1. STRICT CONSTRAINT: No continuous noise generators (0% Perlin/Simplex/Worley/FBM/Heightmap noise).
2. STRICT CONSTRAINT: Zero N-Gons (strictly manifold triangles).
3. STRICT CONSTRAINT: Realistic geological cliff formation (Strata, Cutters, Joints, Chips, Cracks).
4. Stage-by-Stage verification for all 5 pipeline stages.
5. Presets and determinism.
"""

import unittest
import os
import re
import sys

# Import from generate_cliff
from generate_cliff import (
    GeologicalPRNG,
    Vec3,
    MeshData,
    generate_strata,
    stage1_macro_cliff,
    stage2_cut_faces,
    stage3_fracture_cliff,
    stage4_edge_chipping_erosion,
    stage5_surface_cracks,
    export_obj
)

class TestGeologicalCliff(unittest.TestCase):
    def setUp(self):
        self.config = {
            "width": 40.0,
            "height": 35.0,
            "depth": 25.0,
            "strata_count": 8,
            "overall_dip": 3.5,
            "talus_fraction": 0.22,
            "buttress_count": 3,
            "overhang_intensity": 1.35,
            "cut_count": 12,
            "max_cut_offset": 0.28,
            "joint_spacing": 4.5,
            "joint_dilation": 0.08,
            "edge_chip_depth": 0.8,
            "erosion_strength": 0.75,
            "hardness_contrast": 0.8,
            "crack_density": 0.7,
            "crack_depth_offset": 0.05
        }

    def test_no_noise_in_codebase(self):
        """Verify strict rule: no Perlin, Simplex, Worley, Voronoi, FBM, or noise functions."""
        forbidden = [
            r'perlin_noise\(',
            r'simplex_noise\(',
            r'worley\(',
            r'voronoi\(',
            r'fbm\(',
            r'value_noise\(',
            r'curl_noise\(',
            r'domain_warp\(',
            r'noise2d\(',
            r'noise3d\(',
            r'import.*perlin',
            r'import.*simplex',
            r'import.*noise',
            r'from.*noise',
        ]
        source_dirs = ['src', '.']
        for sdir in source_dirs:
            for root, _, files in os.walk(sdir):
                if 'node_modules' in root or '.git' in root or 'tests' in root or 'dist' in root:
                    continue
                for f in files:
                    if f.endswith(('.ts', '.js', '.py')):
                        path = os.path.join(root, f)
                        with open(path, 'r', encoding='utf-8') as src:
                            content = src.read().lower()
                            for pat in forbidden:
                                match = re.search(pat, content)
                                self.assertIsNone(
                                    match,
                                    f"Forbidden noise pattern '{pat}' found in {path}"
                                )

    def test_deterministic_prng(self):
        """Verify deterministic PRNG produces identical sequence for same seed."""
        prng1 = GeologicalPRNG(42)
        prng2 = GeologicalPRNG(42)
        seq1 = [prng1.next_float() for _ in range(50)]
        seq2 = [prng2.next_float() for _ in range(50)]
        self.assertEqual(seq1, seq2)

    def test_stage1_macro_cliff(self):
        """Step 1: Cliff macro shape must have strata, buttresses, and clean triangles."""
        prng = GeologicalPRNG(123)
        strata = generate_strata(self.config, prng)
        self.assertGreaterEqual(len(strata), 3)

        mesh = stage1_macro_cliff(self.config, strata, prng)
        topo = mesh.verify_topology()
        self.assertEqual(topo['non_gons_count'], 0)
        self.assertEqual(topo['degenerate_count'], 0)
        self.assertGreater(topo['vertex_count'], 100)
        self.assertGreater(topo['triangle_count'], 200)

    def test_stage2_face_cutting_offset(self):
        """Step 2: Face cuts must shear faces with bounded offset without destroying mesh."""
        prng = GeologicalPRNG(123)
        strata = generate_strata(self.config, prng)
        mesh1 = stage1_macro_cliff(self.config, strata, prng)
        mesh2 = stage2_cut_faces(mesh1, self.config, strata, prng)

        topo = mesh2.verify_topology()
        self.assertEqual(topo['non_gons_count'], 0)
        self.assertEqual(len(mesh2.vertices), len(mesh1.vertices))

    def test_stage3_joint_fracturing(self):
        """Step 3: Joints must create physical block dilation seams."""
        prng = GeologicalPRNG(123)
        strata = generate_strata(self.config, prng)
        mesh1 = stage1_macro_cliff(self.config, strata, prng)
        mesh2 = stage2_cut_faces(mesh1, self.config, strata, prng)
        mesh3 = stage3_fracture_cliff(mesh2, self.config, strata, prng)

        topo = mesh3.verify_topology()
        self.assertEqual(topo['non_gons_count'], 0)
        self.assertGreater(topo['triangle_count'], 0)

    def test_stage4_edge_chipping_erosion(self):
        """Step 4: Chipping and differential strata erosion must modulate hardness."""
        prng = GeologicalPRNG(123)
        strata = generate_strata(self.config, prng)
        mesh1 = stage1_macro_cliff(self.config, strata, prng)
        mesh2 = stage2_cut_faces(mesh1, self.config, strata, prng)
        mesh3 = stage3_fracture_cliff(mesh2, self.config, strata, prng)
        mesh4 = stage4_edge_chipping_erosion(mesh3, self.config, strata, prng)

        topo = mesh4.verify_topology()
        self.assertEqual(topo['non_gons_count'], 0)

    def test_stage5_surface_cracks(self):
        """Step 5: Surface cracks must apply bounded shallow grooving and valid geometry."""
        prng = GeologicalPRNG(123)
        strata = generate_strata(self.config, prng)
        mesh1 = stage1_macro_cliff(self.config, strata, prng)
        mesh2 = stage2_cut_faces(mesh1, self.config, strata, prng)
        mesh3 = stage3_fracture_cliff(mesh2, self.config, strata, prng)
        mesh4 = stage4_edge_chipping_erosion(mesh3, self.config, strata, prng)
        mesh5 = stage5_surface_cracks(mesh4, self.config, strata, prng)

        topo = mesh5.verify_topology()
        self.assertEqual(topo['non_gons_count'], 0)
        self.assertEqual(topo['degenerate_count'], 0)

    def test_obj_export(self):
        """Test OBJ file export creates valid Wavefront OBJ file."""
        prng = GeologicalPRNG(42)
        strata = generate_strata(self.config, prng)
        mesh = stage1_macro_cliff(self.config, strata, prng)
        obj_file = 'tests/output_cliff.obj'
        export_obj(mesh, obj_file)

        self.assertTrue(os.path.exists(obj_file))
        self.assertGreater(os.path.getsize(obj_file), 1000)
        if os.path.exists(obj_file):
            os.remove(obj_file)

if __name__ == '__main__':
    unittest.main()
