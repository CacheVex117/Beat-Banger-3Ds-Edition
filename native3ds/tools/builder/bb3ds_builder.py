"""Beat Banger 3DS builder: makes BeatBanger.3dsx and BeatBanger.cia from the player's own Beat Banger install.

The download carries no game files. Only the files the 3DS port needs (builder/needed_files.txt) are taken from the game:
the level folders' files straight from <game>/data, everything else unpacked from beatbanger.exe with GDRE Tools
(--recover limited to those files). The port's own build tools then convert them exactly as for the development build.

    BB3DS-Builder.exe [<Beat Banger folder>]      (or drag the folder onto the exe)

Inside the exe the same entry point also runs one build tool:  BB3DS-Builder.exe <tool>.py [args]  (build_stage.py starts its
sub-steps that way, with sys.executable).
"""
from __future__ import annotations

import hashlib
import os
import runpy
import shutil
import subprocess
import sys
import time
from pathlib import Path

FROZEN = getattr(sys, "frozen", False)
BASE = Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parent))   # bundled files (dev: tools/builder)
TOOLS = BASE / "tools" if FROZEN else BASE.parent                          # the port's build tools
BIN = BASE / "bin"                                                         # gdre, ffmpeg, tex3ds, 3dsxtool, smdhtool, makerom, bannertool
PAYLOAD = BASE / "payload"                                                 # beatbanger_3ds.elf (stripped), port-own romfs files
HOME = Path(sys.executable).resolve().parent if FROZEN else Path.cwd()     # output and work folders go next to the exe

TITLE, DESCRIPTION, AUTHOR = "Beat Banger", "Beat Banger 3DS Edition", "CacheVex117"
STAGES = (("bb_vol_1", "cathy_e1 zoe_e1 gale_e1 claire_e1 dawna_e1"), ("bb_vol_2", "zoe_e2 cathy_e2"), ("bb_extras", "makayla"))


def run_tool(script: str, args: list[str]) -> None:
    """Run one build tool in this process, as `python <script> args` would."""
    path = Path(script)
    if not path.is_file():
        path = TOOLS / Path(script).name
    sys.argv = [str(path), *args]
    sys.path.insert(0, str(path.parent))
    runpy.run_path(str(path), run_name="__main__")


def exe(name: str) -> str:
    p = BIN / f"{name}.exe"
    if not p.is_file():
        raise SystemExit(f"missing bundled tool: {p}")
    return str(p)


def say(msg: str) -> None:
    print(msg, flush=True)


def pause() -> None:
    """Keep a double-clicked console window open at the end."""
    if FROZEN and sys.stdin and sys.stdin.isatty():
        try:
            input("\nPress Enter to close this window.")
        except EOFError:
            pass


def find_game(arg: str | None) -> Path:
    candidates = [Path(arg)] if arg else []
    if not candidates:
        for root in (os.environ.get("ProgramFiles(x86)", ""), os.environ.get("ProgramFiles", "")):
            if root:
                candidates.append(Path(root) / "Steam" / "steamapps" / "common" / "Beat Banger")
    for c in candidates:
        c = c.expanduser()
        if c.is_file():
            c = c.parent
        if (c / "beatbanger.exe").is_file() and (c / "data").is_dir():
            return c
    while True:
        say("\nDrag your Beat Banger folder (the one with beatbanger.exe) into this window and press Enter:")
        raw = input("> ").strip().strip('"')
        c = Path(raw)
        if c.is_file():
            c = c.parent
        if (c / "beatbanger.exe").is_file() and (c / "data").is_dir():
            return c
        say("That folder has no beatbanger.exe and data folder. Try again.")


def needed() -> tuple[list[str], list[str]]:
    """(res:// files unpacked from the game exe, data/ files copied as they are)."""
    res, data = [], []
    for line in (BASE / "needed_files.txt").read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        (data if line.startswith("data/") else res).append(line)
    return res, data


