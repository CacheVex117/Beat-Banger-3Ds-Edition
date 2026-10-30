#!/usr/bin/env python3
"""Convert the menu art used by the faithful 3DS menus into romfs/ui/ (Milestone 3.0).

  logo.t3x          Beat Banger logo (350x200 in a 512x256 ETC1A4 texture)
  shop_bg.t3x       Jasmine's shop room (400x240 in 512x256 ETC1)
  extras_bg.t3x     Extras theatre room (400x240 in 512x256 ETC1)
  thumbs.t3x        circular stage portraits, 96x96 cells, 4 per row (ETC1A4) + thumbs.txt (stage_id|cell)
  shop_icons.t3x    shop item icons, 48x48 cells, 8 per row (ETC1A4) + shop_icons.txt (item|cell)
  stars.t3x         grade star fill (cell 0) / outline (cell 1), 64x64 cells (ETC1A4)
  diff_icons.t3x    per-stage difficulty icons (64x64 cells, 8x4) + diff_icons.txt: stage_id|difficulty|cell
  cats.t3x          shop category icons (lives, gallery, song, outfit), 32x32 cells (ETC1A4)
  ../music/*.wav    main / shop / extras scene music (32 kHz stereo WAV)

Sources are read-only from the reference game. The default (non-seasonal) theme art is used.
"""
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_extras import Tres, fit_to_texture, tex3ds  # noqa: E402


