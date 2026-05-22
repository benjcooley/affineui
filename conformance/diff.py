#!/usr/bin/env python3
"""Pixel-subtraction image diff for the conformance harness.

Compares two images (e.g. a real-browser screenshot vs AffineUI's headless
render), reports HOW MUCH they differ (changed-pixel count + %, mean/max
per-channel delta) and WHERE (a diff image highlighting changed pixels).

Reads anything Pillow can open (PNG from the browser, PPM from the AffineUI
shot tool). Usable as a CLI or imported by run.py.
"""
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, asdict
from pathlib import Path

import numpy as np
from PIL import Image


@dataclass
class DiffResult:
    width: int
    height: int
    size_matched: bool          # were the two inputs the same size?
    a_size: tuple[int, int]
    b_size: tuple[int, int]
    changed_pixels: int         # pixels differing by > tolerance on any channel
    total_pixels: int
    pct_changed: float          # changed_pixels / total_pixels * 100
    mean_delta: float           # mean per-channel abs difference (0..255)
    max_delta: int              # max per-channel abs difference (0..255)

    def as_json(self) -> str:
        return json.dumps(asdict(self), indent=2)


def _load_rgb(path: str | Path) -> np.ndarray:
    """Load an image as an (H, W, 3) uint8 RGB array (alpha flattened on black)."""
    img = Image.open(path)
    if img.mode in ("RGBA", "LA", "P"):
        img = img.convert("RGBA")
        bg = Image.new("RGBA", img.size, (0, 0, 0, 255))
        img = Image.alpha_composite(bg, img).convert("RGB")
    else:
        img = img.convert("RGB")
    return np.asarray(img, dtype=np.uint8)


def diff_images(a_path: str | Path, b_path: str | Path,
                out_path: str | Path | None = None,
                tolerance: int = 0) -> DiffResult:
    """Per-pixel absolute-difference comparison of two images.

    `tolerance` ignores per-channel deltas <= it (helps with sub-pixel AA
    noise). When `out_path` is given, writes a diff image: the reference
    dimmed, with changed pixels tinted red by magnitude.
    """
    a = _load_rgb(a_path)
    b = _load_rgb(b_path)
    a_size = (a.shape[1], a.shape[0])
    b_size = (b.shape[1], b.shape[0])
    size_matched = a_size == b_size

    # Compare over the common region (crop to the smaller of each dim) so a
    # size mismatch still yields a useful diff instead of crashing.
    h = min(a.shape[0], b.shape[0])
    w = min(a.shape[1], b.shape[1])
    a = a[:h, :w]
    b = b[:h, :w]

    delta = np.abs(a.astype(np.int16) - b.astype(np.int16)).astype(np.uint8)
    per_pixel_max = delta.max(axis=2)            # (h, w) worst channel per pixel
    changed_mask = per_pixel_max > tolerance
    changed = int(changed_mask.sum())
    total = h * w

    result = DiffResult(
        width=w, height=h,
        size_matched=size_matched, a_size=a_size, b_size=b_size,
        changed_pixels=changed, total_pixels=total,
        pct_changed=(100.0 * changed / total) if total else 0.0,
        mean_delta=float(delta.mean()) if total else 0.0,
        max_delta=int(delta.max()) if total else 0,
    )

    if out_path is not None:
        # Diff image: reference at 25% brightness for context, changed pixels
        # tinted red by magnitude so the eye lands on what moved.
        vis = (a.astype(np.float32) * 0.25).astype(np.uint8)
        mag = per_pixel_max.astype(np.float32)
        if mag.max() > 0:
            red = (64 + 191 * (mag / 255.0)).clip(0, 255).astype(np.uint8)
        else:
            red = np.zeros_like(per_pixel_max, dtype=np.uint8)
        vis[..., 0] = np.where(changed_mask, red, vis[..., 0])
        vis[..., 1] = np.where(changed_mask, 0, vis[..., 1])
        vis[..., 2] = np.where(changed_mask, 0, vis[..., 2])
        Path(out_path).parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(vis, "RGB").save(out_path)

    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("a", help="reference image (e.g. browser PNG)")
    ap.add_argument("b", help="candidate image (e.g. affineui PPM/PNG)")
    ap.add_argument("--out", help="write a diff image here")
    ap.add_argument("--tolerance", type=int, default=0,
                    help="ignore per-channel deltas <= this (0..255)")
    ap.add_argument("--threshold", type=float, default=None,
                    help="fail (exit 1) if pct_changed exceeds this")
    args = ap.parse_args()

    r = diff_images(args.a, args.b, args.out, args.tolerance)
    print(r.as_json())
    if not r.size_matched:
        print(f"warning: size mismatch {r.a_size} vs {r.b_size}", file=sys.stderr)
    if args.threshold is not None and r.pct_changed > args.threshold:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
