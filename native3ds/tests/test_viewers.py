"""Extras > Music and Scene Viewer data (tools/build_viewers.py) against the original scenes and the port's shop data."""
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import build_viewers  # noqa: E402

GAME = Path("C:/Beat Banger")


class SoundTestRecords(unittest.TestCase):
    @unittest.skipUnless((GAME / "src/scenes/sound_test.tscn").exists(), "game source not present")
    def test_records_match_sound_test_tscn(self):
        txt = (GAME / "src/scenes/sound_test.tscn").read_text(encoding="utf-8")
        titles = re.findall(r'^title = "([^"]+)"', txt, re.M)
        enums = [int(x) for x in re.findall(r"^unlock_enum = (\d+)", txt, re.M)]
        self.assertEqual([r[0] for r in build_viewers.RECORDS], titles)
        self.assertEqual([r[3] for r in build_viewers.RECORDS], enums)

    def test_every_record_is_a_shop_music_item(self):
        shop = ROOT / "romfs/extras/shop.txt"
        if not shop.exists():
            self.skipTest("romfs not built")
        music = {int(l.split("|")[1]) for l in shop.read_text(encoding="utf-8").splitlines() if l.startswith("M|")}
        self.assertTrue({r[3] for r in build_viewers.RECORDS} <= music)

    def test_music_ui_table_matches_the_tool(self):
        src = (ROOT / "platform/3ds/music_ui.c").read_text(encoding="utf-8")
        rows = re.findall(r'\{ "([^"]+)", 0x[0-9A-F]{2}, 0x[0-9A-F]{2}, 0x[0-9A-F]{2}, (\d+), (\d+) \}', src)
        self.assertEqual([(t, int(b), int(i)) for t, b, i in rows], [(r[0], r[2], r[3]) for r in build_viewers.RECORDS])


class SceneViewerLevels(unittest.TestCase):
    def test_eight_levels_with_thumbs(self):
        self.assertEqual(len(build_viewers.SV_THUMBS), 8)
        src = (ROOT / "platform/3ds/sceneview_ui.c").read_text(encoding="utf-8")
        ids = re.findall(r'\{ "([a-z0-9_]+)", "[^"]+", "[A-Za-z]+", \d+, \d+ \}', src)
        self.assertEqual(ids, ["cathy_e1", "zoe_e1", "gale_e1", "claire_e1", "dawna_e1", "zoe_e2", "cathy_e2", "makayla"])

    def test_level_items_match_the_stage_index(self):
        index = ROOT / "romfs/stages/index.txt"
        if not index.exists():
            self.skipTest("romfs not built")
        enums = {l.split("|")[0]: int(l.split("|")[7]) for l in index.read_text(encoding="utf-8").splitlines() if l.count("|") >= 7}
        src = (ROOT / "platform/3ds/sceneview_ui.c").read_text(encoding="utf-8")
        for sid, item in re.findall(r'\{ "([a-z0-9_]+)", "[^"]+", "[A-Za-z]+", \d+, (\d+) \}', src):
            if sid in enums:
                self.assertEqual(enums[sid], int(item), sid)


if __name__ == "__main__":
    unittest.main()
