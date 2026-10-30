import copy
import tempfile
import unittest
from pathlib import Path

from mod2ds import discover, limits, names
from mod2ds.model import Issues, LevelError, build_level, check_runtime_limits, game_order, snapped_ms

from synthetic import default_level, make_mod, write_cfg


class ModelTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def level(self, data=None, framing="auto", **kw):
        pack = discover.load_pack(make_mod(self.root, level=data, **kw))
        self.assertEqual(pack.kind, "act")
        return build_level(pack.levels[0], pack, "pid", framing=framing)

    # -- discovery ---------------------------------------------------------------------------------
    def test_discovery_matches_game_rules(self):
        make_mod(self.root)
        (self.root / "Legacy Mod" / "x").mkdir(parents=True)
        (self.root / "Legacy Mod" / "x" / "chart.cfg").write_text("[EASY]\nbpm=100\n")
        (self.root / "Not A Mod").mkdir()
        bad = self.root / "Broken"
        bad.mkdir()
        (bad / "act.cfg").write_text("[main]\ndata={\"a\": \n")
        kinds = {p.path.name: p.kind for p in discover.scan(self.root)}
        self.assertEqual(kinds, {"Synth Pack": "act", "Legacy Mod": "legacy", "Not A Mod": "invalid", "Broken": "invalid"})

    def test_levels_need_meta_and_sort_by_level_index(self):
        pack = make_mod(self.root, level_folder="z_first")
        d = default_level()
        d["meta"]["level_index"] = -1
        make_mod(self.root, level_folder="a_second", level={**d, "meta": {**d["meta"], "level_index": 5}})
        make_mod(self.root, level_folder="b_zero", level={**d, "meta": {**d["meta"], "level_index": 0}})
        (pack / "no_meta" / "config").mkdir(parents=True)
        p = discover.load_pack(pack)
        self.assertEqual([lv.folder for lv in p.levels], ["b_zero", "z_first", "a_second"])

    # -- game semantics --------------------------------------------------------------------------
    def test_note_snapping_holds_and_slots(self):
        lv = self.level()
        easy = lv.charts[0]["notes"]
        self.assertEqual(easy[1]["timestamp_us"], 1_000_000)          # 1.0004 snapped to 1.000
        self.assertEqual(easy[1]["hold_duration_us"], 500_000)
        self.assertEqual(easy[2]["trigger_voice"], 1)
        self.assertEqual(lv.slots, {"virgin": 0, "normal": 1, "expert": 0})   # Difficulty.get_current_chart fallback
        self.assertEqual(lv.song_offset_us, 500_000)

    def test_snapped_ms_matches_godot(self):
        self.assertAlmostEqual(snapped_ms(1.0004), 1.0)
        self.assertAlmostEqual(snapped_ms(1.0006), 1.001)
        self.assertAlmostEqual(snapped_ms(-0.000238095), 0.0)

    def test_hold_without_tail_gets_1us(self):
        d = default_level()
        d["notes"]["charts"][0]["notes"][1].pop("hold_end_timestamp")
        self.assertEqual(self.level(d).charts[0]["notes"][1]["hold_duration_us"], 1)

    def test_variation_string_gets_weight_50(self):
        lv = self.level()
        self.assertEqual(lv.loops[1]["variations"], [{"sheet": lv.loops[0]["sheet"], "weight": 50.0}])

    def test_last_sheet_geometry_wins(self):
        d = default_level()
        d["keyframes"]["loops"].append({"timestamp": 2.0, "animations": {"normal": "a.png"}, "sheet_data": {"h": 2, "v": 2, "total": 3}})
        lv = self.level(d)
        self.assertEqual(lv.sheets[lv.loops[0]["sheet"]].total, 3)
        self.assertTrue(any(i.code == "sheet_geometry_conflict" for i in lv.issues.items))

    def test_unsorted_keyframes_follow_game_index_lookup(self):
        issues = Issues()
        entries = [{"_t": 5.0, "p": "A"}, {"_t": 1.0, "p": "B"}, {"_t": 3.0, "p": "C"}]
        out = game_order(entries, issues, "loops")
        # TimeStampEvent: after 1 s one item has ts < now -> items[0] (A); after 3 s -> items[1] (B); after 5 s -> C
        self.assertEqual([(e["_t"], e["p"]) for e in out], [(1.0, "A"), (3.0, "B"), (5.0, "C")])
        self.assertTrue(issues.has("warning"))

    def test_case_insensitive_assets_and_missing_assets(self):
        d = default_level()
        d["keyframes"]["loops"][0]["animations"]["normal"] = "A.PNG"
        d["keyframes"]["sound_oneshot"].append({"timestamp": 2.0, "path": "nope.ogg"})
        lv = self.level(d)
        self.assertEqual(lv.sheets[lv.loops[0]["sheet"]].source.name, "a.png")
        self.assertEqual(len(lv.oneshots), 1)
        self.assertTrue(any(i.code == "missing_asset" for i in lv.issues.items))

    def test_tempo_fallback_from_mistyped_key(self):
        d = default_level()
        d["keyframes"]["modifiers"] = [{"MC.BPM": 123, "timestamp": 0.0}]
        lv = self.level(d)
        self.assertEqual(lv.bpm_changes[0]["bpm_milli"], 123000)
        self.assertTrue(any(i.code == "tempo_fallback" for i in lv.issues.items))

    def test_tempo_map_beats_to_us(self):
        d = default_level()
        d["keyframes"]["modifiers"] = [{"bpm": 120.0, "timestamp": 0}, {"bpm": 60.0, "timestamp": 8}]
        d["keyframes"]["shutter"] = [[10]]
        lv = self.level(d)
        self.assertEqual([b["timestamp_us"] for b in lv.bpm_changes], [0, 4_000_000])
        self.assertEqual(lv.shutters_us, [6_000_000])     # 8 beats @120 = 4 s, +2 beats @60 = 2 s

    def test_required_configs_and_song(self):
        d = default_level()
        del d["notes"]
        with self.assertRaises(LevelError):
            self.level(d)
        d = default_level()
        d["asset"]["song_path"] = "missing.ogg"
        with self.assertRaises(LevelError):
            self.level(d)

    def test_unloadable_optional_mod_cfg_is_a_warning(self):
        pack = make_mod(self.root)
        (pack / "default" / "config" / "mod.cfg").write_text('[main]\ndata = {"preview_timestamp": 01:06.58}\n')
        p = discover.load_pack(pack)
        lv = build_level(p.levels[0], p, "pid")
        self.assertTrue(any(i.code == "config_unloadable" for i in lv.issues.items))

    def test_unsupported_and_ignored_are_reported(self):
        d = default_level()
        d["keyframes"]["background"] = [{"timestamp": 0.0, "path": "bg.png"}]
        d["keyframes"]["camera"][0]["interpolation"] = 1
        d["keyframes"]["loops"][0]["scale_multiplier"] = 0.7
        d["asset"]["final_video"] = "climax.ogv"
        lv = self.level(d)
        codes = {(i.severity, i.code) for i in lv.issues.items}
        self.assertIn(("unsupported", "keyframes_background"), codes)
        self.assertIn(("unsupported", "camera_interpolation"), codes)
        self.assertIn(("warning", "missing_asset"), codes)   # final_video named but not in video/: nothing to play, like the game
        self.assertEqual(lv.framing, "legacy")      # a legacy loop field switches the level to legacy framing
        self.assertIn(("ignored", "loop_scale_multiplier"),
                      {(i.severity, i.code) for i in self.level(d, framing="game").issues.items})

    def test_final_video_and_audio_become_the_outro(self):
        from mod2ds.model import NO_PICTURE, OUTRO_START

        def outro_of(folder, d, files):
            pack = make_mod(self.root, folder=folder, level=d)
            for rel in files:
                (pack / "default" / rel).parent.mkdir(parents=True, exist_ok=True)
                (pack / "default" / rel).write_bytes(b"x")
            p = discover.load_pack(pack)
            return [v for v in build_level(p.levels[0], p, "pid").videos if v.get("outro")]

        d = default_level()
        d["asset"]["final_video"] = "climax.ogv"
        d["asset"]["final_audio"] = "end.ogg"
        outro = outro_of("A", d, ["video/climax.ogv", "audio/end.ogg"])
        self.assertEqual(len(outro), 1)
        self.assertEqual(outro[0]["start_us"], OUTRO_START)
        self.assertEqual((outro[0]["source"].name, outro[0]["audio_source"].name), ("climax.ogv", "end.ogg"))
        d["asset"]["final_video"] = ""   # sound only: no picture
        outro = outro_of("B", d, ["audio/end.ogg"])
        self.assertEqual((outro[0]["source"], outro[0]["hide_us"]), (None, NO_PICTURE))

    # -- framing ---------------------------------------------------------------------------------
    def level_with_frames(self, frame, extra_loop=None, framing="auto", cameras=None):
        from synthetic import write_sheet
        d = default_level()
        d["keyframes"]["loops"] = [{"timestamp": 0.0, "animations": {"normal": "p.png"}, "sheet_data": {"h": 2, "v": 1, "total": 2}}]
        if extra_loop:
            d["keyframes"]["loops"][0].update(extra_loop)
        d["keyframes"]["camera"] = cameras or []
        pack = make_mod(self.root, level=d)
        write_sheet(pack / "default" / "images" / "p.png", 2, 1, frame=frame)
        p = discover.load_pack(pack)
        return build_level(p.levels[0], p, "pid", framing=framing)

    def test_game_framing_crops_like_the_game_at_default_camera(self):
        # the game draws 600x800 at 1.5x = 900x1200 on a 1080 screen: cropped, never squashed; no camera zooms out
        lv = self.level_with_frames((600, 800))
        self.assertEqual((lv.framing, lv.canvas_k, lv.cameras), ("game", 1.0, []))

    def test_game_framing_grows_canvas_when_the_camera_zooms_out(self):
        lv = self.level_with_frames((600, 800), cameras=[{"timestamp": 5.0, "position": [0, 0], "scale": [0.5, 0.5], "speed": 0}])
        self.assertAlmostEqual(lv.canvas_k, 800 / 720, places=2)     # zoomed out, the whole 800 px height shows
        self.assertEqual(lv.cameras[0]["timestamp_us"], -3_600_000_000)
        self.assertAlmostEqual(lv.cameras[0]["scale"][0], lv.canvas_k)
        self.assertAlmostEqual(lv.cameras[1]["scale"][0], 0.5 * lv.canvas_k)

    def test_1280x720_frames_need_no_canvas_change(self):
        lv = self.level_with_frames((1280, 720))
        self.assertEqual((lv.canvas_k, lv.cameras), (1.0, []))

    def test_legacy_framing_applies_scale_and_offset(self):
        lv = self.level_with_frames((600, 800), {"scale_multiplier": 0.85, "position_offset": {"x": 0, "y": -30}})
        self.assertEqual(lv.framing, "legacy")
        sh = lv.sheets[lv.loops[0]["sheet"]]
        self.assertEqual((sh.scale, sh.offset), (0.85, (0.0, -20.0)))   # screen px / 1.5
        self.assertEqual(lv.canvas_k, 1.0)                                 # 680 + 2*20 <= 720
        lv = self.level_with_frames((600, 800), {"scale_multiplier": 0.85}, framing="game")
        self.assertEqual(lv.sheets[lv.loops[0]["sheet"]].scale, 1.0)

    def test_canvas_is_capped_by_camera_visibility(self):
        # 3840x2160 frames, camera never zooms out past 0.5: only a 2560x1440 area can ever be seen
        lv = self.level_with_frames((3840, 2160), cameras=[{"timestamp": 0.0, "position": [0, 0], "scale": [0.5, 0.5], "speed": 0}])
        self.assertAlmostEqual(lv.canvas_k, 2.0)
        self.assertAlmostEqual(lv.cameras[1]["scale"][0], 1.0)   # the original key: 0.5 * k

    def test_runtime_limits(self):
        d = default_level()
        n_notes, n_shots = limits.MAX_NOTES + 1, limits.MAX_ONESHOTS + 1     # one over whatever the runtime allows
        d["notes"]["charts"][0]["notes"] = [{"timestamp": i * 0.05, "input_type": 0, "note_modifier": 0} for i in range(n_notes)]
        d["keyframes"]["sound_oneshot"] = [{"timestamp": i * 0.05, "path": "hit.wav"} for i in range(n_shots)]
        lv = self.level(d)
        check_runtime_limits(lv)
        msgs = [i.message for i in lv.issues.items if i.severity == "limit"]
        self.assertTrue(any(f"{n_notes} notes" in m for m in msgs))
        self.assertTrue(any(f"{n_shots} sound_oneshot" in m for m in msgs))
        d["notes"]["charts"][0]["notes"] = d["notes"]["charts"][0]["notes"][:limits.MAX_NOTES]
        d["keyframes"]["sound_oneshot"] = d["keyframes"]["sound_oneshot"][:limits.MAX_ONESHOTS]
        lv = self.level(d)
        check_runtime_limits(lv)
        self.assertFalse(lv.issues.has("limit"))                             # exactly at the limit is fine

    def test_chart_beyond_three_slots(self):
        d = default_level()
        d["notes"]["charts"] += copy.deepcopy(d["notes"]["charts"][:2])
        lv = self.level(d)
        self.assertEqual(len(lv.charts), 4)
        self.assertTrue(any(i.code == "extra_charts" for i in lv.issues.items))


