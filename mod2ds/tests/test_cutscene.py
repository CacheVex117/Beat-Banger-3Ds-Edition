import importlib.util
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

from mod2ds import cutscene, media
from mod2ds.model import Issues

from synthetic import write_cfg, write_sheet, write_wav

NATIVE = Path(os.environ.get("MOD2DS_NATIVE3DS", "../native3ds"))
GAME = Path(os.environ.get("MOD2DS_GAME", "C:/Beat Banger"))


def _tools():
    try:
        return media.Tools.find()
    except media.ToolError:
        return None


@unittest.skipUnless(_tools(), "ffmpeg/ffprobe/tex3ds not found")
class CutsceneTest(unittest.TestCase):
    def test_mod_cutscene_with_scene_folder_fallback(self):
        with tempfile.TemporaryDirectory() as d:
            lvl = Path(d) / "level"
            cs = lvl / "cutscene"
            write_cfg(cs / "pre" / "script.cfg", [
                {"src": "a.png", "character": "Kat", "dialogue": "[shake limit=4]Pura   vida![/shake]\nBig | door",
                 "audio": [{"bus": "Voice", "path": "v.wav"}, {"bus": "SFX", "path": "s.wav"}]},
                {"src": "missing.png", "character": "-", "dialogue": "", "audio": "s.wav", "transition": "fade"},
            ], section="data", key="lines")
            write_cfg(cs / "pre" / "global_tracks.cfg", [{"track": {"bus": "Music", "path": "m.wav", "start_panel": 0, "end_panel": 1}}])
            write_cfg(cs / "cutscene_cache.cfg", [{"character": {"name": "Kat", "color": [1.0, 0.5, 0.0]}}])
            write_sheet(cs / "pre" / "shots" / "a.png", 1, 1, frame=(800, 450))   # scene-folder layout (Encore packs)
            for n in ("v.wav", "s.wav", "m.wav"):
                write_wav(cs / "audio" / n, 0.5)
            out = Path(d) / "out"
            out.mkdir()
            issues = Issues()
            info = cutscene.convert_scene(_tools(), lvl, "pre", out, issues)
            self.assertIsNone(cutscene.convert_scene(_tools(), lvl, "post", out, issues))
            self.assertEqual((info["panels"], info["images"], info["clips"], info["segments"]), (2, 1, 3, 1))
            txt = (out / "pre.txt").read_text(encoding="ascii").splitlines()
            self.assertEqual(txt[0], "H|2")
            self.assertEqual(txt[1], "C|Kat|255|127|0")
            self.assertEqual(txt[2], "P|0|Kat|Pura vida! Big door|v|s|")      # tags, newlines and '|' cleaned
            self.assertEqual(txt[3], "P|-1|-|||s|fade")                       # missing image -> no picture
            self.assertEqual(txt[4], "T|m|Music|0|1|0|0|0")
            self.assertTrue((out / "pre_0.t3x").is_file())
            bank = (out / "pre.bbca").read_bytes()
            self.assertEqual(bank[:4], b"BBCA")
            self.assertEqual(struct.unpack_from("<II", bank, 4), (1, 3))
            codes = {i.code for i in issues.items}
            self.assertIn("cutscene_scene_folder", codes)
            self.assertIn("cutscene_missing_asset", codes)

    def test_editor_placeholder_is_skipped(self):
        with tempfile.TemporaryDirectory() as d:
            lvl = Path(d) / "level"
            write_cfg(lvl / "cutscene" / "post" / "script.cfg", [{"character": "Sample Character", "dialogue": "Sample Dialogue"}],
                      section="data", key="lines")
            issues = Issues()
            self.assertIsNone(cutscene.convert_scene(_tools(), lvl, "post", Path(d), issues))
            self.assertEqual([i.code for i in issues.items], ["cutscene_placeholder"])

    @unittest.skipUnless((NATIVE / "tools" / "convert_cutscene.py").is_file() and (GAME / "data" / "bb_vol_1" / "cathy_e1" / "cutscene").is_dir(),
                         "native3ds or the Beat Banger install not present")
    def test_matches_native_converter_on_a_shipped_stage(self):
        sys.path.insert(0, str(NATIVE / "tools"))
        spec = importlib.util.spec_from_file_location("native_convert_cutscene", NATIVE / "tools" / "convert_cutscene.py")
        native = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(native)
        stage = GAME / "data" / "bb_vol_1" / "cathy_e1"
        with tempfile.TemporaryDirectory() as d:
            a, b, gen = Path(d) / "native", Path(d) / "mod2ds", Path(d) / "gen"
            for p in (a, b, gen):
                p.mkdir()
            native.convert_scene(stage, "post", a, gen, _tools().ffmpeg)
            cutscene.convert_scene(_tools(), stage, "post", b, Issues())
            self.assertEqual((a / "post.bbca").read_bytes(), (b / "post.bbca").read_bytes())
            self.assertEqual((a / "post.txt").read_text(), (b / "post.txt").read_text())
            native_imgs = sorted(p.name for p in a.glob("post_*.t3x"))
            self.assertEqual(native_imgs, sorted(p.name for p in b.glob("post_*.t3x")))
            for n in native_imgs:
                self.assertEqual((a / n).read_bytes(), (b / n).read_bytes(), n)


if __name__ == "__main__":
    unittest.main()
