#!/usr/bin/env python3
"""Convert a stage's climax video (keyframes `video`) into ETC1 frame pages + PCM audio.

Theora cannot be decoded in real time on the 3DS, so every source frame is pre-rendered
(cover-fit to 400x240) into 512x256 ETC1 cells, 8 cells per 1024x1024 page (one .t3x per page,
0.5 MiB each).  The app keeps two pages resident and pages the next one in on a worker thread.
The soundtrack is decoded to raw 32 kHz stereo PCM16.

BBVD v2 (little-endian):
    char magic[4]="BBVD"; u32 version=2; i64 start_us (chart time the video starts);
    i64 hide_us (chart time the picture is hidden: the next loop keyframe, or INT64_MAX for a final video; the soundtrack always plays out);
    f32 fps; u32 frames; u16 frame_w; u16 frame_h; u16 cell_w; u16 cell_h; u16 cols; u16 rows;
    u32 page_count; u32 audio_frames; char audio[32];
    per page: char t3x[32]
"""
from __future__ import annotations

import os
import argparse
import shutil
import struct
import subprocess
import tempfile
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from convert_stage import StageSchemaError, parse_godot_cfg_json  # noqa: E402

TEX3DS = Path(os.environ.get("BB_TEX3DS", "C:/devkitPro/tools/bin/tex3ds.exe"))   # the builder exe sets BB_TEX3DS
W, H, CELL_W, CELL_H, COLS, ROWS = 400, 240, 512, 256, 2, 4


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage-dir", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--romfs-dir", type=Path, help="stage romfs dir; files go to <dir>/video")
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--ffprobe", default="ffprobe")
    ap.add_argument("--quality", default="medium", choices=["low", "medium", "high"])
    args = ap.parse_args()

    stage = args.stage_dir.resolve()
    out = args.out_dir.resolve()
    if out == stage or stage in out.parents:
        ap.error("out-dir must not be inside the read-only stage-dir")
    out.mkdir(parents=True, exist_ok=True)

    kf = parse_godot_cfg_json(stage / "config" / "keyframes.cfg")
    vids = kf.get("video", [])
    if not vids:
        print(f"[SKIP] {stage.name}: no video keyframe")
        return 0
    loop_times = sorted(round(float(lp["timestamp"]) * 1_000_000) for lp in kf.get("loops", []))
    written: list[Path] = []
    for vi, entry in enumerate(sorted(vids, key=lambda e: float(e["timestamp"]))):
        src = stage / "video" / entry["path"]
        if not src.is_file():
            raise StageSchemaError(f"video not found: {src}")
        start_us = round(float(entry["timestamp"]) * 1_000_000)
        # game_outro_video / game_character: a later loop keyframe hides the picture (video.hide())
        later = [t for t in loop_times if t > start_us]
        hide_us = later[0] if later else 0x7FFFFFFFFFFFFFFF
        tag = f"{stage.name}_v{vi}"

        frames_dir = Path(tempfile.mkdtemp(prefix=f"{tag}_"))   # scratch frames (system temp, removed best-effort)
        # Every source frame, cover-fit to 400x240 (same crop the game view uses for the 16:9 art)
        subprocess.run([args.ffmpeg, "-v", "error", "-i", str(src), "-fps_mode", "passthrough",
                        "-vf", f"scale=-2:{H}:flags=lanczos,crop={W}:{H}", str(frames_dir / "%05d.png")], check=True)
        frames = sorted(frames_dir.glob("*.png"))
        if not frames:
            raise StageSchemaError("ffmpeg produced no frames")
        probe = subprocess.run([args.ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                                "stream=r_frame_rate", "-of", "csv=p=0", str(src)], check=True, capture_output=True, text=True).stdout.strip()
        num, den = (int(x) for x in probe.split("/"))
        fps = num / den

        per_page = COLS * ROWS
        pages = []
        for p0 in range(0, len(frames), per_page):
            page = Image.new("RGB", (1024, 1024), (0, 0, 0))
            for k, f in enumerate(frames[p0:p0 + per_page]):
                page.paste(Image.open(f).convert("RGB"), ((k % COLS) * CELL_W, (k // COLS) * CELL_H))
            name = f"{tag}_{p0 // per_page}"
            png = out / f"{name}.png"
            t3x = out / f"{name}.t3x"
            page.save(png)
            subprocess.run([str(TEX3DS), "-q", args.quality, "-f", "etc1", "-z", "auto", "-o", str(t3x), str(png)], check=True, capture_output=True)
            pages.append(t3x)

        pcm = subprocess.run([args.ffmpeg, "-v", "error", "-i", str(src), "-vn", "-ac", "2", "-ar", "32000", "-f", "s16le", "-"],
                             check=True, capture_output=True).stdout
        audio_name = f"{tag}.pcm"
        (out / audio_name).write_bytes(pcm)

        head = b"BBVD" + struct.pack("<IqqfIHHHHHHII32s", 2, start_us, hide_us, fps, len(frames), W, H, CELL_W, CELL_H, COLS, ROWS,
                                     len(pages), len(pcm) // 4, audio_name.encode("ascii")[:31])
        body = b"".join(struct.pack("<32s", p.name.encode("ascii")[:31]) for p in pages)
        manifest = out / f"{tag}.bbvid"
        manifest.write_bytes(head + body)
        total = sum(p.stat().st_size for p in pages) + len(pcm)
        print(f"[OK] {tag} ({entry['path']}): {len(frames)} frames @ {fps:g} fps ({len(frames) / fps:.2f} s) start {start_us / 1e6:.2f}s "
              f"hide {'never' if hide_us > 1 << 60 else f'{hide_us / 1e6:.2f}s'} -> {len(pages)} page(s), audio {len(pcm) / 1024:.0f} KiB, {total / 1048576:.2f} MiB")
        written += [manifest, out / audio_name, *pages]
        shutil.rmtree(frames_dir, ignore_errors=True)

    if args.romfs_dir:
        dest = args.romfs_dir / "video"
        dest.mkdir(parents=True, exist_ok=True)
        for f in written:
            shutil.copy2(f, dest / f.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
