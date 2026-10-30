# MOD2DS package format v1

The contract between MOD2DS (PC) and the native Beat Banger 3DS runtime. A package is a folder meant to
be copied to the SD card, by default as `sdmc:/3ds/bb_mods/`.

```
<package>/
  stages/index.txt                 one row per converted level, same format as romfs:/stages/index.txt
  stages/<stage_id>/               same layout and formats as romfs:/stages/<id>/ built by native3ds
    index_row.txt                    this level's own index row (the game also lists folders index.txt does not name)
    charts/<stage_id>_virgin.bbchart   BBCH v1, one file per difficulty slot (always all three)
    charts/<stage_id>_normal.bbchart
    charts/<stage_id>_expert.bbchart
    charts/<stage_id>_c<N>.bbchart     authored charts beyond the third (not selectable yet)
    audio/<stage_id>.wav               song, RIFF PCM16 stereo 32 kHz
    gfx/<stage_id>.bbanim              BBAN v3 (sheets, camera keys, variations)
    gfx/sheet<N>_<P>.t3x               sprite pages (etc1a4 by default), named by the .bbanim
    gfx/game_over.t3x                  512x256 ETC1, 400x240 used (only if the mod has images/game_over.png)
    sfx/<stage_id>.bbsfx               BBSF v1 (clips + loop/oneshot/voice tables + mono PCM16 32 kHz)
    video/<stage_id>_v<n>.bbvid        BBVD v2 + its .t3x pages and .pcm soundtrack (n = 0..2)
    menu/thumb.png                     96x96 level-select portrait source
    menu/thumb.t3x                     the same portrait masked to a circle, RGBA8 (what the runtime's Mods screens draw)
    cutscene/{pre,post}.txt|.bbca|_<n>.t3x   mod cutscenes, same files as native3ds tools/convert_cutscene.py makes
                                             (see "Cutscenes")
    menu/preview.wav                   24 s song preview from mod.cfg preview_timestamp (default 30 s)
    level.json                         the full normalized level (see below)
  packs/<pack_id>.json             one manifest per mod pack (source, act info, per-level status + issues)
  REPORT.md, report.json           what converted, and what did not and why
```

Everything inside `stages/<stage_id>/` is byte-compatible with what `native3ds/tools/build_stage.py`
produces for a shipped stage. The runtime needs no new loaders; it needs a second stage root
(see "Runtime integration").

## Identifiers

| id | form | max | why |
|---|---|---|---|
| `stage_id` | `<slug of level_name, <=13>_<sha1 6>` | 20 | save ids keep 23 chars (`BB_SAVE_ID_LEN`); video page names need id <= 21 |
| `pack_id` (index `act` field) | `<slug of act_name, <=16>_<sha1 4>` | 21 | index `act` field keeps 23 |
| sheet names | `s<NN>_<slug>` | 31 | `char[32]`; BBCH loop names refer to these |
| clip names | `c<NN>_<slug>` | 31 | `char[32]` |

The hash covers the act id, the level id and the level folder, so ids are stable across runs and
re-conversion keeps save data. Titles, characters and act names are transliterated to printable ASCII
and cut to the index limits (47 / 23 / 31).

## stages/index.txt

`stage_id|title|character|pack_id|act_name|act_index|level_index|0`, sorted by pack then level index.
`level_enum` is always 0 (mods have no unlock enum).

Every stage folder also holds its row in `index_row.txt`. The game (native3ds, after `index.txt`) lists any
`sdmc:/3ds/bb_mods/stages/<id>/` folder whose `index_row.txt` names that same id and which `index.txt` does not
already list, sorted by pack then level index. `oneshot` packages (MOD2DS.exe) ship no `index.txt` at all, so copying
one onto a card never overwrites the card's list.

## Difficulty slots

The runtime has three slots (`virgin`, `normal`, `expert`). As in the game's `Difficulty.get_current_chart`,
slot *i* plays authored chart *i*, or chart 0 when the level has fewer than *i+1* charts, so all three
slot files always exist. `level.json` records which authored chart each slot uses.

## level.json (`format: "mod2ds-level"`, `version: 1`)

