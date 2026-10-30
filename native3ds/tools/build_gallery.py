"""Build the Gallery (scenes/Gallery) for the 3DS port into romfs/extras/ (source read-only).

    python tools/build_gallery.py --romfs romfs [--game "C:/Beat Banger"]

Reads gallery.tscn (the exported unlockable_gallery / achievement_gallery / patreon_gallery lists, in order), the GalleryImageData
resources they name, galleries.gd's fan_art_images table (sorted by artist, as populate_gallery_from_files does) and the English
locale, then writes:
  gallery_tabs.txt        tab|item_enum|title|unlock_condition|big_t3x|thumb_cell|w|h   (tab 0 Main, 1 Achievements, 2 Fan Art, 3 Patreon)
  gallery_thumbs.t3x      96x96 cells (TextureRect stretch_mode 6 = keep aspect covered), 10 per row, ETC1 1024x1024:
                          cell k = picture k, cell 64 + k = the same picture through the "censored" look locked items have
  gallery_big_<k>.t3x     the picture fitted (keep aspect) into 1024x512 for the lightbox zoom, ETC1; w/h = the fitted size
  gallery_ui.t3x          tab icons (main, trophy, fanart, patreon) 64x64, the lock (64x64), alpha circle; RGBA8 512x64
  gallery_vignette.t3x    gallery_vignette.png scaled to 512x256 (ETC1A4); gallery_cross.t3x the crosshair, cropped, at its on-screen size (ETC1A4)
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

from PIL import Image, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_extras import Tres, ascii_clean, load_po, strip_bb, tex3ds  # noqa: E402

THUMB, THUMB_COLS, CENSOR_BASE = 96, 10, 64
BIG_W, BIG_H = 1024, 512
LIGHTBOX_K = 400.0 / 1920.0   # the 1920x1080 lightbox on the 400 px top screen
TABS = ("unlockable_gallery", "achievement_gallery", None, "patreon_gallery")   # tab 2 = fan art (galleries.gd)


def res_url_to_path(game: Path, url: str) -> Path:
    return game / url.replace("res://", "")


def cover(im: Image.Image, size: int) -> Image.Image:
    k = max(size / im.width, size / im.height)
    r = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
    x, y = (r.width - size) // 2, (r.height - size) // 2
    return r.crop((x, y, x + size, y + size))


def censored(im: Image.Image) -> Image.Image:
    """censored.gdshader look for locked thumbnails: coarse pixels, desaturated, darkened (the Blackout rect adds 0.784 black)."""
    small = im.resize((12, 12), Image.BILINEAR).resize(im.size, Image.NEAREST).filter(ImageFilter.GaussianBlur(1))
    return Image.blend(small.convert("L").convert("RGB"), small, 0.35)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--gen", type=Path, default=Path(__file__).resolve().parent.parent / "generated" / "gallery")
    args = ap.parse_args()
    game, out, gen = args.game, args.romfs / "extras", args.gen
    out.mkdir(parents=True, exist_ok=True)
    gen.mkdir(parents=True, exist_ok=True)
    po = load_po(game / "locale" / "po" / "en.po")
    scene = (game / "src/scenes/Gallery/gallery.tscn").read_text(encoding="utf-8")
    ext = {m.group(2): m.group(1) for m in re.finditer(r'\[ext_resource [^\]]*path="([^"]+)" id="([^"]+)"\]', scene)}

    entries = []   # (tab, enum, title, unlock, src_path)
    for tab, key in enumerate(TABS):
        if key is None:   # fan art: galleries.gd table, sorted by artist; always unlocked; "By %s"
            gd = (game / "src/scenes/Gallery/galleries.gd").read_text(encoding="utf-8")
            table = re.findall(r'\{"artist":\s*"([^"]+)",\s*"texture":\s*preload\("([^"]+)"\)\}', gd)
            table.sort(key=lambda t: t[0])   # sort_custom(a.artist < b.artist)
            credit = po.get("GALLERY_ARTIST_CREDIT", "By %s")
            for artist, url in table:
                entries.append((tab, 0, credit.replace("%s", artist), "", res_url_to_path(game, url)))
            continue
        m = re.search(rf"^{key} = \[(.*)\]$", scene, re.M)
        for rid in re.findall(r'ExtResource\("([^"]+)"\)', m.group(1)):
            t = Tres(res_url_to_path(game, ext[rid]), game)
            name = t.s("name")
            entries.append((tab, t.n("item_enum"), ascii_clean(strip_bb(po.get(name, name))),
                            ascii_clean(strip_bb(t.s("unlock_condition"))), t.res_path("gallery_texture")))

    sheet = Image.new("RGB", (1024, 1024), (0, 0, 0))
    lines = []
    for k, (tab, enum, title, unlock, src) in enumerate(entries):
        im = Image.open(src).convert("RGB")
        th = cover(im, THUMB)
        sheet.paste(th, ((k % THUMB_COLS) * THUMB, (k // THUMB_COLS) * THUMB))
        c = CENSOR_BASE + k
        sheet.paste(censored(th), ((c % THUMB_COLS) * THUMB, (c // THUMB_COLS) * THUMB))
        s = min(BIG_W / im.width, BIG_H / im.height)
        bw, bh = max(1, round(im.width * s)), max(1, round(im.height * s))
        big = Image.new("RGB", (BIG_W, BIG_H), (0, 0, 0))
        big.paste(im.resize((bw, bh), Image.LANCZOS), (0, 0))
        big.save(gen / f"gallery_big_{k}.png")
        tex3ds(gen / f"gallery_big_{k}.png", out / f"gallery_big_{k}.t3x", "etc1")
        lines.append("|".join([str(tab), str(enum), title, unlock, f"gallery_big_{k}.t3x", str(k), str(bw), str(bh)]))
    assert len(entries) <= CENSOR_BASE, "thumbnail atlas holds 64 pictures"
    sheet.save(gen / "gallery_thumbs.png")
    tex3ds(gen / "gallery_thumbs.png", out / "gallery_thumbs.t3x", "etc1")
    (out / "gallery_tabs.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # UI bits: tab icons (main, trophy, fanart, patreon), lock, alpha circle separator
    ui = Image.new("RGBA", (512, 64), (0, 0, 0, 0))
    srcs = [game / "src/scenes/Gallery/gallery_icon_main.png", game / "assets/ui/trophy_icon.png",
            game / "src/scenes/Gallery/gallery_icon_fanart.png", game / "src/scenes/Gallery/gallery_icon_patreon.png",
            game / "assets/images/ui_lock_128.png", game / "assets/images/alpha_circle_8.png"]
    for i, p in enumerate(srcs):
        icon = Image.open(p).convert("RGBA")
        icon = icon.resize((64, 64), Image.LANCZOS) if icon.size != (64, 64) else icon
        ui.alpha_composite(icon, (i * 64, 0))
    ui.save(gen / "gallery_ui.png")
    tex3ds(gen / "gallery_ui.png", out / "gallery_ui.t3x", "rgba8")

    vig = Image.open(game / "assets/images/gallery_vignette.png").convert("RGBA").resize((512, 256), Image.LANCZOS)
    vig.save(gen / "gallery_vignette.png")
    tex3ds(gen / "gallery_vignette.png", out / "gallery_vignette.t3x", "etc1a4")
    cross = Image.open(game / "assets/images/gallery_crosshair.png").convert("RGBA")
    cross = cross.crop(cross.getchannel("A").getbbox())
    k = LIGHTBOX_K * 0.75   # GalleryCrosshair scale 0.75, at its on-screen size
    cross = cross.resize((max(1, round(cross.width * k)), max(1, round(cross.height * k))), Image.LANCZOS)
    cross.save(gen / "gallery_cross.png")
    tex3ds(gen / "gallery_cross.png", out / "gallery_cross.t3x", "etc1a4")

    counts = [sum(1 for e in entries if e[0] == t) for t in range(4)]
    print(f"[OK] gallery: {len(entries)} pictures (main {counts[0]}, achievements {counts[1]}, fan art {counts[2]}, patreon {counts[3]}) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
