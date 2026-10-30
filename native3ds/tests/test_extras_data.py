"""Sanity checks for the generated shop / collection / lore / gallery data in romfs/extras (tools/build_extras.py)."""
import unittest
from pathlib import Path

EXTRAS = Path(__file__).resolve().parent.parent / "romfs" / "extras"


def rows(name):
    return [l.split("|") for l in (EXTRAS / name).read_text(encoding="utf-8").splitlines() if l.strip()]


@unittest.skipUnless((EXTRAS / "shop.txt").exists(), "run tools/build_extras.py first")
class ExtrasData(unittest.TestCase):
    def test_shop_items(self):
        shop = rows("shop.txt")
        self.assertEqual(len(shop), 36)
        items = set()
        for f in shop:
            self.assertGreaterEqual(len(f), 6)
            self.assertIn(f[0], "PGMO")
            self.assertNotIn(int(f[1]), items, "duplicate shop item")
            items.add(int(f[1]))
            self.assertGreaterEqual(int(f[3]), 0)
        self.assertTrue({100, 101, 102, 103, 104} <= items, "all five power-ups are sold")

    def test_gallery_textures_exist(self):
        for f in rows("gallery.txt"):
            self.assertTrue((EXTRAS / f[3]).exists(), f[3])

    def test_lore_textures_exist(self):
        for f in rows("lore.txt"):
            self.assertTrue((EXTRAS / f[7]).exists(), f[7])

    def test_collectables_have_icons(self):
        for f in rows("collect.txt"):
            self.assertLess(int(f[4]), 48)
        self.assertTrue((EXTRAS / "collect.t3x").exists())

    def test_jasmine_default_outfit(self):
        self.assertTrue((EXTRAS / "jasmine_0.t3x").exists())

    def test_jasmine_sheet_layout(self):
        # FW|FH|mouth_dx|mouth_dy|MW|MH|MX0|MS|shapes: two idle frames then four mouth shapes (screens.c draw_jasmine)
        self.assertEqual((EXTRAS / "jasmine.txt").read_text().strip(), "267|333|99|104|51|54|542|55|4")
        for e in (1100, 1101, 1102, 1103, 1104, 1105):
            self.assertTrue((EXTRAS / f"jasmine_{e}.t3x").exists(), e)

    def test_dialogue_has_every_shop_pool(self):
        kinds = {l.split("|")[1] for l in (EXTRAS / "dialogue.txt").read_text(encoding="utf-8").splitlines() if l.startswith("L|")}
        for k in ("entering", "purchase_success", "purchase_sold_out", "purchase_insufficient_funds"):
            self.assertIn(k, kinds)


if __name__ == "__main__":
    unittest.main()


UI = Path(__file__).resolve().parent.parent / "romfs" / "ui"


