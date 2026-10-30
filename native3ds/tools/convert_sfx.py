#!/usr/bin/env python3
"""Convert a stage's sound effects, voice banks and song into 3DS-ready files.

Reads (read-only) <stage>/config/keyframes.cfg (`sound_loop`, `sound_oneshot`, `voice_bank`) and
<stage>/audio/*.ogg, decodes every referenced clip with ffmpeg to 32 kHz mono PCM16 and packs
them into one BBSF bank.  Optionally converts the song (asset.cfg `song_path`) to the 32 kHz
stereo PCM16 WAV that audio_ndsp.c streams from RomFS.

BBSF v1 (little-endian):
    char magic[4]="BBSF"; u32 version=1; u32 sample_rate; u32 clip_count; u32 loop_count;
    u32 oneshot_count; u32 voice_count
    clips:    char name[32]; u32 byte_offset (into PCM blob); u32 frames        (x clip_count)
    loops:    i64 timestamp_us; u32 mode(0=cycle,1=random); u32 n; u16 clip[8]  (x loop_count)
    oneshots: i64 timestamp_us; u32 clip; u32 pad                                (x oneshot_count)
    voices:   i64 timestamp_us; u32 n; u16 clip[12]; u32 pad                      (x voice_count)
    PCM blob: concatenated s16le mono samples
"""
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from convert_stage import StageSchemaError, parse_godot_cfg_json  # noqa: E402

RATE = 32000
MAX_LOOP_CLIPS = 8
MAX_VOICE_CLIPS = 12
COMMON = {  # shared game sounds (assets/audio) -> clip names looked up by the app
    "__miss": "ui_back.ogg",              # NoteDetector.MissSound (absent press)
    "__damage": "game_damage_double.wav",  # Lives.DamageSound (counted miss)
    "__glass1": "sfx_glass_break_01.wav",  # GameOver.SFX_GLASS
    "__glass2": "sfx_glass_break_02.wav",
    "__glass3": "sfx_glass_break_03.wav",
}


def decode_mono(ffmpeg: str, src: Path) -> bytes:
    out = subprocess.run(
        [ffmpeg, "-v", "error", "-i", str(src), "-ac", "1", "-ar", str(RATE), "-f", "s16le", "-"],
        check=True, capture_output=True,
    ).stdout
    return out