class NamesTest(unittest.TestCase):
    def test_ids_are_ascii_bounded_and_stable(self):
        sid = names.stage_id("Svin’s Hornet ~ Lost Media: The Extended Cut", "act", "lvl", "folder")
        self.assertRegex(sid, r"^[a-z0-9_]+$")
        self.assertLessEqual(len(sid), 20)
        self.assertEqual(sid, names.stage_id("Svin’s Hornet ~ Lost Media: The Extended Cut", "act", "lvl", "folder"))
        self.assertNotEqual(sid, names.stage_id("Svin’s Hornet ~ Lost Media: The Extended Cut", "act", "lvl", "other"))
        self.assertLessEqual(len(names.pack_id("A" * 80, "x")), limits.ACT_ID_MAX)
        self.assertLessEqual(len(f"sdmc:/3ds/bb_mods/stages/{sid}/gfx"), limits.GFX_DIR_MAX)

    def test_ascii_text(self):
        self.assertEqual(names.ascii_text("Zari_Lição ♡ | x"), "Zari_Licao <3 x")
        self.assertEqual(names.ascii_text("Krystal’s Act", 6), "Krysta")

    def test_asset_names_unique_after_truncation(self):
        a = names.asset_name("s", 0, "sheet_cappie_missionary_DoriDoriko_fun.webp")
        b = names.asset_name("s", 1, "sheet_cappie_missionary_DoriDoriko_3.webp")
        self.assertLessEqual(max(len(a), len(b)), 31)
        self.assertNotEqual(a, b)


if __name__ == "__main__":
    unittest.main()
