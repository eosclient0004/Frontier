#!/usr/bin/env python3
"""
Master anti-cheat Visual Parity Loop Runner.
Evaluates every registered (HTML ref PNG, C++ render PNG) pair via VerifyInspectorVisualParity.py,
generates 4-panel proof PNGs, and writes Exhibits/Images/VisualParity/ParityScorecard.md.
"""
import argparse
import os
import re
import subprocess
import sys

ROOT = "/home/user/Frontier"
HTML_DIR = os.path.join(ROOT, "Exhibits/Images/VisualParity/HtmlRef")
CPP_DIR = os.path.join(ROOT, "Exhibits/Images/VisualParity/CppRender")
OUT_DIR = os.path.join(ROOT, "Exhibits/Images/VisualParity")

# Canonical registry: (CardName, TabLabel, HtmlPng, CppPng, ProofPng, MinSsim, MinEdgeF1)
PARITY_CASES = [
    # Card 1: Height Fog
    ("HeightFog", "1-Overview", "height-fog__0-height-fog_overview.png", "HeightFog-01-Overview.png", "HeightFog-01-Overview-Proof.png", 0.86, 0.78),
    ("HeightFog", "2-Density",  "height-fog__1-height-fog_density.png",  "HeightFog-02-Density.png",  "HeightFog-02-Density-Proof.png",  0.86, 0.78),
    ("HeightFog", "3-Light",    "height-fog__2-height-fog_light.png",    "HeightFog-03-Light.png",    "HeightFog-03-Light-Proof.png",    0.86, 0.78),
    # Card 2: Atmospheric Fog
    ("AtmosphericFog", "1-Overview", "aerial-fog__0-aerial-fog_overview.png", "AtmosphericFog-01-Overview.png", "AtmosphericFog-01-Overview-Proof.png", 0.85, 0.75),
    ("AtmosphericFog", "2-Spectrum", "aerial-fog__1-aerial-fog_spectrum.png", "AtmosphericFog-02-Spectrum.png", "AtmosphericFog-02-Spectrum-Proof.png", 0.85, 0.75),
    # Card 3: Local Fog
    ("LocalFog", "1-Overview", "local-fog__0-local-fog_overview.png", "LocalFog-01-Overview.png", "LocalFog-01-Overview-Proof.png", 0.85, 0.75),
    ("LocalFog", "2-Medium",   "local-fog__1-local-fog_medium.png",   "LocalFog-02-Medium.png",   "LocalFog-02-Medium-Proof.png",   0.85, 0.75),
    ("LocalFog", "3-Shape",    "local-fog__2-local-fog_shape.png",    "LocalFog-03-Shape.png",    "LocalFog-03-Shape-Proof.png",    0.85, 0.75),
    # Card 4: Wind
    ("Wind", "1-Overview", "wind__0-wind_overview.png", "Wind-01-Overview.png", "Wind-01-Overview-Proof.png", 0.85, 0.75),
    ("Wind", "2-Air",      "wind__1-wind_air.png",      "Wind-02-Air.png",      "Wind-02-Air-Proof.png",      0.85, 0.75),
    # Card 5: Clouds
    ("Cloud", "1-Overview", "clouds__0-clouds_overview.png", "Cloud-01-Overview.png", "Cloud-01-Overview-Proof.png", 0.84, 0.72),
    ("Cloud", "2-Coverage", "clouds__1-clouds_coverage.png", "Cloud-02-Coverage.png", "Cloud-02-Coverage-Proof.png", 0.84, 0.72),
    ("Cloud", "3-Layer",    "clouds__2-clouds_layer.png",    "Cloud-03-Layer.png",    "Cloud-03-Layer-Proof.png",    0.85, 0.75),
    ("Cloud", "4-Body",     "clouds__3-clouds_body.png",     "Cloud-04-Body.png",     "Cloud-04-Body-Proof.png",     0.85, 0.75),
    ("Cloud", "5-Wind",     "clouds__4-clouds_wind.png",     "Cloud-05-Wind.png",     "Cloud-05-Wind-Proof.png",     0.85, 0.75),
    ("Cloud", "6-Quality",  "clouds__5-clouds_quality.png",  "Cloud-06-Quality.png",  "Cloud-06-Quality-Proof.png",  0.85, 0.75),
    # Card 6: Local Cloud
    ("LocalCloud", "1-Overview", "local-cloud__0-local-cloud_overview.png", "LocalCloud-01-Overview.png", "LocalCloud-01-Overview-Proof.png", 0.84, 0.72),
    ("LocalCloud", "2-Coverage", "local-cloud__1-local-cloud_coverage.png", "LocalCloud-02-Coverage.png", "LocalCloud-02-Coverage-Proof.png", 0.84, 0.72),
    ("LocalCloud", "3-Layer",    "local-cloud__2-local-cloud_layer.png",    "LocalCloud-03-Layer.png",    "LocalCloud-03-Layer-Proof.png",    0.85, 0.75),
    ("LocalCloud", "4-Body",     "local-cloud__3-local-cloud_body.png",     "LocalCloud-04-Body.png",     "LocalCloud-04-Body-Proof.png",     0.85, 0.75),
    # Card 7: God Rays
    ("GodRays", "1-Overview", "god-rays__0-god-rays_overview.png", "GodRays-01-Overview.png", "GodRays-01-Overview-Proof.png", 0.85, 0.75),
    ("GodRays", "2-Settings", "god-rays__1-god-rays_settings.png", "GodRays-02-Settings.png", "GodRays-02-Settings-Proof.png", 0.85, 0.75),
    # Card 8: Precipitation
    ("Precipitation", "1-Overview",   "precipitation__0-precipitation_overview.png",   "Precipitation-01-Overview.png",   "Precipitation-01-Overview-Proof.png",   0.85, 0.75),
    ("Precipitation", "2-Particles",  "precipitation__1-precipitation_particles.png",  "Precipitation-02-Particles.png",  "Precipitation-02-Particles-Proof.png",  0.85, 0.75),
    ("Precipitation", "3-Simulation", "precipitation__2-precipitation_simulation.png", "Precipitation-03-Simulation.png", "Precipitation-03-Simulation-Proof.png", 0.85, 0.75),
    # Card 9: Rainbow
    ("Rainbow", "1-Overview", "rainbow__0-rainbow_overview.png", "Rainbow-01-Overview.png", "Rainbow-01-Overview-Proof.png", 0.85, 0.75),
    ("Rainbow", "2-Bow",      "rainbow__1-rainbow_bow.png",      "Rainbow-02-Bow.png",      "Rainbow-02-Bow-Proof.png",      0.85, 0.75),
    ("Rainbow", "3-Baking",   "rainbow__2-rainbow_baking.png",   "Rainbow-03-Baking.png",   "Rainbow-03-Baking-Proof.png",   0.85, 0.75),
    # Card 10: Atmosphere
    ("Atmosphere", "1-Overview",   "atmosphere__0-atmosphere_overview.png",   "Atmosphere-01-Overview.png",   "Atmosphere-01-Overview-Proof.png",   0.85, 0.75),
    ("Atmosphere", "2-Scattering", "atmosphere__1-atmosphere_scattering.png", "Atmosphere-02-Scattering.png", "Atmosphere-02-Scattering-Proof.png", 0.85, 0.75),
    ("Atmosphere", "3-Density",    "atmosphere__2-atmosphere_density.png",    "Atmosphere-03-Density.png",    "Atmosphere-03-Density-Proof.png",    0.85, 0.75),
    ("Atmosphere", "4-Colour",     "atmosphere__3-atmosphere_colour.png",     "Atmosphere-04-Colour.png",     "Atmosphere-04-Colour-Proof.png",     0.85, 0.75),
    ("Atmosphere", "5-View",       "atmosphere__4-atmosphere_view.png",       "Atmosphere-05-View.png",       "Atmosphere-05-View-Proof.png",       0.85, 0.75),
    ("Atmosphere", "6-Baking",     "atmosphere__5-atmosphere_baking.png",     "Atmosphere-06-Baking.png",     "Atmosphere-06-Baking-Proof.png",     0.85, 0.75),
    # Card 11: Solar / Sun
    ("Solar", "1-Overview",    "sun__0-sun_overview.png",    "Solar-01-Overview.png",    "Solar-01-Overview-Proof.png",    0.85, 0.75),
    ("Solar", "2-Temperature", "sun__1-sun_temperature.png", "Solar-02-Temperature.png", "Solar-02-Temperature-Proof.png", 0.85, 0.75),
    ("Solar", "3-Disc",        "sun__2-sun_disc.png",        "Solar-03-Disc.png",        "Solar-03-Disc-Proof.png",        0.85, 0.75),
    ("Solar", "4-Dynamic",     "sun__3-sun_dynamic.png",     "Solar-04-Dynamic.png",     "Solar-04-Dynamic-Proof.png",     0.85, 0.75),
    ("Solar", "5-Baking",      "sun__4-sun_baking.png",      "Solar-05-Baking.png",      "Solar-05-Baking-Proof.png",      0.85, 0.75),
    # Card 12: Lunar / Moon
    ("Lunar", "1-Overview",   "moon__0-moon_overview.png",   "Lunar-01-Overview.png",   "Lunar-01-Overview-Proof.png",   0.85, 0.75),
    ("Lunar", "2-Appearance", "moon__1-moon_appearance.png", "Lunar-02-Appearance.png", "Lunar-02-Appearance-Proof.png", 0.85, 0.75),
    ("Lunar", "3-Position",   "moon__2-moon_position.png",   "Lunar-03-Position.png",   "Lunar-03-Position-Proof.png",   0.85, 0.75),
    # Card 13: Starfield / Stars
    ("Starfield", "1-Overview", "stars__0-stars_overview.png", "Starfield-01-Overview.png", "Starfield-01-Overview-Proof.png", 0.85, 0.75),
    ("Starfield", "2-Motion",   "stars__1-stars_motion.png",   "Starfield-02-Motion.png",   "Starfield-02-Motion-Proof.png",   0.85, 0.75),
    ("Starfield", "3-Renderer", "stars__2-stars_renderer.png", "Starfield-03-Renderer.png", "Starfield-03-Renderer-Proof.png", 0.85, 0.75),
    ("Starfield", "4-Baking",   "stars__3-stars_baking.png",   "Starfield-04-Baking.png",   "Starfield-04-Baking-Proof.png",   0.85, 0.75),
]


