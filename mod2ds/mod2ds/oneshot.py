"""One mod in, one 3DS package out (MOD2DS.exe, or `python -m mod2ds oneshot <mod> [--preset standard|larger]`).

    input    a mod pack folder, a bbmods .zip, or a mods folder (then every pack inside goes into the one package)
    output   <out>/<mod name> (3DS)/3ds/bb_mods/stages/<id>/... : copy the "3ds" folder onto the SD card's root
    presets  standard - the defaults every mod gets (videos switch to half resolution past 20 s of video)
             larger   - stronger downscaling for heavy mods (lots of video or animation): smaller character frames,
                        half-resolution video always, 24 fps video

The package has no stages/index.txt: every stage folder carries its own index_row.txt, which the game reads, so copying
a package onto a card never overwrites the list of mods already on it. Only levels the game can run are packaged
(the same rule as `install`); the rest are listed with the reason.
"""
from __future__ import annotations

import os
import re
import shutil
import sys
from pathlib import Path

from . import convert, install, paths

DEFAULT_OUT = paths.output_dir()

# frame_width: character frame cell width in texels (drawn at the same screen size either way; 384 is ~56 % of the texels)
PRESETS = {
    "standard": {"frame_width": 512, "video_res": "auto", "video_fps": 30.0},
    "larger": {"frame_width": 384, "video_res": "half", "video_fps": 24.0},
}
PRESET_TEXT = {
    "standard": "Standard - for most mods",
    "larger": "Larger downscaling - for heavy mods with lots of video or animation: softer picture, lighter on the 3DS",
}

README = """Beat Banger 3DS mod package ({preset})

Copy the "3ds" folder in this folder onto the root of your 3DS SD card (merge it with the 3ds folder that is already
there). The levels then appear in the game under MODS > Library.

Nothing on the card is overwritten except these levels' own folders, so packages can be added one after another.
"""


def package_name(item: Path) -> str:
    name = item.stem if item.suffix.lower() == ".zip" else item.name
    name = re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", name).strip(" .") or "mod"
    return name


def ask_preset() -> str:
    print("How much should this mod be downscaled?")
    print(f"  1. {PRESET_TEXT['standard']}")
    print(f"  2. {PRESET_TEXT['larger']}")
    while True:
        ans = input("Type 1 or 2 and press Enter (Enter alone = 1): ").strip()
        if ans in ("", "1"):
            return "standard"
        if ans == "2":
            return "larger"


def run(inputs: list[Path], *, preset: str, out_root: Path, options: convert.Options, log=print) -> int:
    for k, v in PRESETS[preset].items():
        setattr(options, k, v)
    options.fit_runtime = True
    failed = 0
    made = []
    for item in inputs:
        if not item.exists():
            log(f"! {item}: not found")
            failed += 1
            continue
        pkg = out_root / (package_name(item) + (" (3DS)" if preset == "standard" else " (3DS, larger)"))
        shutil.rmtree(pkg, ignore_errors=True)
        target = pkg / "3ds" / "bb_mods"
        (target / "stages").mkdir(parents=True)
        log(f"\n### {item.name}  ->  {pkg}   [{PRESET_TEXT[preset].split(' - ')[0]}]")
        rc = install.run([item], sd=target, options=options, log=log, package=True)
        for extra in (target / "stages" / "index.txt", target / "stages" / install.ORDER_FILE):
            extra.unlink(missing_ok=True)   # the stage folders' index_row.txt carry the rows (see the module doc)
        if rc != 0 or not any(p.is_dir() for p in (target / "stages").iterdir()):
            shutil.rmtree(pkg, ignore_errors=True)
            log(f"No package made for {item.name}: nothing in it can run on the 3DS.")
            failed += 1
            continue
        (pkg / "READ ME.txt").write_text(README.format(preset=PRESET_TEXT[preset].split(" - ")[0]), encoding="utf-8")
        made.append(pkg)
    log("\n" + "=" * 70)
    for pkg in made:
        log(f"Done: {pkg}\n  Copy its \"3ds\" folder onto the root of the SD card.")
    if made and os.name == "nt" and sys.stdin and sys.stdin.isatty():
        try:
            os.startfile(made[-1])   # open the result in Explorer
        except OSError:
            pass
    return 0 if made and not failed else 1
