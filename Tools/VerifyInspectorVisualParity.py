#!/usr/bin/env python3
"""
Honest, zero-cheating visual parity verifier comparing rendered HTML reference PNG
against Frontier C++ rendered PNG at identical dimensions (e.g. 340x825).

Metrics computed (pure stdlib + ImageMagick for PNG decode/encode):
  - Full-frame SSIM (8x8 window luminance+contrast+structure)
  - Card-body SSIM (y in [135, 795], ignoring header/footer chrome differences if desired)
  - Mean Absolute Error (MAE in [0..1]) & RMSE
  - Edge/Layout F1 Score (Sobel edge overlap with 2px tolerance - catches shifted boxes,
    missing side-by-side panels, wrong card heights, invented Reset/Delete buttons)
  - Per-100px vertical band diagnostics showing exact Y ranges that diverge
  - 4-panel visual proof PNG: [HTML | C++ | 5x Abs Diff | Edge Overlay]
"""
import argparse
import math
import os
import subprocess
import sys
import tempfile


def load_rgb(path: str, width: int, height: int) -> bytes:
    cmd = [
        "convert",
        path,
        "-alpha", "remove",
        "-alpha", "off",
        "-extent", f"{width}x{height}",
        "-depth", "8",
        "rgb:-",
    ]
    res = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    expected = width * height * 3
    if len(res.stdout) != expected:
        raise RuntimeError(f"Expected {expected} RGB bytes for {path}, got {len(res.stdout)}")
    return res.stdout


