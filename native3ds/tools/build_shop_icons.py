"""Shop option icons (shop.tscn ShopOptionButton button_icon) -> romfs/ui/shop_opt_icons.t3x (source read-only).

    python tools/build_shop_icons.py --romfs romfs [--game "C:/Beat Banger"]

assets/ui/shop/shop_icon_{buy,talk,outfit}.svg are VTracer traces: <path d="M..L..Z" fill opacity transform="translate(x,y)">
polygons only, painted in order. They are rasterized here (4x supersampled) into 128 px cells of a 512x128 RGBA8 atlas:
cell 0 Buy, 1 Talk, 2 Outfit, 3 Boop (the port's own option: a white nose with three tap strokes, in the same style).
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_extras import tex3ds  # noqa: E402

SS = 4   # supersampling
CELL = 128


def raster_svg(svg: Path) -> Image.Image:
    text = svg.read_text(encoding="utf-8")
    m = re.search(r'width="(\d+)"\s+height="(\d+)"', text)
    w, h = (int(m.group(1)), int(m.group(2))) if m else (CELL, CELL)
    out = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    for pm in re.finditer(r"<path\s+([^>]*)/?>", text):
        attrs = dict(re.findall(r'(\w[\w-]*)="([^"]*)"', pm.group(1)))
        d = attrs.get("d", "")
        tx = ty = 0.0
        t = re.search(r"translate\(([-\d.]+)[ ,]+([-\d.]+)\)", attrs.get("transform", ""))
        if t:
            tx, ty = float(t.group(1)), float(t.group(2))
        col = attrs.get("fill", "#FFFFFF").lstrip("#")
        r, g, b = (int(col[i:i + 2], 16) for i in (0, 2, 4)) if len(col) == 6 else (255, 255, 255)
        a = round(255 * float(attrs.get("opacity", "1")))
        if a <= 2:
            continue   # VTracer's near-invisible speckles
        mask = Image.new("1", out.size, 0)   # even-odd: a subpath inside another one is a hole
        for sub in re.split(r"[Zz]", d):
            pts = [(float(x), float(y)) for x, y in re.findall(r"([-\d.]+)[ ,]([-\d.]+)", sub)]
            if len(pts) >= 3:
                one = Image.new("1", out.size, 0)
                ImageDraw.Draw(one).polygon([((x + tx) * SS, (y + ty) * SS) for x, y in pts], fill=1)
                mask = ImageChops.logical_xor(mask, one)
        layer = Image.new("RGBA", out.size, (r, g, b, 0))
        layer.putalpha(mask.convert("L").point(lambda v: a if v else 0))
        out.alpha_composite(layer)
    return out.resize((CELL, CELL), Image.LANCZOS)


def boop_icon() -> Image.Image:
    """The port's Boop option, drawn in the traced icons' style: a rounded outline (a nose) with a shine and three tap strokes."""
    s = CELL * SS
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    dr = ImageDraw.Draw(im)
    w = int(s * 0.075)   # the stroke weight of the shop icons
    cx, cy, rx, ry = s * 0.42, s * 0.60, s * 0.27, s * 0.21
    dr.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), outline=(255, 255, 255, 255), width=w)
    dr.ellipse((cx - rx * 0.5, cy - ry * 0.55, cx - rx * 0.05, cy - ry * 0.05), fill=(255, 255, 255, 255))
    for k, (dx, dy) in enumerate(((0.66, 0.16), (0.80, 0.30), (0.86, 0.48))):
        x0, y0 = s * dx, s * dy
        dr.line((x0, y0, x0 + s * 0.10, y0 - s * 0.06 + k * s * 0.035), fill=(255, 255, 255, 255), width=w)
    return im.resize((CELL, CELL), Image.LANCZOS)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--work", type=Path, default=Path(__file__).resolve().parent.parent / "generated" / "shop")
    args = ap.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    atlas = Image.new("RGBA", (CELL * 4, CELL), (0, 0, 0, 0))
    for i, name in enumerate(("buy", "talk", "outfit")):
        atlas.paste(raster_svg(args.game / "assets" / "ui" / "shop" / f"shop_icon_{name}.svg"), (i * CELL, 0))
    atlas.paste(boop_icon(), (3 * CELL, 0))
    png = args.work / "shop_opt_icons.png"
    atlas.save(png)
    (args.romfs / "ui").mkdir(parents=True, exist_ok=True)
    tex3ds(png, args.romfs / "ui" / "shop_opt_icons.t3x", "rgba8")
    print(f"[OK] shop option icons -> {args.romfs / 'ui' / 'shop_opt_icons.t3x'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
