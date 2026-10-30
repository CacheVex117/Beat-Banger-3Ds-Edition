"""Build tiny synthetic mods on disk (no Beat Banger content) in the layout the game reads."""
from __future__ import annotations

import json
import struct
import wave
from pathlib import Path
from typing import Any

from PIL import Image


def write_cfg(path: Path, data: Any, section: str = "main", key: str = "data") -> None:
    # JSON text is valid Godot variant text for dict/list/str/number/bool/null
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"[{section}]\n\n{key}={json.dumps(data, indent=0, ensure_ascii=False)}\n", encoding="utf-8")


def write_wav(path: Path, seconds: float = 1.0, rate: int = 22050) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    n = int(seconds * rate)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", (i * 37) % 2000 - 1000) for i in range(n)))


def write_sheet(path: Path, h: int, v: int, frame=(64, 36)) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    im = Image.new("RGBA", (h * frame[0], v * frame[1]), (0, 0, 0, 0))
    for i in range(h * v):
        tile = Image.new("RGBA", frame, ((i * 40) % 256, 100, 200, 255))
        im.paste(tile, ((i % h) * frame[0], (i // h) * frame[1]))
    im.save(path)


def default_level() -> dict[str, Any]:
    return {
        "asset": {"song_path": "song.wav", "final_video": ""},
        "meta": {"level_name": "Synthetic Level", "character": "Testy", "level_index": 0, "level_id": "abc"},
        "settings": {"song_offset": 0.5, "note_offset": 0.0, "post_song_delay": 5.0},
        "mod": {"creator": "tests", "song_title": "Song", "song_author": "Nobody", "preview_timestamp": 0.0},
        "keyframes": {
            "modifiers": [{"bpm": 120.0, "timestamp": 0.0}],
            "shutter": [{"timestamp": 2.0}],
            "loops": [
                {"timestamp": 0.0, "animations": {"normal": "a.png"}, "sheet_data": {"h": 2, "v": 2, "total": 4}},
                {"timestamp": 1.0, "animations": {"normal": "b.png", "variations": ["a.png"]}, "sheet_data": {"h": 2, "v": 1, "total": 2}},
            ],
            "camera": [{"timestamp": 0.0, "position": [0, 0], "scale": [1, 1], "speed": 0.5}],
            "sound_loop": [{"timestamp": 0.0, "path": ["hit.wav"]}],
            "sound_oneshot": [{"timestamp": 1.5, "path": "hit.wav"}],
            "voice_bank": [{"timestamp": 0.0, "voice_paths": ["hit.wav"]}],
            "video": [], "background": [], "effects": [],
        },
        "notes": {"charts": [
            {"name": "Easy", "rating": 0, "notes": [
                {"timestamp": 0.5, "input_type": 0, "note_modifier": 0},
                {"timestamp": 1.0004, "input_type": 1, "note_modifier": 3, "hold_end_timestamp": 1.5},
                {"timestamp": 1.75, "input_type": 2, "note_modifier": 0, "trigger_voice": True},
            ]},
            {"name": "Hard", "rating": 2, "notes": [{"timestamp": 0.5, "input_type": 3, "note_modifier": 1}]},
        ]},
    }


def make_mod(root: Path, folder: str = "Synth Pack", level_folder: str = "default", level: dict[str, Any] | None = None,
             act: dict[str, Any] | None = None) -> Path:
    pack = root / folder
    write_cfg(pack / "act.cfg", act or {"act_name": "Synth Pack", "act_id": "deadbeef", "act_index": 0, "act_description": "line1\nline2"})
    lv = level or default_level()
    ldir = pack / level_folder
    for name in ("asset", "meta", "settings", "mod", "keyframes", "notes"):
        if name in lv:
            write_cfg(ldir / "config" / f"{name}.cfg", lv[name])
    write_wav(ldir / "audio" / "song.wav", 3.0)
    write_wav(ldir / "audio" / "hit.wav", 0.1)
    write_sheet(ldir / "images" / "a.png", 2, 2)
    write_sheet(ldir / "images" / "b.png", 2, 1)
    return pack
