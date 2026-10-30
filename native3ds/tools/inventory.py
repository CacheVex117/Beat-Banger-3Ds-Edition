#!/usr/bin/env python3
"""Inventory and validate every level in the reference game's data tree (read-only).

    python tools/inventory.py --game "C:/Beat Banger" [--json out.json]

For each act/level it checks that the level is playable on the native port: config files parse,
at least one non-empty chart per difficulty, every loop sheet / climax video / sound / song exists,
and reports note counts, hold/ghost counts, tempo changes and unusual keyframe lanes.
A level is READY only if nothing blocking is found; the reasons are printed otherwise.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from convert_stage import StageSchemaError, parse_godot_cfg_json  # noqa: E402

KNOWN_LANES = {"background", "camera", "effects", "loops", "modifiers", "shutter", "sound_loop",
               "sound_oneshot", "video", "voice_bank", "toy", "vibe_control"}


def inspect_level(level: Path) -> dict:
    info: dict = {"id": level.name, "blockers": [], "notes": [], "unusual": []}
    cfg = level / "config"
    try:
        meta = parse_godot_cfg_json(cfg / "meta.cfg")
        asset = parse_godot_cfg_json(cfg / "asset.cfg")
        settings = parse_godot_cfg_json(cfg / "settings.cfg")
        kf = parse_godot_cfg_json(cfg / "keyframes.cfg")
        notes = parse_godot_cfg_json(cfg / "notes.cfg")
    except (StageSchemaError, OSError) as exc:
        info["blockers"].append(f"config: {exc}")
        return info
    info["title"] = meta.get("level_name", level.name)
    info["character"] = meta.get("character", level.name.split("_")[0])
    info["song_offset"] = settings.get("song_offset")
    song = asset.get("song_path", "")
    if not song or not (level / "audio" / song).is_file():
        info["blockers"].append(f"song missing: {song!r}")
    charts = notes.get("charts", [])
    info["charts"] = [c.get("name") for c in charts]
    if not charts:
        info["blockers"].append("no charts")
    for c in charts:
        n = c.get("notes", [])
        holds = sum(1 for x in n if x.get("note_modifier", 0) == 3)
        ghosts = sum(1 for x in n if x.get("note_modifier", 0) == 2)
        other = sorted({x.get("note_modifier", 0) for x in n} - {0, 2, 3})
        info["notes"].append({"chart": c.get("name"), "count": len(n), "holds": holds, "ghosts": ghosts, "other_modifiers": other,
                              "lanes": sorted({x["input_type"] for x in n}), "last": n[-1]["timestamp"] if n else None})
        if not n:
            info["blockers"].append(f"chart {c.get('name')} is empty")
        if len(n) > 512:
            info["blockers"].append(f"chart {c.get('name')} has {len(n)} notes (> 512)")
        if other:
            info["unusual"].append(f"chart {c.get('name')} uses note_modifier {other}")
    loops = kf.get("loops", [])
    if not loops:
        info["blockers"].append("no animation loops")
    for lp in loops:
        a = lp.get("animations", {})
        name = a.get("normal")
        if not name or not (level / "images" / name).is_file():
            info["blockers"].append(f"sheet missing: {name!r}")
        if a.get("variations"):
            info["unusual"].append(f"loop {name} has variations {[v.get('path') for v in a['variations']]}")
        sd = lp.get("sheet_data", {})
        if (int(sd.get("h", 0)), int(sd.get("v", 0)), int(sd.get("total", 0))) != (2, 3, 6):
            info["unusual"].append(f"sheet grid {sd} for {name}")
    for v in kf.get("video", []):
        if not (level / "video" / v["path"]).is_file():
            info["blockers"].append(f"video missing: {v['path']}")
    if len(kf.get("video", [])) > 1:
        info["unusual"].append("multiple video keyframes")
    for lane in ("sound_loop", "sound_oneshot"):
        for k in kf.get(lane, []):
            for p in (k["path"] if isinstance(k["path"], list) else [k["path"]]):
                if p and not (level / "audio" / p).is_file() and not (level.parent.parent / "_common" / "audio" / p).is_file():
                    info["blockers"].append(f"{lane} clip missing: {p}")
    for k in kf.get("voice_bank", []):
        for p in k.get("voice_paths", []):
            if not (level / "audio" / p).is_file():
                info["blockers"].append(f"voice clip missing: {p}")
    if len(kf.get("modifiers", [])) > 1:
        info["unusual"].append(f"{len(kf['modifiers'])} tempo changes")
    for lane in kf:
        if lane not in KNOWN_LANES:
            info["unusual"].append(f"unknown keyframe lane {lane}")
    for lane in ("background", "effects"):
        if kf.get(lane):
            info["unusual"].append(f"{lane} keyframes: {len(kf[lane])}")
    if not (level / "images" / "game_over.png").is_file():
        info["unusual"].append("no game_over.png (a default is needed)")
    info["ready"] = not info["blockers"]
    return info


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--json", type=Path)
    args = ap.parse_args()
    data = args.game / "data"
    out = []
    for act in sorted(p for p in data.iterdir() if p.is_dir() and (p / "act.cfg").is_file()):
        act_cfg = parse_godot_cfg_json(act / "act.cfg")
        for level in sorted(p for p in act.iterdir() if p.is_dir()):
            if not (level / "config").is_dir():
                continue
            r = inspect_level(level)
            r["act"] = act.name
            r["act_name"] = act_cfg.get("act_name")
            r["act_index"] = act_cfg.get("act_index")
            r["coming_soon"] = bool(act_cfg.get("coming_soon"))
            out.append(r)
    for r in out:
        flag = "READY  " if r.get("ready") else "BLOCKED"
        nc = ", ".join(f"{n['chart']}:{n['count']}" for n in r["notes"]) or "-"
        print(f"{flag} {r['act']}/{r['id']:12} {r.get('title', '?')!r:28} coming_soon={r['coming_soon']!s:5} notes[{nc}]")
        for b in r["blockers"]:
            print(f"          BLOCKER: {b}")
        for u in r["unusual"]:
            print(f"          note: {u}")
    if args.json:
        args.json.write_text(json.dumps(out, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
