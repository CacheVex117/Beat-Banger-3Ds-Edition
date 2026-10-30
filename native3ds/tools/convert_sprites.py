#!/usr/bin/env python3
"""Convert Beat Banger character spritesheets into 3DS texture pages + a binary manifest.

Reads (read-only) a stage's config/keyframes.cfg `loops` (each loop names a spritesheet PNG with
sheet_data h/v/total) and the PNG sheets in <stage>/images.  Every sheet is split into frames,
resampled (premultiplied alpha, Lanczos) to --frame-width, and packed into texture pages of at most
--max-page pixels (3DS limit 1024).  The number of pages per sheet is computed, not assumed.
Pages are encoded with tex3ds (-f rgba4 | etc1a4 | ...) into romfs/gfx/*.t3x.

Outputs under --out-dir:
    <sheet>_<n>.png   page images (intermediate, kept for inspection)
    <sheet>_<n>.t3x   3DS textures (also copied to --romfs-dir/gfx when given)
    <stage>.bbanim    binary manifest (see BBAN format below)
    sprites_report.json  sizes / memory estimates

BBAN v3 (little-endian):
    char magic[4]="BBAN"; u32 version=3; u32 sheet_count
    per sheet: char name[32]; u32 frame_count; u16 frame_w; u16 frame_h; u32 page_count
               per page: char t3x[32]; u16 tex_w; u16 tex_h
               per frame: u8 page; u8 pad; u16 x; u16 y   (x,y = top-left texel inside the page)
    u32 camera_count; per key: i64 timestamp_us; f32 pos_x,pos_y,scale_x,scale_y,duration_s
        (Character/Root camera node: world = (960,540) + 1.5*(pos + scale*local), ease EXPO-out)
    u32 variation_count; per: char sheet[32]; char variation[32]; f32 weight
        (game_character.try_variation: roll = rand*max(100, sum(weights)); the variation sheet replaces the
         normal one for that animation cycle)
"""
from __future__ import annotations

import os
import argparse
import json
import math
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from convert_stage import StageSchemaError, parse_godot_cfg_json  # noqa: E402

TEX3DS = Path(os.environ.get("BB_TEX3DS", "C:/devkitPro/tools/bin/tex3ds.exe"))   # the builder exe sets BB_TEX3DS
BYTES_PER_PIXEL = {"rgba8": 4.0, "rgba4": 2.0, "rgba5551": 2.0, "rgb565": 2.0, "etc1a4": 1.0, "etc1": 0.5}


def next_pow2(n: int) -> int:
    return 1 << max(3, (n - 1).bit_length())


def split_frames(sheet: Image.Image, h: int, v: int, total: int) -> list[Image.Image]:
    fw, fh = sheet.width // h, sheet.height // v
    frames = []
    for i in range(total):
        col, row = i % h, i // h
        frames.append(sheet.crop((col * fw, row * fh, (col + 1) * fw, (row + 1) * fh)))
    return frames


def resample(frame: Image.Image, w: int, h: int) -> Image.Image:
    # Premultiply before resampling so transparent texels (black) do not bleed into edges.
    pm = frame.convert("RGBa").resize((w, h), Image.LANCZOS)
    return pm.convert("RGBA")


def pack_pages(frame_w: int, frame_h: int, count: int, max_page: int):
    cols = max_page // frame_w
    rows = max_page // frame_h
    if cols < 1 or rows < 1:
        raise StageSchemaError(f"frame {frame_w}x{frame_h} does not fit a {max_page}px page")
    per_page = cols * rows
    pages = []
    idx = 0
    while idx < count:
        n = min(per_page, count - idx)
        used_cols = min(cols, n)
        used_rows = math.ceil(n / cols)
        tw = next_pow2(used_cols * frame_w)
        th = next_pow2(used_rows * frame_h)
        pages.append({"first": idx, "count": n, "cols": cols, "tex_w": tw, "tex_h": th})
        idx += n
    return pages


