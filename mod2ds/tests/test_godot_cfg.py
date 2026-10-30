import math
import unittest

from mod2ds import godot_cfg
from mod2ds.godot_cfg import CfgError, GodotObject, parse_text, parse_value


class GodotCfgTest(unittest.TestCase):
    def test_beat_banger_layout(self):
        cfg = parse_text('[main]\n\ndata={\n"act_name": "A",\n"act_index": 0,\n}\n')
        self.assertEqual(cfg["main"]["data"], {"act_name": "A", "act_index": 0})

    def test_raw_newline_unicode_and_escapes_in_strings(self):
        v = parse_value('{"d": "By HMioKun\nThank you", "u": "Lição \\u2661 \\"q\\" \\\\ \\t"}')
        self.assertEqual(v["d"], "By HMioKun\nThank you")
        self.assertEqual(v["u"], 'Lição ♡ "q" \\ \t')

    def test_surrogate_pair_escape(self):
        self.assertEqual(parse_value('"\\ud83d\\ude00"'), "\U0001F600")

    def test_numbers(self):
        self.assertEqual(parse_value("[0, -3, 1.5, -0.000238095, 1e-05, 2.0e3, 01]"), [0, -3, 1.5, -0.000238095, 1e-05, 2000.0, 1])
        self.assertIsInstance(parse_value("0"), int)
        self.assertIsInstance(parse_value("0.0"), float)
        self.assertTrue(math.isinf(parse_value("inf_neg")) and parse_value("inf_neg") < 0)
        self.assertTrue(math.isnan(parse_value("nan")))

    def test_constructors_typed_containers_and_names(self):
        v = parse_value('{"c": Color(0.3, 0.2, 0.3, 1), "v": Vector2(1, -2), "p": PackedStringArray("a", "b"), '
                        '"t": Array[int]([1, 2]), "d": Dictionary[String, int]({"k": 1}), "s": &"name", "n": ^"Node/Path", '
                        '"x": null, "b": true, "o": Callable(1, "a")}')
        self.assertEqual(v["c"], [0.3, 0.2, 0.3, 1])
        self.assertEqual(v["v"], [1, -2])
        self.assertEqual(v["p"], ["a", "b"])
        self.assertEqual(v["t"], [1, 2])
        self.assertEqual(v["d"], {"k": 1})
        self.assertEqual((v["s"], v["n"], v["x"], v["b"]), ("name", "Node/Path", None, True))
        self.assertIsInstance(v["o"], GodotObject)

    def test_trailing_commas_and_comments(self):
        cfg = parse_text('; comment\n[main]\ndata = [1, 2,]\n; another\nother={"a": [ ], }\n')
        self.assertEqual(cfg["main"], {"data": [1, 2], "other": {"a": []}})

    def test_keys_follow_godot(self):
        # Godot 3.5 ConfigFile on this text gives exactly these keys: '#' is no comment, spaces in keys are dropped, a quoted
        # string replaces what was collected, and collecting continues across lines until '='
        cfg = parse_text('[main]\n# a hash line\nalpha = 1\n; semi\nbe ta = 2\n//junk "q" more\ngamma = 3\n')
        self.assertEqual(cfg["main"], {"#ahashlinealpha": 1, "beta": 2, "qmoregamma": 3})

    def test_notes_after_the_data_still_load(self):
        # minus8 MegaPack / Amelia N asset.cfg: template notes after the data; the game (Godot) loads the file
        text = ('[main]\r\ndata = {\r\n    "song_path":"song.mp3",\r\n    "final_video": "climax.webm"\r\n}\r\n\r\n'
                '//Change what\'s in "" in song_path to whatever your song is called\r\n'
                '//Change any of the "text" to your audio files. Final audio is used during the climax.webm\r\n')
        self.assertEqual(parse_text(text)["main"]["data"], {"song_path": "song.mp3", "final_video": "climax.webm"})

    def test_errors_carry_position(self):
        # PHONK HOUSE '99 mod.cfg writes a time literal; Godot's parser rejects it too
        with self.assertRaises(CfgError) as cm:
            parse_text('[main]\ndata = {\n\t"preview_timestamp": 01:06.58\n}\n', "mod.cfg")
        self.assertIn("mod.cfg:3:", str(cm.exception))
        # Zari mod.cfg has an unescaped quote inside a string
        with self.assertRaises(CfgError):
            parse_text('[main]\ndata={\n"description": "de "Ofenciva" do",\n}\n')
        with self.assertRaises(CfgError):
            parse_value('"unterminated')

    def test_load_data_default_and_bom(self):
        import tempfile
        from pathlib import Path
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "x.cfg"
            p.write_bytes("﻿[main]\ndata={\"a\": 1}\n".encode("utf-8"))
            self.assertEqual(godot_cfg.load_data(p), {"a": 1})
            self.assertEqual(godot_cfg.load_data(p, key="missing", default={}), {})


if __name__ == "__main__":
    unittest.main()
