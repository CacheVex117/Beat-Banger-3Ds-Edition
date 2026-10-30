"""Structure tests for the sprite (BBAN) and sound (BBSF) converters, against the real Cathy E1 source.

Skipped when C:\\Beat Banger or the generated outputs are not available.
"""
from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

STAGE = Path("C:/Beat Banger/data/bb_vol_1/cathy_e1")
BBANIM = ROOT / "romfs" / "stages" / "cathy_e1" / "gfx" / "cathy_e1.bbanim"
BBSFX = ROOT / "romfs" / "stages" / "cathy_e1" / "sfx" / "cathy_e1.bbsfx"


class TestBBAN(unittest.TestCase):
    def setUp(self) -> None:
        if not BBANIM.is_file() or not STAGE.is_dir():
            self.skipTest("generated sprite manifest or source stage not available")

    def test_manifest_matches_keyframes(self) -> None:
        from convert_stage import parse_godot_cfg_json

        kf = parse_godot_cfg_json(STAGE / "config" / "keyframes.cfg")
        d = BBANIM.read_bytes()
        magic, ver, sheets = struct.unpack_from("<4sII", d, 0)
        self.assertEqual((magic, ver), (b"BBAN", 3))
        self.assertGreaterEqual(sheets, len(kf["loops"]))  # + variation sheets
        off = 12
        names = []
        for _ in range(sheets):
            name, frames, fw, fh, pages = struct.unpack_from("<32sIHHI", d, off)
            off += 44
            names.append(name.rstrip(b"\0").decode())
            self.assertEqual(frames, 6)
            self.assertEqual((fw, fh), (512, 288))
            self.assertGreaterEqual(pages, 1)
            for _p in range(pages):
                t3x, tw, th = struct.unpack_from("<32sHH", d, off)
                off += 36
                self.assertLessEqual(tw, 1024)
                self.assertLessEqual(th, 1024)
                self.assertTrue((ROOT / "romfs" / "stages" / "cathy_e1" / "gfx" / t3x.rstrip(b"\0").decode()).is_file())
            for _f in range(frames):
                page, _pad, x, y = struct.unpack_from("<BBHH", d, off)
                off += 6
                self.assertLess(page, pages)
                self.assertLessEqual(x + fw, 1024)
                self.assertLessEqual(y + fh, 1024)
        self.assertEqual(names[:len(kf["loops"])], [x["animations"]["normal"].removesuffix(".png")[:31] for x in kf["loops"]][:len(names)])
        (cams,) = struct.unpack_from("<I", d, off)
        off += 4
        self.assertEqual(cams, len(kf["camera"]))
        for i in range(cams):
            ts, px, py, sx, sy, dur = struct.unpack_from("<qfffff", d, off)
            off += 28
            c = kf["camera"][i]
            self.assertEqual(ts, round(c["timestamp"] * 1_000_000))
            self.assertAlmostEqual(px, c["position"][0])
            self.assertAlmostEqual(sx, c["scale"][0], places=5)
            self.assertAlmostEqual(dur, c["speed"], places=5)
        (nvar,) = struct.unpack_from("<I", d, off)
        self.assertEqual(off + 4 + nvar * 68, len(d))


class TestBBSF(unittest.TestCase):
    def setUp(self) -> None:
        if not BBSFX.is_file() or not STAGE.is_dir():
            self.skipTest("generated sfx bank or source stage not available")

    def test_bank_matches_keyframes(self) -> None:
        from convert_stage import parse_godot_cfg_json

        kf = parse_godot_cfg_json(STAGE / "config" / "keyframes.cfg")
        d = BBSFX.read_bytes()
        magic, ver, rate, clips, loops, ones, voices = struct.unpack_from("<4sIIIIII", d, 0)
        self.assertEqual((magic, ver, rate), (b"BBSF", 1, 32000))
        self.assertEqual((loops, ones, voices), (len(kf["sound_loop"]), len(kf["sound_oneshot"]), len(kf["voice_bank"])))
        tables = 28 + clips * 40 + loops * 32 + ones * 16 + voices * 40
        pcm = len(d) - tables
        off = 28
        for _ in range(clips):
            _name, byte_off, frames = struct.unpack_from("<32sII", d, off)
            off += 40
            self.assertLessEqual(byte_off + frames * 2, pcm)
            self.assertGreater(frames, 0)
        # first loop bank cycles sfx_lick_1..5 in order
        ts, mode, n = struct.unpack_from("<qII", d, off)
        self.assertEqual((ts, mode, n), (0, 0, 5))


class TestBBVD(unittest.TestCase):
    def test_every_stage_video_manifest(self) -> None:
        stages = ROOT / "romfs" / "stages"
        idx = stages / "index.txt"
        if not idx.is_file() or not STAGE.is_dir():
            self.skipTest("generated stages not available")
        for line in idx.read_text(encoding="utf-8").splitlines():
            sid = line.split("|")[0]
            vids = sorted((stages / sid / "video").glob(f"{sid}_v*.bbvid"))
            self.assertGreaterEqual(len(vids), 1, sid)
            for vp in vids:
                d = vp.read_bytes()
                magic, ver, start_us, hide_us, fps, frames, w, h, cw, ch, cols, rows, pages, aframes, audio = struct.unpack_from("<4sIqqfIHHHHHHII32s", d, 0)
                self.assertEqual((magic, ver, (w, h, cw, ch, cols, rows)), (b"BBVD", 2, (400, 240, 512, 256, 2, 4)), vp.name)
                self.assertGreater(fps, 0)
                self.assertGreater(hide_us, start_us)
                self.assertEqual(pages, -(-frames // (cols * rows)), vp.name)
                self.assertEqual(len(d), 84 + 32 * pages, vp.name)
                self.assertEqual((stages / sid / "video" / audio.rstrip(bytes([0])).decode()).stat().st_size, aframes * 4, vp.name)
                for i in range(pages):
                    name = struct.unpack_from("<32s", d, 84 + 32 * i)[0].rstrip(bytes([0])).decode()
                    self.assertTrue((stages / sid / "video" / name).is_file(), name)
            has_src = any(Path("C:/Beat Banger/data").glob(f"*/{sid}/images/game_over.png"))
            self.assertEqual((stages / sid / "gfx" / "game_over.t3x").is_file(), has_src, sid)  # levels without art show plain black
            # every page referenced by the sprite manifest exists (guards the 31-char name truncation bug)
            a = (stages / sid / "gfx" / f"{sid}.bbanim").read_bytes()
            sheets = struct.unpack_from("<I", a, 8)[0]
            off = 12
            for _ in range(sheets):
                _n, frames, _fw, _fh, npages = struct.unpack_from("<32sIHHI", a, off)
                off += 44
                for _p in range(npages):
                    t3x = struct.unpack_from("<32s", a, off)[0].rstrip(bytes([0])).decode()
                    off += 36
                    self.assertTrue((stages / sid / "gfx" / t3x).is_file(), f"{sid}: missing {t3x}")
                off += 6 * frames
            bank = (stages / sid / "sfx" / f"{sid}.bbsfx").read_bytes()
            self.assertIn(b"__miss", bank, sid)
            self.assertIn(b"__glass3", bank, sid)


if __name__ == "__main__":
    unittest.main()
