"""Package the Beat Banger 3DS builder: a folder with BB3DS-Builder.exe (Python + Pillow + the port's build tools) and the
programs it runs. It carries no Beat Banger files: the player's own install supplies them (see bb3ds_builder.py).

    python tools/builder/build_builder.py --makerom <makerom.exe> --bannertool <bannertool.exe> --gdre <GDRE_tools dir>
        [--ffmpeg-dir C:/Tools/ffmpeg/bin] [--devkitpro-tools C:/devkitPro/tools/bin] [--out build/builder]

Run `make` first: the payload is beatbanger_3ds.elf with its debug sections stripped (3dsxtool needs the symbol table).
A folder, not one file: the build starts the exe again for each tool step, and a one-file exe would unpack itself every time.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent      # native3ds/
HERE = Path(__file__).resolve().parent
STRIP = Path("C:/devkitPro/devkitARM/bin/arm-none-eabi-strip.exe")
SKIP_TOOLS = {"run_smoke.py", "run_showcase.py", "dump_save.py", "orig_ctl.py", "index_original.py", "inventory.py",
              "analyze_chart.py", "contact_sheet.py", "install_mods.py", "export_public.py"}   # development-only


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--makerom", type=Path, required=True)
    ap.add_argument("--bannertool", type=Path, required=True)
    ap.add_argument("--gdre", type=Path, required=True, help="GDRE Tools folder (gdre_tools.exe, gdre_tools.pck)")
    ap.add_argument("--ffmpeg-dir", type=Path, default=Path("C:/Tools/ffmpeg/bin"))
    ap.add_argument("--devkitpro-tools", type=Path, default=Path("C:/devkitPro/tools/bin"))
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "builder")
    args = ap.parse_args()

    stage = args.out / "stage"
    shutil.rmtree(args.out, ignore_errors=True)
    (stage / "bin").mkdir(parents=True)
    (stage / "payload").mkdir()
    (stage / "tools" / "cia").mkdir(parents=True)

    bins = [args.ffmpeg_dir / "ffmpeg.exe", args.ffmpeg_dir / "ffprobe.exe", args.devkitpro_tools / "tex3ds.exe",
            args.devkitpro_tools / "3dsxtool.exe", args.devkitpro_tools / "smdhtool.exe", args.makerom, args.bannertool]
    bins += [p for p in args.gdre.iterdir() if p.is_file() and p.suffix.lower() in (".exe", ".pck", ".dll")]
    for b in bins:
        if not b.is_file():
            raise SystemExit(f"missing {b}")
        dst = stage / "bin" / b.name.lower() if b in (args.makerom, args.bannertool) else stage / "bin" / b.name
        shutil.copy2(b, dst)

    elf = ROOT / "beatbanger_3ds.elf"
    if not elf.is_file():
        raise SystemExit("run make first (beatbanger_3ds.elf)")
    subprocess.run([str(STRIP), "--strip-debug", "-o", str(stage / "payload" / "beatbanger_3ds.elf"), str(elf)], check=True)
    shutil.copy2(ROOT / "tools" / "cia" / "beatbanger.rsf", stage / "payload" / "beatbanger.rsf")
    # the HOME Menu's 3D banner (the logo badge; its sound still comes from the player's game): made by
    # build_cia.py --pycgfx, which needs a pycgfx checkout the builder does not carry
    cgfx = ROOT / "generated" / "cia" / "banner.cgfx"
    if not cgfx.is_file():
        raise SystemExit("missing generated/cia/banner.cgfx: run tools/build_cia.py --pycgfx <checkout> once")
    shutil.copy2(cgfx, stage / "payload" / "banner.cgfx")
    # the warning screen's B easter egg (build_boot.py --egg; a clip from outside the game, not rebuilt from it): the
    # builder copies payload/romfs over the romfs it makes
    eggs = sorted((ROOT / "romfs" / "boot").glob("egg*"))
    if not any(e.name == "egg.bbvid" for e in eggs):
        raise SystemExit("missing romfs/boot/egg.bbvid: run tools/build_boot.py --egg <clip> --egg-only once")
    (stage / "payload" / "romfs" / "boot").mkdir(parents=True)
    for e in eggs:
        shutil.copy2(e, stage / "payload" / "romfs" / "boot" / e.name)

    sys.path.insert(0, str(ROOT / "tools"))
    from export_public import REWRITES   # the same local-path rewrites as the public source tree
    for t in sorted((ROOT / "tools").glob("*.py")):
        if t.name not in SKIP_TOOLS:
            s = t.read_text(encoding="utf-8")
            for a, b in REWRITES:
                s = s.replace(a, b)
            (stage / "tools" / t.name).write_text(s, encoding="utf-8", newline="\n")
    for t in ("beatbanger.rsf", "blank_logo.bcma.lz"):
        shutil.copy2(ROOT / "tools" / "cia" / t, stage / "tools" / "cia" / t)
    for t in ("needed_files.txt", "pck_includes.txt"):
        shutil.copy2(HERE / t, stage / t)

    sep = ";" if sys.platform == "win32" else ":"
    cmd = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onedir", "--console", "--name", "BB3DS-Builder",
           "--distpath", str(args.out / "dist"), "--workpath", str(args.out / "pyi"), "--specpath", str(args.out),
           "--hidden-import", "PIL.Image", "--collect-submodules", "PIL",
           # the tools are data files PyInstaller does not analyse: the standard modules they import
           *[a for m in ("argparse", "collections", "csv", "ctypes", "ctypes.wintypes", "decimal", "hashlib", "importlib.util",
                         "json", "math", "re", "struct", "tempfile", "types", "typing", "zlib")
             for a in ("--hidden-import", m)],
           "--add-data", f"{stage / 'tools'}{sep}tools", "--add-data", f"{stage / 'payload'}{sep}payload",
           "--add-data", f"{stage / 'needed_files.txt'}{sep}.", "--add-data", f"{stage / 'pck_includes.txt'}{sep}.",
           "--add-binary", f"{stage / 'bin'}{sep}bin", str(HERE / "bb3ds_builder.py")]
    subprocess.run(cmd, check=True)
    out = args.out / "dist" / "BB3DS-Builder"
    size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"\nBuilt {out} ({size / 1e6:.0f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
