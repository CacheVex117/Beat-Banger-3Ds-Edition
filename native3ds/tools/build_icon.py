"""HOME Menu icon: the original game's icon.png (256x256 "BB") downscaled to the 48x48 SMDH icon.

Writes generated/icon.png (git-ignored); the Makefile uses it when present (APP_ICON), otherwise libctru's default icon.

    python tools/build_icon.py [--src "C:/Beat Banger/icon.png"] [--out generated/icon.png]
"""
import argparse
from pathlib import Path

from PIL import Image

DEFAULT_SRC = Path(r"C:/Beat Banger/icon.png")


def build_icon(src: Path, out: Path) -> Image.Image:
    im = Image.open(src).convert("RGBA")
    icon = im.resize((48, 48), Image.LANCZOS)
    out.parent.mkdir(parents=True, exist_ok=True)
    icon.save(out)
    return icon


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--src", type=Path, default=DEFAULT_SRC)
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parent.parent / "generated" / "icon.png")
    args = ap.parse_args()
    build_icon(args.src, args.out)
    print("[OK] icon:", args.out)


if __name__ == "__main__":
    main()
