import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

from mod2ds import convert, discover, formats, media, verify
from mod2ds.discover import LevelRef, ModPack
from mod2ds.model import build_level

from synthetic import make_mod

NATIVE = Path(os.environ.get("MOD2DS_NATIVE3DS", "../native3ds"))
GAME = Path(os.environ.get("MOD2DS_GAME", "C:/Beat Banger"))


class LayoutTest(unittest.TestCase):
    def test_struct_sizes_match_runtime_loaders(self):
        # sizes the native loaders step by (chart_loader.c, anim.c, sfx.c, video.c)
        self.assertEqual(formats.BBCH_HEADER.size, 152)
        self.assertEqual(formats.BBCH_NOTE.size, 16)
        self.assertEqual(formats.BBCH_LOOP.size, 40)
        self.assertEqual(formats.BBCH_BPM.size, 16)
        self.assertEqual(formats.BBAN_SHEET.size, 44)
        self.assertEqual(formats.BBAN_PAGE.size, 36)
        self.assertEqual(formats.BBAN_FRAME.size, 6)
        self.assertEqual(formats.BBAN_CAMERA.size, 28)
        self.assertEqual(formats.BBAN_VARIATION.size, 68)
        self.assertEqual(formats.BBSF_HEADER.size, 28)
        self.assertEqual(formats.BBSF_CLIP.size, 40)
        self.assertEqual(formats.BBSF_LOOP.size, 32)
        self.assertEqual(formats.BBSF_ONESHOT.size, 16)
        self.assertEqual(formats.BBSF_VOICE.size, 40)
        self.assertEqual(formats.BBVD_HEADER.size, 84)

    def test_bbchart_roundtrip_passes_loader_rules(self):
        notes = [{"timestamp_us": -2_000_000, "input_type": 0, "modifier": 0, "trigger_voice": 0, "hold_duration_us": 0},
                 {"timestamp_us": 10, "input_type": 3, "modifier": 3, "trigger_voice": 1, "hold_duration_us": 1}]
        blob = formats.pack_bbchart(stage_id="x" * 40, title="t", difficulty="virgin", rating=0, song_offset_us=0,
                                    note_offset_us=0, notes=notes, shutters_us=[5], loops=[{"timestamp_us": 0, "name": "s00_a"}],
                                    bpm_changes=[{"timestamp_us": 0, "bpm_milli": 120000}])
        self.assertEqual(verify.check_bbchart(blob), [])
        self.assertEqual(blob[8 + 31], 0)              # char[32] stays NUL-terminated after truncation
        bad = bytearray(blob)
        bad[152 + 16 + 10:152 + 16 + 14] = b"\0\0\0\0"  # HOLD with zero length
        self.assertTrue(verify.check_bbchart(bytes(bad)))

    def test_index_line_rejects_separators(self):
        self.assertEqual(formats.index_line("a", "b", "c", "d", "e", 0, 1), "a|b|c|d|e|0|1|0")
        with self.assertRaises(AssertionError):
            formats.index_line("a", "b|x", "c", "d", "e", 0, 1)


@unittest.skipUnless((NATIVE / "tools" / "convert_stage.py").is_file() and (GAME / "data" / "bb_vol_1" / "cathy_e1").is_dir(),
                     "native3ds tools or the Beat Banger install not present")
