"""Read a MOD2DS output folder back the way the native runtime loaders do, and check it.

Mirrors the acceptance rules of chart_loader.c (BBCH), anim.c (BBAN), sfx.c (BBSF), video.c (BBVD) and
main.c load_stage_index, plus the limits in limits.py and the path-length budget for an SD-card root.
For BBCH the authoritative check is host/verify_charts.bat, which runs the runtime's own C loader.
"""
from __future__ import annotations

import struct
from pathlib import Path

from . import formats, limits

SD_ROOT = "sdmc:/3ds/bb_mods"


def _name(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("ascii", errors="replace")


def check_bbchart(data: bytes) -> list[str]:
    errs = []
    if not 152 <= len(data) <= 1 << 20:
        return [f"size {len(data)} outside 152..1 MiB"]
    h = formats.BBCH_HEADER.unpack_from(data)
    if h[0] != b"BBCH" or h[1] != 1:
        return ["bad magic/version"]
    if h[6] == 0:
        errs.append("initial bpm is 0")
    n, s, lp, b = h[9:13]
    if len(data) != 152 + n * 16 + s * 8 + lp * 40 + b * 16:
        return [f"size {len(data)} does not match counts {n}/{s}/{lp}/{b}"]
    for what, count, lim in (("notes", n, limits.MAX_NOTES), ("shutters", s, limits.MAX_SHUTTERS),
                             ("loops", lp, limits.MAX_LOOPS), ("bpm changes", b, limits.MAX_BPM_CHANGES)):
        if count > lim:
            errs.append(f"{count} {what} > {lim}")
    off, prev = 152, None
    for i in range(n):
        ts, it, mod, _v, hold = formats.BBCH_NOTE.unpack_from(data, off)
        off += 16
        if prev is not None and ts < prev:
            errs.append(f"note {i} unsorted")
        if it > 3 or mod > 3 or (hold != 0) != (mod == 3):
            errs.append(f"note {i}: input {it} modifier {mod} hold {hold}")
        prev = ts
    for what, size, count in (("shutter", 8, s), ("loop", 40, lp), ("bpm", 16, b)):
        prev = None
        for i in range(count):
            ts = struct.unpack_from("<q", data, off)[0]
            off += size
            if prev is not None and ts < prev:
                errs.append(f"{what} {i} unsorted")
            prev = ts
    return errs


def check_bbanim(data: bytes, gfx: Path) -> list[str]:
    if not 12 <= len(data) <= limits.ANIM_FILE_MAX:
        return [f"size {len(data)} outside 12..{limits.ANIM_FILE_MAX}"]
    magic, ver, sheets = struct.unpack_from("<4sII", data)
    if magic != b"BBAN" or ver != 3:
        return ["bad magic/version"]
    errs, off = [], 12
    if sheets > limits.MAX_SHEETS:
        errs.append(f"{sheets} sheets > {limits.MAX_SHEETS}")
    for _ in range(sheets):
        name, frames, fw, fh, pages = formats.BBAN_SHEET.unpack_from(data, off)
        off += 44
        if pages > limits.MAX_PAGES_PER_SHEET:
            errs.append(f"sheet {_name(name)}: {pages} pages > {limits.MAX_PAGES_PER_SHEET}")
        if frames > limits.MAX_FRAMES_PER_SHEET:
            errs.append(f"sheet {_name(name)}: {frames} frames > {limits.MAX_FRAMES_PER_SHEET}")
        page_dims = []
        for _p in range(pages):
            t3x, tw, th = formats.BBAN_PAGE.unpack_from(data, off)
            off += 36
            page_dims.append((tw, th))
            if not (gfx / _name(t3x)).is_file():
                errs.append(f"missing page {_name(t3x)}")
        for _f in range(frames):
            page, _pad, x, y = formats.BBAN_FRAME.unpack_from(data, off)
            off += 6
            if page >= pages or x + fw > page_dims[page][0] or y + fh > page_dims[page][1]:
                errs.append(f"sheet {_name(name)}: frame rect outside its page")
    cams = struct.unpack_from("<I", data, off)[0]
    off += 4 + cams * 28
    if cams > limits.MAX_CAMERA_KEYS:
        errs.append(f"{cams} camera keys > {limits.MAX_CAMERA_KEYS}")
    if off + 4 <= len(data):
        nv = struct.unpack_from("<I", data, off)[0]
        off += 4 + nv * 68
    if off != len(data):
        errs.append(f"trailing/short data ({off} != {len(data)})")
    return errs


def check_bbsfx(data: bytes) -> list[str]:
    if not 28 <= len(data) <= limits.SFX_FILE_MAX:
        return [f"size {len(data)} outside 28..{limits.SFX_FILE_MAX}"]
    magic, ver, rate, clips, loops, shots, voices = formats.BBSF_HEADER.unpack_from(data)
    if magic != b"BBSF" or ver not in (1, 2):
        return ["bad magic/version"]
    errs = []
    for what, n, lim in (("clips", clips, limits.MAX_CLIPS), ("sound loops", loops, limits.MAX_SOUND_LOOPS),
                         ("oneshots", shots, limits.MAX_ONESHOTS), ("voice banks", voices, limits.MAX_VOICE_BANKS)):
        if n > lim:
            errs.append(f"{n} {what} > {lim}")
    if ver == 1:
        tables = 28 + clips * 40 + loops * 32 + shots * 16 + voices * 40
    else:   # v2: loop record = ts, mode, n, n ids; voice record = ts, n, n ids; each padded to 4 bytes
        tables = 28 + clips * 40
        for _ in range(loops):
            if tables + 16 > len(data):
                return errs + ["tables run past end of file"]
            n = struct.unpack_from("<I", data, tables + 12)[0]
            if n > limits.MAX_LOOP_CLIPS:
                errs.append(f"a sound loop has {n} clips > {limits.MAX_LOOP_CLIPS}")
            tables += (16 + 2 * n + 3) & ~3
        tables += shots * 16
        for _ in range(voices):
            if tables + 12 > len(data):
                return errs + ["tables run past end of file"]
            n = struct.unpack_from("<I", data, tables + 8)[0]
            if n > limits.MAX_VOICE_CLIPS:
                errs.append(f"a voice bank has {n} clips > {limits.MAX_VOICE_CLIPS}")
            tables += (12 + 2 * n + 3) & ~3
    blob = len(data) - tables
    if blob < 0:
        return errs + ["tables run past end of file"]
    for i in range(clips):
        _n, off, frames = formats.BBSF_CLIP.unpack_from(data, 28 + i * 40)
        if off + frames * 2 > blob:
            errs.append(f"clip {i} outside PCM blob")
    return errs


def check_bbvid(data: bytes, vdir: Path) -> list[str]:
    h = formats.BBVD_HEADER.unpack_from(data)
    if h[0] != b"BBVD" or h[1] != 2:
        return ["bad magic/version"]
    errs, pages = [], h[12]
    sound_only = h[2] == -(1 << 63) and h[3] == -(1 << 63)   # an outro final_audio without a picture (native3ds video.c)
    if not (0 if sound_only else 1) <= pages <= limits.MAX_VIDEO_PAGES:
        errs.append(f"{pages} pages outside 1..{limits.MAX_VIDEO_PAGES}")
    if not (vdir / _name(h[14])).is_file():
        errs.append(f"missing audio {_name(h[14])}")
    for i in range(pages):
        if not (vdir / _name(data[84 + i * 32:116 + i * 32])).is_file():
            errs.append(f"missing page {i}")
    return errs


def verify(out: Path, sd_root: str = SD_ROOT) -> dict[str, list[str]]:
    """Return {stage_id: [problems]} for every row of <out>/stages/index.txt (empty list = loads)."""
    results: dict[str, list[str]] = {}
    index = out / "stages" / "index.txt"
    rows = [r.split("|") for r in index.read_text(encoding="utf-8").splitlines() if r.strip()] if index.is_file() else []
    if len(rows) > limits.MAX_STAGES:
        results["(index)"] = [f"{len(rows)} mod levels; the game lists {limits.MAX_STAGES}"]
    for r in rows:
        sid = r[0]
        errs: list[str] = []
        if len(r) != 8:
            errs.append(f"index row has {len(r)} fields, expected 8")
        if len(sid) > limits.STAGE_ID_MAX or len(r[1]) > limits.TITLE_MAX or len(r[3]) > limits.ACT_ID_MAX:
            errs.append("index field over its length limit")
        if len(f"{sd_root}/stages/{sid}/gfx") > limits.GFX_DIR_MAX:
            errs.append(f"gfx dir path exceeds {limits.GFX_DIR_MAX} chars under {sd_root}")
        d = out / "stages" / sid
        for slot in limits.DIFFICULTY_SLOTS:
            p = d / "charts" / f"{sid}_{slot}.bbchart"
            errs += [f"{p.name}: {e}" for e in (check_bbchart(p.read_bytes()) if p.is_file() else ["missing"])]
        for p, fn in ((d / "gfx" / f"{sid}.bbanim", lambda b: check_bbanim(b, d / "gfx")),
                      (d / "sfx" / f"{sid}.bbsfx", check_bbsfx)):
            if p.is_file():
                errs += [f"{p.name}: {e}" for e in fn(p.read_bytes())]
        if not (d / "audio" / f"{sid}.wav").is_file():
            errs.append("song WAV missing (runtime falls back to a click track)")
        vids = sorted((d / "video").glob(f"{sid}_v*.bbvid"))
        if len(vids) > limits.MAX_VIDEOS:
            errs.append(f"{len(vids)} videos; the runtime only probes _v0.._v{limits.MAX_VIDEOS - 1}")
        for p in vids:
            errs += [f"{p.name}: {e}" for e in check_bbvid(p.read_bytes(), d / "video")]
        results[sid] = errs
    return results
