#!/usr/bin/env python3
"""Convert a stage's pre/post-level cutscenes into romfs/stages/<id>/cutscene/ (Milestone 3.1).

Source (read-only): <stage>/cutscene/{pre,post}/{script.cfg,global_tracks.cfg}, <stage>/cutscene/shots/*.png, <stage>/cutscene/audio/*.ogg,
<stage>/cutscene/cutscene_cache.cfg (character colours).

Output per scene S in {pre, post}:
  S.txt        script:  H|panel_count            (header)
                        C|name|r|g|b              (character colour 0..255)
                        P|image|character|dialogue|voice|sfx1;sfx2|transition     (one per panel; image = index or -1)
                        T|clip|bus|start|end|restart|fade_in|fade_out             (global-track segment)
  S.bbca       audio bank: "BBCA" u32 version=1, u32 clip_count, clips[name32, u32 byte_offset, u32 frames, u32 rate], PCM16 mono blob
  S_<n>.t3x    panel image n (400x225 letterboxed into a 512x256 ETC1 texture)
Audio is mono PCM16: voice/SFX 22.05 kHz (SFX capped at 6 s, voice at 30 s), looping tracks 16 kHz capped at 16 s (they loop on the console).
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from build_extras import fit_to_texture, tex3ds, strip_bb, ascii_clean  # noqa: E402

RATE = {"sfx": 22050, "voice": 22050, "track": 16000}   # looping tracks (ambience/music) are stored at a lower rate
CAP = {"sfx": 6.0, "voice": 9.0, "track": 12.0}   # clips stay under 2^18 samples: longer buffers hang the emulator DSP


def load_cfg(path: Path, key: str):
    raw = path.read_text(encoding="utf-8")
    i = raw.find(key + "=")
    if i < 0:
        return None
    payload = raw[i + len(key) + 1:].strip()
    payload = re.sub(r"\bArray\[Dictionary\]\(", "(", payload)
    payload = re.sub(r"\b(?:Color|Vector2i?|Vector3i?|Rect2)\(([^)]*)\)", r"[\1]", payload)
    if payload.startswith("("):               # Array[Dictionary]( [...] )
        payload = payload[1:]
    payload = re.sub(r",(\s*[}\]])", r"\1", payload)
    return json.JSONDecoder(strict=False).raw_decode(payload.strip())[0]


def strip_tags(s: str) -> str:
    """BBCode tags incl. attributes with spaces, e.g. [shake limit=4] (the 3DS text has no colour/shake effects)."""
    return re.sub(r"\[/?[a-zA-Z_]+(?:[ =][^\]]*)?\]", "", s)


def clip_name(path: str) -> str:
    stem = Path(path).stem
    return re.sub(r"[^A-Za-z0-9_.-]", "_", stem)[:31]


def pcm(ffmpeg: str, src: Path, cap: float, rate: int) -> bytes:
    r = subprocess.run([ffmpeg, "-v", "error", "-i", str(src), "-t", f"{cap}", "-ac", "1", "-ar", str(rate), "-f", "s16le", "-"],
                       capture_output=True, check=True)
    return r.stdout


def convert_scene(sdir: Path, which: str, out: Path, gen: Path, ffmpeg: str) -> dict | None:
    cs = sdir / "cutscene"
    sp = cs / which / "script.cfg"
    if not sp.is_file():
        return None
    lines = load_cfg(sp, "lines") or []
    if not lines:
        return None
    gt = cs / which / "global_tracks.cfg"
    tracks = (load_cfg(gt, "data") or []) if gt.is_file() else []

    # character colours
    colors: dict[str, tuple[int, int, int]] = {}
    cache = cs / "cutscene_cache.cfg"
    if cache.is_file():
        for e in load_cfg(cache, "data") or []:
            c = e.get("character") if isinstance(e, dict) else None
            if c and "color" in c:
                col = c["color"]
                colors[c["name"]] = tuple(int(max(0.0, min(1.0, float(x))) * 255) for x in col[:3])

    # audio clips (role decides the cap; a clip used in several roles keeps the longest)
    roles: dict[str, str] = {}
    srcs: dict[str, Path] = {}

    def want(path: str, role: str) -> str | None:
        if not path:
            return None
        f = cs / "audio" / path
        if not f.is_file():
            print(f"  [warn] {which}: missing audio {path}")
            return None
        n = clip_name(path)
        prev = roles.get(n)
        if prev is None or CAP[role] > CAP[prev]:
            roles[n] = role
        srcs[n] = f
        return n

    panels, images, img_idx = [], [], {}
    for ln in lines:
        src = ln.get("src", "")
        idx = -1
        if src and (cs / "shots" / src).is_file():
            if src not in img_idx:
                img_idx[src] = len(images)
                images.append(src)
            idx = img_idx[src]
        sfx, voice = [], ""
        raw_audio = ln.get("audio", [])
        if isinstance(raw_audio, str):
            raw_audio = [raw_audio]
        for a in raw_audio:
            if isinstance(a, str):
                a = {"path": a}
            if not isinstance(a, dict) or not a.get("path"):
                continue
            is_voice = a.get("bus", "SFX") == "Voice"
            n = want(a["path"], "voice" if is_voice else "sfx")
            if n:
                if is_voice and not voice:
                    voice = n
                else:
                    sfx.append(n)
        vl = ln.get("voice_line", "")
        if vl:
            n = want(vl, "voice")
            if n:
                voice = n
        text = ascii_clean(strip_bb(strip_tags(str(ln.get("dialogue", "")))))
        panels.append((idx, ascii_clean(str(ln.get("character", ""))), text, voice, sfx, str(ln.get("transition", ""))))

    segs = []
    for t in tracks:
        tr = t.get("track", t) if isinstance(t, dict) else None
        if not tr or not tr.get("path"):
            continue
        n = want(tr["path"], "track")
        if not n:
            continue
        bus = tr.get("bus", "Music") or "Music"
        if isinstance(tr.get("segments"), list):
            for s in tr["segments"]:
                segs.append((n, bus, int(s.get("start", 0)), int(s.get("end", 0)), s.get("resume") == "restart", bool(s.get("fade_in")), bool(s.get("fade_out"))))
        else:
            st, en = max(0, int(tr.get("start_panel", 0))), int(tr.get("end_panel", 0))
            en = max(st, en)
            pause, resume = int(tr.get("pause_panel", 0)), int(tr.get("resume_panel", 0))
            fade = bool(tr.get("fade", False))
            if pause > st and pause <= en:
                segs.append((n, bus, st, pause - 1, False, fade, False))
                if resume > pause and resume <= en:
                    segs.append((n, bus, resume, en, False, False, False))
            else:
                segs.append((n, bus, st, en, False, fade, False))

    # audio bank
    names = sorted(roles)
    blob = bytearray()
    table = []
    for n in names:
        data = pcm(ffmpeg, srcs[n], CAP[roles[n]], RATE[roles[n]])
        if roles[n] != "track" and len(data) > 2000:      # short fade-out so capped clips do not click
            a = bytearray(data)
            nfade = min(len(a) // 2, 441)
            for i in range(nfade):
                k = len(a) // 2 - nfade + i
                v = int.from_bytes(a[k * 2:k * 2 + 2], "little", signed=True)
                v = int(v * (1 - (i + 1) / nfade))
                a[k * 2:k * 2 + 2] = v.to_bytes(2, "little", signed=True)
            data = bytes(a)
        while len(blob) % 64:                 # NDSP/DSP wants aligned sample data
            blob += bytes(1)
        table.append((n, len(blob), len(data) // 2, RATE[roles[n]]))
        blob += data
    bank = bytearray(b"BBCA") + struct.pack("<II", 1, len(table))
    for n, off, fr, rate in table:
        bank += n.encode("ascii").ljust(32, bytes(1)) + struct.pack("<III", off, fr, rate)
    bank += blob
    (out / f"{which}.bbca").write_bytes(bytes(bank))

    # images
    for i, name in enumerate(images):
        png = gen / f"{which}_{i}.png"
        fit_to_texture(cs / "shots" / name, 400, 240, 512, 256, png, alpha=False)
        tex3ds(png, out / f"{which}_{i}.t3x", "etc1")

    # script
    txt = [f"H|{len(panels)}"]
    for name, (r, g, b) in sorted(colors.items()):
        txt.append(f"C|{ascii_clean(name)}|{r}|{g}|{b}")
    for idx, ch, text, voice, sfx, tr in panels:
        txt.append("P|" + "|".join([str(idx), ch, text.replace("|", "/"), voice, ";".join(sfx), tr]))
    for n, bus, st, en, restart, fi, fo in segs:
        txt.append(f"T|{n}|{bus}|{st}|{en}|{int(restart)}|{int(fi)}|{int(fo)}")
    (out / f"{which}.txt").write_text("\n".join(txt) + "\n", encoding="utf-8")
    return {"panels": len(panels), "images": len(images), "clips": len(table), "pcm_kib": len(blob) // 1024, "tracks": len(segs)}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage-dir", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True, help="generated scratch dir")
    ap.add_argument("--romfs-dir", type=Path, required=True, help="romfs/stages/<id>")
    ap.add_argument("--ffmpeg", default="ffmpeg")
    args = ap.parse_args()
    out = args.romfs_dir / "cutscene"
    out.mkdir(parents=True, exist_ok=True)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for which in ("pre", "post"):
        info = convert_scene(args.stage_dir, which, out, args.out_dir, args.ffmpeg)
        print(f"[OK] {args.stage_dir.name} {which}: {info if info else 'no cutscene'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
