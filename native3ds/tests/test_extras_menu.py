"""tools/build_extras_menu.py outputs: the Extras menu art and the seasonal outfit icon."""
import unittest
from pathlib import Path

ROMFS = Path(__file__).resolve().parent.parent / "romfs" / "extras"


class ExtrasMenuAssetsTest(unittest.TestCase):
    def test_files_exist(self):
        if not ROMFS.is_dir():
            self.skipTest("romfs not built")
        for name in ("xm_bg.t3x", "xm_text.t3x", "xm_icons.t3x", "outfit_icon_1106.t3x", "jasmine_1106.t3x"):
            with self.subTest(name=name):
                self.assertTrue((ROMFS / name).is_file(), name)
                self.assertGreater((ROMFS / name).stat().st_size, 64)


if __name__ == "__main__":
    unittest.main()
