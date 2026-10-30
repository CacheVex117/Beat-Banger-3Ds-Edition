# Beat Banger for Nintendo 3DS

**18+ only. Beat Banger is an adult game with explicit content.**

Beat Banger, the rhythm game by Bunfan Studios, running natively on a homebrewed **Nintendo 3DS**. This is not an emulator
or a stream: the game was rebuilt in C for the 3DS, with the character scene on the top screen and the four-lane note
highway on the bottom screen.

Play the story volumes, unlock things in the shop, and bring your PC mods along with the included converter. The port
aims to keep the original's timing, scoring and progression while adapting the presentation to two small screens.
**It is a fan project: not made, endorsed or supported by Bunfan Studios. If you enjoy it, support the original game.**

> **This repository contains no Beat Banger files.** You build the 3DS game yourself from your own copy of the PC game
> (Steam, version 26w40) with a one-click Windows builder.

## Features

- **Volume 1, Volume 2** (In The Closet, In The Dorms) and **After Class**, each on Virgin, Normal and Expert.
- The **character art and climax videos** on the top screen; the **note highway, score, combo and hearts** on the bottom.
- **Pre- and post-level cutscenes**, the **shop**, power-ups, ranks, achievements, the gallery, the collection, the theater,
  the sound test, the scene viewer and the **Halloween theme**.
- **Controls your way:** face buttons, D-Pad, both at once, the touch screen, or the shoulder buttons (New models).
- **SD-card mods** converted from the PC game with **MOD2DS**, including old pre-release **Legacy** mods, which play sideways
  with the 3DS held like a book.
- **HOME Menu launch through an installable CIA**, with an icon and banner made from your copy. Homebrew Launcher is also
  supported.
- Progress saves to the SD card, with a backup copy.

## What to expect

| Console | Status |
|---|---|
| New 2DS XL | Developed and played on one |
| New 3DS / New 3DS XL | Supported; same hardware family as the New 2DS XL |
| Original 3DS, 3DS XL, 2DS | Supported in code and tested in the Azahar emulator only; not yet tried on real hardware |

Large mods with lots of animation or video load more on the fly; MOD2DS has a **Larger** preset that downscales harder for
them. Hardware reports, especially from original 3DS owners, are welcome.

## What you need

- **Beat Banger for PC** (Steam, version 26w40) installed on a **Windows** PC, and about **2 GB** of free space there.
- A homebrewed **3DS or 2DS** with custom firmware (Luma3DS) and **FBI** for CIA installation.
- About **450 MB** of free SD space for the game (plus the same again while installing the CIA), and room for mods.

## Build it the easy way (Windows)

