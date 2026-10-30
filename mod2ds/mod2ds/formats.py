"""Byte-exact writers for the native Beat Banger 3DS runtime formats.

Layouts are copied from native3ds (tools/convert_*.py writers, core/chart_loader.c and platform/3ds
loaders) so a MOD2DS stage folder is indistinguishable from one built by native3ds/tools/build_stage.py.
tests/test_formats.py pins the struct sizes; when native3ds is present it also cross-checks BBCH output.

    BBCH v1  charts/<id>_<slot>.bbchart   152-byte header, 16-byte notes, i64 shutters, 40-byte loops, 16-byte tempo
    BBAN v3  gfx/<id>.bbanim              sheets -> pages (t3x) -> frame rects, camera keys, variations
    BBSF v1  sfx/<id>.bbsfx               clip table + loop/oneshot/voice tables + mono s16 PCM blob
    BBSF v2  (only when a loop has > 8 or a voice bank > 12 clips) variable-length loop / voice records
    BBVD v2  video/<id>_v<n>.bbvid        ETC1 frame pages + raw stereo s16 PCM
"""
from __future__ import annotations

import struct
from typing import Any, Iterable

from . import limits

BBCH_HEADER = struct.Struct("<4sI32s48s24sIIqqIIII")
BBCH_NOTE = struct.Struct("<qBBBIx")
BBCH_LOOP = struct.Struct("<q32s")
BBCH_BPM = struct.Struct("<qII")
BBAN_SHEET = struct.Struct("<32sIHHI")
BBAN_PAGE = struct.Struct("<32sHH")
BBAN_FRAME = struct.Struct("<BBHH")
BBAN_CAMERA = struct.Struct("<qfffff")
BBAN_VARIATION = struct.Struct("<32s32sf")
BBSF_HEADER = struct.Struct("<4sIIIIII")
BBSF_CLIP = struct.Struct("<32sII")
BBSF_LOOP = struct.Struct("<qII8H")
BBSF_ONESHOT = struct.Struct("<qII")
BBSF_VOICE = struct.Struct("<qI12HI")
BBVD_HEADER = struct.Struct("<4sIqqfIHHHHHHII32s")


def cname(s: str, size: int = 32) -> bytes:
    """char[size] field: ASCII, truncated to size-1 so it stays NUL-terminated (native writers do the same)."""
    return s.encode("ascii", errors="replace")[: size - 1]


def pack_bbchart(*, stage_id: str, title: str, difficulty: str, rating: int, song_offset_us: int, note_offset_us: int,
                 notes: list[dict[str, int]], shutters_us: list[int], loops: list[dict[str, Any]],
                 bpm_changes: list[dict[str, int]]) -> bytes:
    out = [BBCH_HEADER.pack(b"BBCH", 1, cname(stage_id), cname(title, 48), cname(difficulty, 24), int(rating),
                            int(bpm_changes[0]["bpm_milli"]), int(song_offset_us), int(note_offset_us),
                            len(notes), len(shutters_us), len(loops), len(bpm_changes))]
    out += [BBCH_NOTE.pack(n["timestamp_us"], n["input_type"], n["modifier"], n["trigger_voice"], n["hold_duration_us"]) for n in notes]
    out += [struct.pack("<q", t) for t in shutters_us]
    out += [BBCH_LOOP.pack(lp["timestamp_us"], cname(lp["name"])) for lp in loops]
    out += [BBCH_BPM.pack(b["timestamp_us"], b["bpm_milli"], 0) for b in bpm_changes]
    return b"".join(out)


