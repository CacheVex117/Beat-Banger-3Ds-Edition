"""Package the current build as an installable CIA (HOME Menu title) for custom firmware.

    python tools/build_cia.py --makerom <makerom.exe> --bannertool <bannertool.exe> [--pycgfx <dir>] [--logo <png>] [--out BeatBanger.cia]

Run `make` first (the CIA uses beatbanger_3ds.elf, its SMDH icon and the romfs/ folder, the same as the .3dsx).
Banner: with --pycgfx, the 3D logo badge from tools/cia/make_banner3d.py (the HOME Menu turns it; it pulses at The Boss's 128 BPM);
without it, the logo flat on a 256x128 card. Sound: 3 s of In The Dorms' level-select preview (Cathy, Volume 2: romfs/music/pv_cathy_e2.wav) faded in
over 0.5 s and out over 0.8 s. Title ID 000400000BB3D500 (tools/cia/beatbanger.rsf). The HOME Menu keeps a title's banner, so delete
the installed title in FBI before installing a CIA with a new banner.
The tools are not part of devkitPro: makerom (3DSGuy/Project_CTR) and bannertool (Epicpkmn11/bannertool) GitHub releases, and a
pycgfx checkout (skyfloogle/pycgfx).
"""
from __future__ import annotations

import os
import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(os.environ.get("BB3DS_ROOT") or Path(__file__).resolve().parent.parent)   # the builder exe sets BB3DS_ROOT


BUILTIN_LOGOS = {"nintendo": "Nintendo", "licensed": "Licensed", "distributed": "Distributed", "homebrew": "Homebrew"}


def logo_args(choice: str) -> list[str]:
    """makerom arguments for --boot-logo (the RSF reads Logo from $(APP_LOGO))."""
    if choice.lower() in BUILTIN_LOGOS:
        return [f"-DAPP_LOGO={BUILTIN_LOGOS[choice.lower()]}"]
    path = ROOT / "tools" / "cia" / "blank_logo.bcma.lz" if choice.lower() == "blank" else Path(choice)
    if not path.is_file():
        raise SystemExit(f"--boot-logo: {path} not found")
    return ["-DAPP_LOGO=Homebrew", "-logo", str(path)]   # the file replaces the built-in logo


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--makerom", type=Path, required=True)
    ap.add_argument("--bannertool", type=Path, required=True)
    ap.add_argument("--pycgfx", type=Path, help="pycgfx checkout: build the 3D banner (default: the flat one); the one-card banner "
                    "works on a New 2DS XL, try any change to it with --test first (SPRINT_HANDOFF 3AB)")
    ap.add_argument("--cgfx", type=Path, help="a ready 3D banner model (banner.cgfx from a --pycgfx build) instead of --pycgfx")
    ap.add_argument("--logo", type=Path, default=ROOT / "generated" / "cia" / "logo.png", help="the banner logo (RGBA PNG)")
    ap.add_argument("--clip", type=Path, default=ROOT / "romfs" / "music" / "pv_cathy_e2.wav",
                    help="banner sound source (In The Dorms level-select preview, Volume 2)")
    ap.add_argument("--boot-logo", default="licensed",
                    help="the logo shown while the game starts: licensed ('Licensed by Nintendo', the default), "
                         "blank (the Homebrew logo with its pictures blacked out, "
                         "tools/cia/blank_logo.bcma.lz), one of makerom's built-in logos (nintendo, licensed = 'Licensed by Nintendo', "
                         "distributed = 'Distributed by Nintendo', homebrew), or a logo file. A CIA with no logo at all does not start "
                         "(HOME Menu: 'SD card was removed')")
    ap.add_argument("--ffmpeg", default="C:/Tools/ffmpeg/bin/ffmpeg.exe")
    ap.add_argument("--out", type=Path, default=ROOT / "BeatBanger.cia")
    ap.add_argument("--test", action="store_true", help="a separate title (000400000BB3D600) next to the real one, "
                    "for trying a banner without touching the real install")
    args = ap.parse_args()
    work = ROOT / "generated" / "cia"
    work.mkdir(parents=True, exist_ok=True)
    rsf = ROOT / "tools" / "cia" / "beatbanger.rsf"
    if args.test:
        rsf = work / "test.rsf"
        rsf.write_text((ROOT / "tools" / "cia" / "beatbanger.rsf").read_text().replace("CTR-H-BBNG", "CTR-H-BBNT")
                       .replace("0xBB3D5 ", "0xBB3D6 "))
    # HOME Menu text (the Makefile's APP_TITLE / APP_DESCRIPTION / APP_AUTHOR): SMDH = 16 language entries of 0x200 bytes from 0x8,
    # each short title 0x80, long title 0x100, publisher 0x80 (UTF-16LE)
    data = bytearray((ROOT / "beatbanger_3ds.smdh").read_bytes())
    for lang in range(16):
        o = 8 + lang * 0x200
        for off, size, text in ((0, 0x80, "Beat Banger"), (0x80, 0x100, "Beat Banger 3DS Edition"), (0x180, 0x80, "CacheVex117")):
            data[o + off:o + off + size] = text.encode("utf-16-le").ljust(size, b"\0")
    smdh = work / "icon.smdh"
    smdh.write_bytes(data)

    src = Image.open(args.logo).convert("RGBA")
    W, H = 256, 128
    card = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    k = min((W - 16) / src.width, (H - 8) / src.height)
    logo = src.resize((round(src.width * k), round(src.height * k)), Image.LANCZOS)
    card.alpha_composite(logo, ((W - logo.width) // 2, (H - logo.height) // 2))
    card.save(work / "banner.png")
    subprocess.run([args.ffmpeg, "-v", "error", "-y", "-i", str(args.clip), "-t", "3.0",
                    "-af", "afade=t=in:st=0:d=0.5,afade=t=out:st=2.2:d=0.8", "-ar", "32000", "-ac", "2", "-c:a", "pcm_s16le",
                    str(work / "banner.wav")], check=True)
    image = ["-i", str(work / "banner.png")]
    if args.cgfx:   # a 3D banner made earlier with --pycgfx (the builder ships one: it carries only the logo)
        image = ["-ci", str(args.cgfx)]
    elif args.pycgfx:
        subprocess.run([sys.executable, str(ROOT / "tools" / "cia" / "make_banner3d.py"), "--pycgfx", str(args.pycgfx),
                        "--logo", str(args.logo), "--out", str(work / "banner.cgfx")], check=True)
        image = ["-ci", str(work / "banner.cgfx")]
    subprocess.run([str(args.bannertool), "makebanner", *image, "-a", str(work / "banner.wav"),
                    "-o", str(work / "banner.bnr")], check=True)
    subprocess.run([str(args.makerom), "-f", "cia", "-o", str(args.out), "-target", "t",
                    "-elf", str(ROOT / "beatbanger_3ds.elf"), "-rsf", str(rsf),
                    "-icon", str(smdh), "-banner", str(work / "banner.bnr"), *logo_args(args.boot_logo),
                    f"-DAPP_ROMFS={ROOT / 'romfs'}"], check=True)
    print(f"[OK] {args.out} ({args.out.stat().st_size / 1048576:.1f} MiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
