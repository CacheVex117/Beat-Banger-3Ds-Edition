"""Checks for the handheld-polish pass: HOME Menu icon and Theater reel previews."""
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
STAGES = ROOT / "romfs" / "stages"


class HomeMenuIcon(unittest.TestCase):
    def test_makefile_uses_generated_icon_and_real_title(self):
        mk = (ROOT / "Makefile").read_text(encoding="utf-8")
        self.assertIn("generated/icon.png", mk)
        self.assertRegex(mk, r"APP_TITLE\s*:=\s*Beat Banger\s*\n")
        # APP_ICON must be set before 3ds_rules is included, or the default icon wins
        self.assertLess(mk.index("APP_ICON"), mk.index("include $(DEVKITARM)/3ds_rules"))

    @unittest.skipUnless(Path(r"C:/Beat Banger/icon.png").exists(), "original game not present")
    def test_icon_is_48px(self):
        import build_icon
        with tempfile.TemporaryDirectory() as d:
            im = build_icon.build_icon(build_icon.DEFAULT_SRC, Path(d) / "icon.png")
            self.assertEqual(im.size, (48, 48))
            self.assertEqual(im.mode, "RGBA")


@unittest.skipUnless((STAGES / "index.txt").exists(), "build the stages first")
class TheaterReels(unittest.TestCase):
    def test_every_cutscene_has_a_first_panel_preview(self):
        ids = [l.split("|")[0] for l in (STAGES / "index.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
        self.assertGreater(len(ids), 0)
        for sid in ids:
            cs = STAGES / sid / "cutscene"
            for which in ("pre", "post"):
                if (cs / f"{which}.txt").exists():
                    self.assertTrue((cs / f"{which}_0.t3x").exists(), f"{sid}/{which}_0.t3x (Theater preview)")

    def test_reel_capacity(self):
        h = (ROOT / "platform" / "3ds" / "screens.h").read_text(encoding="utf-8")
        cap = int(re.search(r"#define SC_MAX_REEL_STAGES (\d+)", h).group(1))
        ids = [l for l in (STAGES / "index.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
        self.assertLessEqual(len(ids), cap)


if __name__ == "__main__":
    unittest.main()
