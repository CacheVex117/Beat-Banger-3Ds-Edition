"""Pre-release ("Legacy") mods: a chart.cfg ([EASY] section) and asset folders, no act.cfg.

The game converts them at play time (src/prefabs/LegacyConverter/legacy_converter.gd + template_generator.gd) into the
same in-memory configs a current mod loads from disk. to_act() writes exactly those configs into a regular act pack
folder, so the rest of MOD2DS converts it like any other mod:

    <dest>/<mod folder>/act.cfg
    <dest>/<mod folder>/legacy/config/{asset,meta,notes,keyframes,settings,mod}.cfg
    <dest>/<mod folder>/legacy/images/   anims/, anims/fx/, textures/   (the game loads every file by name)
    <dest>/<mod folder>/legacy/audio/    songs/, sfx/, voice/

Where the game's converter would stop on an error (no effect to turn into the climax when there is no last_transition),
the rest of the level is still converted.
"""
from __future__ import annotations

import hashlib
import json
import shutil
from pathlib import Path
from typing import Any

from . import godot_cfg

ASSET_DIRS = {"anims": "images", "anims/fx": "images", "textures": "images", "songs": "audio", "sfx": "audio", "voice": "audio"}
MARKER = ".mod2ds_legacy"


def find_chart(mod_dir: Path) -> Path | None:
    """The game lists a mods-folder entry whose own files include chart.cfg; a wrapper folder (zip) is looked into."""
    if (mod_dir / "chart.cfg").is_file():
        return mod_dir / "chart.cfg"
    found = sorted(mod_dir.rglob("chart.cfg"), key=lambda p: len(p.parts))
    return found[0] if found else None


def beats_to_secs(beats: float, bpm: float) -> float:
    """Math.f_beats_to_secs."""
    return float(beats / (bpm / 60.0))


def converted_chart(easy: dict[str, Any], bpm: float) -> list[dict[str, Any]]:
    """LegacyConverter.get_converted_chart, step for step."""
    initial = easy.get("initial_data") or {}
    init_note = int(initial.get("note_type", 0) or 0)
    no_spawn = set(easy.get("no_spawn") or [])
    half, quarter, eighth = set(easy.get("half_spawn") or []), set(easy.get("quarter_spawn") or []), set(easy.get("eighth_spawn") or [])
    last_beat = int((easy.get("last_beat") or [0])[0])
    if last_beat <= 0:
        last_beat = int((easy.get("lastBeat") or [0])[0])
    new_note = {"input_type": init_note + 1, "beat": [4, 3, 2, 1][min(max(init_note + 1, 0), 3)]}
    notes = []
    for i in range(last_beat):
        if i > 0:
            if i in half:
                new_note = {"input_type": 0, "beat": 4}
            elif i in quarter:
                new_note = {"input_type": 1, "beat": 2}
            elif i in eighth:
                new_note = {"input_type": 2, "beat": 1}
        else:
            if init_note == 0:
                new_note = {"input_type": -1, "beat": -1}
                continue
            new_note = {"input_type": [0, 1, 2][init_note - 1], "beat": [4, 2, 1][init_note - 1]}
        if i in no_spawn and i > 0:
            new_note = {"input_type": -1, "beat": -1}
            continue
        if i % new_note["beat"] == 0:
            if new_note["input_type"] == -1:
                continue
            notes.append({"input_type": new_note["input_type"], "note_modifier": 0, "timestamp": beats_to_secs(i, bpm * 2.0)})
    return notes


def converted_keyframes(easy: dict[str, Any], bpm: float, last_note_ts: float) -> dict[str, Any]:
    """LegacyConverter.get_converted_keyframes."""
    sheet = {"h": 3.0, "total": 6.0, "v": 2.0}
    initial = easy.get("initial_data") or {}
    transitions = easy.get("transitions") or {}
    last = easy.get("last_transition") or {}
    loops = [{"animations": {"normal": initial.get("animation", "")}, "position_offset": {"x": 0.0, "y": 0.0},
              "sheet_data": dict(sheet), "timestamp": 0.0}]
    effects: list[dict[str, Any]] = []
    sound_oneshot: list[dict[str, Any]] = []
    sound_loop = [{"path": initial.get("sound_fx", ""), "timestamp": 0.0}]
    background: list[dict[str, Any]] = []
    for t, tr in transitions.items():
        ts = beats_to_secs(float(t), bpm * 2.0)
        if tr.get("animation"):
            loops.append({"animations": {"normal": tr["animation"]}, "position_offset": {"x": 0.0, "y": 0.0},
                          "sheet_data": dict(sheet), "timestamp": ts})
        if tr.get("effects"):
            effects.append({"duration": beats_to_secs(1, bpm), "path": tr["effects"], "sheet_data": dict(sheet), "timestamp": ts})
        if tr.get("transition_sound"):
            sound_oneshot.append({"path": tr["transition_sound"], "timestamp": ts})
        if tr.get("sound_fx"):
            sound_loop.append({"path": tr["sound_fx"], "timestamp": ts})
        bg = tr.get("background")
        if bg:
            background.append({"path": bg[0], "type": (bg[1] if len(bg) > 1 and isinstance(bg[1], dict) else {}).get("static", False),
                               "timestamp": ts})
    if not last:
        if effects:   # the game's converter indexes effects[-1] unconditionally
            effects[-1]["sheet_data"] = {"h": 6.0, "total": 24.0, "v": 4.0}
            effects[-1]["duration"] = 5.0
    else:
        loops.append({"animations": {"normal": last.get("animation", "")}, "position_offset": {"x": 0.0, "y": 0.0},
                      "sheet_data": dict(sheet), "timestamp": last_note_ts})
        effects.append({"duration": 5.0, "path": last.get("effects", ""), "sheet_data": {"h": 6.0, "total": 24.0, "v": 4.0},
                        "timestamp": last_note_ts})
    return {"loops": loops, "effects": effects, "background": background, "modifiers": [{"bpm": bpm, "timestamp": 0.0}],
            "shutter": [], "sound_loop": sound_loop, "sound_oneshot": sound_oneshot, "voice_bank": []}


