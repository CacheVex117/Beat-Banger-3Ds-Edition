#!/usr/bin/env python3
"""Build the Collection screen (scenes/Collection) for the 3DS port into romfs/extras/ (source read-only).

Reads Collection.tscn (pages, item offsets, per-item dim_color), the gachapon item resources (enum, name key, description,
icon) and the scene art, then writes:
  coll_scene.t3x   the bottom screen backdrop, composited like the scene: Background (modulate 0.4375), the Book sprite at
                   (960,500) scale 0.9 and its Decor Line2D strokes, centre-cropped 1440x1080 -> 320x240 (ETC1)
  coll_items.t3x   every item icon at 0.2444 px per source px (K = 240/1080 times the popup hover peak 1.1), 112 px cells,
                   9 per row, plus the page arrows (ui_arrow_left/right) in cells 21/22 (ETC1A4, 1024x512)
  coll_layout.txt  page|slot|enum|offset_left|offset_top|dim|cell|cell_w|cell_h|native_w|native_h|name|description
The items themselves, their tints, the popup and the dim are drawn by platform/3ds/collection_ui.c.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_extras import Tres, ascii_clean, load_po, strip_bb, tex3ds  # noqa: E402

K = 240.0 / 1080.0
ICON_SCALE = K * 1.8          # the top-screen popup draws icons 1.6x larger (peak hover 1.1): sharp, not upscaled
CELL, COLS = 160, 6
ARROW_CELLS = (21, 22)
BOOK_POS, BOOK_SCALE = (960.0, 500.0), 0.9
DECOR_OFFSET = (-960.0, -540.0)
CROP_X = 120                  # the whole book (scene x 125..1792) fits: 1680 px wide -> 320 px, uniform scale
BOOK_SC = 320.0 / (1920 - 2 * CROP_X)
OFF_Y = 10                    # the book drawn 10 px down; the background fills the whole screen behind it


def parse_scene(path: Path) -> tuple[dict[str, str], list[dict]]:
    """ext_resource ids -> res paths, and the [node] blocks in order (name, parent, type, props)."""
    text = path.read_text(encoding="utf-8")
    ext = {m.group(2): m.group(1) for m in re.finditer(r'\[ext_resource [^\]]*path="([^"]+)" id="([^"]+)"\]', text)}
    nodes: list[dict] = []
    cur: dict | None = None
    for line in text.splitlines():
        m = re.match(r'\[node name="([^"]+)"(?: type="([^"]+)")?(?: parent="([^"]+)")?', line)
        if m:
            cur = {"name": m.group(1), "type": m.group(2), "parent": m.group(3) or "", "props": {}}
            nodes.append(cur)
            continue
        if line.startswith("["):
            cur = None
            continue
        if cur is not None and " = " in line:
            k, v = line.split(" = ", 1)
            cur["props"][k.strip()] = v.strip()
    return ext, nodes


def nums(v: str) -> list[float]:
    """The numbers inside a Godot constructor, e.g. PackedVector2Array(224, 400, 456, 400) -> [224, 400, 456, 400]."""
    inner = v[v.index("(") + 1:v.rindex(")")] if "(" in v else v
    return [float(x) for x in re.findall(r"-?[\d.]+(?:e-?\d+)?", inner)]


def vec2(v: str) -> tuple[float, float]:
    a = nums(v)
    return a[0], a[1]


def color_gray(v: str) -> float:
    return nums(v)[0]


def book_to_screen(x: float, y: float) -> tuple[float, float]:
    return BOOK_POS[0] + BOOK_SCALE * x, BOOK_POS[1] + BOOK_SCALE * y


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--gen", type=Path, default=Path("generated/collection"))
    args = ap.parse_args()
    game = args.game
    out = args.romfs / "extras"
    out.mkdir(parents=True, exist_ok=True)
    args.gen.mkdir(parents=True, exist_ok=True)
    po = load_po(game / "locale" / "po" / "en.po")

    def res(p: str) -> Path:
        return game / p.removeprefix("res://")

    ext, nodes = parse_scene(game / "src/scenes/Collection/Collection.tscn")

    # ---- items: Book/Pages/PageN children, in scene order (= focus / page order)
    items = []
    page_names = [n["name"] for n in nodes if n["parent"] == "Book/Pages"]
    for page_idx, page in enumerate(page_names):
        slot = 0
        for n in nodes:
            if n["parent"] != f"Book/Pages/{page}":
                continue
            p = n["props"]
            rid = re.search(r'ExtResource\("([^"]+)"\)', p.get("gachapon_item", ""))
            if not rid:
                continue
            t = Tres(res(ext[rid.group(1)]), game)
            icon = t.res_path("icon")
            name_key = t.s("name")
            items.append({
                "page": page_idx, "slot": slot, "enum": t.n("collectable_enum"),
                "ox": float(p.get("offset_left", "0")), "oy": float(p.get("offset_top", "0")),
                "dim": color_gray(p["dim_color"]) if "dim_color" in p else 0.67894405,   # collection_item.tscn dim_color
                "icon": icon, "name": ascii_clean(strip_bb(po.get(name_key, name_key))),
                "desc": ascii_clean(strip_bb(po.get(t.s("description"), t.s("description")))),
            })
            slot += 1

    # ---- bottom backdrop: Background (modulate 0.4375) + Book + Decor strokes
    scene = Image.open(res(ext[[n for n in nodes if n["name"] == "Background"][0]["props"]["texture"].split('"')[1]])).convert("RGBA")
    r, g, b, a = scene.split()
    mod = 0.4375
    scene = Image.merge("RGBA", (r.point(lambda v: int(v * mod)), g.point(lambda v: int(v * mod)), b.point(lambda v: int(v * mod)), a))
    bgflat = Image.new("RGBA", scene.size, (16, 16, 16, 255))   # theme background behind the scene
    bgflat.alpha_composite(scene)
    flat = Image.new("RGBA", scene.size, (0, 0, 0, 0))         # book + decor, scaled on their own
    book_tex = Image.open(res(ext[[n for n in nodes if n["name"] == "Visual" and n["parent"] == "Book"][0]["props"]["texture"].split('"')[1]])).convert("RGBA")
    bw, bh = round(book_tex.width * BOOK_SCALE), round(book_tex.height * BOOK_SCALE)
    book = book_tex.resize((bw, bh), Image.LANCZOS)
    flat.alpha_composite(book, (round(BOOK_POS[0] - bw / 2), round(BOOK_POS[1] - bh / 2)))
    layer = Image.new("RGBA", flat.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    for n in nodes:
        if n["parent"] != "Book/Decor" or n["type"] != "Line2D":
            continue
        p = n["props"]
        px, py = vec2(p.get("position", "Vector2(0, 0)"))
        pts = nums(p["points"])
        width = float(p.get("width", "10"))
        c = nums(p["default_color"])
        col = tuple(round(v * 255) for v in c[:3]) + (255,)
        for i in range(0, len(pts) - 2, 2):
            x0, y0 = book_to_screen(DECOR_OFFSET[0] + px + pts[i], DECOR_OFFSET[1] + py + pts[i + 1])
            x1, y1 = book_to_screen(DECOR_OFFSET[0] + px + pts[i + 2], DECOR_OFFSET[1] + py + pts[i + 3])
            hw = width * BOOK_SCALE / 2
            draw.rectangle([min(x0, x1), min(y0, y1) - hw, max(x0, x1), max(y0, y1) + hw], fill=col)   # Line2D, no caps
    flat.alpha_composite(layer)
    bottom = bgflat.crop((240, 0, 1680, 1080)).resize((320, 240), Image.LANCZOS)   # background: covers the 4:3 screen
    bw2, bh2 = 320, round(1080 * BOOK_SC)
    bottom.alpha_composite(flat.crop((CROP_X, 0, 1920 - CROP_X, 1080)).resize((bw2, bh2), Image.LANCZOS), (0, OFF_Y))
    bottom = bottom.convert("RGB")
    canvas = Image.new("RGB", (512, 256), (0, 0, 0))
    canvas.paste(bottom, (0, 0))
    canvas.save(args.gen / "coll_scene.png")
    tex3ds(args.gen / "coll_scene.png", out / "coll_scene.t3x", "etc1")

    # ---- icon atlas (+ the page arrows)
    atlas = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
    for i, it in enumerate(items):
        im = Image.open(it["icon"]).convert("RGBA")
        it["nw"], it["nh"] = im.width, im.height
        cw, ch = min(CELL, round(im.width * ICON_SCALE)), min(CELL, round(im.height * ICON_SCALE))
        im = im.convert("RGBa").resize((cw, ch), Image.LANCZOS).convert("RGBA")
        it["cell"], it["cw"], it["ch"] = i, cw, ch
        atlas.paste(im, ((i % COLS) * CELL, (i // COLS) * CELL))
    for cell, name in zip(ARROW_CELLS, ("assets/images/ui_arrow_left.png", "assets/images/ui_arrow_right.png")):
        im = Image.open(game / name).convert("RGBa").resize((32, 32), Image.LANCZOS).convert("RGBA")
        atlas.paste(im, ((cell % COLS) * CELL, (cell // COLS) * CELL))
    assert len(items) <= ARROW_CELLS[0], "icon cells overlap the arrow cells"
    atlas.save(args.gen / "coll_items.png")
    tex3ds(args.gen / "coll_items.png", out / "coll_items.t3x", "etc1a4")

    lines = [f'{it["page"]}|{it["slot"]}|{it["enum"]}|{it["ox"]:g}|{it["oy"]:g}|{it["dim"]:.4f}|{it["cell"]}|{it["cw"]}|{it["ch"]}|'
             f'{it["nw"]}|{it["nh"]}|{it["name"]}|{it["desc"]}' for it in items]
    (out / "coll_layout.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[OK] collection: {len(items)} items on {len(page_names)} pages -> coll_scene.t3x, coll_items.t3x, coll_layout.txt")
    return 0


if __name__ == "__main__":
    sys.exit(main())
