# MOD2DS: Beat Banger mod-to-3DS converter

Converts Beat Banger PC mods (the folders under
`%APPDATA%\Godot\app_userdata\Beat Banger\mods`) into stage folders that the native Beat Banger 3DS
port loads: the same BBCH / BBAN / BBSF / BBVD files and layout that `native3ds/tools/build_stage.py`
makes for the shipped stages. The output contract is in [SPEC.md](SPEC.md).

Mod folders are only read. The output folder must lie outside them, which the tool checks.

## Easiest: MOD2DS.exe (one mod in, one package out)

Drag one mod folder or bbmods `.zip` onto **`MOD2DS.exe`** (or double-click it and drag the mod into its window). Pick:

1. **Standard** - for most mods.
2. **Larger downscaling** - for heavy mods with lots of video or animation: smaller character frames (384 instead of
   512 texels wide), half-resolution video always, video at 24 fps instead of 30. The picture is softer; the 3DS reads
   and holds less.

The package appears in `output\<mod name> (3DS)` (or `(3DS, larger)`). Copy its `3ds` folder onto the root of the SD card.
Packages carry no `index.txt`; each level folder holds its own `index_row.txt`, which the game reads, so adding a package
never overwrites the mods already on the card. Cutscenes come along when the mod has them. Levels the game cannot run are
left out and listed with the reason.

`MOD2DS.exe` is portable: one ~100 MB file that carries Python, Pillow, MOD2DS, ffmpeg, ffprobe, tex3ds and the few shared
Beat Banger sounds conversion uses (miss / damage / glass, `data/_common/audio`), so it runs from any folder on any Windows
PC with nothing installed. Packages go to `output\` next to the exe; `MOD2DS cache\` beside it makes converting the same mod
again instant and can be deleted any time. If the Beat Banger install is at `C:\Beat Banger`, its sounds are used instead
of the bundled copies (they are the same files).

Rebuild the exe with `python launcher\build_portable.py` (needs `pip install pyinstaller`, plus ffmpeg, tex3ds and the game
install to copy the tools and sounds from). From source, `python -m mod2ds oneshot <mod> [--preset standard|larger]` does
the same thing.

## Drag and drop straight onto the card

Drag mod folders, bbmods `.zip` downloads, or your whole mods folder onto **`Drop mods here.bat`**.
It does three things:

- **Converts** each mod. Converted packs are kept in `library/`, so dropping the same mod again is
  instant.
- **Installs** only the levels the 3DS game can run. Each skipped level is listed with its reason.
- **Updates** `sdmc:/3ds/bb_mods/stages/index.txt` on the SD card.

The target is chosen automatically: any drive with a `Nintendo 3DS` folder (a 3DS SD card in a reader), or
Azahar's SD folder. If both are present, it asks which. From a terminal:
`python -m mod2ds install <mods...> [--sd E:\] [--include-over-limit]`.

## Requirements

- Python 3.10+ with Pillow (`pip install pillow`)
- ffmpeg + ffprobe (found on PATH, or `C:/Tools/ffmpeg/bin`; or pass `--ffmpeg` / `--ffprobe`)
- tex3ds from devkitPro (`C:/devkitPro/tools/bin/tex3ds.exe`, or pass `--tex3ds`)
- Optional: the Beat Banger install (`--game-dir`, default `C:/Beat Banger` when present). It supplies
  the shared miss/damage/glass sounds and `data/_common/audio` fallbacks. Without it those are left out
  and reported.

`scan` and `inspect` need only Python. `convert` needs the tools.

## Usage

```
python -m mod2ds scan                                  # every installed mod: what converts, what is over the limits
python -m mod2ds inspect "Kat's Audition"              # one pack in detail, every issue per level
python -m mod2ds convert "Kat's Audition" --out out    # convert one pack
python -m mod2ds convert --all --out out --fit-runtime # convert everything, fitting sprite sheets to the runtime
python -m mod2ds verify out                            # read the output back with the runtime loaders' rules
host\verify_charts.bat ..\native3ds out   # load every chart with the runtime's own C loader (MSVC)
```

Useful options for `convert`:

- `--level <folder or level_name>`: only that level. Repeat the flag for more than one.
- `--steps charts,song,sfx,sprites,video,menu,cutscene`: run only some steps. The rest of an existing stage
  folder is kept.
- `--fit-runtime`: decimate sprite sheets to 16 frames, the current runtime limit. Loop timing is
  unchanged because the game tweens frame 0 to the last frame over the loop; motion is less smooth.
- `--keep-oversize`: also build videos the runtime cannot play.
- `--framing auto|game|legacy`: where sprites sit on screen (see SPEC.md "Framing"). The default, `auto`,
  uses the Godot 3 rules (`scale_multiplier` / `position_offset`) for levels that carry those fields and
  the current game's rules otherwise.
- `--frame-width 512 --sprite-format etc1a4 --quality medium`: the native defaults.

Copy the output folder to the SD card as `sdmc:/3ds/bb_mods/` (or let `Drop mods here.bat` do it). The 3DS game
lists the levels under MODS > Library; SPEC.md "Runtime support" lists its limits.

## Results

`REPORT.md` in the output folder lists every pack and level with its status:

- **ready**: within every current runtime limit
- **converted, over runtime limits**: files written, but the current runtime would reject or truncate
  something (for example, more than 512 notes or 16 one-shot sounds)
- **not converted**: the game itself could not play it, or a tool failed

Each issue also has a category: `limit`, `unsupported`, `warning` or `ignored`. SPEC.md explains them.
Pre-release "Legacy" mods (a `chart.cfg` and no `act.cfg`) are listed by `analyze` / `convert`; `install` and
`oneshot` (MOD2DS.exe) first rewrite them as an act pack with the game's own LegacyConverter rules (legacy.py), then
convert them like any other mod. They play in the 3DS LEGACY LAYOUT (held sideways) unless the modifier is switched off.

## Tests

```
python -m unittest discover -s tests -p "test_*.py"
```

The tests use synthetic mods built on the fly; no Beat Banger content is in this repo. When native3ds
and the game install are present, `NativeParityTest` checks that the five shipped vol-1 stages give the
same chart data through MOD2DS as through native3ds `convert_stage.py`. Set `MOD2DS_NATIVE3DS` and
`MOD2DS_GAME` if they live elsewhere. The end-to-end test runs only when ffmpeg and tex3ds are found.

## Layout

```
mod2ds/godot_cfg.py   Godot 4 ConfigFile/Variant text parser (multi-line strings, constructors, StringNames)
mod2ds/discover.py    mod pack / level discovery, same rules as the game
mod2ds/model.py       level IR + the game's normalization rules + runtime limit checks
mod2ds/limits.py      native runtime limits, with source references
mod2ds/names.py       ASCII, length-bounded, stable ids
mod2ds/formats.py     BBCH / BBAN / BBSF / BBVD writers, index.txt rows
mod2ds/media.py       ffmpeg / tex3ds / Pillow transforms (sprites, audio, video, thumbnails)
mod2ds/cutscene.py    pre/post cutscenes (script, sound bank, panel images)
mod2ds/convert.py     per-stage emitters, pack manifests, merged index
mod2ds/verify.py      read-back checks with the runtime loaders' acceptance rules
mod2ds/report.py      REPORT.md / report.json
host/                 runtime chart-loader check (compiles native3ds core/chart_loader.c)
tests/                unit, parity and end-to-end tests
```
