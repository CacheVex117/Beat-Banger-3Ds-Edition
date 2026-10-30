"""Unit and stage-contract tests for native3ds/tools/convert_stage.py."""
from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from convert_stage import (
    BB_CHART_MAGIC,
    BB_CHART_VERSION,
    StageSchemaError,
    convert_stage_configs,
    normalize_chart_notes,
    normalize_modifiers,
    normalize_stage_shutters,
    pack_bbchart_binary,
)


class TestConvertStage(unittest.TestCase):
    def test_modifiers_constant_and_piecewise(self) -> None:
        mods = normalize_modifiers([{"bpm": 126.0, "timestamp": 0.0}])
        self.assertEqual(mods, [{"timestamp_us": 0, "beat": 0, "bpm_milli": 126000}])

        piecewise = normalize_modifiers([{"bpm": 120.0, "timestamp": 0}, {"bpm": 150.0, "timestamp": 60}])
        self.assertEqual(piecewise[0]["timestamp_us"], 0)
        self.assertEqual(piecewise[1]["timestamp_us"], 30_000_000)
        self.assertEqual(piecewise[1]["bpm_milli"], 150000)

    def test_modifiers_reject_invalid(self) -> None:
        with self.assertRaises(StageSchemaError):
            normalize_modifiers([])
        with self.assertRaises(StageSchemaError):
            normalize_modifiers([{"bpm": 0, "timestamp": 0}])
        with self.assertRaises(StageSchemaError):
            normalize_modifiers([{"bpm": 120, "timestamp": 4}])

    def test_shutter_normalization_mixed(self) -> None:
        mods_raw = [{"bpm": 126.0, "timestamp": 0.0}]
        shutters = normalize_stage_shutters([[64], [96], {"timestamp": 76.1905}], mods_raw)
        self.assertEqual(shutters, [30476190, 45714286, 76190500])

    def test_notes_validation(self) -> None:
        notes = normalize_chart_notes(
            [
                {"timestamp": 0, "input_type": 0, "note_modifier": 0},
                {"timestamp": 0.952381, "input_type": 1, "note_modifier": 0, "trigger_voice": True},
            ],
            "Virgin",
        )
        self.assertEqual(len(notes), 2)
        self.assertEqual(notes[0]["timestamp_us"], 0)
        self.assertEqual(notes[1]["timestamp_us"], 952381)
        self.assertEqual(notes[1]["trigger_voice"], 1)

        with self.assertRaises(StageSchemaError):
            normalize_chart_notes([{"timestamp": 1.0, "input_type": 5, "note_modifier": 0}], "BadInput")
        with self.assertRaises(StageSchemaError):
            normalize_chart_notes(
                [
                    {"timestamp": 2.0, "input_type": 0, "note_modifier": 0},
                    {"timestamp": 1.0, "input_type": 0, "note_modifier": 0},
                ],
                "Unsorted",
            )

    def test_hold_notes_require_valid_tail(self) -> None:
        base = {"timestamp": 1.0, "input_type": 0, "note_modifier": 3}
        # Holds without a tail / empty tail complete immediately in the game: 1 us minimum duration
        self.assertEqual(normalize_chart_notes([dict(base)], "NoTail")[0]["hold_duration_us"], 1)
        self.assertEqual(normalize_chart_notes([dict(base, hold_end_timestamp=1.0)], "ZeroLen")[0]["hold_duration_us"], 1)
        stray = normalize_chart_notes([{"timestamp": 1.0, "input_type": 0, "note_modifier": 0, "hold_end_timestamp": 2.0}], "StrayTail")
        self.assertEqual(stray[0]["hold_duration_us"], 0)  # ignored on non-hold notes
        ok = normalize_chart_notes([dict(base, hold_end_timestamp=1.5)], "Ok")
        self.assertEqual(ok[0]["hold_duration_us"], 500000)

    def test_cathy_e1_real_stage_conversion(self) -> None:
        stage_dir = Path(__file__).resolve().parents[2] / "data" / "bb_vol_1" / "cathy_e1"
        if not stage_dir.is_dir():
            self.skipTest("cathy_e1 stage directory not available")

        charts = convert_stage_configs(stage_dir)
        self.assertEqual(len(charts), 3)

        by_name = {c["difficulty"]: c for c in charts}
        self.assertIn("Virgin", by_name)
        self.assertIn("Normal", by_name)
        self.assertIn("Expert", by_name)

        virgin = by_name["Virgin"]
        self.assertEqual(virgin["stage_id"], "cathy_e1")
        self.assertEqual(virgin["stage_title"], "Virgin Newbie")
        self.assertEqual(virgin["initial_bpm_milli"], 126000)
        self.assertEqual(virgin["song_offset_us"], 1_900_000)
        self.assertEqual(virgin["note_offset_us"], 0)
        self.assertEqual(len(virgin["notes"]), 279)
        self.assertEqual(virgin["notes"][0]["timestamp_us"], 0)
        self.assertEqual(virgin["notes"][-1]["timestamp_us"], 152_381_000)
        # Virgin uses strictly A (0) and B (1)
        self.assertEqual({n["input_type"] for n in virgin["notes"]}, {0, 1})
        self.assertEqual(
            virgin["shutters_us"],
            [30476190, 45714286, 76190476, 106666667, 152380952],
        )
        self.assertEqual(len(virgin["loops"]), 5)

        blob = pack_bbchart_binary(virgin)
        magic, version = struct.unpack_from("<4sI", blob, 0)
        self.assertEqual(magic, BB_CHART_MAGIC)
        self.assertEqual(version, BB_CHART_VERSION)
        expected_len = 152 + 279 * 16 + 5 * 8 + 5 * 40 + 1 * 16
        self.assertEqual(len(blob), expected_len)

    def test_all_source_timestamps_and_binary_records(self) -> None:
        # Read this isolated worktree's original-baseline data, not the Windows sprint.
        from decimal import Decimal, ROUND_HALF_UP
        from tools.convert_stage import parse_godot_cfg_json
        stage = Path(__file__).resolve().parents[2] / "data/bb_vol_1/cathy_e1"
        if not (stage / "config/notes.cfg").is_file():
            self.skipTest("cathy_e1 stage directory not available")
        source = parse_godot_cfg_json(stage / "config/notes.cfg")["charts"]
        keyframes = parse_godot_cfg_json(stage / "config/keyframes.cfg")
        converted = convert_stage_configs(stage)
        def us(value):
            return int((Decimal(str(value)) * 1000000).quantize(Decimal(1), rounding=ROUND_HALF_UP))
        for raw, chart in zip(source, converted):
            self.assertEqual(len(raw["notes"]), len(chart["notes"]))
            blob = pack_bbchart_binary(chart)
            for i, note in enumerate(raw["notes"]):
                expected = (max(0, us(note["timestamp"])), note["input_type"], note["note_modifier"], int(bool(note.get("trigger_voice", False))))
                self.assertEqual(struct.unpack_from("<qBBB", blob, 152 + i * 16), expected)
                dur = struct.unpack_from("<I", blob, 152 + i * 16 + 11)[0]
                if note["note_modifier"] == 3:
                    end_us = max(0, us(note["hold_end_timestamp"]))
                    self.assertEqual(dur, end_us - expected[0])
                    self.assertGreater(dur, 0)
                else:
                    self.assertEqual(dur, 0)
            self.assertEqual([x["timestamp_us"] for x in chart["loops"]], [us(x["timestamp"]) for x in keyframes["loops"]])
            self.assertEqual([x["name"] for x in chart["loops"]], [x["animations"]["normal"].removesuffix(".png") for x in keyframes["loops"]])
            expected_shutters = [int((Decimal(x[0]) * 60000000 / 126).quantize(Decimal(1), rounding=ROUND_HALF_UP)) for x in keyframes["shutter"]]
            self.assertEqual(chart["shutters_us"], expected_shutters)


if __name__ == "__main__":
    unittest.main()