def circle_cell(src: Path, size: int) -> Image.Image:
    im = Image.open(src).convert("RGBA")
    s = min(im.size)
    im = im.crop(((im.width - s) // 2, (im.height - s) // 2, (im.width + s) // 2, (im.height + s) // 2))
    im = im.convert("RGBa").resize((size, size), Image.LANCZOS).convert("RGBA")
    mask = Image.new("L", (size * 4, size * 4), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, size * 4 - 1, size * 4 - 1), fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)
    from PIL import ImageChops
    im.putalpha(ImageChops.multiply(im.getchannel("A"), mask))
    return im


def icon_cell(src: Path, size: int) -> Image.Image:
    im = Image.open(src).convert("RGBA")
    s = max(im.size)
    sq = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sq.paste(im, ((s - im.width) // 2, (s - im.height) // 2))
    return sq.convert("RGBa").resize((size, size), Image.LANCZOS).convert("RGBA")


def atlas(cells: list[Image.Image], cell: int, per_row: int, tw: int, th: int, out_png: Path) -> None:
    sheet = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        sheet.paste(c, ((i % per_row) * cell, (i // per_row) * cell))
    sheet.save(out_png)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--gen", type=Path, default=Path("generated/uiart"))
    ap.add_argument("--ffmpeg", default="C:/Tools/ffmpeg/bin/ffmpeg.exe")
    ap.add_argument("--skip-music", action="store_true")
    args = ap.parse_args()
    game = args.game
    out = args.romfs / "ui"
    gen = args.gen
    out.mkdir(parents=True, exist_ok=True)
    gen.mkdir(parents=True, exist_ok=True)

    fit_to_texture(game / "assets/images/main_menu_logo_consolidated.png", 350, 200, 512, 256, gen / "logo.png", alpha=True)
    tex3ds(gen / "logo.png", out / "logo.t3x", "etc1a4")
    # the room backgrounds are 16:9; the top screen is 5:3, so they are stretched ~7 % vertically to fill it (no black strip)
    for src, name in ((game / "src/scenes/Shop/ui_shop_bg.png", "shop_bg"), (game / "assets/ui/ui_extras_dark.png", "extras_bg")):
        im = Image.open(src).convert("RGBA")
        flat = Image.new("RGBA", im.size, (0, 0, 0, 255))
        flat.alpha_composite(im)
        canvas = Image.new("RGB", (512, 256), (0, 0, 0))
        canvas.paste(flat.convert("RGB").resize((400, 240), Image.LANCZOS), (0, 0))
        canvas.save(gen / f"{name}.png")
        tex3ds(gen / f"{name}.png", out / f"{name}.t3x", "etc1")

    # stage portraits, in index.txt order
    idx = args.romfs / "stages" / "index.txt"
    rows = [l.split("|") for l in idx.read_text(encoding="utf-8").splitlines() if l.strip()]
    cells, lines = [], []
    for i, f in enumerate(rows):
        thumb = game / "data" / f[3] / f[0] / "thumb.png"
        if not thumb.is_file():
            thumb = game / "data" / f[3] / f[0] / "splash.png"
        cells.append(circle_cell(thumb, 96))
        lines.append(f"{f[0]}|{i}")
    atlas(cells, 96, 4, 512, 256, gen / "thumbs.png")
    tex3ds(gen / "thumbs.png", out / "thumbs.t3x", "etc1a4")
    (out / "thumbs.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # shop item icons
    items, seen = [], {}
    for p in sorted((game / "resources" / "shop_items").rglob("*.tres")):
        t = Tres(p, game)
        ic, en = t.res_path("icon"), t.n("item_enum")
        if ic and ic.is_file() and en and en not in seen:
            seen[en] = len(items)
            items.append((en, ic))
    for p in sorted((game / "resources" / "wearable_outfits").glob("*.tres")):
        t = Tres(p, game)
        ic, en = t.res_path("icon"), t.n("unlock_enum")
        if ic and ic.is_file() and en and en not in seen:
            seen[en] = len(items)
            items.append((en, ic))
        # the outfit grid uses the wearable outfit's own icon (the shop list uses the shop item's icon), keyed 10000 + enum
        if ic and ic.is_file() and en and 1100 <= en <= 1105 and 10000 + en not in seen:
            seen[10000 + en] = len(items)
            items.append((10000 + en, ic))
    cells = [icon_cell(ic, 48) for _, ic in items]
    atlas(cells, 48, 8, 512, 256, gen / "shop_icons.png")
    tex3ds(gen / "shop_icons.png", out / "shop_icons.t3x", "etc1a4")
    (out / "shop_icons.txt").write_text("\n".join(f"{en}|{seen[en]}" for en, _ in items) + "\n", encoding="utf-8")

    # grade stars + shop category icons
    # 64 px cells: the results screen draws its grade stars ~40 px wide on the bottom screen
    stars = [icon_cell(game / "assets/images/score_screen_star_fill.png", 64), icon_cell(game / "assets/images/score_screen_star_outline.png", 64)]
    atlas(stars, 64, 2, 128, 64, gen / "stars.png")
    tex3ds(gen / "stars.png", out / "stars.t3x", "etc1a4")
    # per-difficulty character icons (DifficultyNode: chart "icon" in <level>/config/notes.cfg, file in <level>/images/); a chart whose
    # icon file does not exist (Makayla names icon0-2.png but ships none) gets no icon. diff_icons.txt: stage_id|difficulty|cell
    import re
    idx = args.romfs / "stages" / "index.txt"
    cells, rows_di = [], []
    if idx.exists():
        for line in idx.read_text(encoding="utf-8").splitlines():
            f = line.split("|")
            if len(f) < 4:
                continue
            lvl = game / "data" / f[3] / f[0]
            cfg = lvl / "config" / "notes.cfg"
            if not cfg.exists():
                continue
            names = {"virgin": 0, "normal": 1, "expert": 2}
            for chart in re.finditer(r'"icon":\s*"([^"]*)",\s*"name":\s*"([^"]*)"', cfg.read_text(encoding="utf-8", errors="replace")):
                icon_png, dname = lvl / "images" / chart.group(1), chart.group(2).lower()
                if dname in names and icon_png.is_file() and len(cells) < 32:
                    rows_di.append(f"{f[0]}|{names[dname]}|{len(cells)}")
                    cells.append(icon_cell(icon_png, 64))
    atlas(cells, 64, 8, 512, 256, gen / "diff_icons.png")
    tex3ds(gen / "diff_icons.png", out / "diff_icons.t3x", "etc1a4")
    (out / "diff_icons.txt").write_text("\n".join(rows_di) + "\n", encoding="utf-8")
    print(f"[OK] difficulty icons: {len(cells)}")
    # gameplay HUD art (GameNote / NoteDetector / DownscrollLaneArrow): 0 note_body, 1 alpha_circle_glow, 2 note_detector_outline_64,
    # 3 bb_direction_arrow, 4 bb_direction_arrow_outline - 64 px cells, white art tinted with the lane colour at runtime
    hud = [icon_cell(game / "assets/images/note_body.png", 64), icon_cell(game / "assets/images/alpha_circle_glow.png", 64),
           icon_cell(game / "assets/images/note_detector_outline_64.png", 64), icon_cell(game / "assets/ui/bb_direction_arrow.png", 64),
           icon_cell(game / "assets/ui/bb_direction_arrow_outline.png", 64)]
    atlas(hud, 64, 8, 512, 64, gen / "hud.png")
    tex3ds(gen / "hud.png", out / "hud.t3x", "rgba8")
    # power-up selection (Powerups.data icon_large, 160 px): none, pills, condom, double edged sword, blindfold, lube -> 96 px cells
    pu = [icon_cell(game / "assets/images" / n, 96) for n in ("powerup_none.png", "pointshop_item_lives.png", "pointshop_no_fail_condom_icon.png",
                                                             "pointshop_double_edge.png", "pointshop_blindfold.png", "pointshop_hardcore_lube.png")]
    atlas(pu, 96, 5, 512, 256, gen / "powerups.png")
    tex3ds(gen / "powerups.png", out / "powerups.t3x", "etc1a4")
    cats = [icon_cell(game / f"assets/images/pointshop_item_{n}.png", 32) for n in ("lives", "gallery", "song", "outfit")]
    atlas(cats, 32, 4, 128, 32, gen / "cats.png")
    tex3ds(gen / "cats.png", out / "cats.t3x", "etc1a4")

    if not args.skip_music:   # scene music: 32 kHz stereo WAV streamed through the song path (loops when it ends)
        import subprocess
        mdir = args.romfs / "music"
        mdir.mkdir(parents=True, exist_ok=True)
        for name, src in (("main", "mus_menu_main.ogg"), ("shop", "mus_pointshop.ogg"), ("extras", "mus_extras.ogg")):
            subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(game / "assets/audio" / src), "-ac", "2", "-ar", "32000",
                            "-c:a", "pcm_s16le", str(mdir / f"{name}.wav")], check=True)
        print("[OK] scene music:", ", ".join(f"{f.name} {f.stat().st_size / 1048576:.1f} MiB" for f in sorted(mdir.glob("*.wav"))))

    # menu feedback sounds (BBCA bank: same layout as the cutscene banks, 22.05 kHz mono)
    import struct
    import subprocess as sp
    # Original sounds only (see the trigger table in SPRINT_HANDOFF.md section 3h). Order == bb_uisfx_t in platform/3ds/ui_sfx.h.
    A = game / "assets" / "audio"
    J = game / "src" / "prefabs" / "Jasmine"
    M = game / "src" / "scenes" / "MainMenu"
    clips = [("touch", A / "sfx_menu_touch.wav", 0.1), ("tick", A / "ui_tick.ogg", 0.2), ("accept", A / "ui_accept.ogg", 1.0),
             ("click", A / "ui_click.ogg", 0.4), ("click_accept", A / "ui_click_accept.ogg", 0.5), ("back", A / "ui_back.ogg", 0.4),
             ("lock", A / "ui_lock.ogg", 0.4), ("error", A / "ui_mod_error.ogg", 0.45), ("buy", A / "sfx_item_buy.ogg", 1.3),
             ("drawer_open", M / "drawer_open.wav", 0.5), ("drawer_close", M / "drawer_close.wav", 0.3),
             ("voice", J / "voice_jasmine.ogg", 0.1), ("outfit_vest", J / "outfit_shirt.ogg", 0.4), ("outfit_noshirt", J / "outfit_no_shirt.ogg", 0.6),
             ("outfit_piercings", J / "outfit_piercings.ogg", 0.6), ("outfit_tits", J / "outfit_tits_out.ogg", 1.0),
             ("gallery_preview", A / "sfx_gallery_preview_image.ogg", 0.8), ("analog_switch", A / "sfx_analog_switch.ogg", 0.4),
             ("score", A / "sfx_score_inc.ogg", 2.2), ("star", A / "sfx_refill_star.ogg", 1.0), ("cheer", A / "sfx_cheering.wav", 3.0),
             ("low", A / "sfx_low_stars.wav", 2.5), ("ach", A / "ui_ach_unlock.ogg", 2.4), ("unlock", A / "ui_unlock.ogg", 2.5),
             ("page_turn", A / "sfx_gallery_page_turn.ogg", 0.8), ("analog_click", A / "sfx_analog_click.ogg", 0.2),
             ("reverse", A / "sfx_reverse.ogg", 1.6), ("hit_stab", A / "sfx_hit_stab.ogg", 0.9),
             ("shutter", A / "sfx_shutter.ogg", 1.0), ("arp", A / "menu_arp_3.ogg", 2.5),   # main-menu easter egg (background_video.gd)
             ("white_noise", A / "env_studio_white_noise.ogg", 3.0),   # GameOver StudioWhiteNoiseSound (looped by ui_sfx.c)
             ("gallery_zoom_in", A / "sfx_gallery_zoom_in.ogg", 0.8), ("gallery_zoom_out", A / "sfx_gallery_zoom_out.ogg", 0.8),   # gallery.gd zoom()
             ("honk", A / "misc_honk.ogg", 1.0),
             ("fail_halloween", game / "assets" / "seasonal" / "halloween" / "level_failed_halloween.wav", 1.0)]   # GameOver StudioWhiteNoiseSound, halloween stream   # shop_honk.gd: Jasmine's nose
    blob, table = bytearray(), []
    for name, src, cap in clips:
        pcm = sp.run([args.ffmpeg, "-v", "error", "-i", str(src), "-t", str(cap), "-ac", "1", "-ar", "22050", "-f", "s16le", "-"],
                     capture_output=True, check=True).stdout
        while len(blob) % 64:
            blob += bytes(1)
        table.append((name, len(blob), len(pcm) // 2))
        blob += pcm
    bank = bytearray(b"BBCA") + struct.pack("<II", 1, len(table))
    for name, off, fr in table:
        bank += name.encode("ascii").ljust(32, bytes(1)) + struct.pack("<III", off, fr, 22050)
    (out / "ui_sfx.bbca").write_bytes(bytes(bank + blob))

    total = sum(f.stat().st_size for f in out.iterdir())
    print(f"[OK] ui art: {len(list(out.glob('*.t3x')))} textures, {len(items)} shop icons, {len(rows)} portraits, {total / 1024:.0f} KiB in romfs/ui")
    return 0


if __name__ == "__main__":
    sys.exit(main())
