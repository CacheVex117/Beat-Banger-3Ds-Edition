#!/usr/bin/env python3
"""Halloween theme assets (Calendar.THEME.HALLOWEEN in the original; sources read-only) -> romfs.

    python tools/build_theme.py --romfs romfs [--game "C:/Beat Banger"] [--ffmpeg ffmpeg] [--fps 12]

  ui/mmh_<n>.t3x + mmh.txt  main-menu loop for the bottom screen from assets/video/halloween_background.ogv (background_video.gd
                            HALLOWEEN_BACKGROUND), same cell layout as mm_* (build_menu_art.py); the picture is centred, so the window
                            is the full-height 1440x1080 centre (x 240)
  ui/logo_h.t3x             assets/images/main_menu_logo_halloween_consolidated.png (logo.gd %Halloween) fitted into the default logo's
                            350x200 box (centred), so every caller keeps the default logo's geometry
  ui/theme_icon.t3x         the ChangeTheme MainMenuButtonIcon icon_texture (src/scenes/Gallery/gallery_icon_fanart.png), 64x64 RGBA8
  music/spooky.wav          main_menu.gd set_themed_song HALLOWEEN: assets/audio/mus_spooky_theme.ogg (150 BPM), 32 kHz stereo PCM16
  boot/splash_h.t3x         splash_bunny_halloween.tscn art in one 512x512 RGBA8 atlas: SplashBunNofan 349x340 at (0,0), SplashFan
                            100x100 at (352,0), SplashText 315x106 at (0,344), the Rain particle 16x32 at (352,104)
  boot/splash_h.wav         its AudioStreamPlayer stream (splash_intro_halloween.wav), started at 0.4 s by the animation: 0.4 s of
                            silence is prepended so the port starts it with the screen
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_menu_art import tex3ds  # noqa: E402

CW, CH, COLS, ROWS = 320, 240, 3, 4          # mm_* cell layout (build_menu_art.py)
HX, HY, HW = 240, 0, 1440                     # halloween_background.ogv window: the 4:3 centre


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--gen", type=Path, default=Path(__file__).resolve().parent.parent / "generated" / "theme")
    ap.add_argument("--ffmpeg", default="C:/Tools/ffmpeg/bin/ffmpeg.exe")
    ap.add_argument("--fps", type=int, default=12)
    ap.add_argument("--skip-video", action="store_true")
    args = ap.parse_args()
    g, gen = args.game, args.gen
    ui, boot, music = args.romfs / "ui", args.romfs / "boot", args.romfs / "music"
    for d in (gen, ui, boot, music):
        d.mkdir(parents=True, exist_ok=True)
    H = g / "assets" / "seasonal" / "halloween"

    # --- logo: fitted into the default 350x200 box
    logo = Image.open(g / "assets" / "images" / "main_menu_logo_halloween_consolidated.png").convert("RGBA")
    k = min(350 / logo.width, 200 / logo.height)
    logo = logo.convert("RGBa").resize((round(logo.width * k), round(logo.height * k)), Image.LANCZOS).convert("RGBA")
    canvas = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    canvas.paste(logo, ((350 - logo.width) // 2, (200 - logo.height) // 2))
    canvas.save(gen / "logo_h.png")
    tex3ds(gen / "logo_h.png", ui / "logo_h.t3x", "etc1a4")

    # --- the Change Theme button icon
    Image.open(g / "src" / "scenes" / "Gallery" / "gallery_icon_fanart.png").convert("RGBA").save(gen / "theme_icon.png")
    tex3ds(gen / "theme_icon.png", ui / "theme_icon.t3x", "rgba8")

    # --- halloween splash atlas
    atlas = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    for name, pos in (("splash_bun_nofan_halloween", (0, 0)), ("splash_fan_halloween", (352, 0)), ("splash_text_halloween", (0, 344)),
                      ("particle_rain_texture", (352, 104))):
        atlas.paste(Image.open(H / f"{name}.png").convert("RGBA"), pos)
    atlas.save(gen / "splash_h.png")
    tex3ds(gen / "splash_h.png", boot / "splash_h.t3x", "rgba8")
    subprocess.run([args.ffmpeg, "-v", "error", "-y", "-f", "lavfi", "-t", "0.4", "-i", "anullsrc=r=32000:cl=stereo", "-i",
                    str(H / "splash_intro_halloween.wav"), "-filter_complex", "[1:a]aresample=32000,aformat=channel_layouts=stereo[b];[0:a][b]concat=n=2:v=0:a=1",
                    "-ac", "2", "-ar", "32000", "-c:a", "pcm_s16le", str(boot / "splash_h.wav")], check=True)

    # --- menu music
    subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(g / "assets" / "audio" / "mus_spooky_theme.ogg"), "-ac", "2", "-ar", "32000",
                    "-c:a", "pcm_s16le", str(music / "spooky.wav")], check=True)

    # --- main-menu loop
    if not args.skip_video:
        frames_dir = gen / "mmh_frames"
        frames_dir.mkdir(exist_ok=True)
        for f in frames_dir.glob("*.png"):
            f.unlink()
        subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(g / "assets" / "video" / "halloween_background.ogv"), "-vf",
                        f"fps={args.fps},crop={HW}:{HW * 3 // 4}:{HX}:{HY},scale={CW}:{CH}:flags=lanczos", str(frames_dir / "f_%03d.png")], check=True)
        files = sorted(frames_dir.glob("f_*.png"))
        per = COLS * ROWS
        pages = (len(files) + per - 1) // per
        if pages > 8:
            raise SystemExit(f"mmh: {pages} pages > 8 (ui_art.c keeps 8 resident)")
        for old in ui.glob("mmh_*.t3x"):
            old.unlink()
        for p in range(pages):
            page = Image.new("RGB", (1024, 1024), (0, 0, 0))
            for i, f in enumerate(files[p * per:(p + 1) * per]):
                page.paste(Image.open(f).convert("RGB"), ((i % COLS) * CW, (i // COLS) * CH))
            page.save(gen / f"mmh_{p}.png")
            tex3ds(gen / f"mmh_{p}.png", ui / f"mmh_{p}.t3x", "etc1")
        (ui / "mmh.txt").write_text(f"{args.fps}|{len(files)}|{CW}|{CH}|{COLS}|{ROWS}|{pages}|{HX}|{HY}|{HW}\n", encoding="utf-8")
        print(f"[OK] halloween menu loop: {len(files)} frames, {pages} pages")
    build_skins(args, g, gen, ui, music)
    print(f"[OK] halloween theme assets -> {args.romfs}")
    return 0


def _fit(im: Image.Image, w: int, h: int) -> Image.Image:
    k = min(w / im.width, h / im.height)
    return im.convert("RGBa").resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS).convert("RGBA")


def build_skins(args, g: Path, gen: Path, ui: Path, music: Path) -> None:
    """The Halloween skins of the other screens (Calendar.THEME.HALLOWEEN checks in the source):
      ui/hw_game.t3x   512x256 RGBA8: row 0, 64 px cells: 0 game_heart_drippy (Heart.gd), 1 candy_corn_outline, 2 candy_corn_filled
                       (game_star.gd / GradeStar.gd 'halloween'), 3 ui_particle_candy (game_particles.gd); 'Spider Web 8' (game.tscn
                       SeasonalOverlay/Halloween) at half size (400x190) from (0, 64)
      ui/hw_levels.t3x 512x256 ETC1A4: level_select.tscn SeasonalOverlay/Halloween at its on-screen size (K = 240/1080 x node scale):
                       spooky_tree_2 (0,0), spooky_tree_4 (120,0), simple_web_1 (250,0), simple_web_2 (250,70)
      ui/shop_bg_h.t3x shop_background.gd HALLOWEEN_SHOP, processed like shop_bg (400x240 in 512x256 ETC1)
      music/shop_h.wav shop.gd HALLOWEEN music_bb_spooky_theme_slowed_reverb.wav (80 BPM), 32 kHz stereo PCM16
      stages/<id>/gfx/game_over_h.t3x  GameOver.gd HALLOWEEN game_over_halloween.png, processed like game_over.t3x"""
    H = g / "assets" / "seasonal" / "halloween"
    atlas = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    for cell, src in enumerate((g / "assets" / "images" / "game_heart_drippy.png", H / "candy_corn_outline.png", H / "candy_corn_filled.png",
                                H / "ui_particle_candy.png")):
        im = _fit(Image.open(src).convert("RGBA"), 64, 64)
        atlas.paste(im, (cell * 64 + (64 - im.width) // 2, (64 - im.height) // 2))
    web = Image.open(H / "Spider Web 8.png").convert("RGBA").resize((400, 190), Image.LANCZOS)
    atlas.paste(web, (0, 64))
    atlas.save(gen / "hw_game.png")
    tex3ds(gen / "hw_game.png", ui / "hw_game.t3x", "rgba8")

    K = 240.0 / 1080.0
    lv = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    for name, scale, pos in (("spooky_tree_2", 0.457923, (0, 0)), ("spooky_tree_4", 0.565561, (120, 0)),
                             ("simple_web_1", 0.734997, (250, 0)), ("simple_web_2", 0.736009, (250, 70))):
        im = Image.open(H / f"{name}.png").convert("RGBA")
        w, h = round(im.width * scale * K), round(im.height * scale * K)
        if h > 256:
            w, h = round(w * 256 / h), 256
        lv.paste(im.convert("RGBa").resize((w, h), Image.LANCZOS).convert("RGBA"), pos)
    lv.save(gen / "hw_levels.png")
    tex3ds(gen / "hw_levels.png", ui / "hw_levels.t3x", "etc1a4")

    im = Image.open(H / "halloween_shop.png").convert("RGBA")
    flat = Image.new("RGBA", im.size, (0, 0, 0, 255))
    flat.alpha_composite(im)
    canvas = Image.new("RGB", (512, 256), (0, 0, 0))
    canvas.paste(flat.convert("RGB").resize((400, 240), Image.LANCZOS), (0, 0))
    canvas.save(gen / "shop_bg_h.png")
    tex3ds(gen / "shop_bg_h.png", ui / "shop_bg_h.t3x", "etc1")

    subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(g / "assets" / "audio" / "music_bb_spooky_theme_slowed_reverb.wav"), "-ac", "2",
                    "-ar", "32000", "-c:a", "pcm_s16le", str(music / "shop_h.wav")], check=True)

    n = 0
    for stage in sorted((args.romfs / "stages").iterdir()):
        src = next(iter(sorted(g.glob(f"data/*/{stage.name}/images/game_over_halloween.png"))), None)
        if not stage.is_dir() or src is None or not (stage / "gfx").is_dir():
            continue
        rgba = Image.open(src).convert("RGBA")
        im = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
        im.alpha_composite(rgba)
        im = im.convert("RGB")
        k = max(400 / im.width, 240 / im.height)
        nw, nh = round(im.width * k), round(im.height * k)
        im = im.resize((nw, nh), Image.LANCZOS).crop(((nw - 400) // 2, (nh - 240) // 2, (nw - 400) // 2 + 400, (nh - 240) // 2 + 240))
        tex = Image.new("RGB", (512, 256), (0, 0, 0))
        tex.paste(im, (0, 0))
        tex.save(gen / f"go_h_{stage.name}.png")
        tex3ds(gen / f"go_h_{stage.name}.png", stage / "gfx" / "game_over_h.t3x", "etc1")
        n += 1
    print(f"[OK] halloween skins: hw_game, hw_levels, shop_bg_h, shop_h.wav, {n} game_over_h")


if __name__ == "__main__":
    sys.exit(main())