1. Download the release zip from the [latest release](https://github.com/CacheVex117/Beat-Banger-3Ds-Edition/releases/latest) and extract it.
2. Double-click **`3DS Builder (PC)\BB3DS-Builder.exe`**. It finds a normal Steam install by itself; otherwise drag your
   Beat Banger folder (the one with `beatbanger.exe`) into its window and press Enter.
3. Wait about **10 minutes**. `BeatBanger.cia` and `BeatBanger.3dsx` appear in the `output` folder next to the builder.

The builder brings its own tools, so there is nothing to install and no internet connection is needed. It copies only the
files the port needs out of your game folder (your game is only read, never changed) and converts them; the list is in
[`native3ds/tools/builder/needed_files.txt`](native3ds/tools/builder/needed_files.txt). To build from source instead,
see [Building from source](#building-from-source).

## Install with FBI

1. **Sound (once):** if you have never done it, run **DSP1** from the Homebrew Launcher. It creates
   `SD:/3ds/dspfirm.cdc`; without it the game runs silent.
2. Copy **`BeatBanger.cia`** to the SD card.
3. Open **FBI → SD → BeatBanger.cia → Install CIA**, and confirm. You can delete the `.cia` file afterwards.
4. Launch **Beat Banger** from the HOME Menu.

For the Homebrew Launcher instead, copy **`BeatBanger.3dsx`** to `SD:/3ds/` and start it from there. Both use the same save
in `SD:/3ds/beatbanger/`. If the HOME Menu icon or banner looks wrong after installing a new build, delete the old title in
FBI first, then install again.

## Playing

Levels unlock in order, as in the original. After Class opens once you read the Dorm Key in **Extras → Collection**.
**Settings → Controls** picks the control scheme; **Settings → Gameplay** has the calibration menu if notes feel early or late.

| Input | Action |
|---|---|
| B / A / Y / X | Play the four lanes (Buttons scheme) |
| D-Pad Down / Right / Left / Up | Play the four lanes (D-Pad scheme; Hybrid accepts both) |
| L / ZL / ZR / R | Play the Y / X / B / A lanes (Shoulders scheme, New models) |
| Touch the lanes | Play notes on the touch screen |
| START | Pause |
| START + SELECT | Quit the game |

**Legacy mods** play sideways: hold the 3DS like a book, touch screen on the right. The art stands upright and the buttons
turn with the console (the lanes are played by Y / B / X / A, and the D-Pad likewise). **Modifiers → LEGACY LAYOUT** turns it
off before you start the level.

## Mods (MOD2DS)

**MOD2DS** turns Beat Banger PC mods into 3DS mods. In the release zip it is **`Mod Converter (PC)\MOD2DS.exe`**:

1. Drag a mod folder or `.zip` onto `MOD2DS.exe` (PC mods are usually in `%APPDATA%\Godot\app_userdata\Beat Banger\mods`).
2. Choose **Standard** (most mods) or **Larger** (heavy mods with lots of video or animation: a softer picture, much
   less for the 3DS to load).
3. Copy the `3ds` folder from the result in `output\` onto the root of your SD card, merging folders.
4. On the 3DS: **Mods → Library**.

The 3DS lists up to **56 mod levels**; the Mods screen shows how full the list is (Good, Okay, Near Full, Full). Mod
cutscenes and end-of-level videos are converted. Moving background layers, effect overlays and a few rare extras are left
out; the level itself plays in full. Details: [`mod2ds/README.md`](mod2ds/README.md).

## Building from source

- **The 3DS app:** devkitPro with devkitARM, libctru and citro2d; then `make` in `native3ds/`.
- **The builder package:** Python 3 with Pillow and PyInstaller, plus makerom, bannertool, GDRE Tools and ffmpeg:
  `python tools/builder/build_builder.py --makerom <makerom.exe> --bannertool <bannertool.exe> --gdre <GDRE folder>`
  (from `native3ds/`), then run `build/builder/dist/BB3DS-Builder/BB3DS-Builder.exe`.
- **MOD2DS:** `python -m mod2ds oneshot <mod>` from `mod2ds/`, or `python launcher/build_portable.py` for the exe.
- **Tests:** `python -m unittest discover -s tests -p "test_*.py"` in `native3ds/` (tests that need the game skip without
  it) and in `mod2ds/`.

## Reporting problems

Bug reports need facts: the console model, what you did, what happened, and **`SD:/bb3ds_evidence/telemetry.log`**, copied
right after the problem. For builder problems, include the text from the builder window. Holding **L + R** and pressing
**Y** shows a live diagnostics overlay.

## Scope and credits

This is an unofficial homebrew port. The warning screen's easter-egg clip is not included. Some presentation is simplified
for the 3DS, and some mod features are not converted (see above). Bugs may remain.

Thanks to **Bunfan Studios** for Beat Banger, **devkitPro** (libctru, citro2d, citro3d, tex3ds, 3dstools), **GDRE Tools**,
**makerom**, **bannertool** and **FFmpeg**, and to the 3DS homebrew projects that showed the way.

The code in this repository is under the [MIT licence](LICENSE). It covers this repository's code only: Beat Banger, its
characters, art, music and sounds belong to Bunfan Studios and are not part of it. Third-party tools bundled with the
builder keep their own licences (listed in the release's `THIRD-PARTY-NOTICES.txt`).
