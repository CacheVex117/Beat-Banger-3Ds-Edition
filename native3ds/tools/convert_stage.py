#!/usr/bin/env python3
"""Beat Banger Stage-to-Portable-3DS Converter (Milestone 1).

Reads a converted Beat Banger stage directory (e.g. data/bb_vol_1/cathy_e1)
STRICTLY READ-ONLY, validates its timing & event schema, and produces:
  1. Portable JSON chart files (generated/<stage_id>_<diff>.json)
  2. Binary BBCH v1 chart files (generated/<stage_id>_<diff>.bbchart & romfs/charts/...)
  3. 32,000 Hz 16-bit stereo RIFF WAV audio stream (romfs/audio/<stage_id>.wav)
  4. SHA-256 conversion manifest (generated/<stage_id>_manifest.json)

Explicitly rejects unsupported schema variants, malformed tempo maps, unsorted
events, or invalid input types rather than silently guessing.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from pathlib import Path
from typing import Any

BB_CHART_MAGIC = b"BBCH"
BB_CHART_VERSION = 1
MAX_NOTES = 512
MAX_SHUTTERS = 64
MAX_LOOPS = 32
MAX_BPM_CHANGES = 16


class StageSchemaError(ValueError):
    """Raised when a stage config violates the explicit conversion contract."""


def parse_godot_cfg_json(cfg_path: Path) -> dict[str, Any]:
    """Parse a Beat Banger .cfg file containing [main] and data={...}."""
    if not cfg_path.is_file():
        raise StageSchemaError(f"Missing config file: {cfg_path}")
    raw = cfg_path.read_text(encoding="utf-8")
    idx = raw.find("data")
    if idx < 0:
        raise StageSchemaError(f"{cfg_path.name}: missing 'data' key")
    eq_idx = raw.find("=", idx)
    if eq_idx < 0:
        raise StageSchemaError(f"{cfg_path.name}: missing '=' after 'data'")
    payload = raw[eq_idx + 1 :].strip()
    # The current game writes Godot constructors (e.g. meta.cfg `Color(0.3, 0.2, 0.3, 1)`); map them to arrays.
    payload = re.sub(r"\b(?:Color|Vector2i?|Vector3i?|Rect2)\(([^)]*)\)", r"[\1]", payload)
    payload = re.sub(r",(\s*[}\]])", r"\1", payload)  # Godot writes trailing commas
    try:
        parsed = json.loads(payload)
    except json.JSONDecodeError as exc:
        raise StageSchemaError(f"{cfg_path.name}: invalid JSON payload: {exc}") from exc
    if not isinstance(parsed, dict):
        raise StageSchemaError(f"{cfg_path.name}: root data must be a JSON object")
    return parsed


def _dec(value: Any, field_name: str, allow_negative: bool = False) -> Decimal:
    if isinstance(value, bool) or not isinstance(value, (int, float, Decimal)):
        raise StageSchemaError(f"{field_name}: expected numeric value, got {type(value).__name__}")
    try:
        d = Decimal(str(value))
    except InvalidOperation as exc:
        raise StageSchemaError(f"{field_name}: invalid numeric literal {value!r}") from exc
    if not d.is_finite():
        raise StageSchemaError(f"{field_name}: non-finite number")
    if not allow_negative and d < Decimal(0):
        raise StageSchemaError(f"{field_name}: negative value {d} not allowed")
    return d


def _sec_to_us(seconds: Decimal) -> int:
    return int((seconds * Decimal(1_000_000)).quantize(Decimal(1), rounding=ROUND_HALF_UP))


def normalize_modifiers(modifiers: Any) -> list[dict[str, int]]:
    """Validate and normalize keyframes['modifiers'] tempo map."""
    if not isinstance(modifiers, list) or len(modifiers) == 0:
        raise StageSchemaError("keyframes.modifiers: must be a non-empty list")
    if len(modifiers) > MAX_BPM_CHANGES:
        raise StageSchemaError(f"keyframes.modifiers: count {len(modifiers)} exceeds {MAX_BPM_CHANGES}")

    normalized: list[dict[str, int]] = []
    prev_beat: Decimal | None = None
    elapsed_us = Decimal(0)
    prev_bpm: Decimal | None = None

    for idx, entry in enumerate(modifiers):
        if not isinstance(entry, dict):
            raise StageSchemaError(f"keyframes.modifiers[{idx}]: expected object")
        if "bpm" not in entry or "timestamp" not in entry:
            raise StageSchemaError(f"keyframes.modifiers[{idx}]: requires 'bpm' and 'timestamp'")
        bpm = _dec(entry["bpm"], f"modifiers[{idx}].bpm")
        if bpm <= Decimal(0):
            raise StageSchemaError(f"modifiers[{idx}].bpm: must be > 0")
        beat_pos = _dec(entry["timestamp"], f"modifiers[{idx}].timestamp")
        if prev_beat is not None:
            if beat_pos < prev_beat:
                raise StageSchemaError(f"modifiers[{idx}].timestamp: out of order ({beat_pos} < {prev_beat})")
            assert prev_bpm is not None
            delta_beats = beat_pos - prev_beat
            elapsed_us += (delta_beats * Decimal(60_000_000)) / prev_bpm
        elif beat_pos != Decimal(0):
            raise StageSchemaError(f"modifiers[0].timestamp: first BPM modifier must start at 0, got {beat_pos}")

        bpm_milli = int((bpm * Decimal(1000)).quantize(Decimal(1), rounding=ROUND_HALF_UP))
        ts_us = int(elapsed_us.quantize(Decimal(1), rounding=ROUND_HALF_UP))
        normalized.append({"timestamp_us": ts_us, "beat": int(beat_pos), "bpm_milli": bpm_milli})
        prev_beat = beat_pos
        prev_bpm = bpm

    return normalized


def beat_to_us(beat: Decimal, modifiers_raw: list[dict[str, Any]]) -> int:
    """Convert a 0-based beat index to microseconds using the validated tempo map."""
    if beat < Decimal(0):
        raise StageSchemaError(f"beat_to_us: negative beat {beat}")
    active_bpm = _dec(modifiers_raw[0]["bpm"], "modifiers[0].bpm")
    prev_beat = Decimal(0)
    elapsed_sec = Decimal(0)

    for entry in modifiers_raw[1:]:
        mod_beat = _dec(entry["timestamp"], "modifier.timestamp")
        mod_bpm = _dec(entry["bpm"], "modifier.bpm")
        if beat <= mod_beat:
            break
        if mod_beat > prev_beat:
            elapsed_sec += (mod_beat - prev_beat) * Decimal(60) / active_bpm
            prev_beat = mod_beat
        active_bpm = mod_bpm

    elapsed_sec += (beat - prev_beat) * Decimal(60) / active_bpm
    return _sec_to_us(elapsed_sec)


def normalize_stage_shutters(shutter_entries: Any, modifiers_raw: list[dict[str, Any]]) -> list[int]:
    """Normalize mixed Beat Banger shutter entries ([beat] or {'timestamp': sec}) to microseconds."""
    if not isinstance(shutter_entries, list):
        raise StageSchemaError("keyframes.shutter: expected list")
    if len(shutter_entries) > MAX_SHUTTERS:
        raise StageSchemaError(f"keyframes.shutter: count {len(shutter_entries)} exceeds {MAX_SHUTTERS}")

    result_us: list[int] = []
    prev_us: int | None = None
    for i, entry in enumerate(shutter_entries):
        if isinstance(entry, list):
            if len(entry) != 1:
                raise StageSchemaError(f"shutter[{i}]: beat-index array must have length 1, got {len(entry)}")
            beat = _dec(entry[0], f"shutter[{i}][0]")
            ts_us = beat_to_us(beat, modifiers_raw)
        elif isinstance(entry, dict):
            if set(entry.keys()) != {"timestamp"}:
                raise StageSchemaError(f"shutter[{i}]: expected only 'timestamp' key, got {sorted(entry.keys())}")
            sec = _dec(entry["timestamp"], f"shutter[{i}].timestamp")
            ts_us = _sec_to_us(sec)
        else:
            raise StageSchemaError(f"shutter[{i}]: unsupported type {type(entry).__name__}")

        if prev_us is not None and ts_us < prev_us:
            raise StageSchemaError(f"shutter[{i}]: non-monotonic timestamp ({ts_us} < {prev_us})")
        result_us.append(ts_us)
        prev_us = ts_us

    return result_us


def normalize_loops(loop_entries: Any) -> list[dict[str, Any]]:
    """Normalize keyframes['loops'] section transitions."""
    if not isinstance(loop_entries, list):
        raise StageSchemaError("keyframes.loops: expected list")
    if len(loop_entries) > MAX_LOOPS:
        raise StageSchemaError(f"keyframes.loops: count {len(loop_entries)} exceeds {MAX_LOOPS}")

    result: list[dict[str, Any]] = []
    prev_us: int | None = None
    for i, entry in enumerate(loop_entries):
        if not isinstance(entry, dict) or "timestamp" not in entry or "animations" not in entry:
            raise StageSchemaError(f"loops[{i}]: requires 'timestamp' and 'animations'")
        sec = _dec(entry["timestamp"], f"loops[{i}].timestamp")
        ts_us = _sec_to_us(sec)
        if prev_us is not None and ts_us < prev_us:
            raise StageSchemaError(f"loops[{i}]: non-monotonic timestamp ({ts_us} < {prev_us})")
        anims = entry["animations"]
        normal_name = str(anims.get("normal", f"loop_{i}")).replace(".png", "")[:31]
        result.append({"timestamp_us": ts_us, "name": normal_name})
        prev_us = ts_us

    return result


def normalize_chart_notes(notes_raw: Any, chart_name: str) -> list[dict[str, int]]:
    """Normalize a single difficulty's notes list."""
    if not isinstance(notes_raw, list) or len(notes_raw) == 0:
        raise StageSchemaError(f"chart '{chart_name}': notes must be a non-empty list")
    if len(notes_raw) > MAX_NOTES:
        raise StageSchemaError(f"chart '{chart_name}': note count {len(notes_raw)} exceeds {MAX_NOTES}")

    result: list[dict[str, int]] = []
    prev_us: int | None = None
    for i, n in enumerate(notes_raw):
        if not isinstance(n, dict):
            raise StageSchemaError(f"chart '{chart_name}' note[{i}]: expected dict")
        if "timestamp" not in n or "input_type" not in n or "note_modifier" not in n:
            raise StageSchemaError(f"chart '{chart_name}' note[{i}]: missing required fields")

        # Note: Cathy E1 Expert note[0] has timestamp -0.000238095 (-238 us, sub-millisecond quantization near 0).
        # Allow tiny negative jitter >= -5 ms and clamp or preserve exact integer microseconds.
        sec = _dec(n["timestamp"], f"note[{i}].timestamp", allow_negative=True)
        if sec < Decimal("-0.005"):
            raise StageSchemaError(f"chart '{chart_name}' note[{i}]: negative timestamp {sec}s < -5ms")
        ts_us = max(0, _sec_to_us(sec))

        in_type = n["input_type"]
        mod = n["note_modifier"]
        if isinstance(in_type, bool) or not isinstance(in_type, int) or not (0 <= in_type <= 3):
            raise StageSchemaError(f"chart '{chart_name}' note[{i}]: invalid input_type {in_type!r}")
        if isinstance(mod, bool) or not isinstance(mod, int) or not (0 <= mod <= 3):
            raise StageSchemaError(f"chart '{chart_name}' note[{i}]: invalid note_modifier {mod!r}")

        if prev_us is not None and ts_us < prev_us:
            raise StageSchemaError(f"chart '{chart_name}' note[{i}]: unsorted timestamp ({ts_us} < {prev_us})")

        trigger_voice = 1 if bool(n.get("trigger_voice", False)) else 0
        hold_us = 0
        if mod == 3:  # HOLD: HoldNote.gd uses hold_end_timestamp = data.get("hold_end_timestamp", timestamp)
            end_raw = n.get("hold_end_timestamp", n["timestamp"])
            end_us = _sec_to_us(_dec(end_raw, f"note[{i}].hold_end_timestamp"))
            hold_us = end_us - ts_us
            if hold_us <= 0:
                # Authored data has holds without (or with an empty) tail (e.g. Claire E1 Virgin note 180).
                # The game completes them on the next frame; 1 us keeps "non-zero iff hold" in the format.
                hold_us = 1
            if hold_us > 0xFFFFFFFF:
                raise StageSchemaError(f"chart '{chart_name}' note[{i}]: invalid hold duration {hold_us} us")
        # a stray hold_end_timestamp on a non-hold note (e.g. Zoe E2 Expert note 77) is ignored, as GameNote does
        result.append(
            {
                "timestamp_us": ts_us,
                "input_type": in_type,
                "modifier": mod,
                "trigger_voice": trigger_voice,
                "hold_duration_us": hold_us,
            }
        )
        prev_us = ts_us

    return result


