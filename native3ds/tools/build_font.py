#!/usr/bin/env python3
"""Rasterise the original game's UI fonts into one glyph atlas for the 3DS menus (romfs/ui/font.t3x + font.txt).

Faces (reference assets/font): body = Encode Sans SC Condensed Regular, bold = Encode Sans SC Condensed Bold,
head = Anton (titles, character names, extras banner), hand = Delicious Handrawn (achievements header). Every face is rendered at the exact pixel sizes the menus use, so text is
drawn 1:1 (no scaling blur). ASCII 32..126 only; the menus' text is ASCII.

font.txt:  F|face|size|ascent|line_height          then   G|face|size|char_code|x|y|w|h|xoff|yoff|advance
Atlas is 8-bit alpha (a8) 1024x1024; the runtime tints it with a solid colour.
"""
import os
import argparse
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

TEX3DS = Path(os.environ.get("BB_TEX3DS", "C:/devkitPro/tools/bin/tex3ds.exe"))   # the builder exe sets BB_TEX3DS
FACES = {
    "body": ("assets/font/encode_sans/EncodeSansSC_Condensed-Regular.ttf", (10, 12, 14, 17, 21, 26)),
    "bold": ("assets/font/encode_sans/EncodeSansSC_Condensed-Bold.ttf", (10, 12, 14, 17, 21, 26, 32)),
    "head": ("assets/font/anton/Anton-Regular.ttf", (20, 28, 40, 56)),
    "hand": ("assets/font/delicious/DeliciousHandrawn-Regular.ttf", (14, 17, 21, 26)),   # achievements title, handwritten accents
    "notob": ("assets/font/noto/NotoSansJP-Black.ttf", (14, 17)),     # cutscene name plate (LabelSettings_6ypsg)
    "notor": ("assets/font/noto/NotoSansJP-Regular.ttf", (12, 14)),   # cutscene dialogue (RichTextLabel normal font)
}
ATLAS = 1024
# Gameplay HUD (NoteDetector AccuracyLabel / ComboLabel / PointsLabel: Noto Sans JP Black with a black outline 7 / 4 at 36 / 28 px):
# rendered with the outline baked in (white fill, black stroke) into a second RGBA atlas that the game tints multiplicatively, so the
# fill takes the text colour and the outline stays black - one quad per glyph. Only the characters the HUD prints are rasterised.
OL_FACES = {"noto": ("assets/font/noto/NotoSansJP-Black.ttf", ((14, 2), (17, 2), (21, 3)))}
OL_CHARS = set("0123456789+-x.?!% ABCDEFGHIJKLMNOPQRSTUVWXYZ")
OL_ATLAS = (512, 256)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", type=Path, default=Path("C:/Beat Banger"))
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--gen", type=Path, default=Path("generated/font"))
    args = ap.parse_args()
    args.gen.mkdir(parents=True, exist_ok=True)
    out = args.romfs / "ui"
    out.mkdir(parents=True, exist_ok=True)

    atlas = Image.new("L", (ATLAS, ATLAS), 0)
    lines = []
    x = y = row_h = 0
    for face, (path, sizes) in FACES.items():
        for size in sizes:
            font = ImageFont.truetype(str(args.game / path), size)
            asc, desc = font.getmetrics()
            lines.append(f"F|{face}|{size}|{asc}|{asc + desc}")
            for code in range(32, 127):
                ch = chr(code)
                adv = round(font.getlength(ch))
                bbox = font.getbbox(ch)            # relative to the ascender-top origin
                w, h = max(0, bbox[2] - bbox[0]), max(0, bbox[3] - bbox[1])
                if w == 0 or h == 0:
                    lines.append(f"G|{face}|{size}|{code}|0|0|0|0|0|0|{adv}")
                    continue
                gw, gh = w + 2, h + 2              # 1 px padding for linear filtering
                if x + gw > ATLAS:
                    x, y, row_h = 0, y + row_h, 0
                if y + gh > ATLAS:
                    raise SystemExit("font atlas full")
                glyph = Image.new("L", (gw, gh), 0)
                ImageDraw.Draw(glyph).text((1 - bbox[0], 1 - bbox[1]), ch, font=font, fill=255)
                atlas.paste(glyph, (x, y))
                lines.append(f"G|{face}|{size}|{code}|{x + 1}|{y + 1}|{w}|{h}|{bbox[0]}|{bbox[1]}|{adv}")
                x += gw
                row_h = max(row_h, gh)
    png = args.gen / "font.png"
    Image.merge("RGBA", (atlas.point(lambda v: 255),) * 3 + (atlas,)).save(png)   # white + glyph coverage as alpha (a8 uses alpha)
    subprocess.run([str(TEX3DS), "-f", "a8", "-z", "auto", "-o", str(out / "font.t3x"), str(png)], check=True, capture_output=True)
    # outlined HUD face
    ol = Image.new("RGBA", OL_ATLAS, (0, 0, 0, 0))
    x = y = row_h = 0
    for face, (path, sizes) in OL_FACES.items():
        for size, stroke in sizes:
            font = ImageFont.truetype(str(args.game / path), size)
            asc, desc = font.getmetrics()
            lines.append(f"F|{face}|{size}|{asc}|{asc + desc}")
            for code in range(32, 127):
                ch = chr(code)
                adv = round(font.getlength(ch))   # the outline overlaps the neighbours, as a Godot label outline does
                if ch not in OL_CHARS or ch == " ":
                    lines.append(f"G|{face}|{size}|{code}|0|0|0|0|0|0|{adv}")
                    continue
                bbox = font.getbbox(ch, stroke_width=stroke)
                w, h = max(0, bbox[2] - bbox[0]), max(0, bbox[3] - bbox[1])
                gw, gh = w + 2, h + 2
                if x + gw > OL_ATLAS[0]:
                    x, y, row_h = 0, y + row_h, 0
                if y + gh > OL_ATLAS[1]:
                    raise SystemExit("outlined font atlas full")
                glyph = Image.new("RGBA", (gw, gh), (0, 0, 0, 0))
                ImageDraw.Draw(glyph).text((1 - bbox[0], 1 - bbox[1]), ch, font=font, fill=(255, 255, 255, 255), stroke_width=stroke, stroke_fill=(0, 0, 0, 255))
                ol.paste(glyph, (x, y))
                lines.append(f"G|{face}|{size}|{code}|{x + 1}|{y + 1}|{w}|{h}|{bbox[0]}|{bbox[1]}|{adv}")
                x += gw
                row_h = max(row_h, gh)
    png2 = args.gen / "font_ol.png"
    ol.save(png2)
    subprocess.run([str(TEX3DS), "-f", "rgba4", "-z", "auto", "-o", str(out / "font_ol.t3x"), str(png2)], check=True, capture_output=True)
    (out / "font.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[OK] font atlas: {len(lines)} records, used {y + row_h}/{ATLAS} rows, {(out / 'font.t3x').stat().st_size / 1024:.0f} KiB")
    return 0


if __name__ == "__main__":
    sys.exit(main())
