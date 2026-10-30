"""Build the Extras > Music (sound_test.tscn) and Scene Viewer (scene_viewer.tscn) assets (source read-only: C:/Beat Banger).

    python tools/build_viewers.py --romfs romfs [--game "C:/Beat Banger"] [--ffmpeg ffmpeg] [--skip-audio]

  romfs/music/st_<k>.wav      the ten Sound Test records (assets/audio/rare_*.ogg) as stereo PCM16 at 22.05 kHz (half the size of
                               the 32 kHz stage songs; the streamer takes any rate from the WAV header)
  romfs/extras/st_base.t3x    studio monitor, back layers (ui_studio_bottom + ui_studio_speakers), 1800x1080 centre -> 400x240
  romfs/extras/st_inside.t3x  ui_studio_speakers_inside (pulses on the beat)
  romfs/extras/st_front.t3x   ui_studio_screen + ui_studio_screen_shine + ui_studio_top
  romfs/extras/st_record.t3x  the Record prefab: backing, centre (tinted per record), outline, alpha circle; 128 px cells
  romfs/extras/sv_thumbs.t3x  scene_viewer_thumb_<char>_<vol>.png, 96 px cells in the scene viewer's level order
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_extras import tex3ds  # noqa: E402

# sound_test.tscn MenuComponent records, in order (title, song, record colour, bpm, unlock item)
RECORDS = [
    ("Pre Alpha Theme", "rare_bb_menu_pre_alpha", 128, 201),
    ("1.0 New Theme", "rare_bb_menu_1.0v2", 128, 202),
    ("2.0 Theme", "rare_bb_menu_2.0", 126, 203),
    ("Legacy Bunny", "rare_legacy_judy", 128, 204),
    ("Legacy Goat", "rare_legacy_toriel", 128, 205),
    ("Legacy Dinosaur", "rare_legacy_yoshi", 140, 206),
    ("Legacy Wolf", "rare_legacy_jack", 126, 207),
    ("Cat Jam", "rare_catjam", 140, 297),
    ("BB PS1 JUNGLE", "rare_secret_shop", 160, 298),
    ("I Didn't Sleep", "rare_fun", 150, 299),
]
# scene_viewer.gd bumper_scenes order: thumb file per level
SV_THUMBS = ["cathy_1", "zoe_1", "gale_1", "claire_1", "dawna_1", "zoe_2", "cathy_2", "makayla_1"]
CROP = (60, 0, 1860, 1080)   # 1800x1080 centre = the 400x240 top screen at K = 240/1080
MUSIC_RATE = 22050


def layered(game: Path, names: list[str]) -> Image.Image:
    out = Image.new("RGBA", (1921, 1083), (0, 0, 0, 0))
    for n in names:
        out.alpha_composite(Image.open(game / "assets" / "ui" / f"{n}.png").convert("RGBA"))
    return out.crop(CROP).resize((400, 240), Image.LANCZOS)


def save_tex(im: Image.Image, size: tuple[int, int], gen: Path, out: Path, name: str, fmt: str) -> None:
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    canvas.paste(im, (0, 0))
    canvas.save(gen / f"{name}.png")
    tex3ds(gen / f"{name}.png", out / f"{name}.t3x", fmt)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--ffmpeg", default="C:/Tools/ffmpeg/bin/ffmpeg.exe")
    ap.add_argument("--gen", type=Path, default=Path(__file__).resolve().parent.parent / "generated" / "viewers")
    ap.add_argument("--skip-audio", action="store_true")
    args = ap.parse_args()
    game, gen = args.game, args.gen
    ext, music = args.romfs / "extras", args.romfs / "music"
    for d in (gen, ext, music):
        d.mkdir(parents=True, exist_ok=True)

    # studio monitor (Screen node at 960,540: every layer is a full-frame sprite)
    save_tex(layered(game, ["ui_studio_bottom", "ui_studio_speakers"]), (512, 256), gen, ext, "st_base", "etc1a4")
    save_tex(layered(game, ["ui_studio_speakers_inside"]), (512, 256), gen, ext, "st_inside", "etc1a4")
    save_tex(layered(game, ["ui_studio_screen", "ui_studio_screen_shine", "ui_studio_top"]), (512, 256), gen, ext, "st_front", "etc1a4")

    # Record prefab parts, 128 px cells: 0 backing, 1 centre, 2 outline, 3 alpha circle
    rec = Image.new("RGBA", (512, 128), (0, 0, 0, 0))
    for k, n in enumerate(["extras_record_backing", "extras_record_center", "alpha_outline", "alpha_circle"]):
        im = Image.open(game / "assets" / "images" / f"{n}.png").convert("RGBA")
        s = 128 / max(im.size)
        im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
        rec.paste(im, (k * 128 + (128 - im.width) // 2, (128 - im.height) // 2))
    rec.save(gen / "st_record.png")
    tex3ds(gen / "st_record.png", ext / "st_record.t3x", "rgba8")

    # scene viewer level thumbnails, 96 px cells, 5 per row
    th = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    for k, n in enumerate(SV_THUMBS):
        im = Image.open(game / "src" / "scenes" / "SceneViewer" / f"scene_viewer_thumb_{n}.png").convert("RGBA").resize((96, 96), Image.LANCZOS)
        th.paste(im, ((k % 5) * 96, (k // 5) * 96))
    th.save(gen / "sv_thumbs.png")
    tex3ds(gen / "sv_thumbs.png", ext / "sv_thumbs.t3x", "etc1a4")

    # scene viewer web frame (scene_viewer.tscn WebUi): the site logo (218x64 at x 0), the hamburger (64 at x 224) and the
    # FullscreenButton icon (64 at x 296), one 512x64 RGBA8 strip
    web = Image.new("RGBA", (512, 64), (0, 0, 0, 0))
    for x, rel in ((0, "assets/ui/ui_scene_preview_web_logo.png"), (224, "assets/ui/ui_scene_preview_web_hamburger.png"),
                   (296, "assets/ui/ui_scene_viewer_fullscreen_icon.png")):
        web.paste(Image.open(game / rel).convert("RGBA"), (x, 0))
    web.save(gen / "sv_web.png")
    tex3ds(gen / "sv_web.png", ext / "sv_web.t3x", "rgba8")

    if not args.skip_audio:
        for k, (title, song, _bpm, _item) in enumerate(RECORDS):
            src = game / "assets" / "audio" / f"{song}.ogg"
            subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(src), "-ac", "2", "-ar", str(MUSIC_RATE), "-c:a", "pcm_s16le",
                            str(music / f"st_{k}.wav")], check=True)
    print(f"[OK] viewers: studio + records + {len(SV_THUMBS)} thumbs -> {ext}; {0 if args.skip_audio else len(RECORDS)} sound-test WAVs -> {music}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
