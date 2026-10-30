#!/usr/bin/env python3
"""Convert the reference game's shop / collection / lore / gallery / dialogue data into romfs/extras/ (read-only source).

Reads Godot .tres resources under `<game>/resources`, the English locale, and the referenced art, then writes:
  shop.txt        cat|enum|name|price|required_exp|description      (cat: P power-up, G photo, M music, O outfit)
  collect.txt     enum|name|description|unlock_description|icon_index  + collect.t3x (64x64 ETC1A4 atlas, 16 per row)
  lore.txt        name|caption|occupation|height|lore|residence|hobbies;;...|portrait_t3x  (outfits 1, 2: <portrait>_1.t3x, _2.t3x)
  gallery.txt     enum|title|subtitle|t3x|unlock_text|price        (+ gal_<enum>.t3x, art fitted into 400x240 ETC1)
  dialogue.txt    sections: pool lines, talk sets (see load code in platform/3ds/extras.c)
  jasmine_<n>.t3x shop keeper art per outfit (204x256 ETC1A4)
Text is single-line UTF-8 with BBCode stripped.
"""
from __future__ import annotations

import os
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image

TEX3DS = Path(os.environ.get("BB_TEX3DS", "C:/devkitPro/tools/bin/tex3ds.exe"))   # the builder exe sets BB_TEX3DS


# ---------------------------------------------------------------- helpers
def strip_bb(s: str) -> str:
    s = re.sub(r"\[/?[a-zA-Z_]+(?:=[^\]]*)?\]", "", s)
    s = s.replace("\u2019", "'").replace("\u2018", "'").replace("\u201c", '"').replace("\u201d", '"').replace("\u2026", "...")
    s = s.replace("\u2013", "-").replace("\u2014", "-")
    return re.sub(r"\s+", " ", s).strip()


def ascii_clean(s: str) -> str:
    return s.encode("ascii", errors="replace").decode("ascii").replace("|", "/")


def load_po(path: Path) -> dict[str, str]:
    """English strings keyed by locale id, from locale/locale.csv (key,en,es_MX,ja,ru)."""
    import csv
    out: dict[str, str] = {}
    with open(path.parent.parent / "locale.csv", encoding="utf-8", newline="") as f:
        for row in csv.reader(f):
            if len(row) >= 2 and row[0]:
                out[row[0]] = row[1]
    return out


class Tres:
    def __init__(self, path: Path, game: Path):
        self.path = path
        self.game = game
        text = path.read_text(encoding="utf-8")
        self.ext = {m.group(2): m.group(1) for m in re.finditer(r'\[ext_resource [^\]]*path="([^"]+)" id="([^"]+)"\]', text)}
        body = text.split("[resource]", 1)[1] if "[resource]" in text else ""
        self.kv: dict[str, str] = {}
        key, buf = None, []
        for line in body.splitlines():
            m = re.match(r"^([A-Za-z_][A-Za-z0-9_/]*) = (.*)$", line)
            if m and not buf:
                if key:
                    self.kv[key] = "\n".join(buf)
                key, buf = m.group(1), [m.group(2)]
                if self._balanced("\n".join(buf)):
                    self.kv[key] = "\n".join(buf)
                    key, buf = None, []
            elif key:
                buf.append(line)
                if self._balanced("\n".join(buf)):
                    self.kv[key] = "\n".join(buf)
                    key, buf = None, []
        if key:
            self.kv[key] = "\n".join(buf)

    @staticmethod
    def _balanced(s: str) -> bool:
        depth, instr, esc = 0, False, False
        for ch in s:
            if instr:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == '"':
                    instr = False
            elif ch == '"':
                instr = True
            elif ch in "[{(":
                depth += 1
            elif ch in "]})":
                depth -= 1
        return depth <= 0 and not instr

    def s(self, key: str, default: str = "") -> str:
        v = self.kv.get(key)
        if v is None:
            return default
        m = re.match(r'^"(.*)"$', v, re.S)
        if not m:
            return default
        body = m.group(1).replace(chr(10), chr(92) + "n")
        return json.loads('"' + body + '"')

    def n(self, key: str, default: int = 0) -> int:
        v = self.kv.get(key)
        try:
            return int(float(v)) if v is not None else default
        except ValueError:
            return default

    def res_path(self, key: str) -> Path | None:
        v = self.kv.get(key)
        if not v:
            return None
        m = re.match(r'ExtResource\("([^"]+)"\)', v)
        if not m or m.group(1) not in self.ext:
            return None
        p = self.ext[m.group(1)]
        return self.game / p.removeprefix("res://") if p.startswith("res://") else None

    def res_paths(self, key: str) -> list[Path]:
        v = self.kv.get(key, "")
        return [self.game / self.ext[i].removeprefix("res://") for i in re.findall(r'ExtResource\("([^"]+)"\)', v) if i in self.ext]