class NativeParityTest(unittest.TestCase):
    """The same level through native convert_stage.py and through MOD2DS gives the same chart data.

    Known, intentional difference: MOD2DS applies the game's Config.round_note_timestamps (0.001 s snap);
    native3ds does not, so note/hold times may differ by < 0.5 ms.
    """

    def test_shipped_stages(self):
        spec = importlib.util.spec_from_file_location("native_convert_stage", NATIVE / "tools" / "convert_stage.py")
        native = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(native)
        for sid in ("cathy_e1", "zoe_e1", "gale_e1", "claire_e1", "dawna_e1"):
            sdir = GAME / "data" / "bb_vol_1" / sid
            with self.subTest(stage=sid):
                theirs = native.convert_stage_configs(sdir)
                pack = ModPack(sdir.parent, "act", act={"act_id": "bb_vol_1"})
                ours = build_level(LevelRef(sdir, {}), pack, "bb_vol_1", GAME)
                self.assertEqual(len(theirs), len(ours.charts))
                self.assertEqual(ours.bpm_changes, theirs[0]["bpm_changes"])
                self.assertEqual(ours.shutters_us, theirs[0]["shutters_us"])
                self.assertEqual([lp["timestamp_us"] for lp in ours.loops], [lp["timestamp_us"] for lp in theirs[0]["loops"]])
                self.assertEqual(ours.song_offset_us, theirs[0]["song_offset_us"])
                for t, o in zip(theirs, ours.charts):
                    self.assertEqual(len(t["notes"]), len(o["notes"]))
                    for a, b in zip(t["notes"], o["notes"]):
                        self.assertEqual((a["input_type"], a["modifier"], a["trigger_voice"]),
                                         (b["input_type"], b["modifier"], b["trigger_voice"]))
                        self.assertLessEqual(abs(a["timestamp_us"] - b["timestamp_us"]), 500)
                        self.assertLessEqual(abs(a["hold_duration_us"] - b["hold_duration_us"]), 1000)


class SpriteFitTest(unittest.TestCase):
    def test_decimate_keeps_ends(self):
        self.assertEqual(media.decimate(10, 16), list(range(10)))
        idx = media.decimate(113, 16)
        self.assertEqual((len(idx), idx[0], idx[-1]), (16, 0, 112))

    def test_tall_frames_shrink_to_page_budget(self):
        fw, fh = media.fit_frame_size(400, 700, 512, 1024)
        self.assertEqual(len(media.pack_pages(fw, fh, 16, 1024)), 8)    # what the runtime would reject (4 max)
        with tempfile.TemporaryDirectory() as d:
            from PIL import Image
            src = Path(d) / "tall.png"
            Image.new("RGBA", (400 * 4, 700 * 4), (9, 9, 9, 255)).save(src)

            class FakeTools:
                def tex3ds_encode(self, png, t3x, fmt, quality="medium"):
                    t3x.write_bytes(b"t3x")
            rec = media.build_sheet(FakeTools(), src, 4, 4, 16, sheet_no=0, out_dir=Path(d), frame_w=512, max_page=1024,
                                    fmt="etc1a4", quality="medium", max_frames=16, max_pages=4)
            self.assertLessEqual(len(rec["pages"]), 4)
            self.assertEqual(rec["frame_count"], 16)


class PlaceFrameTest(unittest.TestCase):
    def test_frame_lands_where_the_game_draws_it(self):
        from PIL import Image
        frame = Image.new("RGBA", (600, 800), (255, 0, 0, 255))
        # canvas k=1: 512x288 texels for 1280x720 units -> 0.4 texel/unit; 600x800 px -> 240x320 texels, clipped to 288
        cell = media.place_frame(frame, 512, 288, 1.0, 1.0, (0.0, 0.0))
        bbox = cell.getbbox()
        self.assertEqual((bbox[0], bbox[2]), (136, 376))       # centered: 256 -/+ 120
        self.assertEqual((bbox[1], bbox[3]), (0, 288))          # 320 tall: top/bottom clipped, as on screen
        cell = media.place_frame(frame, 512, 288, 800 / 720, 1.0, (0.0, 0.0))
        bbox = cell.getbbox()
        self.assertEqual((bbox[1], bbox[3]), (0, 288))          # k = 800/720: exactly fits the height
        self.assertLess(bbox[2] - bbox[0], 240)
        cell = media.place_frame(frame, 512, 288, 1.0, 0.85, (0.0, -20.0))
        bbox = cell.getbbox()
        self.assertEqual(bbox[3] - bbox[1], 272)                # 800 * .85 * .4
        self.assertEqual(bbox[1], 0)                            # (288 - 272) / 2 - 8 texels up