def _write_cfg(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    # JSON is a subset of Godot's Variant text for these values (dicts, lists, strings, numbers, booleans)
    path.write_text("[main]\n\ndata = " + json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")


def _signature(src: Path) -> str:
    h = hashlib.sha1(b"legacy-v1")
    for f in sorted(src.rglob("*")):
        if f.is_file():
            st = f.stat()
            h.update(f"{f.relative_to(src).as_posix()}|{st.st_size}|{int(st.st_mtime)}\n".encode("utf-8"))
    return h.hexdigest()


def to_act(mod_dir: Path, dest_root: Path) -> Path:
    """Write mod_dir (a Legacy mod) as an act pack under dest_root; reused while the source is unchanged."""
    chart = find_chart(mod_dir)
    if chart is None:
        raise ValueError(f"{mod_dir}: no chart.cfg")
    root = chart.parent
    out = dest_root / mod_dir.name
    sig = _signature(root)
    if (out / MARKER).is_file() and (out / MARKER).read_text(encoding="utf-8") == sig:
        return out
    shutil.rmtree(out, ignore_errors=True)
    easy = godot_cfg.load(chart).get("EASY", {})   # ConfigFile.load: a malformed chart.cfg raises CfgError like the game fails
    name = str(easy.get("name", "Legacy Mod") or "Legacy Mod")
    bpm = float(easy.get("bpm", 0.0) or 0.0)
    if bpm <= 0:
        raise ValueError(f"{chart.name}: bpm {bpm} (the game cannot time this chart)")
    notes = converted_chart(easy, bpm)
    if not notes:
        raise ValueError(f"{chart.name}: no notes (last_beat / spawn lists empty)")
    level = out / "legacy"
    # template_generator.gd (meta.character is the literal "character" in the game; the act name reads better on 3DS)
    _write_cfg(out / "act.cfg", {"act_description": "Converted From Legacy", "act_index": 0, "act_name": name, "act_legacy": True})
    _write_cfg(level / "config" / "asset.cfg", {"song_path": str(easy.get("song_path", "") or ""), "horny_mode_sound": "", "final_video": ""})
    _write_cfg(level / "config" / "meta.cfg", {"character": name, "level_index": 0, "level_name": name})
    _write_cfg(level / "config" / "notes.cfg", {"charts": [{"name": "Normal", "rating": 1, "notes": notes}]})
    _write_cfg(level / "config" / "keyframes.cfg", converted_keyframes(easy, bpm, notes[-1]["timestamp"]))
    _write_cfg(level / "config" / "settings.cfg",
               {"song_offset": 0.0, "post_song_delay": float(easy.get("post_song_delay", 5.0) or 5.0), "background_type": 0})
    _write_cfg(level / "config" / "mod.cfg", {"mod_title": name, "description": "Converted From Legacy", "song_title": "", "song_creator": ""})
    for src_dir, kind in ASSET_DIRS.items():   # Assets.load_asset by file name: a later folder wins a name clash, as in the game
        d = root / src_dir
        if not d.is_dir():
            continue
        for f in sorted(d.iterdir()):
            if f.is_file():
                (level / kind).mkdir(parents=True, exist_ok=True)
                shutil.copy2(f, level / kind / f.name)
    thumb = next((p for p in (root / "bbmods_thumb.webp", root / "bbmods_thumb.png", root / "thumb.png") if p.is_file()), None)
    if thumb is not None:
        try:
            from PIL import Image
            with Image.open(thumb) as im:
                im.convert("RGBA").save(level / "thumb.png")
        except OSError:
            pass
    (out / MARKER).write_text(sig, encoding="utf-8")
    return out
