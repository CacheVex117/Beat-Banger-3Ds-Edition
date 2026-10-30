"""The Extras menu (scenes/extras.tscn) for the 3DS bottom screen -> romfs/extras/xm_*.t3x (source read-only).

    python tools/build_extras_menu.py --romfs romfs [--game "C:/Beat Banger"]

  xm_bg.t3x     EnvBackground ui_extras_dark + the monitor's screen (Polygon2D, white) + EnvMonitor ui_extras_dark_monitor, the
                1080p scene cropped to x 480..1920 (the whole monitor) at K = 240/1080 -> 320x240 (ETC1, 512x256)
  xm_text.t3x   ExtrasText ui_extras_text (its 1388..1806 x 56..319 box) at 1.5 K (RGBA8)
  xm_icons.t3x  the six ExtrasButton icon_textures (music, gallery, theater, characters, scene viewer, collection) and the
                Sawtooth tile ui_sawtooth.png, 64 px cells in one 512x64 strip (RGBA8): cells 0..5 icons, cell 6 sawtooth
  outfit_icon_<item>.t3x  the seasonal outfits' shop icons (64 px RGBA8): 1106 Nurse
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_extras import tex3ds  # noqa: E402

K = 240.0 / 1080.0
CROP_X = 480          # keep the whole monitor (x 858..1920) on the 4:3 screen
TEXT_BOX = (1388, 56, 1806, 319)
SCREEN_POLY = [(1114, 407), (1118, 840), (1786, 869), (1798, 314)]   # extras.tscn Polygon2D (the monitor's lit screen)
ICONS = ["ui_extras_music_icon.png", "ui_extras_gallery_icon.png", "ui_extras_theater_icon.png", "Characters_icons.png",
         "ui_scene_viewer_icon.png", "ui_extras_collection_icon.png"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--work", type=Path, default=Path(__file__).resolve().parent.parent / "generated" / "extras_menu")
    args = ap.parse_args()
    ui = args.game / "assets" / "ui"
    out = args.romfs / "extras"
    out.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)

    scene = Image.open(ui / "ui_extras_dark.png").convert("RGBA").resize((1920, 1080), Image.LANCZOS)
    ImageDraw.Draw(scene).polygon(SCREEN_POLY, fill=(255, 255, 255, 255))
    scene.alpha_composite(Image.open(ui / "ui_extras_dark_monitor.png").convert("RGBA").resize((1920, 1080), Image.LANCZOS))
    bg = scene.crop((CROP_X, 0, CROP_X + 1440, 1080)).convert("RGB").resize((320, 240), Image.LANCZOS)
    canvas = Image.new("RGB", (512, 256), (0, 0, 0))
    canvas.paste(bg, (0, 0))
    canvas.save(args.work / "xm_bg.png")
    tex3ds(args.work / "xm_bg.png", out / "xm_bg.t3x", "etc1")

    text = Image.open(ui / "ui_extras_text.png").convert("RGBA").crop(TEXT_BOX)
    tw, th = round(text.width * K * 1.5), round(text.height * K * 1.5)
    text.convert("RGBa").resize((tw, th), Image.LANCZOS).convert("RGBA").save(args.work / "xm_text.png")
    tex3ds(args.work / "xm_text.png", out / "xm_text.t3x", "rgba8")

    strip = Image.new("RGBA", (512, 64), (0, 0, 0, 0))
    for i, name in enumerate(ICONS):
        strip.paste(Image.open(ui / name).convert("RGBA").resize((64, 64), Image.LANCZOS), (i * 64, 0))
    strip.paste(Image.open(ui / "ui_sawtooth.png").convert("RGBA").resize((64, 64), Image.NEAREST), (6 * 64, 0))
    strip.save(args.work / "xm_icons.png")
    tex3ds(args.work / "xm_icons.png", out / "xm_icons.t3x", "rgba8")
    # seasonal outfits (clothes_menu.tscn "Seasonal" page): their WearableOutfitData icons, 64 px (RGBA8), named by item enum
    for enum, rel in ((1106, "assets/seasonal/halloween/Jamine_nurse_outfit_icon.png"),):
        im = Image.open(args.game / rel).convert("RGBA").resize((64, 64), Image.LANCZOS)
        im.save(args.work / f"outfit_icon_{enum}.png")
        tex3ds(args.work / f"outfit_icon_{enum}.png", out / f"outfit_icon_{enum}.t3x", "rgba8")
    print(f"[OK] extras menu: xm_bg (crop x {CROP_X}), xm_text {tw}x{th}, xm_icons -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