The level after applying the game's rules, with every time in integer microseconds:
`timing` (song/note offset, tempo map, shutters), `charts` (all authored charts), `difficulty_slots`,
`loops` (sheet + weighted variations), `sheets`, `camera`, `clips`, `sound_loop`, `sound_oneshot`,
`voice_bank`, `video`, plus:

- `extras_not_in_3ds_formats`: data the game uses but no 3DS format carries yet (background layers,
  effects, flags, toys, outro `final_video`/`final_audio`, level start/game over sounds).
  A runtime that gains support can read it from here without re-reading the mod.
- `outputs`: the files written per step, with sizes, frame counts and the sprite VRAM estimate.
- `issues`: every finding, with severity:
  - `limit`: over a current runtime limit (`mod2ds/limits.py`); the stage will not load as-is
  - `unsupported`: used by the game, no 3DS representation yet (dropped from the binaries)
  - `warning`: a source-data problem, handled the way the game handles it
  - `ignored`: the current game ignores it too (pre-release fields such as horny-mode sheets)
- `runtime_compatible`: true when there is no `limit` issue.

## Game rules applied (source: the Godot 4.7 game scripts)

| rule | from |
|---|---|
| act = sub-folder with a loadable `act.cfg`; level = sub-folder with a loadable `config/meta.cfg`, sorted by `level_index` | `GameManager.get_act_datatree`, `Level.get_level_list` |
| `asset/keyframes/meta/notes/settings.cfg` must load; `mod.cfg`/`toys.cfg` optional | `Config.store_config_data` |
| note and hold-end times snapped to 0.001 s | `Config.round_note_timestamps` |
| bare-string loop variations get weight 50 | `Config.normalize_loop_variations` |
| a sheet used by several loops is split with the last loop's `sheet_data` | `AssetManager.get_sheet_geometry` |
| unsorted keyframe lists: the k-th smallest time shows the k-th entry in list order | `TimeStampEvent` (`timestamp_component.gd`) |
| camera = position, scale, speed, interpolation (default EXPO) | `game_character_root.gd` |
| asset files resolved per extension in `audio/`, `images/`, `video/` (case-insensitive here) | `AssetManager.get_requested_asset_folder` |

Shutters, tempo map and hold-without-tail handling are the native3ds converter rules
(`convert_stage.py`), so shipped stages convert to the same chart data (`tests/test_formats.py`).

## Framing (sprite placement)

The runtime draws every animation frame as a fixed 1280x720 rect of character space
(`game_view.c draw_character`; x1.5 gives the 1920x1080 screen). The game draws a frame at its
**source pixel size**, centered. MOD2DS composes each frame onto a 16:9 cell exactly where the game
draws it, so the fixed rect never distorts a frame that is not 1280x720:

| mode | rule | source |
|---|---|---|
| `game` | size = frame px; loop `scale_multiplier` / `position_offset` are ignored | Godot 4.7 `game_character.gd` |
| `legacy` | size = frame px x `scale_multiplier` (it persists into later loops, starting at 1.0); center = screen center + `position_offset` (screen px, i.e. /1.5 in character units) | Godot 3 `Backport/src/scenes/Game/Graphics/character.gd` |

`auto` (default) picks `legacy` for a level whose loops carry `scale_multiplier` or `position_offset`;
those mods were tuned for the Godot 3 rules. Any other level gets `game`.

Frames bigger than the screen are cropped as in the game. If a camera key zooms out far enough to show
more, the cell canvas grows by a factor k (`level.json` `framing.canvas_k`). Every camera scale is then
multiplied by k, and a zero-length key at -3600 s sets the starting scale, so on-screen size is
unchanged. `manual_speed_multiplier` (legacy animation speed) has no 3DS field; it is reported as
`unsupported`.

## Cutscenes

A level's `cutscene/pre/script.cfg` and `cutscene/post/script.cfg` are converted to the runtime's cutscene files
(`mod2ds/cutscene.py`). For the shipped stages the output is byte-identical to native3ds `convert_cutscene.py`
(`tests/test_cutscene.py`).

