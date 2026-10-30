"""Build the portable MOD2DS.exe: one file with Python, Pillow, MOD2DS, ffmpeg, ffprobe and tex3ds. Nothing else needs to be
installed on the PC that runs it, and it carries no Beat Banger files: the game's shared clips some mods use are read from the
player's own install (data/_common/audio), and the 3DS app plays its miss / damage / glass sounds from its own build.

    python launcher/build_portable.py [--ffmpeg-dir C:/Tools/ffmpeg/bin] [--tex3ds C:/devkitPro/tools/bin/tex3ds.exe]

Needs PyInstaller (pip install pyinstaller). Writes MOD2DS.exe into the repository root; build files go to build/portable.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ffmpeg-dir", type=Path, default=Path("C:/Tools/ffmpeg/bin"))
    ap.add_argument("--tex3ds", type=Path, default=Path("C:/devkitPro/tools/bin/tex3ds.exe"))
    args = ap.parse_args()

    tools = [args.ffmpeg_dir / "ffmpeg.exe", args.ffmpeg_dir / "ffprobe.exe", args.tex3ds]
    for t in tools:
        if not t.is_file():
            raise SystemExit(f"missing {t}")
    work = REPO / "build" / "portable"
    shutil.rmtree(work, ignore_errors=True)

    sep = ";" if sys.platform == "win32" else ":"
    cmd = [sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean", "--onefile", "--console", "--name", "MOD2DS",
           "--distpath", str(REPO), "--workpath", str(work / "pyi"), "--specpath", str(work),
           "--paths", str(REPO), "--collect-submodules", "mod2ds"]
    for t in tools:
        cmd += ["--add-binary", f"{t}{sep}tools"]
    cmd.append(str(REPO / "launcher" / "portable_main.py"))
    subprocess.run(cmd, check=True)
    exe = REPO / "MOD2DS.exe"
    print(f"\nBuilt {exe} ({exe.stat().st_size / 1e6:.0f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