def save_rgb(path: str, width: int, height: int, rgb: bytes) -> None:
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    cmd = [
        "convert",
        "-size", f"{width}x{height}",
        "-depth", "8",
        "rgb:-",
        path,
    ]
    subprocess.run(cmd, input=rgb, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def to_luma(rgb: bytes, w: int, h: int) -> list[float]:
    out = [0.0] * (w * h)
    for i in range(w * h):
        o = i * 3
        out[i] = 0.299 * rgb[o] + 0.587 * rgb[o + 1] + 0.114 * rgb[o + 2]
    return out


def compute_ssim_region(la: list[float], lb: list[float], w: int, y0: int, y1: int, block: int = 8) -> float:
    c1 = (0.01 * 255.0) ** 2
    c2 = (0.03 * 255.0) ** 2
    total = 0.0
    count = 0
    for by in range(y0, y1 - block + 1, block):
        for bx in range(0, w - block + 1, block):
            n = block * block
            sa = sb = saa = sbb = sab = 0.0
            for dy in range(block):
                row = (by + dy) * w + bx
                for dx in range(block):
                    va = la[row + dx]
                    vb = lb[row + dx]
                    sa += va
                    sb += vb
                    saa += va * va
                    sbb += vb * vb
                    sab += va * vb
            ma = sa / n
            mb = sb / n
            va = max(0.0, saa / n - ma * ma)
            vb = max(0.0, sbb / n - mb * mb)
            cov = sab / n - ma * mb
            num = (2.0 * ma * mb + c1) * (2.0 * cov + c2)
            den = (ma * ma + mb * mb + c1) * (va + vb + c2)
            total += num / den
            count += 1
    return total / max(1, count)


def compute_mae_rmse(ra: bytes, rb: bytes, w: int, y0: int, y1: int) -> tuple[float, float]:
    s_abs = 0.0
    s_sq = 0.0
    start = y0 * w * 3
    end = y1 * w * 3
    n = max(1, end - start)
    for i in range(start, end):
        d = abs(ra[i] - rb[i]) / 255.0
        s_abs += d
        s_sq += d * d
    return s_abs / n, math.sqrt(s_sq / n)


def compute_edges(luma: list[float], w: int, h: int, thresh: float = 22.0) -> list[bool]:
    edges = [False] * (w * h)
    for y in range(1, h - 1):
        row = y * w
        for x in range(1, w - 1):
            gx = (
                -luma[row - w + x - 1] + luma[row - w + x + 1]
                - 2.0 * luma[row + x - 1] + 2.0 * luma[row + x + 1]
                - luma[row + w + x - 1] + luma[row + w + x + 1]
            )
            gy = (
                -luma[row - w + x - 1] - 2.0 * luma[row - w + x] - luma[row - w + x + 1]
                + luma[row + w + x - 1] + 2.0 * luma[row + w + x] + luma[row + w + x + 1]
            )
            if math.hypot(gx, gy) >= thresh:
                edges[row + x] = True
    return edges


def dilate_edges(edges: list[bool], w: int, h: int, radius: int = 2) -> list[bool]:
    out = [False] * (w * h)
    for y in range(h):
        y_lo = max(0, y - radius)
        y_hi = min(h - 1, y + radius)
        for x in range(w):
            if not edges[y * w + x]:
                continue
            x_lo = max(0, x - radius)
            x_hi = min(w - 1, x + radius)
            for yy in range(y_lo, y_hi + 1):
                r = yy * w
                for xx in range(x_lo, x_hi + 1):
                    out[r + xx] = True
    return out


def compute_edge_f1(ea: list[bool], eb: list[bool], w: int, h: int, y0: int, y1: int) -> tuple[float, float, float]:
    da = dilate_edges(ea, w, h, 2)
    db = dilate_edges(eb, w, h, 2)
    a_cnt = b_cnt = a_hit = b_hit = 0
    for y in range(y0, y1):
        r = y * w
        for x in range(w):
            idx = r + x
            if ea[idx]:
                a_cnt += 1
                if db[idx]:
                    a_hit += 1
            if eb[idx]:
                b_cnt += 1
                if da[idx]:
                    b_hit += 1
    recall = a_hit / max(1, a_cnt)
    prec = b_hit / max(1, b_cnt)
    f1 = (2.0 * prec * recall) / max(1e-9, prec + recall)
    return f1, prec, recall


def build_four_panel(ra: bytes, rb: bytes, ea: list[bool], eb: list[bool], w: int, h: int) -> bytes:
    gap = 6
    out_w = w * 4 + gap * 3
    out = bytearray(out_w * h * 3)
    # Dark separator fill
    for i in range(0, len(out), 3):
        out[i] = 10
        out[i + 1] = 10
        out[i + 2] = 12

    for y in range(h):
        for x in range(w):
            si = (y * w + x) * 3
            a_r, a_g, a_b = ra[si], ra[si + 1], ra[si + 2]
            b_r, b_g, b_b = rb[si], rb[si + 1], rb[si + 2]

            # Panel 0: HTML reference
            d0 = (y * out_w + x) * 3
            out[d0], out[d0 + 1], out[d0 + 2] = a_r, a_g, a_b

            # Panel 1: C++ render
            d1 = (y * out_w + (w + gap) + x) * 3
            out[d1], out[d1 + 1], out[d1 + 2] = b_r, b_g, b_b

            # Panel 2: 5x amplified pixel error heat
            dr = min(255, abs(a_r - b_r) * 5)
            dg = min(255, abs(a_g - b_g) * 5)
            db = min(255, abs(a_b - b_b) * 5)
            d2 = (y * out_w + 2 * (w + gap) + x) * 3
            out[d2], out[d2 + 1], out[d2 + 2] = dr, dg, db

            # Panel 3: Edge overlay (Green = HTML only, Red = C++ only, Yellow = aligned!)
            idx = y * w + x
            ha = ea[idx]
            hb = eb[idx]
            d3 = (y * out_w + 3 * (w + gap) + x) * 3
            if ha and hb:
                out[d3], out[d3 + 1], out[d3 + 2] = 245, 225, 70
            elif ha:
                out[d3], out[d3 + 1], out[d3 + 2] = 50, 230, 120
            elif hb:
                out[d3], out[d3 + 1], out[d3 + 2] = 245, 75, 75
            else:
                # Subtle dimmed average background so you see context
                out[d3] = (a_r + b_r) // 6
                out[d3 + 1] = (a_g + b_g) // 6
                out[d3 + 2] = (a_b + b_b) // 6
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description="Compare HTML reference PNG vs Frontier C++ PNG visually.")
    ap.add_argument("--html", required=True, help="Path to HTML reference PNG")
    ap.add_argument("--cpp", required=True, help="Path to C++ rendered PNG")
    ap.add_argument("--out", required=True, help="Path to 4-panel visual diff proof PNG")
    ap.add_argument("--width", type=int, default=340)
    ap.add_argument("--height", type=int, default=825)
    ap.add_argument("--min-ssim", type=float, default=0.88)
    ap.add_argument("--min-edge-f1", type=float, default=0.72)
    ap.add_argument("--max-mae", type=float, default=0.045)
    args = ap.parse_args()

    w, h = args.width, args.height
    ra = load_rgb(args.html, w, h)
    rb = load_rgb(args.cpp, w, h)
    la = to_luma(ra, w, h)
    lb = to_luma(rb, w, h)
    ea = compute_edges(la, w, h)
    eb = compute_edges(lb, w, h)

    full_ssim = compute_ssim_region(la, lb, w, 0, h)
    body_ssim = compute_ssim_region(la, lb, w, 130, h - 30)
    mae, rmse = compute_mae_rmse(ra, rb, w, 0, h)
    body_mae, body_rmse = compute_mae_rmse(ra, rb, w, 130, h - 30)
    edge_f1, edge_prec, edge_rec = compute_edge_f1(ea, eb, w, h, 0, h - 30)

    four_panel = build_four_panel(ra, rb, ea, eb, w, h)
    save_rgb(args.out, w * 4 + 18, h, four_panel)

    print(f"=== VISUAL PARITY REPORT ===")
    print(f"HTML : {args.html}")
    print(f"C++  : {args.cpp}")
    print(f"Proof: {args.out}")
    print(f"Full SSIM   : {full_ssim:.4f} (min {args.min_ssim:.2f})")
    print(f"Body SSIM   : {body_ssim:.4f} (min {args.min_ssim:.2f})")
    print(f"Full MAE    : {mae:.4f} (max {args.max_mae:.3f}) | RMSE: {rmse:.4f}")
    print(f"Body MAE    : {body_mae:.4f} | Body RMSE: {body_rmse:.4f}")
    print(f"Edge F1     : {edge_f1:.4f} (min {args.min_edge_f1:.2f}) | Prec={edge_prec:.3f} (extra C++ edges) | Rec={edge_rec:.3f} (missing HTML edges)")
    print(f"--- Vertical Band Breakdown (100px slices) ---")
    for y0 in range(0, h, 100):
        y1 = min(h, y0 + 100)
        b_ssim = compute_ssim_region(la, lb, w, y0, y1)
        b_mae, _ = compute_mae_rmse(ra, rb, w, y0, y1)
        b_f1, _, _ = compute_edge_f1(ea, eb, w, h, y0, y1)
        flag = "  <-- DIVERGENT" if (b_ssim < 0.85 or b_f1 < 0.65 or b_mae > 0.05) else ""
        print(f"  Y [{y0:3d}..{y1:3d}]: SSIM={b_ssim:.3f}  MAE={b_mae:.4f}  EdgeF1={b_f1:.3f}{flag}")

    passed = (full_ssim >= args.min_ssim) and (body_ssim >= args.min_ssim) and (edge_f1 >= args.min_edge_f1) and (mae <= args.max_mae)
    print(f"VERDICT: {'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
