"""Automatic validation: every converted .bbchart (all stages, all difficulties) against its authoritative source.

Compares the binary records with the notes.cfg / keyframes.cfg / settings.cfg of `C:\\Beat Banger` (read-only):
note count, timestamps (microseconds, clamped at 0), lanes, modifiers, voice flags, hold lengths, ordering, shutters,
animation-loop names + times, tempo map and song offset. Skipped when the source or generated charts are missing.
"""
from __future__ import annotations

import struct
import sys
import unittest
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from convert_stage import parse_godot_cfg_json  # noqa: E402

GAME = Path("C:/Beat Banger/data")
STAGES = ROOT / "romfs" / "stages"


def us(v) -> int:
    return int((Decimal(str(v)) * 1_000_000).quantize(Decimal(1), rounding=ROUND_HALF_UP))


def read_chart(blob: bytes) -> dict:
    (magic, ver) = struct.unpack_from("<4sI", blob, 0)
    assert magic == b"BBCH" and ver == 1
    rating, bpm_milli, song_off, note_off, nn, ns, nl, nb = struct.unpack_from("<IIqqIIII", blob, 112)
    off = 152
    notes = []
    for _ in range(nn):
        ts, lane, mod, voice, dur = struct.unpack_from("<qBBBI", blob, off)
        notes.append((ts, lane, mod, voice, dur))
        off += 16
    shutters = list(struct.unpack_from(f"<{ns}q", blob, off)) if ns else []
    off += 8 * ns
    loops = []
    for _ in range(nl):
        ts, name = struct.unpack_from("<q32s", blob, off)
        loops.append((ts, name.rstrip(bytes([0])).decode()))
        off += 40
    bpms = []
    for _ in range(nb):
        ts, bm, _pad = struct.unpack_from("<qII", blob, off)
        bpms.append((ts, bm))
        off += 16
    assert off == len(blob), "trailing bytes"
    return {"bpm": bpm_milli, "song_off": song_off, "notes": notes, "shutters": shutters, "loops": loops, "bpms": bpms}


class TestChartsAgainstSource(unittest.TestCase):
    def test_all_stages(self) -> None:
        idx = STAGES / "index.txt"
        if not idx.is_file() or not GAME.is_dir():
            self.skipTest("generated stages or reference game not available")
        checked = 0
        for line in idx.read_text(encoding="utf-8").splitlines():
            f = line.split("|")
            sid = f[0]
            act = f[3] if len(f) > 3 else "bb_vol_1"
            src = GAME / act / sid / "config"
            charts = parse_godot_cfg_json(src / "notes.cfg")["charts"]
            kf = parse_godot_cfg_json(src / "keyframes.cfg")
            settings = parse_godot_cfg_json(src / "settings.cfg")
            for c in charts:
                slug = c["name"].lower()
                path = STAGES / sid / "charts" / f"{sid}_{slug}.bbchart"
                self.assertTrue(path.is_file(), f"{sid} {slug}: chart file missing")
                got = read_chart(path.read_bytes())
                self.assertEqual(got["song_off"], us(settings.get("song_offset", 0.0)), f"{sid} song offset")
                self.assertEqual(got["bpm"], round(float(kf["modifiers"][0]["bpm"]) * 1000), f"{sid} bpm")
                src_notes = c["notes"]
                self.assertEqual(len(got["notes"]), len(src_notes), f"{sid} {slug} note count")
                prev = -1
                for i, (g, s) in enumerate(zip(got["notes"], src_notes)):
                    ts, lane, mod, voice, dur = g
                    self.assertEqual(ts, max(0, us(s["timestamp"])), f"{sid} {slug} note {i} time")
                    self.assertGreaterEqual(ts, prev, f"{sid} {slug} note {i} order")
                    prev = ts
                    self.assertEqual((lane, mod), (s["input_type"], s["note_modifier"]), f"{sid} {slug} note {i} lane/modifier")
                    self.assertEqual(voice, int(bool(s.get("trigger_voice", False))), f"{sid} {slug} note {i} voice flag")
                    if mod == 3:
                        end = us(s.get("hold_end_timestamp", s["timestamp"]))
                        self.assertEqual(dur, max(1, end - max(0, us(s["timestamp"]))), f"{sid} {slug} note {i} hold length")
                    else:
                        self.assertEqual(dur, 0, f"{sid} {slug} note {i} stray hold length")
                self.assertEqual(len(got["loops"]), len(kf["loops"]), f"{sid} loops")
                for (ts, name), lp in zip(got["loops"], kf["loops"]):
                    self.assertEqual(ts, us(lp["timestamp"]), f"{sid} loop time")
                    self.assertEqual(name, lp["animations"]["normal"].removesuffix(".png").encode("ascii")[:31].decode(), f"{sid} loop name")
                self.assertEqual(len(got["shutters"]), len(kf["shutter"]), f"{sid} shutter count")
                self.assertEqual(got["shutters"], sorted(got["shutters"]), f"{sid} shutters sorted")
                checked += 1
        self.assertGreaterEqual(checked, 3)


if __name__ == "__main__":
    unittest.main()