@unittest.skipUnless((UI / "thumbs.txt").exists(), "run tools/build_ui_assets.py first")
class UiArt(unittest.TestCase):
    def test_every_stage_has_a_portrait(self):
        stages = [l.split("|")[0] for l in (EXTRAS.parent / "stages" / "index.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
        cells = {l.split("|")[0]: int(l.split("|")[1]) for l in (UI / "thumbs.txt").read_text(encoding="utf-8").splitlines() if l.strip()}
        for sid in stages:
            self.assertIn(sid, cells)
            self.assertLess(cells[sid], 8)

    def test_textures_exist(self):
        for n in ("logo", "shop_bg", "extras_bg", "thumbs", "shop_icons", "stars", "cats"):
            self.assertTrue((UI / f"{n}.t3x").exists(), n)

    def test_every_powerup_has_an_icon(self):
        items = {int(l.split("|")[0]) for l in (UI / "shop_icons.txt").read_text(encoding="utf-8").splitlines() if l.strip()}
        self.assertTrue({100, 101, 102, 103, 104} <= items)

    def test_scene_music(self):
        for n in ("main", "shop", "extras"):
            self.assertGreater((EXTRAS.parent / "music" / f"{n}.wav").stat().st_size, 1_000_000)


STAGES = EXTRAS.parent / "stages"


@unittest.skipUnless((STAGES / "cathy_e1" / "cutscene" / "pre.txt").exists(), "run tools/convert_cutscene.py first")
class Cutscenes(unittest.TestCase):
    def scenes(self):
        for d in sorted(STAGES.glob("*/cutscene")):
            for which in ("pre", "post"):
                if (d / f"{which}.txt").exists():
                    yield d, which

    def test_every_stage_has_pre_and_post(self):
        stages = [l.split("|")[0] for l in (STAGES / "index.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
        for sid in stages:
            for which in ("pre", "post"):
                self.assertTrue((STAGES / sid / "cutscene" / f"{which}.txt").exists(), f"{sid} {which}")

    def test_scripts_reference_existing_images_and_clips(self):
        import struct
        for d, which in self.scenes():
            bank = (d / f"{which}.bbca").read_bytes()
            self.assertEqual(bank[:4], b"BBCA")
            n = struct.unpack("<II", bank[4:12])[1]
            names, blob = set(), len(bank) - (12 + 44 * n)
            for i in range(n):
                o = 12 + i * 44
                name = bank[o:o + 32].split(b"\0")[0].decode()
                off, frames, rate = struct.unpack("<III", bank[o + 32:o + 44])
                names.add(name)
                self.assertLessEqual(off + frames * 2, blob, f"{d} {which} {name}")
                self.assertEqual(off % 64, 0)
                self.assertLess(frames, 262144, "long buffers hang the emulator DSP")
                self.assertIn(rate, (16000, 22050))
            panels = 0
            for line in (d / f"{which}.txt").read_text(encoding="utf-8").splitlines():
                f = line.split("|")
                if f[0] == "P":
                    panels += 1
                    if int(f[1]) >= 0:
                        self.assertTrue((d / f"{which}_{f[1]}.t3x").exists(), f"{d} {which} image {f[1]}")
                    for c in [f[4]] + [x for x in f[5].split(";") if x]:
                        if c:
                            self.assertIn(c, names)
                elif f[0] == "T":
                    self.assertIn(f[1], names)
                    self.assertLessEqual(int(f[3]), int(f[4]))
            self.assertEqual(panels, int(next(l for l in (d / f"{which}.txt").read_text(encoding="utf-8").splitlines() if l.startswith("H|")).split("|")[1]))

    def test_no_bbcode_left_in_dialogue(self):
        for d, which in self.scenes():
            for line in (d / f"{which}.txt").read_text(encoding="utf-8").splitlines():
                if line.startswith("P|"):
                    self.assertNotRegex(line.split("|")[3], r"\[/?[a-z]+")


@unittest.skipUnless((UI / "font.txt").exists(), "run tools/build_font.py first")
class FontAtlas(unittest.TestCase):
    def test_faces_and_glyph_coverage(self):
        rows = [l.split("|") for l in (UI / "font.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
        faces = {(r[1], int(r[2])) for r in rows if r[0] == "F"}
        self.assertTrue({f for f, _ in faces} == {"body", "bold", "head", "hand", "noto", "notob", "notor"})
        for face, size in faces:
            codes = {int(r[3]) for r in rows if r[0] == "G" and r[1] == face and int(r[2]) == size}
            self.assertEqual(codes, set(range(32, 127)), (face, size))
        for r in rows:
            if r[0] == "G":
                x, y, w, h = map(int, r[4:8])
                self.assertLessEqual(x + w, 1024)
                self.assertLessEqual(y + h, 1024)

    def test_ui_sfx_bank_matches_enum(self):
        import re
        import struct
        d = (UI / "ui_sfx.bbca").read_bytes()
        self.assertEqual(d[:4], b"BBCA")
        count = struct.unpack("<II", d[4:12])[1]
        names = [d[12 + 44 * i: 12 + 44 * i + 32].split(b"\0")[0].decode() for i in range(count)]
        header = (Path(__file__).resolve().parent.parent / "platform" / "3ds" / "ui_sfx.h").read_text(encoding="utf-8")
        enum = re.findall(r"BB_UISFX_([A-Z_]+)", header[header.index("typedef enum"):header.index("} bb_uisfx_t")])
        self.assertEqual(count, 35)
        self.assertEqual([n.upper() for n in names], enum)   # the C enum order is the bank order

    def test_level_previews_and_calibration_clip(self):
        # level-select previews (SongPreview, 30 s = 64 beats at 128 BPM) and the calibrator jam loop (100 BPM, 24 s = 40 beats)
        music = UI.parent / "music"
        stages = [l.split("|")[0] for l in (UI.parent / "stages" / "index.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
        for sid in stages:
            self.assertTrue((music / f"pv_{sid}.wav").exists(), sid)
        self.assertTrue((music / "calib.wav").exists())

    def test_settings_dialog_uses_the_original_frame(self):
        self.assertTrue((UI / "settings_frame.t3x").exists())

    def test_outfit_grid_icons_exist(self):
        keys = {int(l.split("|")[0]) for l in (UI / "shop_icons.txt").read_text().splitlines() if l.strip()}
        for e in range(1100, 1106):
            self.assertIn(10000 + e, keys)


@unittest.skipUnless((UI / "mm.txt").exists(), "run tools/build_menu_art.py first")
class MenuArt(unittest.TestCase):
    def test_main_menu_loop_pages(self):
        # bottom-screen main menu: the default loop and the naked easter-egg loop share one layout (only one set is resident)
        for tag in ("mm", "mmn"):
            fields = list(map(int, (UI / f"{tag}.txt").read_text().strip().split("|")))
            fps, frames, cw, ch, cols, rows, pages = fields[:7]
            if len(fields) >= 10:   # crop window in the 1920x1080 video, 4:3, inside the frame
                x, y, w = fields[7:10]
                self.assertTrue(0 <= x and x + w <= 1920 and 0 <= y and y + w * 3 // 4 <= 1080, tag)
            self.assertEqual((cw, ch, cols, rows), (320, 240, 3, 4), tag)
            self.assertEqual(pages, (frames + cols * rows - 1) // (cols * rows))
            self.assertLessEqual(pages, 8, "ui_art.c keeps at most 8 pages")
            self.assertGreaterEqual(frames, 8 * fps - 1)
            for i in range(pages):
                self.assertTrue((UI / f"{tag}_{i}.t3x").exists())

    def test_difficulty_icons(self):
        # DifficultyNode icons: three per stage that ships them; Makayla's notes.cfg names icon files that do not exist -> none
        rows = [l.split("|") for l in (UI / "diff_icons.txt").read_text().splitlines() if l.strip()]
        self.assertTrue((UI / "diff_icons.t3x").exists())
        ids = {r[0] for r in rows}
        self.assertNotIn("makayla", ids)
        for sid in ids:
            self.assertEqual(sorted(int(r[1]) for r in rows if r[0] == sid), [0, 1, 2], sid)
        self.assertEqual(len({int(r[2]) for r in rows}), len(rows), "one cell per icon")

    def test_icon_sheets(self):
        self.assertTrue((UI / "icons.t3x").exists())
        names = [l.split("|")[0] for l in (UI / "ach_icons.txt").read_text().splitlines() if l.strip()]
        self.assertIn("trophy", names)
        # every Achievements-grid tile's own icon (achievements.tscn icon_texture; names as in core/progress.c)
        for n in ("fingies", "combo_king", "gear", "cathy_bell", "feather", "gale_perfect", "mug", "dawna_perfect_candy", "impossible",
                  "stopwatch", "staring_jasmine", "nice", "chad", "gigachad", "honk_1", "honk_2", "honk_3"):
            self.assertIn(n, names)
        self.assertEqual(len(set(names)), len(names), "one cell per icon")
        self.assertLessEqual(len(names), 32, "ui_art.c reads up to 32 names; the 256x512 sheet has 32 cells")

    def test_font_has_hand_face(self):
        faces = {l.split("|")[1] for l in (UI / "font.txt").read_text(encoding="utf-8").splitlines() if l.startswith("F|")}
        self.assertEqual(faces, {"body", "bold", "head", "hand", "noto", "notob", "notor"})
