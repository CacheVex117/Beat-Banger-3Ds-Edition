import tempfile
import unittest
from pathlib import Path

from mod2ds import convert, install, media, oneshot

from synthetic import make_mod


def _tools():
    try:
        return media.Tools.find()
    except media.ToolError:
        return None


class OneshotTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_package_name_is_a_safe_folder_name(self):
        self.assertEqual(oneshot.package_name(Path("Kat's Audition")), "Kat's Audition")
        self.assertEqual(oneshot.package_name(Path("x/Mod: A?.zip")), "Mod_ A_")
        self.assertEqual(oneshot.package_name(Path("Svin’s Hornet")), "Svin’s Hornet")

    def test_presets(self):
        self.assertEqual(oneshot.PRESETS["standard"], {"frame_width": 512, "video_res": "auto", "video_fps": 30.0})
        larger = oneshot.PRESETS["larger"]
        self.assertLess(larger["frame_width"], 512)
        self.assertEqual(larger["video_res"], "half")
        self.assertLess(larger["video_fps"], 30.0)

    def test_merge_index_writes_each_stage_row(self):
        t = self.root / "bb_mods"
        for sid in ("a", "b"):
            (t / "stages" / sid).mkdir(parents=True)
        install.merge_index(t, ["a|A|x|p1|P|0|4|0", "b|B|x|p1|P|0|7|0"])
        self.assertEqual((t / "stages" / "a" / "index_row.txt").read_text(encoding="utf-8"), "a|A|x|p1|P|0|0|0\n")
        self.assertEqual((t / "stages" / "b" / "index_row.txt").read_text(encoding="utf-8"), "b|B|x|p1|P|0|1|0\n")

    @unittest.skipUnless(_tools(), "ffmpeg/ffprobe/tex3ds not found")
    def test_one_mod_in_one_package_out(self):
        pack = make_mod(self.root / "src")
        out = self.root / "output"
        lines = []
        for preset in ("standard", "larger"):
            o = convert.Options(out=self.root / "library", tools=_tools(), log=lambda *a: None)
            self.assertEqual(oneshot.run([pack], preset=preset, out_root=out, options=o, log=lines.append), 0)
            pkg = out / (pack.name + (" (3DS)" if preset == "standard" else " (3DS, larger)"))
            stages = pkg / "3ds" / "bb_mods" / "stages"
            self.assertTrue((pkg / "READ ME.txt").is_file())
            self.assertFalse((stages / "index.txt").exists())          # never overwrites the card's list
            self.assertFalse((stages / install.ORDER_FILE).exists())
            dirs = [d for d in stages.iterdir() if d.is_dir()]
            self.assertEqual(len(dirs), 1)
            row = (dirs[0] / "index_row.txt").read_text(encoding="utf-8")
            self.assertTrue(row.startswith(dirs[0].name + "|"))
            self.assertEqual(o.frame_width, oneshot.PRESETS[preset]["frame_width"])

    def test_missing_input_makes_no_package(self):
        o = convert.Options(out=self.root / "library", log=lambda *a: None)
        self.assertEqual(oneshot.run([self.root / "nope"], preset="standard", out_root=self.root / "output", options=o,
                                     log=lambda *a: None), 1)
        self.assertFalse((self.root / "output").exists())


if __name__ == "__main__":
    unittest.main()
