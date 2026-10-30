import tempfile
import unittest
import zipfile
from pathlib import Path

from mod2ds import convert, install, media

from synthetic import make_mod


def _tools():
    try:
        return media.Tools.find()
    except media.ToolError:
        return None


def zip_dir(src: Path, zpath: Path, prefix: str = "") -> None:
    with zipfile.ZipFile(zpath, "w") as zf:
        for f in src.rglob("*"):
            if f.is_file():
                zf.write(f, prefix + f.relative_to(src.parent).as_posix())


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_zip_extraction_follows_game_rules(self):
        pack = make_mod(self.root / "src")
        z = self.root / "mod.zip"
        zip_dir(pack, z, prefix="wrapper/")
        with zipfile.ZipFile(z, "a") as zf:
            zf.writestr("wrapper/Synth Pack/evil.exe", b"MZ")
            zf.writestr("wrapper/Synth Pack/../../escape.txt", b"x")
            zf.writestr("__MACOSX/wrapper/._act.cfg", b"x")
        out = install.extract_zip(z, self.root / "x")
        self.assertEqual(out.name, "Synth Pack")
        self.assertTrue((out / "act.cfg").is_file())
        self.assertTrue((out / "default" / "config" / "meta.cfg").is_file())
        self.assertFalse((out / "evil.exe").exists())
        self.assertFalse((self.root / "escape.txt").exists())
        self.assertIsNone(install.extract_zip(self._junk_zip(), self.root / "y"))

    def _junk_zip(self):
        z = self.root / "junk.zip"
        with zipfile.ZipFile(z, "w") as zf:
            zf.writestr("readme.txt", "hi")
        return z

    def test_collect_packs_accepts_pack_mods_folder_and_level(self):
        pack = make_mod(self.root / "mods")
        log = []
        self.assertEqual(install.collect_packs([pack], self.root / "w", log.append), [pack])
        self.assertEqual(install.collect_packs([self.root / "mods"], self.root / "w", log.append), [pack])
        self.assertEqual(install.collect_packs([pack / "default"], self.root / "w", log.append), [pack])
        self.assertEqual(install.collect_packs([self.root / "nothing"], self.root / "w", log.append), [])
        legacy = self.root / "mods" / "Old Mod" / "x"
        legacy.mkdir(parents=True)
        (legacy / "chart.cfg").write_text("[EASY]\nbpm=100\n")
        self.assertEqual(install.collect_packs([self.root / "mods"], self.root / "w", log.append),
                         [self.root / "mods" / "Old Mod", pack])      # a mods folder holding a legacy mod is still a mods folder
        self.assertTrue(log)

    def test_normalize_target(self):
        sd = self.root / "sd"
        (sd / "Nintendo 3DS").mkdir(parents=True)
        self.assertEqual(install.normalize_target(sd), sd / "3ds" / "bb_mods")
        self.assertEqual(install.normalize_target(sd / "3ds"), sd / "3ds" / "bb_mods")
        self.assertEqual(install.normalize_target(sd / "3ds" / "bb_mods"), sd / "3ds" / "bb_mods")

    def test_merge_index_keeps_other_installs(self):
        t = self.root / "bb_mods"
        for sid in ("a", "b", "c"):
            (t / "stages" / sid).mkdir(parents=True)
        (t / "stages" / "index.txt").write_text("a|A|x|p1|P|0|0|0\ngone|G|x|p1|P|0|1|0\n", encoding="utf-8")
        rows = install.merge_index(t, ["c|C|x|p2|Q|0|3|0", "b|B|x|p1|P|0|5|0"])
        self.assertEqual([r.split("|")[0] for r in rows], ["a", "b", "c"])   # 'gone' has no folder any more
        self.assertEqual([r.split("|")[6] for r in rows], ["0", "1", "0"])   # contiguous per pack: no locked gaps
        (t / "stages" / "d").mkdir()
        rows = install.merge_index(t, ["d|D|x|p1|P|0|2|0"])                # pack position 2 sorts between a(0) and b(5)
        self.assertEqual([r.split("|")[0] for r in rows], ["a", "d", "b", "c"])
        self.assertEqual([r.split("|")[6] for r in rows], ["0", "1", "2", "0"])

    @unittest.skipUnless(_tools(), "ffmpeg/ffprobe/tex3ds not found")
    def test_drop_a_zip_onto_an_sd_card(self):
        pack = make_mod(self.root / "src")
        z = self.root / "Synth.zip"
        zip_dir(pack, z)
        sd = self.root / "sd"
        (sd / "Nintendo 3DS").mkdir(parents=True)
        o = convert.Options(out=self.root / "library", tools=_tools(), fit_runtime=True, log=lambda *a: None)
        lines = []
        self.assertEqual(install.run([z], sd=sd, options=o, log=lines.append), 0)
        stages = sd / "3ds" / "bb_mods" / "stages"
        rows = (stages / "index.txt").read_text(encoding="utf-8").splitlines()
        self.assertEqual(len(rows), 1)
        sid = rows[0].split("|")[0]
        self.assertTrue((stages / sid / "menu" / "thumb.t3x").is_file() or not (pack / "default" / "thumb.png").exists())
        self.assertTrue((stages / sid / f"charts/{sid}_normal.bbchart").is_file())
        self.assertFalse((stages / sid / "level.json").exists())
        # dropping it again reuses the library copy
        lines.clear()
        install.run([z], sd=sd, options=o, log=lines.append)
        self.assertTrue(any("already converted" in ln for ln in lines))


if __name__ == "__main__":
    unittest.main()
