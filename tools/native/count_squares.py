#!/usr/bin/env python3
"""Count wrongly lit 16x16 lighting tiles in captured frames (1280x720 guest-resolution frames,
e.g. GTAV_NATIVE_SHOT_DIR captures).

usage: count_squares.py FRAME.ppm|png ... [--show OUT.png] [--min STEP]

GTA V lights in 16x16 screen tiles; a tile classified wrongly (seen on the phone while moving)
comes out as a square aligned to the 16 px grid that is brighter (or darker) than everything
around it, with the texture still visible inside. A tile counts when the step across each of its
four edges (one pixel inside against one pixel outside, averaged along the edge) has the same
sign on all four sides, is at least --min levels (default 6) on every side, and is clearly larger
than the gradient one pixel further in. Meant for comparing settings over many frames.
"""
import sys

import numpy as np
from PIL import Image, ImageDraw


def tiles(a, min_step):
    g = a.astype(np.float32).mean(axis=2)
    h, w = g.shape
    found = []
    for ty in range(1, h // 16 - 1):
        for tx in range(1, w // 16 - 1):
            y0, x0, y1, x1 = ty * 16, tx * 16, ty * 16 + 15, tx * 16 + 15
            sides = (
                (g[y0, x0:x1 + 1], g[y0 - 1, x0:x1 + 1], g[y0 + 1, x0:x1 + 1]),  # top
                (g[y1, x0:x1 + 1], g[y1 + 1, x0:x1 + 1], g[y1 - 1, x0:x1 + 1]),  # bottom
                (g[y0:y1 + 1, x0], g[y0:y1 + 1, x0 - 1], g[y0:y1 + 1, x0 + 1]),  # left
                (g[y0:y1 + 1, x1], g[y0:y1 + 1, x1 + 1], g[y0:y1 + 1, x1 - 1]),  # right
            )
            steps, inner = [], []
            for edge, outside, inside in sides:
                steps.append(float((edge - outside).mean()))
                inner.append(float(np.abs(inside - edge).mean()))
            if not (all(s >= min_step for s in steps) or all(s <= -min_step for s in steps)):
                continue
            if min(abs(s) for s in steps) < 2.0 * max(inner):
                continue
            found.append((x0, y0, x1, y1))
    return found


def main():
    args = sys.argv[1:]
    show, min_step = None, 6.0
    if "--show" in args:
        i = args.index("--show")
        show = args[i + 1]
        del args[i:i + 2]
    if "--min" in args:
        i = args.index("--min")
        min_step = float(args[i + 1])
        del args[i:i + 2]
    total = 0
    for path in args:
        img = Image.open(path).convert("RGB")
        f = tiles(np.asarray(img), min_step)
        total += len(f)
        print(f"{len(f):4d}  {path}")
        if show and f:
            d = ImageDraw.Draw(img)
            for x0, y0, x1, y1 in f:
                d.rectangle((x0 - 1, y0 - 1, x1 + 1, y1 + 1), outline=(255, 0, 255))
            img.save(show)
    print(f"total {total} in {len(args)} frames")


if __name__ == "__main__":
    main()