def us(t) -> int:
    return round(float(t) * 1_000_000)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage-dir", type=Path)
    ap.add_argument("--out-dir", type=Path)
    ap.add_argument("--common-bank", type=Path, help="write only the shared miss / damage / glass clips (from --common-dir) to "
                    "this bank (romfs:/audio/common.bbsfx: the app plays them from it when a level's own bank has none, e.g. "
                    "SD-card mods) and stop")
    ap.add_argument("--romfs-dir", type=Path)
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--common-dir", type=Path, help="game assets/audio dir holding the shared miss/damage/glass sounds")
    ap.add_argument("--song-wav", action="store_true", help="also convert the song to a 32 kHz stereo WAV")
    args = ap.parse_args()

    if args.common_bank:
        if not args.common_dir:
            ap.error("--common-bank needs --common-dir")
        clips = []
        for cname, fname in COMMON.items():
            src = args.common_dir / fname
            if not src.is_file():
                raise StageSchemaError(f"common sound not found: {src}")
            clips.append((cname, decode_mono(args.ffmpeg, src)))
        tables, blob = bytearray(), bytearray()
        for name, pcm in clips:
            tables += struct.pack("<32sII", name.encode("ascii"), len(blob), len(pcm) // 2)
            blob += pcm
        args.common_bank.parent.mkdir(parents=True, exist_ok=True)
        args.common_bank.write_bytes(b"BBSF" + struct.pack("<IIIIII", 1, RATE, len(clips), 0, 0, 0) + bytes(tables) + bytes(blob))
        print(f"[OK] {args.common_bank.name}: {len(clips)} shared clips, {len(blob) / 1024:.0f} KiB PCM")
        return 0
    if not args.stage_dir or not args.out_dir:
        ap.error("--stage-dir and --out-dir are required")

    stage = args.stage_dir.resolve()
    out = args.out_dir.resolve()
    if out == stage or stage in out.parents:
        ap.error("out-dir must not be inside the read-only stage-dir")
    out.mkdir(parents=True, exist_ok=True)

    kf = parse_godot_cfg_json(stage / "config" / "keyframes.cfg")
    audio_dir = stage / "audio"
    clip_index: dict[str, int] = {}
    clips: list[tuple[str, bytes]] = []

    def clip(name: str) -> int:
        if name not in clip_index:
            src = audio_dir / name
            if not src.is_file():
                src = stage.parent.parent / "_common" / "audio" / name
            if not src.is_file():
                raise StageSchemaError(f"audio clip not found: {name}")
            clip_index[name] = len(clips)
            clips.append((Path(name).stem, decode_mono(args.ffmpeg, src)))
        return clip_index[name]

    if args.common_dir:
        for cname, fname in COMMON.items():
            src = args.common_dir / fname
            if not src.is_file():
                raise StageSchemaError(f"common sound not found: {src}")
            clip_index[cname] = len(clips)
            clips.append((cname, decode_mono(args.ffmpeg, src)))

    loops, oneshots, voices = [], [], []
    for k in kf.get("sound_loop", []):
        paths = k["path"] if isinstance(k["path"], list) else [k["path"]]
        if not paths or len(paths) > MAX_LOOP_CLIPS:
            raise StageSchemaError(f"sound_loop clip count {len(paths)} unsupported")
        loops.append((us(k["timestamp"]), 0, [clip(p) for p in paths]))
    for k in kf.get("sound_oneshot", []):
        oneshots.append((us(k["timestamp"]), clip(k["path"])))
    for k in kf.get("voice_bank", []):
        vp = k.get("voice_paths", [])
        if len(vp) > MAX_VOICE_CLIPS:
            raise StageSchemaError(f"voice bank with {len(vp)} clips unsupported")
        voices.append((us(k["timestamp"]), [clip(p) for p in vp]))

    head = b"BBSF" + struct.pack("<IIIIII", 1, RATE, len(clips), len(loops), len(oneshots), len(voices))
    tables = bytearray()
    blob = bytearray()
    for name, pcm in clips:
        if len(name) > 31:
            name = name[:31]
        tables += struct.pack("<32sII", name.encode("ascii")[:31], len(blob), len(pcm) // 2)
        blob += pcm
    for ts, mode, ids in loops:
        ids8 = ids + [0] * (MAX_LOOP_CLIPS - len(ids))
        tables += struct.pack("<qII8H", ts, mode, len(ids), *ids8)
    for ts, cid in oneshots:
        tables += struct.pack("<qII", ts, cid, 0)
    for ts, ids in voices:
        ids12 = ids + [0] * (MAX_VOICE_CLIPS - len(ids))
        tables += struct.pack("<qI12HI", ts, len(ids), *ids12, 0)

    data = head + bytes(tables) + bytes(blob)
    bank = out / f"{stage.name}.bbsfx"
    bank.write_bytes(data)
    print(f"[OK] {bank.name}: {len(clips)} clips, {len(blob) / 1024:.0f} KiB PCM, "
          f"{len(loops)} loop banks, {len(oneshots)} oneshots, {len(voices)} voice banks")

    files = [bank]
    if args.song_wav:
        asset = parse_godot_cfg_json(stage / "config" / "asset.cfg")
        song = audio_dir / asset["song_path"]
        wav = out / f"{stage.name}.wav"
        subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(song), "-ac", "2", "-ar", str(RATE),
                        "-c:a", "pcm_s16le", str(wav)], check=True)
        print(f"[OK] {wav.name}: {wav.stat().st_size / 1048576:.1f} MiB (source {song.name})")
        files.append(wav)

    if args.romfs_dir:
        (args.romfs_dir / "sfx").mkdir(parents=True, exist_ok=True)
        (args.romfs_dir / "audio").mkdir(parents=True, exist_ok=True)
        shutil.copy2(bank, args.romfs_dir / "sfx" / bank.name)
        if args.song_wav:
            shutil.copy2(wav, args.romfs_dir / "audio" / wav.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
