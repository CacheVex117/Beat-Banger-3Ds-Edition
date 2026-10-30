"""build_previews.py --romfs romfs --ffmpeg ffmpeg.exe : level-select preview clips (romfs/music/pv_<stage>.wav).

Original: BaseLevelNode.gd sets preview_timestamp = Math.f_beats_to_secs(64, 128) = 30.0 s for standard levels and
SongPreview.play_preview_music() plays the song from there, looping. The port streams a 24 s clip (32 kHz stereo PCM16, like the stage
audio it is cut from) through the scene-music path and loops it.
"""
import argparse
import subprocess
from pathlib import Path

START_S = 64 * 60.0 / 128.0   # 30.0
LEN_S = 24

ap = argparse.ArgumentParser()
ap.add_argument("--romfs", type=Path, required=True)
ap.add_argument("--ffmpeg", default="ffmpeg")
a = ap.parse_args()
out = a.romfs / "music"
out.mkdir(parents=True, exist_ok=True)
n = 0
for line in (a.romfs / "stages" / "index.txt").read_text(encoding="utf-8").splitlines():
    if not line.strip():
        continue
    sid = line.split("|")[0]
    src = a.romfs / "stages" / sid / "audio" / f"{sid}.wav"
    if not src.is_file():
        print("skip", sid)
        continue
    subprocess.run([a.ffmpeg, "-v", "error", "-y", "-ss", f"{START_S}", "-t", str(LEN_S), "-i", str(src), "-ac", "2", "-ar", "32000",
                    "-c:a", "pcm_s16le", str(out / f"pv_{sid}.wav")], check=True)
    n += 1
print(f"[OK] {n} preview clips, {sum(f.stat().st_size for f in out.glob('pv_*.wav')) / 1048576:.1f} MiB in {out}")

# Settings > Gameplay > Calibration Menu: calibrator_jam.ogg (100 BPM); 24 s = 40 beats, so the loop keeps the beat phase
subprocess.run([a.ffmpeg, "-v", "error", "-y", "-t", "24", "-i", "C:/Beat Banger/assets/audio/calibrator_jam.ogg", "-ac", "2", "-ar", "32000",
                "-c:a", "pcm_s16le", str(out / "calib.wav")], check=True)
print("[OK] calib.wav")
