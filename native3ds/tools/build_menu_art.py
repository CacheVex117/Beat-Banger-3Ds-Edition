#!/usr/bin/env python3
"""Menu art for the presentation pass (romfs/ui/): the animated main-menu backdrop, extras icons and achievement icons.

  mm_<n>.t3x + mm.txt   main-menu loop for the bottom screen: the original assets/video/zoe_fix_main_menu.ogv (8 s, spotlights + idle
                        animation), a 1280x960 window (x 495, y 60) scaled to 320x240, 12 fps, 3x4 cells per 1024x1024 ETC1 page (8 x 0.5 MiB resident)
  mmn_<n>.t3x + mmn.txt the naked alternative loop (main_menu_zoe_loop_naked_fix.ogv), same layout, loaded only after the 5-tap easter egg
  settings_frame.t3x    the original settings dialog frame art (speakers + cables), 301x228 in a 512x256 ETC1A4 texture
  icons.t3x             32x32 cells (ETC1A4): 0 music 1 gallery 2 theater 3 characters 4 scene viewer 5 collection 6 trophy 7 back button
  ach_icons.t3x         64x64 cells (ETC1A4) in 256x512, 4 per row: trophy + each Achievements-grid tile's icon (ach_icons.txt: name|cell)
"""
import os
import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image

TEX3DS = Path(os.environ.get("BB_TEX3DS", "C:/devkitPro/tools/bin/tex3ds.exe"))   # the builder exe sets BB_TEX3DS


def tex3ds(png: Path, t3x: Path, fmt: str) -> None:
    cmd = [str(TEX3DS), "-f", fmt, "-z", "auto", "-o", str(t3x), str(png)]
    if fmt.startswith("etc1"):
        cmd[1:1] = ["-q", "medium"]
    subprocess.run(cmd, check=True, capture_output=True)