def unpack(game: Path, work: Path) -> Path:
    """The needed files only: data/ copied, the rest recovered from beatbanger.exe by GDRE (--include per file)."""
    src = work / "game"
    sig = hashlib.sha1((BASE / "needed_files.txt").read_bytes() + (BASE / "pck_includes.txt").read_bytes()
                       + str(game.resolve()).encode() + str((game / "beatbanger.exe").stat().st_size).encode()).hexdigest()
    done = src / ".complete"
    if done.is_file() and done.read_text(encoding="utf-8") == sig:
        say("Game files already unpacked (delete the work folder to redo it).")
        return src
    shutil.rmtree(src, ignore_errors=True)
    res, data = needed()
    say(f"Copying {len(data)} level files from {game / 'data'} ...")
    missing = 0
    for rel in data:
        s = game / rel
        if not s.is_file():
            missing += 1
            continue
        d = src / rel
        d.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(s, d)
    if missing:
        say(f"  {missing} expected file(s) not found: this Beat Banger version may not match the builder (made for 26w40).")
    say(f"Unpacking {len(res)} files from beatbanger.exe with GDRE Tools ...")
    gd = work / "gdre"
    shutil.rmtree(gd, ignore_errors=True)
    # one line per needed file: its pack entries (pointer .import / .remap + the content it points at, or a script's .gdc),
    # which must be recovered in the same call
    groups = [l.split("\t") for l in (BASE / "pck_includes.txt").read_text(encoding="utf-8").splitlines() if l.strip()]
    batch = 60    # groups per call: keeps the command line well under Windows' 32k limit
    for i in range(0, len(groups), batch):
        cmd = [exe("gdre_tools"), "--headless", f"--recover={game / 'beatbanger.exe'}", f"--output={gd}"]
        cmd += [f"--include=res://{e}" for g_ in groups[i:i + batch] for e in g_]
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        (work / "gdre.log").open("a", encoding="utf-8").write(p.stdout)
        if p.returncode != 0:
            raise SystemExit(f"GDRE Tools failed (exit {p.returncode}); see {work / 'gdre.log'}")
    got = 0
    for r in res:
        s = gd / r
        if s.is_file():
            d = src / r
            d.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(s), str(d))
            got += 1
    if got < len(res):
        say(f"  {len(res) - got} file(s) were not recovered; the build may stop or miss some art.")
    shutil.rmtree(gd, ignore_errors=True)
    done.write_text(sig, encoding="utf-8")
    return src