def pack_bbchart_binary(chart_doc: dict[str, Any]) -> bytes:
    """Serialize a normalized chart dictionary into the portable BBCH v1 binary format."""
    stage_id_b = chart_doc["stage_id"].encode("ascii", errors="replace")[:31]
    title_b = chart_doc["stage_title"].encode("ascii", errors="replace")[:47]
    diff_b = chart_doc["difficulty"].encode("ascii", errors="replace")[:23]

    notes = chart_doc["notes"]
    shutters = chart_doc["shutters_us"]
    loops = chart_doc["loops"]
    bpm_changes = chart_doc["bpm_changes"]

    header = struct.pack(
        "<4sI32s48s24sIIqqIIII",
        BB_CHART_MAGIC,
        BB_CHART_VERSION,
        stage_id_b,
        title_b,
        diff_b,
        int(chart_doc["rating"]),
        int(chart_doc["initial_bpm_milli"]),
        int(chart_doc["song_offset_us"]),
        int(chart_doc["note_offset_us"]),
        len(notes),
        len(shutters),
        len(loops),
        len(bpm_changes),
    )
    assert len(header) == 152

    chunks = [header]
    for n in notes:
        chunks.append(
            struct.pack(
                "<qBBBIx",
                int(n["timestamp_us"]),
                int(n["input_type"]),
                int(n["modifier"]),
                int(n["trigger_voice"]),
                int(n.get("hold_duration_us", 0)),
            )
        )
    for sh_us in shutters:
        chunks.append(struct.pack("<q", int(sh_us)))
    for lp in loops:
        lp_name = lp["name"].encode("ascii", errors="replace")[:31]
        chunks.append(struct.pack("<q32s", int(lp["timestamp_us"]), lp_name))
    for bc in bpm_changes:
        chunks.append(struct.pack("<qII", int(bc["timestamp_us"]), int(bc["bpm_milli"]), 0))

    return b"".join(chunks)