def tex3ds(png: Path, t3x: Path, fmt: str) -> None:
    cmd = [str(TEX3DS), "-f", fmt, "-z", "auto", "-o", str(t3x), str(png)]
    if fmt.startswith("etc1"):
        cmd[1:1] = ["-q", "medium"]
    subprocess.run(cmd, check=True, capture_output=True)


def fit_to_texture(src: Path, w: int, h: int, tw: int, th: int, out_png: Path, alpha: bool, bg=(0, 0, 0)) -> tuple[int, int]:
    im = Image.open(src).convert("RGBA")
    scale = min(w / im.width, h / im.height)
    nw, nh = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    im = im.resize((nw, nh), Image.LANCZOS) if not alpha else Image.fromarray if False else im.convert("RGBa").resize((nw, nh), Image.LANCZOS).convert("RGBA")
    if alpha:
        canvas = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
        canvas.paste(im, (0, 0))
    else:
        flat = Image.new("RGBA", im.size, bg + (255,))
        flat.alpha_composite(im)
        canvas = Image.new("RGB", (tw, th), bg)
        canvas.paste(flat.convert("RGB"), (0, 0))
    canvas.save(out_png)
    return nw, nh


# ---------------------------------------------------------------- main
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--gen", type=Path, default=Path("generated/extras"))
    ap.add_argument("--skip-art", action="store_true", help="text data only")
    args = ap.parse_args()

    game, res = args.game, args.game / "resources"
    out = args.romfs / "extras"
    gen = args.gen
    out.mkdir(parents=True, exist_ok=True)
    gen.mkdir(parents=True, exist_ok=True)
    po = load_po(game / "locale" / "po" / "en.po")

    def tr(key: str) -> str:
        return strip_bb(po.get(key, key))

    # ---- shop ----
    shop_lines: list[str] = []
    cats = {"powerups": "P", "photos": "G", "music": "M", "Outfits": "O"}
    sub_order = {"powerups": "submenu_powerups.tres", "photos": "submenu_photos.tres", "music": "submenu_music.tres", "Outfits": "submenu_outfits.tres"}
    for folder, code in cats.items():
        sub = Tres(res / "shop_submenus" / sub_order[folder], game)
        for p in sub.res_paths("inventory"):
            t = Tres(p, game)
            name = tr(t.s("name"))
            desc = strip_bb(tr(t.s("description")) if t.s("description") in po else t.s("description"))
            shop_lines.append("|".join([code, str(t.n("item_enum")), ascii_clean(name), str(t.n("price")), str(t.n("required_exp")), ascii_clean(desc)]))
    (out / "shop.txt").write_text("\n".join(shop_lines) + "\n", encoding="utf-8")
    print(f"[OK] shop.txt: {len(shop_lines)} items")

    # ---- collection ----
    coll = []
    icons: list[Path] = []
    for p in sorted((res / "gachapon_items").glob("*.tres")):
        t = Tres(p, game)
        icon = t.res_path("icon")
        coll.append((t.n("collectable_enum"), ascii_clean(tr(t.s("name"))), ascii_clean(strip_bb(t.s("description"))),
                     ascii_clean(strip_bb(t.s("unlock_description"))), len(icons)))
        icons.append(icon)
    (out / "collect.txt").write_text("\n".join("|".join(map(str, c)) for c in sorted(coll)) + "\n", encoding="utf-8")
    print(f"[OK] collect.txt: {len(coll)} collectables")

    # ---- lore ----
    lore_lines = []
    lore_art: list[tuple[str, Path]] = []   # (texture name stem, source): lore_<tag> = outfit 0, lore_<tag>_<k> = outfit k
    for p in sorted((res / "character_lore").glob("lore_*.tres")):
        t = Tres(p, game)
        name = t.s("character_name")
        hobbies = [h.strip() for h in strip_bb(t.s("hobbies").replace("\n", ";;")).split(";;") if h.strip()]
        outfits = t.res_paths("outfits")
        art = outfits[0] if outfits else None
        tag = re.sub(r"[^a-z0-9]", "", name.lower())
        lore_lines.append("|".join([ascii_clean(name), ascii_clean(t.s("caption")), ascii_clean(t.s("occupation")), ascii_clean(t.s("height_ft")),
                                    ascii_clean(strip_bb(t.s("lore"))), ascii_clean(strip_bb(t.s("residence"))), ascii_clean(";;".join(hobbies)),
                                    f"lore_{tag}.t3x"]))
        for k, o in enumerate(outfits[:3]):   # lore_character.gd change_outfit: clothed -> alt (lingerie) -> nude -> clothed
            lore_art.append((tag if k == 0 else f"{tag}_{k}", o))
    (out / "lore.txt").write_text("\n".join(lore_lines) + "\n", encoding="utf-8")
    print(f"[OK] lore.txt: {len(lore_lines)} characters")

    # ---- gallery ----
    gal_lines = []
    gal_art: list[tuple[int, Path]] = []
    seen: set[int] = set()
    for folder in ("shop_items/photos", "gallery_images/patreon"):
        for p in sorted((res / folder).glob("*.tres")):
            t = Tres(p, game)
            enum = t.n("item_enum")
            tex = t.res_path("gallery_texture")
            if enum in seen or tex is None:
                continue
            seen.add(enum)
            title = ascii_clean(tr(t.s("name")))
            sub = ascii_clean(strip_bb(t.s("description")))
            unlock = ascii_clean(strip_bb(t.s("unlock_condition")))
            gal_lines.append("|".join([str(enum), title, sub, f"gal_{enum}.t3x", unlock, str(t.n("price"))]))
            gal_art.append((enum, tex))
    (out / "gallery.txt").write_text("\n".join(gal_lines) + "\n", encoding="utf-8")
    print(f"[OK] gallery.txt: {len(gal_lines)} images")

    # ---- dialogue ----
    pool = Tres(res / "shop_dialogue" / "dialogue_pool.tres", game)

    def dict_of_lists(raw: str) -> dict:
        txt = re.sub(r"(?m)^(\s*)(\d+):", r'\1"\2":', raw)
        txt = re.sub(r"([{,]\s*)(\d+):", r'\1"\2":', txt)
        return json.loads(txt)

    dlg = []
    for key in ("entering", "idle", "buying", "purchase_success", "purchase_sold_out", "purchase_insufficient_funds"):
        if key in pool.kv:
            for k, lines in dict_of_lists(pool.kv[key]).items():
                for ln in lines:
                    dlg.append(f"L|{key}|{k}|{ascii_clean(strip_bb(ln))}")
    sets: dict[str, list[Path]] = {}
    for p in sorted((res / "shop_dialogue_talk_menu").glob("set_*.tres")):
        sets[p.stem] = Tres(p, game).res_paths("options")
    for sname, opts in sets.items():
        for op in opts:
            t = Tres(op, game)
            nxt = t.res_path("next_dialogue_set")
            resp = [ascii_clean(strip_bb(x)) for x in json.loads(re.sub(r",\s*\]", "]", t.kv.get("responses", "[]")))]
            dlg.append("T|" + "|".join([sname, ascii_clean(strip_bb(t.s("input"))), nxt.stem if nxt else "", ";;".join(resp)]))
    (out / "dialogue.txt").write_text("\n".join(dlg) + "\n", encoding="utf-8")
    print(f"[OK] dialogue.txt: {len(dlg)} lines, {len(sets)} talk sets")

    if args.skip_art:
        return 0

    # ---- art ----
    # collectable icons: 64x64 ETC1A4 atlas, 16 per row
    cols = 16
    rows = (len(icons) + cols - 1) // cols
    atlas = Image.new("RGBA", (1024, 128 if rows <= 2 else 256), (0, 0, 0, 0))
    for i, ip in enumerate(icons):
        if ip and ip.is_file():
            im = Image.open(ip).convert("RGBa").resize((64, 64), Image.LANCZOS).convert("RGBA")
            atlas.paste(im, ((i % cols) * 64, (i // cols) * 64))
    atlas.save(gen / "collect.png")
    tex3ds(gen / "collect.png", out / "collect.t3x", "etc1a4")
    for tag, art in lore_art:
        fit_to_texture(art, 150, 240, 256, 256, gen / f"lore_{tag}.png", alpha=True)
        tex3ds(gen / f"lore_{tag}.png", out / f"lore_{tag}.t3x", "etc1a4")
    for enum, art in gal_art:
        fit_to_texture(art, 400, 240, 512, 256, gen / f"gal_{enum}.png", alpha=False)
        tex3ds(gen / f"gal_{enum}.png", out / f"gal_{enum}.t3x", "etc1")
    # Jasmine (dual-screen shop): both idle frames of each outfit sheet at 0.29x plus the four mouth shapes, in one 1024x512 ETC1A4 texture.
    #   frame 0 at (0,0), frame 1 at (FW,0), mouth shape k at (MX0 + k*MS, 0); jasmine.txt: FW|FH|mouth_dx|mouth_dy|MW|MH|MX0|MS|shapes
    JS = 0.29
    jdir = res.parent / "src/prefabs/Jasmine"
    jas = {"0": jdir / "jasmine_jacket.png"}
    lipstick: set[str] = set()   # outfits with mouth_shapes_red_lipstick (the Nurse): the *_halloween mouth shapes
    for p in sorted((res / "wearable_outfits").glob("*.tres")):
        t = Tres(p, game)
        sp, en = t.res_path("outfit_spritesheet"), t.n("unlock_enum")
        if sp and en:
            jas[str(en)] = sp
            mf = t.res_path("mouth_shape_frames")
            if mf and "red_lipstick" in mf.name:
                lipstick.add(str(en))
    mouth_srcs = [Image.open(jdir / f"jasmine_mouth_{m}.png").convert("RGBA") for m in "asuo"]
    mb = [m.getchannel("A").getbbox() for m in mouth_srcs]
    mx0, my0 = min(b[0] for b in mb), min(b[1] for b in mb)
    mx1, my1 = max(b[2] for b in mb), max(b[3] for b in mb)
    FW, FH = round(919 * JS), round(1148 * JS)
    MW, MH = round((mx1 - mx0) * JS), round((my1 - my0) * JS)
    MX0, MS = 2 * FW + 8, MW + 4
    (out / "jasmine.txt").write_text(f"{FW}|{FH}|{round(mx0 * JS)}|{round(my0 * JS)}|{MW}|{MH}|{MX0}|{MS}|4" + chr(10), encoding="utf-8")
    mouth_cells = [m.crop((mx0, my0, mx1, my1)).convert("RGBa").resize((MW, MH), Image.LANCZOS).convert("RGBA") for m in mouth_srcs]
    lip_srcs = [Image.open(jdir / f"jasmine_mouth_{m}_halloween.png").convert("RGBA") for m in "asuo"]
    lip_cells = [m.crop((mx0, my0, mx1, my1)).convert("RGBa").resize((MW, MH), Image.LANCZOS).convert("RGBA") for m in lip_srcs]
    for k, sp in jas.items():
        if not sp.is_file():
            continue
        src = Image.open(sp).convert("RGBA")
        frames = [src.crop((i * 919, 0, (i + 1) * 919, 1148)) if src.width >= (i + 1) * 919 else src.crop((0, 0, 919, 1148)) for i in range(2)]
        canvas = Image.new("RGBA", (1024, 512), (0, 0, 0, 0))
        for i, fr in enumerate(frames):
            canvas.paste(fr.convert("RGBa").resize((FW, FH), Image.LANCZOS).convert("RGBA"), (i * FW, 0))
        for i, mc in enumerate(lip_cells if k in lipstick else mouth_cells):
            canvas.paste(mc, (MX0 + i * MS, 0))
        png = gen / f"jasmine_{k}.png"
        canvas.save(png)
        tex3ds(png, out / f"jasmine_{k}.t3x", "etc1a4")
    total = sum(f.stat().st_size for f in out.iterdir())
    print(f"[OK] extras art: {len(list(out.glob('*.t3x')))} textures, {total / 1048576:.2f} MiB in romfs/extras")
    return 0


if __name__ == "__main__":
    sys.exit(main())
