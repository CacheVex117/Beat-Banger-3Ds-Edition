"""Gallery assets (tools/build_gallery.py -> romfs/extras/gallery_*): the four tabs of scenes/Gallery in the original order."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXTRAS = ROOT / "romfs" / "extras"
GAME = Path("C:/Beat Banger")


@unittest.skipUnless((EXTRAS / "gallery_tabs.txt").is_file(), "run tools/build_gallery.py --romfs romfs first")
class GalleryAssetsTest(unittest.TestCase):
    def rows(self):
        return [ln.split("|") for ln in (EXTRAS / "gallery_tabs.txt").read_text(encoding="utf-8").splitlines() if ln.strip()]

    def test_rows_well_formed_and_files_exist(self):
        rows = self.rows()
        self.assertGreater(len(rows), 0)
        self.assertLessEqual(len(rows), 64)   # gallery_ui.c MAX_PICS / thumbnail atlas (64 + 64 censored cells)
        for r in rows:
            self.assertEqual(len(r), 8)
            self.assertIn(int(r[0]), range(4))
            self.assertTrue((EXTRAS / r[4]).is_file(), r[4])
            self.assertLessEqual(len(r[2]), 47)
            self.assertLessEqual(len(r[3]), 95)
            w, h = int(r[6]), int(r[7])
            self.assertTrue(0 < w <= 1024 and 0 < h <= 512)
        for name in ("gallery_thumbs.t3x", "gallery_ui.t3x", "gallery_vignette.t3x", "gallery_cross.t3x"):
            self.assertTrue((EXTRAS / name).is_file(), name)

    def test_tabs_follow_the_scene(self):
        rows = self.rows()
        tabs = [int(r[0]) for r in rows]
        self.assertEqual(tabs, sorted(tabs))   # Main, Achievements, Fan Art, Patreon, each contiguous
        if (GAME / "src/scenes/Gallery/gallery.tscn").is_file():
            scene = (GAME / "src/scenes/Gallery/gallery.tscn").read_text(encoding="utf-8")
            for tab, key in ((0, "unlockable_gallery"), (1, "achievement_gallery"), (3, "patreon_gallery")):
                m = re.search(rf"^{key} = \[(.*)\]$", scene, re.M)
                self.assertEqual(tabs.count(tab), len(re.findall(r"ExtResource", m.group(1))), key)

    def test_fan_art_is_credited_sorted_and_unlocked(self):
        fan = [r for r in self.rows() if r[0] == "2"]
        self.assertGreater(len(fan), 0)
        artists = [r[2][3:] for r in fan]
        self.assertTrue(all(r[2].startswith("By ") for r in fan))
        self.assertEqual(artists, sorted(artists))   # galleries.gd sorts by artist
        self.assertTrue(all(r[1] == "0" for r in fan))   # BASE_ITEM: always unlocked

    def test_zoom_sounds_in_the_ui_bank(self):
        header = (ROOT / "platform" / "3ds" / "ui_sfx.h").read_text(encoding="utf-8")
        self.assertIn("BB_UISFX_GALLERY_ZOOM_IN", header)
        self.assertIn("BB_UISFX_GALLERY_ZOOM_OUT", header)


if __name__ == "__main__":
    unittest.main()