def run_case(case):
    card, tab, html_png, cpp_png, proof_png, min_ssim, min_edge = case
    hp = os.path.join(HTML_DIR, html_png)
    cp = os.path.join(CPP_DIR, cpp_png)
    op = os.path.join(OUT_DIR, proof_png)
    if not os.path.exists(hp) or not os.path.exists(cp):
        return (card, tab, "MISSING", 0.0, 0.0, 0.0, 0.0, proof_png)
    cmd = [
        "python3",
        os.path.join(ROOT, "Tools/VerifyInspectorVisualParity.py"),
        "--html", hp, "--cpp", cp, "--out", op,
        "--min-ssim", str(min_ssim), "--min-edge-f1", str(min_edge), "--max-mae", "0.045",
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    out = proc.stdout
    def grab(pat):
        m = re.search(pat, out)
        return float(m.group(1)) if m else 0.0
    full_ssim = grab(r"Full SSIM\s*:\s*([0-9.]+)")
    body_ssim = grab(r"Body SSIM\s*:\s*([0-9.]+)")
    full_mae = grab(r"Full MAE\s*:\s*([0-9.]+)")
    edge_f1 = grab(r"Edge F1\s*:\s*([0-9.]+)")
    verdict = "PASS" if proc.returncode == 0 else "FAIL"
    return (card, tab, verdict, full_ssim, body_ssim, edge_f1, full_mae, proof_png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="", help="Substring filter on CardName (e.g. AtmosphericFog)")
    args = ap.parse_args()

    selected = [c for c in PARITY_CASES if not args.filter or args.filter.lower() in c[0].lower()]
    rows = []
    passed = 0
    for c in selected:
        res = run_case(c)
        rows.append(res)
        if res[2] == "PASS":
            passed += 1
        print(f"[{res[2]:7s}] {res[0]:16s} {res[1]:12s} | FullSSIM={res[3]:.4f} BodySSIM={res[4]:.4f} EdgeF1={res[5]:.4f} MAE={res[6]:.4f}")

    if not args.filter:
        md = ["# Inspector Visual Parity Scorecard (Automated HTML vs C++ Verification)\n"]
        md.append("| Card | Tab | Verdict | Full SSIM | Body SSIM | Edge F1 | MAE | 4-Panel Proof |")
        md.append("| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :--- |")
        for r in rows:
            md.append(f"| {r[0]} | {r[1]} | **{r[2]}** | `{r[3]:.4f}` | `{r[4]:.4f}` | `{r[5]:.4f}` | `{r[6]:.4f}` | `{r[7]}` |")
        with open(os.path.join(OUT_DIR, "ParityScorecard.md"), "w") as f:
            f.write("\n".join(md) + "\n")

    print(f"\nSummary: {passed}/{len(rows)} PASS")
    return 0 if passed == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
