"""tools/build_theme.py output: the Halloween theme assets the runtime loads (romfs/ui, romfs/boot, romfs/music)."""
import unittest
from pathlib import Path

ROMFS = Path(__file__).resolve().parent.parent / "romfs"


@unittest.skipUnless((ROMFS / "ui" / "mmh.txt").exists(), "run tools/build_theme.py --romfs romfs first")
class ThemeAssetsTest(unittest.TestCase):
    def test_halloween_menu_loop_fits_the_resident_set(self):
        f = (ROMFS / "ui" / "mmh.txt").read_text().strip().split("|")
        fps, frames, cw, ch, cols, rows, pages = (int(x) for x in f[:7])
        self.assertEqual((cw, ch, cols, rows), (320, 240, 3, 4))   # same cells as mm_* (ui_art.c mm_load)
        self.assertLessEqual(pages, 8)                              # ui_art.c keeps 8 pages resident
        self.assertEqual(pages, (frames + 11) // 12)
        for p in range(pages):
            self.assertTrue((ROMFS / "ui" / f"mmh_{p}.t3x").exists())

    def test_logo_icon_splash_and_music_exist(self):
        for rel in ("ui/logo_h.t3x", "ui/theme_icon.t3x", "boot/splash_h.t3x", "boot/splash_h.wav", "music/spooky.wav"):
            self.assertTrue((ROMFS / rel).exists(), rel)

    def test_splash_sound_has_the_0_4_s_lead(self):
        import wave
        with wave.open(str(ROMFS / "boot" / "splash_h.wav")) as w:
            self.assertEqual((w.getframerate(), w.getnchannels()), (32000, 2))
            lead = w.readframes(int(0.39 * 32000))
            self.assertEqual(lead.count(0), len(lead))   # silence until the AudioStreamPlayer key at 0.4 s


if __name__ == "__main__":
    unittest.main()
