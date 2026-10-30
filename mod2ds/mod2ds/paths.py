"""Where MOD2DS finds its tools and keeps its files, from source or as the portable MOD2DS.exe (PyInstaller).

From source: everything is relative to this repository (library/, output/), and tools come from PATH or their usual
install folders. Portable exe: ffmpeg, ffprobe, tex3ds and the few shared Beat Banger sounds MOD2DS needs are inside the
exe (unpacked to a temporary folder while it runs); packages and the conversion cache go next to the exe.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

FROZEN = bool(getattr(sys, "frozen", False))
REPO = Path(__file__).resolve().parent.parent


def app_dir() -> Path:
    """The folder the user sees: the exe's folder, or the repository."""
    return Path(sys.executable).resolve().parent if FROZEN else REPO


def bundle_dir() -> Path | None:
    """Files packed into the portable exe (None when running from source)."""
    return Path(getattr(sys, "_MEIPASS")) if FROZEN and hasattr(sys, "_MEIPASS") else None


def output_dir() -> Path:
    return app_dir() / "output"


def library_dir() -> Path:
    return app_dir() / ("MOD2DS cache" if FROZEN else "library")


def bundled_tool(name: str) -> str | None:
    b = bundle_dir()
    if b is None:
        return None
    p = b / "tools" / (name + ".exe")
    return str(p) if p.is_file() else None


def steam_game_dirs() -> list[Path]:
    """Where Steam installs Beat Banger by default. Conversion reads only <game>/data/_common/audio from it (the game's
    shared clips some mods use); the download carries no game files."""
    out = []
    for env in ("ProgramFiles(x86)", "ProgramFiles"):
        root = os.environ.get(env)
        if root:
            out.append(Path(root) / "Steam" / "steamapps" / "common" / "Beat Banger")
    return out