- **Assets:** looked up by file name in the shared `cutscene/shots` and `cutscene/audio` folders, as the game does.
  If a file is not there, MOD2DS tries the scene's own `cutscene/<scene>/shots|audio`, the layout of the Encore packs,
  and reports `cutscene_scene_folder`.
- **Clean-up:** dialogue loses its BBCode and newlines and is transliterated to ASCII.
- **Runtime limits:** 64 panels, 64 sounds, 24 track segments, 8 character colours, 4 sounds per panel, 239-character
  text. Content beyond them is cut, with a `cutscene_limit` warning.
- **Placeholders:** the cutscene editor's untouched template (one "Sample Dialogue" panel, no art or sound) is skipped
  (`cutscene_placeholder`).

## Not converted in v1

- Pre-release ("Legacy") mods: a `chart.cfg` and no `act.cfg`. The game converts them at play time.
  They are listed in the report.
- Outro videos, background/effect layers, flags and toys. These are kept in `level.json` and reported as
  `unsupported`.

## Runtime support (native3ds)

The runtime reads this package since native3ds `cf39d5d`: a second stage index at
`sdmc:/3ds/bb_mods/stages/index.txt`, a root per stage, and the Mods menu (library, per-mod screen, round
`menu/thumb.t3x` portraits, `menu/preview.wav` previews). Mod packs do not take act slots.

Limits raised for SD mods in `bc7297f`. Keep `mod2ds/limits.py` in step with these:

| limit | value | where |
|---|---|---|
| notes per chart | 1024 | `BB_MAX_NOTES`, core/rhythm_core.h |
| loop keyframes | 256 | `BB_MAX_LOOPS`, core/rhythm_core.h |
| sprite sheets | 64 | `BB_ANIM_MAX_SHEETS`, platform/3ds/anim.h; above 12 MiB, sheets stream |
| timed one-shots | 512 | `BB_SFX_MAX_ONESHOTS`, platform/3ds/sfx.h |
| sound-loop / voice-bank keyframes | 64 / 64 | `BB_SFX_MAX_LOOPS` / `BB_SFX_MAX_VOICES`, platform/3ds/sfx.h |
| videos per stage | 16 | `BB_VIDEO_MAX`, platform/3ds/video.h |
| pages per video | 384 | `BB_VIDEO_MAX_PAGES`, about 102 s at 30 fps |

**Video on hardware (New 2DS XL, measured 2026-10-29):**

- The console reads the SD card at only about 1.5 MB/s. File I/O, the GPU service and audio all run on the system
  core, so heavy reads slow the whole game, not just the video.
- Full-resolution pages (8 frames, about 250 KB each) at 30 fps needed about 1 MB/s. Tracer fell to 30-50 ms frames,
  starved the song and failed.
- MOD2DS therefore converts any level with more than 20 s of video at **half resolution**: 200x120 frames, 32 per page,
  a quarter of the reads (`--video-res`). It also caps the frame rate at 30 fps (`--video-fps`).
- Half resolution fills the screen, but a little softer. Tracer then played start to finish at 17-18 ms frames, with
  short bumps to about 20 ms at clip changes.
- The runtime loads one page at a time, on core 2 (New models; Old 3DS / 2DS have no core 2, so there the loader runs on
  core 0 below the game thread), in 32 KiB reads, keeping 3 pages ahead.

Videos open lazily: a clip holds pages and audio only from 2 s before its start until it ends. Its soundtrack
streams from the `.pcm` file through four 0.25 s buffers on one of three shared NDSP channels (6-8). A
full-motion mod (a dozen clips covering the whole song) therefore never holds more than two clips in memory.

The game lists 56 mod levels (`MAX_STAGES` 64 shared with the 8 built-in stages) and keeps scores for all of them
(`BB_SAVE_MAX_STAGES` 64). Level unlocking inside a pack is recorded per pack (`BB_SAVE_MAX_ACTS` 32, 3 kept for the
built-in volumes): past 29 packs, a pack's first level still plays but its later levels do not unlock. With the root `sdmc:/3ds/bb_mods`, every path fits the runtime's buffers;
`mod2ds verify --sd-root` checks a different root.