def convert_stage_configs(stage_dir: Path) -> list[dict[str, Any]]:
    """Read and validate all charts in a stage directory without modifying any source files."""
    cfg_dir = stage_dir / "config"
    settings = parse_godot_cfg_json(cfg_dir / "settings.cfg")
    meta = parse_godot_cfg_json(cfg_dir / "meta.cfg")
    asset = parse_godot_cfg_json(cfg_dir / "asset.cfg")
    keyframes = parse_godot_cfg_json(cfg_dir / "keyframes.cfg")
    notes_cfg = parse_godot_cfg_json(cfg_dir / "notes.cfg")

    song_offset_us = _sec_to_us(_dec(settings.get("song_offset", 0), "settings.song_offset", allow_negative=True))
    note_offset_us = _sec_to_us(_dec(settings.get("note_offset", 0), "settings.note_offset", allow_negative=True))

    bpm_changes = normalize_modifiers(keyframes.get("modifiers"))
    shutters_us = normalize_stage_shutters(keyframes.get("shutter", []), keyframes["modifiers"])
    loops = normalize_loops(keyframes.get("loops", []))

    charts_raw = notes_cfg.get("charts")
    if not isinstance(charts_raw, list) or len(charts_raw) == 0:
        raise StageSchemaError("notes.cfg: 'charts' must be a non-empty list")

    stage_id = stage_dir.name
    stage_title = str(meta.get("level_name", stage_id))
    song_filename = str(asset.get("song_path", ""))

    converted_charts: list[dict[str, Any]] = []
    for c in charts_raw:
        if not isinstance(c, dict) or "name" not in c or "notes" not in c:
            raise StageSchemaError("notes.cfg: chart entry missing 'name' or 'notes'")
        diff_name = str(c["name"])
        rating = int(c.get("rating", 0))
        norm_notes = normalize_chart_notes(c["notes"], diff_name)
        converted_charts.append(
            {
                "schema_version": BB_CHART_VERSION,
                "stage_id": stage_id,
                "stage_title": stage_title,
                "difficulty": diff_name,
                "rating": rating,
                "initial_bpm_milli": bpm_changes[0]["bpm_milli"],
                "song_offset_us": song_offset_us,
                "note_offset_us": note_offset_us,
                "song_filename": song_filename,
                "notes": norm_notes,
                "shutters_us": shutters_us,
                "loops": loops,
                "bpm_changes": bpm_changes,
            }
        )

    return converted_charts


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-dir", type=Path, required=True, help="Read-only source stage path")
    parser.add_argument("--out-dir", type=Path, required=True, help="Output directory for JSON/binary charts")
    parser.add_argument("--romfs-dir", type=Path, help="Optional romfs directory to populate for .3dsx build")
    parser.add_argument("--ffmpeg", type=Path, help="Optional path to ffmpeg executable for WAV conversion")
    parser.add_argument("--sample-rate", type=int, default=32000, help="Target PCM16 WAV sample rate (default: 32000)")
    args = parser.parse_args()

    stage_dir = args.stage_dir.resolve()
    out_dir = args.out_dir.resolve()
    if out_dir == stage_dir or stage_dir in out_dir.parents:
        parser.error("out-dir must not be inside the read-only stage-dir")

    charts = convert_stage_configs(stage_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    if args.romfs_dir:
        (args.romfs_dir / "charts").mkdir(parents=True, exist_ok=True)
        (args.romfs_dir / "audio").mkdir(parents=True, exist_ok=True)

    manifest: dict[str, Any] = {
        "stage_id": stage_dir.name,
        "source_dir": str(stage_dir),
        "charts": [],
        "audio": None,
    }

    for chart in charts:
        slug = f"{chart['stage_id']}_{chart['difficulty'].lower()}"
        json_path = out_dir / f"{slug}.json"
        bin_path = out_dir / f"{slug}.bbchart"

        json_path.write_text(json.dumps(chart, indent=2) + "\n", encoding="utf-8")
        blob = pack_bbchart_binary(chart)
        bin_path.write_bytes(blob)

        if args.romfs_dir:
            romfs_bin = args.romfs_dir / "charts" / f"{slug}.bbchart"
            romfs_bin.write_bytes(blob)

        manifest["charts"].append(
            {
                "difficulty": chart["difficulty"],
                "rating": chart["rating"],
                "note_count": len(chart["notes"]),
                "shutter_count": len(chart["shutters_us"]),
                "loop_count": len(chart["loops"]),
                "initial_bpm_milli": chart["initial_bpm_milli"],
                "song_offset_us": chart["song_offset_us"],
                "bbchart_file": bin_path.name,
                "bbchart_bytes": len(blob),
                "bbchart_sha256": sha256_file(bin_path),
            }
        )
        print(
            f"[OK] {slug}: {len(chart['notes'])} notes, "
            f"{len(chart['shutters_us'])} shutters, "
            f"{len(chart['loops'])} loops -> {bin_path.name} ({len(blob)} bytes)"
        )

    if args.ffmpeg and charts:
        song_name = charts[0]["song_filename"]
        src_mp3 = stage_dir / "audio" / song_name
        if not src_mp3.is_file():
            raise StageSchemaError(f"Audio file not found: {src_mp3}")
        wav_out = (args.romfs_dir / "audio" / f"{stage_dir.name}.wav") if args.romfs_dir else (out_dir / f"{stage_dir.name}.wav")
        cmd = [
            str(args.ffmpeg),
            "-y",
            "-v",
            "error",
            "-i",
            str(src_mp3),
            "-vn",
            "-acodec",
            "pcm_s16le",
            "-ar",
            str(args.sample_rate),
            "-ac",
            "2",
            "-f",
            "wav",
            "-bitexact",
            str(wav_out),
        ]
        subprocess.run(cmd, check=True)
        manifest["audio"] = {
            "source_mp3": song_name,
            "source_sha256": sha256_file(src_mp3),
            "wav_file": str(wav_out.name),
            "sample_rate": args.sample_rate,
            "channels": 2,
            "wav_bytes": wav_out.stat().st_size,
            "wav_sha256": sha256_file(wav_out),
        }
        print(f"[OK] Audio converted: {wav_out.name} ({wav_out.stat().st_size} bytes, {args.sample_rate} Hz stereo PCM16)")

    manifest_path = out_dir / f"{stage_dir.name}_manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"[OK] Wrote manifest: {manifest_path}")


if __name__ == "__main__":
    main()