def build(game_src: Path, work: Path, out: Path) -> None:
    root = work / "root"                      # the layout build_cia.py expects (BB3DS_ROOT)
    romfs, gen = root / "romfs", work / "gen"
    shutil.rmtree(romfs, ignore_errors=True)
    romfs.mkdir(parents=True)
    os.environ["BB_TEX3DS"] = exe("tex3ds")
    os.environ["BB3DS_ROOT"] = str(root)
    ff, fp = exe("ffmpeg"), exe("ffprobe")
    g = str(game_src)
    steps = [
        ("convert_ui.py", ["--out-dir", gen / "ui", "--romfs-dir", romfs]),
        ("build_font.py", ["--game", g, "--romfs", romfs, "--gen", gen / "font"]),
        ("build_menu_art.py", ["--game", g, "--romfs", romfs, "--gen", gen / "menuart", "--ffmpeg", ff]),
        ("build_extras.py", ["--game", g, "--romfs", romfs, "--gen", gen / "extras"]),
        ("build_extras_menu.py", ["--game", g, "--romfs", romfs, "--work", gen / "xm"]),
        ("build_collection.py", ["--game", g, "--romfs", romfs, "--gen", gen / "collection"]),
        ("build_gallery.py", ["--game", g, "--romfs", romfs, "--gen", gen / "gallery"]),
        ("build_shop_icons.py", ["--game", g, "--romfs", romfs, "--work", gen / "shop"]),
        ("build_viewers.py", ["--game", g, "--romfs", romfs, "--gen", gen / "viewers", "--ffmpeg", ff]),
        ("build_boot.py", ["--game", g, "--romfs", romfs, "--work", gen / "boot", "--ffmpeg", ff, "--ffprobe", fp]),
    ]
    for act, ids in STAGES:
        steps.append(("build_stage.py", ["--game", g, "--act", act, "--stages", *ids.split(), "--romfs", romfs,
                                         "--gen", gen / "stages", "--ffmpeg", ff, "--ffprobe", fp]))
    steps += [
        ("build_theme.py", ["--game", g, "--romfs", romfs, "--gen", gen / "theme", "--ffmpeg", ff]),   # game_over_h per stage
        ("convert_sfx.py", ["--common-bank", romfs / "audio" / "common.bbsfx", "--common-dir", Path(g) / "assets" / "audio",
                            "--ffmpeg", ff]),   # miss / damage / glass for banks without them (SD-card mods)
        ("build_ui_assets.py", ["--game", g, "--romfs", romfs, "--gen", gen / "uiart", "--ffmpeg", ff]),   # needs stages/index.txt
        ("build_previews.py", ["--romfs", romfs, "--ffmpeg", ff]),
        ("build_icon.py", ["--src", Path(g) / "icon.png", "--out", gen / "icon.png"]),
    ]
    for i, (tool, args) in enumerate(steps, 1):
        say(f"\n[{i}/{len(steps)}] {tool}")
        t0 = time.time()
        p = subprocess.run([sys.executable, *([] if FROZEN else [__file__]), str(TOOLS / tool), *map(str, args)])
        if p.returncode != 0:
            raise SystemExit(f"{tool} failed (exit {p.returncode})")
        say(f"    done in {time.time() - t0:.0f} s")

    own = PAYLOAD / "romfs"                   # the port's own files (not from the game)
    if own.is_dir():
        shutil.copytree(own, romfs, dirs_exist_ok=True)

    elf = root / "beatbanger_3ds.elf"
    shutil.copy2(PAYLOAD / "beatbanger_3ds.elf", elf)
    smdh = root / "beatbanger_3ds.smdh"
    subprocess.run([exe("smdhtool"), "--create", TITLE, DESCRIPTION, AUTHOR, str(gen / "icon.png"), str(smdh)], check=True)
    out.mkdir(parents=True, exist_ok=True)
    say("\nWriting BeatBanger.3dsx ...")
    subprocess.run([exe("3dsxtool"), str(elf), str(out / "BeatBanger.3dsx"), f"--smdh={smdh}", f"--romfs={romfs}"], check=True)
    say("Writing BeatBanger.cia ...")
    cia_dir = root / "tools" / "cia"
    cia_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(PAYLOAD / "beatbanger.rsf", cia_dir / "beatbanger.rsf")
    p = subprocess.run([sys.executable, *([] if FROZEN else [__file__]), str(TOOLS / "build_cia.py"),
                        "--makerom", exe("makerom"), "--bannertool", exe("bannertool"), "--ffmpeg", ff,
                        "--logo", str(gen / "uiart" / "logo.png"), "--clip", str(romfs / "music" / "pv_cathy_e2.wav"),
                        "--out", str(out / "BeatBanger.cia")])
    if p.returncode != 0:
        raise SystemExit("build_cia.py failed")


def main() -> int:
    if len(sys.argv) > 1 and sys.argv[1].lower().endswith(".py"):
        run_tool(sys.argv[1], sys.argv[2:])
        return 0
    say("BEAT BANGER 3DS BUILDER\nMakes BeatBanger.3dsx and BeatBanger.cia from your own copy of Beat Banger (Steam, version 26w40).")
    game = find_game(sys.argv[1] if len(sys.argv) > 1 else None)
    say(f"Game: {game}")
    work, out = HOME / "BB3DS build work", HOME / "output"
    work.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    try:
        src = unpack(game, work)
        build(src, work, out)
    except SystemExit as exc:
        say(f"\nBUILD FAILED: {exc}")
        pause()
        return 1
    say(f"\nDone in {(time.time() - t0) / 60:.0f} min:\n  {out / 'BeatBanger.cia'}\n  {out / 'BeatBanger.3dsx'}\n"
        f"You can delete the \"{work.name}\" folder now (keep it to rebuild faster).")
    pause()
    return 0


if __name__ == "__main__":
    sys.exit(main())
