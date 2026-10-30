"""Checks for the rebuilt Collection screen data (tools/build_collection.py -> romfs/extras/coll_*)."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXTRAS = ROOT / "romfs" / "extras"
SCENE = Path("C:/Beat Banger/src/scenes/Collection/Collection.tscn")
sys.path.insert(0, str(ROOT / "tools"))

K = 240.0 / 1080.0


def rows():
    return [l.split("|") for l in (EXTRAS / "coll_layout.txt").read_text(encoding="utf-8").splitlines() if l.strip()]


@unittest.skipUnless((EXTRAS / "coll_layout.txt").exists(), "run tools/build_collection.py first")
class CollectionLayout(unittest.TestCase):
    def test_pages_and_items(self):
        r = rows()
        self.assertEqual(len(r), 21, "21 CollectionItems in Collection.tscn")
        per_page = {}
        for f in r:
            self.assertEqual(len(f), 13)
            per_page[int(f[0])] = per_page.get(int(f[0]), 0) + 1
        self.assertEqual(per_page, {0: 6, 1: 6, 2: 6, 3: 3})
        enums = [int(f[2]) for f in r]
        self.assertEqual(len(set(enums)), 21, "every collectable once")
        self.assertTrue(all(700 < e < 800 for e in enums))

    def test_scene_order_and_offsets(self):
        r = rows()
        first = r[0]
        self.assertEqual((first[0], first[1], first[2]), ("0", "0", "701"), "Page1 starts with Claire's Panties")
        self.assertEqual((float(first[3]), float(first[4])), (188.0, 72.0))
        self.assertEqual(r[-1][2], "718", "Page4 ends with Makayla's stash")

    def test_condoms_dim_override(self):
        dims = {int(f[2]): float(f[5]) for f in rows()}
        self.assertAlmostEqual(dims[719], 0.2159, places=3)
        self.assertAlmostEqual(dims[701], 0.6789, places=3)

    def test_items_inside_bottom_screen(self):
        for f in rows():
            x0 = (960 + 0.9 * (float(f[3]) - 960) - 240) * K
            y0 = (500 + 0.9 * (float(f[4]) - 540)) * K
            box = 360 * 0.9 * K
            self.assertGreaterEqual(x0, 0.0, f[11])
            self.assertLessEqual(x0 + box, 321.0, f[11])   # the outer column touches the 4:3 crop (Zoe's Beanie: 320.4)
            self.assertGreaterEqual(y0, 0.0, f[11])
            self.assertLessEqual(y0 + box, 240.0, f[11])

    def test_icon_cells_fit(self):
        for f in rows():
            cell, cw, ch = int(f[6]), int(f[7]), int(f[8])
            self.assertLess(cell, 21, "cells 21/22 hold the page arrows")
            self.assertTrue(0 < cw <= 160 and 0 < ch <= 160)
            self.assertTrue(f[11] and f[12], "name and description")

    def test_textures_exist(self):
        self.assertTrue((EXTRAS / "coll_scene.t3x").exists())
        self.assertTrue((EXTRAS / "coll_items.t3x").exists())


@unittest.skipUnless(SCENE.exists(), "reference game not present")
class CollectionSource(unittest.TestCase):
    def test_scene_parser(self):
        from build_collection import nums, parse_scene
        self.assertEqual(nums("PackedVector2Array(224, 400, 456, 400)"), [224.0, 400.0, 456.0, 400.0])
        ext, nodes = parse_scene(SCENE)
        pages = [n["name"] for n in nodes if n["parent"] == "Book/Pages"]
        self.assertEqual(pages, ["Page1", "Page2", "Page3", "Page4"])
        self.assertEqual(sum(1 for n in nodes if n["parent"] == "Book/Decor"), 6, "six Decor Line2D strokes")


if __name__ == "__main__":
    unittest.main()
