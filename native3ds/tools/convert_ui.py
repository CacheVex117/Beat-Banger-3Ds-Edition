#!/usr/bin/env python3
"""Generate the shared top-screen UI textures (replaces per-frame circle/heart geometry with sprites).

ui.t3x      64x64 RGBA atlas (white silhouettes, tinted at runtime):
              disc  at (0,0)   32x32  filled circle, anti-aliased
              heart at (32,0)  32x32
              ring  at (0,32)  32x32  outline ring
checker.t3x 32x32 repeating tile = 2x2 cells of 16 px (CheckerBackground colours at 50 % darkness)
Layout must match platform/3ds/game_view.c (UI_* constants).
"""
from __future__ import annotations

import os
import argparse
import math
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw

TEX3DS = Path(os.environ.get("BB_TEX3DS", "C:/devkitPro/tools/bin/tex3ds.exe"))   # the builder exe sets BB_TEX3DS
SS = 8  # supersampling


def aa(draw_fn, size: int) -> Image.Image:
    big = Image.new("L", (size * SS, size * SS), 0)
    draw_fn(ImageDraw.Draw(big), size * SS)
    return big.resize((size, size), Image.LANCZOS)


def disc(d, n):
    d.ellipse((SS, SS, n - SS, n - SS), fill=255)


def ring(d, n):
    d.ellipse((SS, SS, n - SS, n - SS), outline=255, width=3 * SS)


def heart(d, n):
    r = n * 0.26
    d.ellipse((n * 0.5 - 2 * r + 0.1 * n, n * 0.14, n * 0.5 + 0.1 * n, n * 0.14 + 2 * r), fill=255)
    d.ellipse((n * 0.5 - 0.1 * n, n * 0.14, n * 0.5 + 2 * r - 0.1 * n, n * 0.14 + 2 * r), fill=255)
    d.polygon([(n * 0.06, n * 0.42), (n * 0.94, n * 0.42), (n * 0.5, n * 0.92)], fill=255)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--romfs-dir", type=Path, help="romfs root; files go to <dir>/gfx")
    args = ap.parse_args()
    out = args.out_dir
    out.mkdir(parents=True, exist_ok=True)

    atlas = Image.new("RGBA", (64, 64), (255, 255, 255, 0))
    for mask, pos in ((aa(disc, 32), (0, 0)), (aa(heart, 32), (32, 0)), (aa(ring, 32), (0, 32))):
        white = Image.new("RGBA", mask.size, (255, 255, 255, 255))
        white.putalpha(mask)
        atlas.paste(white, pos)
    atlas.save(out / "ui.png")

    tile = Image.new("RGBA", (32, 32), (6, 6, 7, 255))
    d = ImageDraw.Draw(tile)
    d.rectangle((16, 0, 31, 15), fill=(10, 10, 11, 255))
    d.rectangle((0, 16, 15, 31), fill=(10, 10, 11, 255))
    tile.save(out / "checker.png")

    for name in ("ui", "checker"):
        subprocess.run([str(TEX3DS), "-f", "rgba", "-z", "auto", "-o", str(out / f"{name}.t3x"), str(out / f"{name}.png")],
                       check=True, capture_output=True)
        print(f"[OK] {name}.t3x")
        if args.romfs_dir:
            (args.romfs_dir / "gfx").mkdir(parents=True, exist_ok=True)
            shutil.copy2(out / f"{name}.t3x", args.romfs_dir / "gfx" / f"{name}.t3x")
    return 0


if __name__ == "__main__":
    sys.exit(main())
