#!/usr/bin/env python3
"""Build every runtime asset of one or more stages into romfs/stages/<id>/ and update romfs/stages/index.txt.

    python tools/build_stage.py --game "C:/Beat Banger" --act bb_vol_1 --stages cathy_e1 zoe_e1 gale_e1 claire_e1 dawna_e1 \
        --romfs romfs --ffmpeg C:/Tools/ffmpeg/bin/ffmpeg.exe

Per stage this runs convert_stage (charts), convert_sprites (character sheets + game over art),
convert_sfx (sound bank + song WAV) and convert_video (climax frames + audio). Sources are read-only.
Stage specs are `<id>` (uses --act) or `<act_dir>/<id>` (e.g. bb_vol_2/cathy_e2).
index.txt lines: id|title|character|act_dir|act_name|act_index|level_index|level_enum|level_prereq   (rows keep act/level order;
level_prereq = meta.cfg "level_prereq": the item that opens the level, 0 = always open)
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from convert_stage import parse_godot_cfg_json  # noqa: E402


def run(script: str, *a: str) -> None:
    subprocess.run([sys.executable, str(HERE / script), *a], check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--act", default="bb_vol_1")
    ap.add_argument("--stages", nargs="+", required=True)
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--gen", type=Path, default=Path("generated"))
    ap.add_argument("--ffmpeg", default="ffmpeg")
    ap.add_argument("--ffprobe", default="ffprobe")
    ap.add_argument("--sprite-format", default="etc1a4")
    ap.add_argument("--skip-song", action="store_true")
    ap.add_argument("--steps", default="charts,sprites,sfx,video,cutscene", help="comma list of steps to run")
    args = ap.parse_args()

    stages_root = args.romfs / "stages"
    stages_root.mkdir(parents=True, exist_ok=True)
    idx_path = stages_root / "index.txt"
    entries: dict[str, str] = {}
    if idx_path.is_file():
        for line in idx_path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                entries[line.split("|")[0]] = line

    steps = set(args.steps.split(","))
    for spec in args.stages:
        act, sid = spec.split("/") if "/" in spec else (args.act, spec)
        sdir = args.game / "data" / act / sid
        dest = stages_root / sid
        gen = args.gen / sid
        print(f"===== {act}/{sid}")
        if "charts" in steps:
            run("convert_stage.py", "--stage-dir", str(sdir), "--out-dir", str(gen), "--romfs-dir", str(dest))
        if "sprites" in steps:
            run("convert_sprites.py", "--stage-dir", str(sdir), "--out-dir", str(gen / "spr"), "--romfs-dir", str(dest),
                "--format", args.sprite_format)
        if "sfx" in steps:
            sfx = ["convert_sfx.py", "--stage-dir", str(sdir), "--out-dir", str(gen), "--romfs-dir", str(dest),
                   "--ffmpeg", args.ffmpeg, "--common-dir", str(args.game / "assets" / "audio")]
            if not args.skip_song:
                sfx.append("--song-wav")
            run(*sfx)
        if "video" in steps:
            for old in (dest / "video").glob(f"{sid}.bbvid"):   # pre-v2 single-video manifest
                old.unlink()
            run("convert_video.py", "--stage-dir", str(sdir), "--out-dir", str(gen / "vid"), "--romfs-dir", str(dest),
                "--ffmpeg", args.ffmpeg, "--ffprobe", args.ffprobe)
        if "cutscene" in steps:
            run("convert_cutscene.py", "--stage-dir", str(sdir), "--out-dir", str(gen / "cs"), "--romfs-dir", str(dest), "--ffmpeg", args.ffmpeg)
        meta = parse_godot_cfg_json(sdir / "config" / "meta.cfg")
        act_cfg = parse_godot_cfg_json(args.game / "data" / act / "act.cfg")
        entries[sid] = "|".join([sid, str(meta.get("level_name", sid)), str(meta.get("character", sid.split("_")[0])), act,
                                 str(act_cfg.get("act_name", act)), str(act_cfg.get("act_index", 0)), str(meta.get("level_index", 0)),
                                 str(meta.get("level_enum", 0)), str(meta.get("level_prereq", 0))])   # BaseLevelNode: open when the prereq item is owned

    def sort_key(line: str):
        f = line.split("|")
        order = ["cathy", "zoe", "gale", "claire", "dawna", "makayla"]
        ch = f[2]
        return (int(f[5]) if len(f) > 5 else 0, order.index(ch) if ch in order else 99, f[0])
    lines = sorted(entries.values(), key=sort_key)
    idx_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[OK] index.txt: {len(lines)} stage(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