def encode_t3x(png: Path, t3x: Path, fmt: str, quality: str) -> None:
    cmd = [str(TEX3DS), "-f", fmt, "-z", "auto", "-o", str(t3x), str(png)]
    if fmt.startswith("etc1"):
        cmd[1:1] = ["-q", quality]
    subprocess.run(cmd, check=True, capture_output=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage-dir", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--romfs-dir", type=Path)
    ap.add_argument("--frame-width", type=int, default=512)
    ap.add_argument("--format", default="rgba4", choices=sorted(BYTES_PER_PIXEL))
    ap.add_argument("--quality", default="medium", choices=["low", "medium", "high"])
    ap.add_argument("--max-page", type=int, default=1024)
    args = ap.parse_args()

    stage = args.stage_dir.resolve()
    out = args.out_dir.resolve()
    if out == stage or stage in out.parents:
        ap.error("out-dir must not be inside the read-only stage-dir")
    out.mkdir(parents=True, exist_ok=True)
    gfx = None
    if args.romfs_dir:
        gfx = args.romfs_dir / "gfx"
        gfx.mkdir(parents=True, exist_ok=True)

    keyframes = parse_godot_cfg_json(stage / "config" / "keyframes.cfg")
    loops = keyframes.get("loops", [])
    if not loops:
        raise StageSchemaError("keyframes.loops is empty")

    seen: dict[str, dict] = {}
    variations: list[tuple[str, str, float]] = []   # (normal sheet, variation sheet, weight)
    for lp in loops:
        name = str(lp["animations"]["normal"])
        sd = lp["sheet_data"]
        geom = {"h": int(sd["h"]), "v": int(sd["v"]), "total": int(sd["total"])}
        if name not in seen:
            seen[name] = geom
        for var in lp["animations"].get("variations", []):
            vname = str(var["path"])
            if vname not in seen:
                seen[vname] = geom   # Config.pick_variation_index: weighted random swap of the sheet per animation cycle
            variations.append((name, vname, float(var.get("weight", 20.0))))

    # Game-over artwork: cover-fit to 400x240 inside a 512x256 ETC1 texture (GameOver.tscn FailedGraphic)
    go = stage / "images" / "game_over.png"
    if go.is_file():
        rgba = Image.open(go).convert("RGBA")
        im = Image.new("RGBA", rgba.size, (0, 0, 0, 255))   # the game shows the artwork over black
        im.alpha_composite(rgba)
        im = im.convert("RGB")
        scale = max(400 / im.width, 240 / im.height)
        nw, nh = round(im.width * scale), round(im.height * scale)
        im = im.resize((nw, nh), Image.LANCZOS)
        im = im.crop(((nw - 400) // 2, (nh - 240) // 2, (nw - 400) // 2 + 400, (nh - 240) // 2 + 240))
        tex = Image.new("RGB", (512, 256), (0, 0, 0))
        tex.paste(im, (0, 0))
        go_png, go_t3x = out / "game_over.png", out / "game_over.t3x"
        tex.save(go_png)
        subprocess.run([str(TEX3DS), "-q", "medium", "-f", "etc1", "-z", "auto", "-o", str(go_t3x), str(go_png)], check=True, capture_output=True)
        if gfx:
            shutil.copy2(go_t3x, gfx / "game_over.t3x")
        print(f"[OK] game_over.t3x ({go_t3x.stat().st_size // 1024} KiB)")

    manifest = bytearray(b"BBAN" + struct.pack("<II", 3, len(seen)))
    report = {"format": args.format, "frame_width": args.frame_width, "sheets": []}
    total_bytes = 0
    for sheet_no, (fname, sd) in enumerate(seen.items()):
        img = Image.open(stage / "images" / fname).convert("RGBA")
        frames = split_frames(img, sd["h"], sd["v"], sd["total"])
        src_w, src_h = frames[0].size
        fw = args.frame_width
        fh = round(fw * src_h / src_w)
        pages = pack_pages(fw, fh, len(frames), args.max_page)
        stem = Path(fname).stem
        manifest += struct.pack("<32sIHHI", stem.encode("ascii")[:31], len(frames), fw, fh, len(pages))
        page_records, frame_records = bytearray(), bytearray()
        sheet_bytes = 0
        for pi, pg in enumerate(pages):
            page_img = Image.new("RGBA", (pg["tex_w"], pg["tex_h"]), (0, 0, 0, 0))
            for k in range(pg["count"]):
                fi = pg["first"] + k
                x, y = (k % pg["cols"]) * fw, (k // pg["cols"]) * fh
                page_img.paste(resample(frames[fi], fw, fh), (x, y))
                frame_records += struct.pack("<BBHH", pi, 0, x, y)
            # short file names: the manifest stores 31 chars, long sheet names (e.g. zoe_titfuck_nipples_out_face_alt) overflow
            png = out / f"sheet{sheet_no}_{pi}.png"
            t3x = out / f"sheet{sheet_no}_{pi}.t3x"
            page_img.save(png)
            encode_t3x(png, t3x, args.format, args.quality)
            if gfx:
                shutil.copy2(t3x, gfx / t3x.name)
            page_records += struct.pack("<32sHH", t3x.name.encode("ascii")[:31], pg["tex_w"], pg["tex_h"])
            sheet_bytes += int(pg["tex_w"] * pg["tex_h"] * BYTES_PER_PIXEL[args.format])
        manifest += page_records + frame_records
        total_bytes += sheet_bytes
        report["sheets"].append({
            "name": stem, "frames": len(frames), "frame": [fw, fh], "pages": len(pages),
            "tex": [[p["tex_w"], p["tex_h"]] for p in pages], "vram_bytes": sheet_bytes,
        })
        print(f"[OK] {stem}: {len(frames)} frames {fw}x{fh} -> {len(pages)} page(s), "
              f"{sheet_bytes / 1048576:.2f} MiB as {args.format}")

    cams = keyframes.get("camera", [])
    manifest += struct.pack("<I", len(cams))
    for c in cams:
        manifest += struct.pack(
            "<qfffff", round(float(c["timestamp"]) * 1_000_000),
            float(c["position"][0]), float(c["position"][1]),
            float(c["scale"][0]), float(c["scale"][1]), float(c.get("speed", 0.5)),
        )
    manifest += struct.pack("<I", len(variations))
    for normal, vname, weight in variations:
        manifest += struct.pack("<32s32sf", Path(normal).stem.encode("ascii")[:31], Path(vname).stem.encode("ascii")[:31], weight)
    (out / f"{stage.name}.bbanim").write_bytes(manifest)
    if gfx:
        shutil.copy2(out / f"{stage.name}.bbanim", gfx / f"{stage.name}.bbanim")
    report["total_vram_bytes_all_sheets"] = total_bytes
    (out / "sprites_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"[OK] manifest {stage.name}.bbanim; all sheets {total_bytes / 1048576:.2f} MiB (only current+next resident)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