def pack_bbanim(sheets: list[dict[str, Any]], cameras: list[dict[str, Any]], variations: list[tuple[str, str, float]]) -> bytes:
    """sheets: {name, frame_w, frame_h, pages:[{t3x, tex_w, tex_h}], frames:[(page, x, y)]}."""
    out = bytearray(b"BBAN" + struct.pack("<II", 3, len(sheets)))
    for s in sheets:
        out += BBAN_SHEET.pack(cname(s["name"]), len(s["frames"]), s["frame_w"], s["frame_h"], len(s["pages"]))
        for p in s["pages"]:
            out += BBAN_PAGE.pack(cname(p["t3x"]), p["tex_w"], p["tex_h"])
        for page, x, y in s["frames"]:
            out += BBAN_FRAME.pack(page, 0, x, y)
    out += struct.pack("<I", len(cameras))
    for c in cameras:
        out += BBAN_CAMERA.pack(c["timestamp_us"], c["pos"][0], c["pos"][1], c["scale"][0], c["scale"][1], c["speed"])
    out += struct.pack("<I", len(variations))
    for normal, var, weight in variations:
        out += BBAN_VARIATION.pack(cname(normal), cname(var), weight)
    return bytes(out)


def pack_bbsfx(clips: list[tuple[str, bytes]], loops: Iterable[tuple[int, int, list[int]]],
               oneshots: Iterable[tuple[int, int]], voices: Iterable[tuple[int, list[int]]],
               rate: int = limits.SAMPLE_RATE) -> bytes:
    """v1 (the fixed records every runtime reads) unless a loop or voice bank has more clips than v1 holds; then v2."""
    loops = [(ts, mode, ids[: limits.MAX_LOOP_CLIPS]) for ts, mode, ids in loops]
    voices = [(ts, ids[: limits.MAX_VOICE_CLIPS]) for ts, ids in voices]
    oneshots = list(oneshots)
    v2 = any(len(ids) > limits.V1_LOOP_SLOTS for _t, _m, ids in loops) or any(len(ids) > limits.V1_VOICE_SLOTS for _t, ids in voices)
    tables = bytearray()
    blob = bytearray()
    for name, pcm in clips:
        tables += BBSF_CLIP.pack(cname(name), len(blob), len(pcm) // 2)
        blob += pcm
    for ts, mode, ids in loops:
        if v2:
            rec = struct.pack(f"<qII{len(ids)}H", ts, mode, len(ids), *ids)
            tables += rec + bytes(-len(rec) % 4)
        else:
            tables += BBSF_LOOP.pack(ts, mode, len(ids), *(ids + [0] * (limits.V1_LOOP_SLOTS - len(ids))))
    for ts, cid in oneshots:
        tables += BBSF_ONESHOT.pack(ts, cid, 0)
    for ts, ids in voices:
        if v2:
            rec = struct.pack(f"<qI{len(ids)}H", ts, len(ids), *ids)
            tables += rec + bytes(-len(rec) % 4)
        else:
            tables += BBSF_VOICE.pack(ts, len(ids), *(ids + [0] * (limits.V1_VOICE_SLOTS - len(ids))), 0)
    head = BBSF_HEADER.pack(b"BBSF", 2 if v2 else 1, rate, len(clips), len(loops), len(oneshots), len(voices))
    return head + bytes(tables) + bytes(blob)


def pack_bbvid(*, start_us: int, hide_us: int, fps: float, frames: int, pages: list[str], audio_frames: int, audio_name: str,
               frame_w: int = 400, frame_h: int = 240, cell_w: int = 512, cell_h: int = 256, cols: int = 2, rows: int = 4) -> bytes:
    head = BBVD_HEADER.pack(b"BBVD", 2, start_us, hide_us, fps, frames, frame_w, frame_h, cell_w, cell_h, cols, rows,
                            len(pages), audio_frames, cname(audio_name))
    return head + b"".join(struct.pack("<32s", cname(p)) for p in pages)


def index_line(stage_id: str, title: str, character: str, act: str, act_name: str, act_index: int, level_index: int,
               level_enum: int = 0) -> str:
    """romfs:/stages/index.txt row (main.c load_stage_index): id|title|character|act|act_name|act_index|level_index|level_enum."""
    fields = [stage_id, title, character, act, act_name, str(act_index), str(level_index), str(level_enum)]
    assert all("|" not in f and "\n" not in f for f in fields), fields
    return "|".join(fields)
