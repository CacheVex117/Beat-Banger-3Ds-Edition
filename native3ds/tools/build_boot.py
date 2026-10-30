"""Build the boot sequence assets (source read-only: C:/Beat Banger) into romfs/boot/.

    python tools/build_boot.py --romfs romfs [--game "C:/Beat Banger"] [--ffmpeg ffmpeg] [--ffprobe ffprobe]

  keyart.t3x      assets/images/splash_cast.png (the cast + logo key art) fitted on black, 320x240 for the bottom screen (ETC1)
  rabbit.t3x      assets/images/bunfan_icon_rabbit_long_small.png (359x119, RGBA8)
  intro.bbvid     assets/video/bunfan_game_intro.ogv (scenes/Splash) as BBVD v2: 30 fps (the 60 fps source has more frames than
                  the player's 24 pages), cover-fit 400x240 ETC1 cells 8 per page, soundtrack as 32 kHz stereo PCM16 (intro.pcm)
"""
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_extras import tex3ds  # noqa: E402

W, H, CELL_W, CELL_H, COLS, ROWS = 400, 240, 512, 256, 2, 4
INTRO_FPS = 30
MAX_PAGES = 24   # platform/3ds/video.h BB_VIDEO_MAX_PAGES


def build_intro(src: Path, out: Path, work: Path, ffmpeg: str) -> None:
    frames_dir = Path(tempfile.mkdtemp(prefix="bb_intro_"))
    subprocess.run([ffmpeg, "-v", "error", "-i", str(src), "-vf", f"fps={INTRO_FPS},scale=-2:{H}:flags=lanczos,crop={W}:{H}",
                    str(frames_dir / "%05d.png")], check=True)
    frames = sorted(frames_dir.glob("*.png"))
    per_page = COLS * ROWS
    pages = []
    for p0 in range(0, len(frames), per_page):
        page = Image.new("RGB", (1024, 1024), (0, 0, 0))
        for k, f in enumerate(frames[p0:p0 + per_page]):
            page.paste(Image.open(f).convert("RGB"), ((k % COLS) * CELL_W, (k // COLS) * CELL_H))
        name = f"intro_{p0 // per_page}"
        page.save(work / f"{name}.png")
        tex3ds(work / f"{name}.png", out / f"{name}.t3x", "etc1")
        pages.append(f"{name}.t3x")
    if len(pages) > MAX_PAGES:
        raise SystemExit(f"intro: {len(pages)} pages > {MAX_PAGES}")
    pcm = subprocess.run([ffmpeg, "-v", "error", "-i", str(src), "-vn", "-ac", "2", "-ar", "32000", "-f", "s16le", "-"],
                         check=True, capture_output=True).stdout
    (out / "intro.pcm").write_bytes(pcm)
    head = b"BBVD" + struct.pack("<IqqfIHHHHHHII32s", 2, 0, 0x7FFFFFFFFFFFFFFF, float(INTRO_FPS), len(frames), W, H, CELL_W, CELL_H,
                                 COLS, ROWS, len(pages), len(pcm) // 4, b"intro.pcm")
    (out / "intro.bbvid").write_bytes(head + b"".join(struct.pack("<32s", p.encode("ascii")) for p in pages))
    shutil.rmtree(frames_dir, ignore_errors=True)
    print(f"[OK] intro: {len(frames)} frames @ {INTRO_FPS} fps ({len(frames) / INTRO_FPS:.2f} s), {len(pages)} pages, audio {len(pcm) // 1024} KiB")


def build_egg(src: Path, out: Path, work: Path, ffmpeg: str, ffprobe: str) -> None:
    """The warning screen's B easter egg: a green-screen clip, chroma-keyed (pure green #02FD00) and cover-fit 400x240, as BBVD v2
    with ETC1A4 cells (transparent where the green was) so it plays over the warning, and its soundtrack as 32 kHz PCM16."""
    frames_dir = Path(tempfile.mkdtemp(prefix="bb_egg_"))
    subprocess.run([ffmpeg, "-v", "error", "-i", str(src), "-vf",
                    f"scale=-2:{H}:flags=lanczos,crop={W}:{H},chromakey=0x02FD00:0.16:0.06,format=rgba",
                    str(frames_dir / "%05d.png")], check=True)
    frames = sorted(frames_dir.glob("*.png"))
    probe = subprocess.run([ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=r_frame_rate", "-of", "csv=p=0", str(src)],
                           check=True, capture_output=True, text=True).stdout.strip()
    num, den = (int(x) for x in probe.split("/"))
    fps = num / den
    per_page = COLS * ROWS
    pages = []
    for p0 in range(0, len(frames), per_page):
        page = Image.new("RGBA", (1024, 1024), (0, 0, 0, 0))
        for k, f in enumerate(frames[p0:p0 + per_page]):
            page.paste(Image.open(f).convert("RGBA"), ((k % COLS) * CELL_W, (k // COLS) * CELL_H))
        name = f"egg_{p0 // per_page}"
        page.save(work / f"{name}.png")
        tex3ds(work / f"{name}.png", out / f"{name}.t3x", "etc1a4")
        pages.append(f"{name}.t3x")
    if len(pages) > MAX_PAGES:
        raise SystemExit(f"egg: {len(pages)} pages > {MAX_PAGES}")
    pcm = subprocess.run([ffmpeg, "-v", "error", "-i", str(src), "-vn", "-ac", "2", "-ar", "32000", "-f", "s16le", "-"],
                         check=True, capture_output=True).stdout
    (out / "egg.pcm").write_bytes(pcm)
    head = b"BBVD" + struct.pack("<IqqfIHHHHHHII32s", 2, 0, 0x7FFFFFFFFFFFFFFF, float(fps), len(frames), W, H, CELL_W, CELL_H,
                                 COLS, ROWS, len(pages), len(pcm) // 4, b"egg.pcm")
    (out / "egg.bbvid").write_bytes(head + b"".join(struct.pack("<32s", p.encode("ascii")) for p in pages))
    shutil.rmtree(frames_dir, ignore_errors=True)
    print(f"[OK] egg: {len(frames)} frames @ {fps:.3f} fps ({len(frames) / fps:.2f} s), {len(pages)} pages, audio {len(pcm) // 1024} KiB")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--ffmpeg", default="C:/Tools/ffmpeg/bin/ffmpeg.exe")
    ap.add_argument("--ffprobe", default="C:/Tools/ffmpeg/bin/ffprobe.exe")
    ap.add_argument("--egg", type=Path, help="green-screen clip for the warning screen's B easter egg (user-supplied; romfs:/boot/egg.bbvid)")
    ap.add_argument("--egg-only", action="store_true", help="only (re)build the easter egg")
    ap.add_argument("--work", type=Path, default=Path(__file__).resolve().parent.parent / "generated" / "boot")
    args = ap.parse_args()
    out = args.romfs / "boot"
    out.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)

    if args.egg:
        build_egg(args.egg, out, args.work, args.ffmpeg, args.ffprobe)
    if args.egg_only:
        return 0

    cast = Image.open(args.game / "assets" / "images" / "splash_cast.png").convert("RGBA")
    k = 236 / cast.height
    cast = cast.resize((round(cast.width * k), 236), Image.LANCZOS)
    bottom = Image.new("RGBA", (320, 240), (0, 0, 0, 255))
    bottom.alpha_composite(cast, ((320 - cast.width) // 2, 2))
    bottom.convert("RGB").save(args.work / "keyart.png")
    tex3ds(args.work / "keyart.png", out / "keyart.t3x", "etc1")

    rabbit = Image.open(args.game / "assets" / "images" / "bunfan_icon_rabbit_long_small.png").convert("RGBA")
    rabbit.save(args.work / "rabbit.png")
    tex3ds(args.work / "rabbit.png", out / "rabbit.t3x", "rgba8")

    build_intro(args.game / "assets" / "video" / "bunfan_game_intro.ogv", out, args.work, args.ffmpeg)
    print(f"[OK] boot assets -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