def icon(src: Path, size: int) -> Image.Image:
    im = Image.open(src).convert("RGBA")
    s = max(im.size)
    sq = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    sq.paste(im, ((s - im.width) // 2, (s - im.height) // 2))
    return sq.convert("RGBa").resize((size, size), Image.LANCZOS).convert("RGBA")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--gen", type=Path, default=Path("generated/menuart"))
    ap.add_argument("--ffmpeg", default="C:/Tools/ffmpeg/bin/ffmpeg.exe")
    ap.add_argument("--fps", type=int, default=12)
    ap.add_argument("--skip-video", action="store_true")
    args = ap.parse_args()
    out = args.romfs / "ui"
    gen = args.gen
    out.mkdir(parents=True, exist_ok=True)
    gen.mkdir(parents=True, exist_ok=True)
    g = args.game

    # --- icons
    ui = g / "assets" / "ui"
    cells = [icon(ui / n, 32) for n in ("ui_extras_music_icon.png", "ui_extras_gallery_icon.png", "ui_extras_theater_icon.png", "Characters_icons.png",
                                         "ui_scene_viewer_icon.png", "ui_extras_collection_icon.png", "trophy_icon.png")]
    cells.append(icon(g / "assets" / "images" / "ui_back.png", 32))
    sheet = Image.new("RGBA", (256, 32), (0, 0, 0, 0))
    for i, c in enumerate(cells):
        sheet.paste(c, (i * 32, 0))
    sheet.save(gen / "icons.png")
    tex3ds(gen / "icons.png", out / "icons.t3x", "etc1a4")

    # --- settings dialog frame: the original 9-patch art (assets/images/themed_settings_ui_tester.png: speakers, cables, connectors)
    frame = Image.open(g / "assets" / "images" / "themed_settings_ui_tester.png").convert("RGBA")
    frame = frame.convert("RGBa").resize((301, 228), Image.LANCZOS).convert("RGBA")   # 0.2344x; interior (x 33..267, y 26..212) is the dark content area
    canvas = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    canvas.paste(frame, (0, 0))
    canvas.save(gen / "settings_frame.png")
    tex3ds(gen / "settings_frame.png", out / "settings_frame.t3x", "etc1a4")

    # --- achievement art: each Achievements-grid tile's own icon_texture (achievements.tscn) + the trophy (results banners, fallback)
    ad = g / "src" / "scenes" / "Achievements" / "assets"
    srcs = [("trophy", ui / "trophy_icon.png"), ("fingies", ad / "ach_fingies.png"), ("combo_king", ad / "ach_combo_king.png"),
            ("gear", ad / "gear.png"), ("cathy_bell", ad / "ach_cathy_bell.png"), ("feather", ad / "ach_feather.png"),
            ("gale_perfect", ad / "gale_perfect_ach.png"), ("mug", ad / "mug.png"), ("dawna_perfect_candy", ad / "ach_dawna_perfect_candy.png"),
            ("impossible", ad / "ach_impossible.png"), ("stopwatch", ad / "steam_ach_stopwatch.png"), ("staring_jasmine", ad / "ach_staring_jasmine.png"),
            ("nice", ad / "ach_nice.png"), ("chad", ad / "ach_chad.png"), ("gigachad", ad / "ach_gigachad.png"),
            ("honk_1", ad / "ach_honk_1.png"), ("honk_2", ad / "ach_honk_2.png"), ("honk_3", ad / "ach_honk_3.png")]
    sheet = Image.new("RGBA", (256, 512), (0, 0, 0, 0))   # 4 x 8 cells of 64 px (cell = row * 4 + col)
    lines = []
    for i, (n, src) in enumerate(srcs):
        sheet.paste(icon(src, 64), ((i % 4) * 64, (i // 4) * 64))
        lines.append(f"{n}|{i}")
    sheet.save(gen / "ach_icons.png")
    tex3ds(gen / "ach_icons.png", out / "ach_icons.t3x", "etc1a4")
    (out / "ach_icons.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # --- main menu loop (bottom screen): a 1280x960 window of the 1920x1080 video (x 495, y 60: Zoe zoomed in a little and sitting
    #     right of centre) -> 320x240, 3x4 cells per 1024x1024 ETC1 page.
    #     mm_*  = zoe_fix_main_menu.ogv (resident while the menus run), mmn_* = main_menu_zoe_loop_naked_fix.ogv (the 5-tap easter egg in
    #     background_video.gd, loaded in place of mm_* only when it triggers).
    #     <set>.txt: fps|frames|cell_w|cell_h|cols|rows|pages|crop_x|crop_y|crop_w (the game maps the original BoobClick rect through it)
    if not args.skip_video:
        CW, CH, COLS, ROWS = 320, 240, 3, 4
        CX, CY, CWID = 495, 60, 1280
        for tag, video in (("mm", "zoe_fix_main_menu.ogv"), ("mmn", "main_menu_zoe_loop_naked_fix.ogv")):
            src = g / "assets" / "video" / video
            frames_dir = gen / f"{tag}_frames"
            frames_dir.mkdir(exist_ok=True)
            for f in frames_dir.glob("*.png"):
                f.unlink()
            subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(src), "-vf", f"fps={args.fps},crop={CWID}:{CWID * 3 // 4}:{CX}:{CY},scale={CW}:{CH}:flags=lanczos",
                            str(frames_dir / "f_%03d.png")], check=True)
            files = sorted(frames_dir.glob("f_*.png"))
            per = COLS * ROWS
            pages = (len(files) + per - 1) // per
            for old in out.glob(f"{tag}_*.t3x"):
                old.unlink()
            for p in range(pages):
                page = Image.new("RGB", (1024, 1024), (0, 0, 0))
                for i, f in enumerate(files[p * per:(p + 1) * per]):
                    page.paste(Image.open(f).convert("RGB"), ((i % COLS) * CW, (i // COLS) * CH))
                png = gen / f"{tag}_{p}.png"
                page.save(png)
                tex3ds(png, out / f"{tag}_{p}.t3x", "etc1")
            (out / f"{tag}.txt").write_text(f"{args.fps}|{len(files)}|{CW}|{CH}|{COLS}|{ROWS}|{pages}|{CX}|{CY}|{CWID}\n", encoding="utf-8")
            print(f"[OK] main menu loop {tag}: {len(files)} frames, {pages} pages")
    total = sum(f.stat().st_size for f in out.iterdir())
    print(f"[OK] menu art, romfs/ui now {total / 1048576:.2f} MiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
