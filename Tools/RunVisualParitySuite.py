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