def _tools():
    try:
        return media.Tools.find()
    except media.ToolError:
        return None


@unittest.skipUnless(_tools(), "ffmpeg/ffprobe/tex3ds not found")
class EndToEndTest(unittest.TestCase):
    def test_picture_only_video(self):
        # full-motion mods (Tracer) ship clips without an audio track; the song carries the sound
        t = _tools()
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "clip.ogv"
            t.run(t.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc=size=320x180:rate=30:duration=1",
                  "-c:v", "libtheora", "-an", str(src))
            self.assertFalse(t.has_audio(src))
            rec = media.build_video(t, src, "x_v0", Path(d))
            self.assertEqual((rec["frames"], len(rec["pages"]), rec["audio_frames"]), (30, 4, 0))
            self.assertEqual((Path(d) / "x_v0.pcm").stat().st_size, 0)

    def test_half_res_video_layout(self):
        # full-motion mods: 200x120 frames, 32 per 1024x1024 page (a quarter of the SD reads), header carries the layout
        t = _tools()
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "clip.ogv"
            t.run(t.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc=size=320x180:rate=30:duration=1.1",
                  "-c:v", "libtheora", "-an", str(src))
            rec = media.build_video(t, src, "h_v0", Path(d), res="half")
            self.assertEqual((rec["frames"], len(rec["pages"])), (33, 2))
            self.assertEqual(rec["layout"], {"frame_w": 200, "frame_h": 120, "cell_w": 256, "cell_h": 128, "cols": 4, "rows": 8})
            blob = formats.pack_bbvid(start_us=0, hide_us=1, fps=rec["fps"], frames=rec["frames"], pages=rec["pages"],
                                      audio_frames=0, audio_name=rec["audio_name"], **rec["layout"])
            self.assertEqual(verify.check_bbvid(blob, Path(d)), [])
            self.assertEqual(formats.BBVD_HEADER.unpack_from(blob)[6:12], (200, 120, 256, 128, 4, 8))

    def test_video_frame_rate_is_capped(self):
        t = _tools()
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "fast.ogv"
            t.run(t.ffmpeg, "-v", "error", "-f", "lavfi", "-i", "testsrc=size=320x180:rate=60:duration=1",
                  "-c:v", "libtheora", "-an", str(src))
            rec = media.build_video(t, src, "x_v0", Path(d))                    # default cap 30
            self.assertEqual((rec["fps"], rec["frames"], len(rec["pages"])), (30.0, 30, 4))
            rec = media.build_video(t, src, "y_v0", Path(d), max_fps=0)          # 0 keeps the source rate
            self.assertEqual((rec["fps"], rec["frames"]), (60.0, 60))

    def test_convert_synthetic_mod(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            pack = discover.load_pack(make_mod(root / "mods"))
            out = root / "out"
            o = convert.Options(out=out, tools=_tools(), log=lambda *a: None)
            manifest = convert.convert_pack(pack, o)
            convert.write_index(out)
            self.assertEqual(manifest["stages"][0]["status"], "ok", manifest["stages"][0])
            sid = manifest["stages"][0]["stage_id"]
            stage = out / "stages" / sid
            for rel in (f"charts/{sid}_virgin.bbchart", f"charts/{sid}_normal.bbchart", f"charts/{sid}_expert.bbchart",
                        f"audio/{sid}.wav", f"gfx/{sid}.bbanim", "gfx/sheet0_0.t3x", "gfx/sheet1_0.t3x",
                        f"sfx/{sid}.bbsfx", "menu/preview.wav", "level.json"):
                self.assertTrue((stage / rel).is_file(), rel)
            self.assertEqual(verify.verify(out), {sid: []})
            self.assertEqual((out / "stages" / "index.txt").read_text().split("|")[0], sid)
            # the source mod is untouched
            self.assertEqual(sorted(p.name for p in (root / "mods" / "Synth Pack" / "default").iterdir()),
                             ["audio", "config", "images"])


if __name__ == "__main__":
    unittest.main()
