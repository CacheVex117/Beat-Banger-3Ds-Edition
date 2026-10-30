"""Mod cutscenes -> the runtime's cutscene files (stages/<id>/cutscene/), byte-compatible with native3ds
tools/convert_cutscene.py, which converts the built-in stages' cutscenes.

Source (game rules: class/cutscene.gd, AssetManager.load_cutscene_assets):
    cutscene/<pre|post>/script.cfg          [data] lines = panels (src image, character, dialogue, audio[], voice_line)
    cutscene/<pre|post>/global_tracks.cfg   [main] data = looping tracks (start/end panels, or segments)
    cutscene/cutscene_cache.cfg             character colours
    cutscene/shots/<file>, cutscene/audio/<file>   assets, looked up by file name
Some mods (the Encore packs) keep shots/ and audio/ inside the scene folder instead; those are used as a fallback and
reported, since the game itself only loads the shared folders.

Output per scene S:
    S.txt      H|panels, C|name|r|g|b, P|image|character|text|voice|sfx1;sfx2|transition, T|clip|bus|start|end|restart|fadein|fadeout
    S.bbca     "BBCA" v1, clip table (name32, offset, frames, rate) + mono PCM16 blob (64-byte aligned clips)
    S_<n>.t3x  panel image n, fitted into 400x240 at the top-left of a 512x256 ETC1 texture
Runtime limits (platform/3ds/cutscene.c): 64 panels, 64 clips, 24 track segments, 8 character colours, 4 tracks,
4 sfx per panel, 239-character text, 511-character lines.
"""
from __future__ import annotations

import re
import struct
from pathlib import Path
from typing import Any

from PIL import Image

from . import godot_cfg, names
from .model import DirIndex, Issues

RATE = {"sfx": 22050, "voice": 22050, "track": 16000}   # looping tracks are stored at a lower rate
CAP = {"sfx": 6.0, "voice": 9.0, "track": 12.0}         # seconds; keeps clips under 2^18 samples (emulator DSP)
MAX_PANELS, MAX_CLIPS, MAX_SEGS, MAX_CHARS, MAX_SFX, MAX_TEXT, MAX_LINE = 64, 64, 24, 8, 4, 239, 511


def strip_tags(s: str) -> str:
    """BBCode tags, including attributes with spaces ([shake limit=4]); the 3DS text has no colour or shake effects."""
    s = re.sub(r"\[/?[a-zA-Z_]+(?:[ =][^\]]*)?\]", "", s)
    return re.sub(r"\s+", " ", s).strip()


def clip_name(path: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]", "_", Path(path).stem)[:31]


def _cfg(path: Path, section: str, key: str) -> Any:
    if not path.is_file():
        return None
    try:
        cfg = godot_cfg.load(path)
    except godot_cfg.CfgError:
        return None
    if key in cfg.get(section, {}):
        return cfg[section][key]
    return next((sec[key] for sec in cfg.values() if key in sec), None)


def has_cutscene(level_dir: Path) -> bool:
    return any((level_dir / "cutscene" / w / "script.cfg").is_file() for w in ("pre", "post"))


def fit_image(src: Path, png: Path) -> None:
    """native3ds build_extras.fit_to_texture(src, 400, 240, 512, 256, alpha=False): contain-fit over black, top-left."""
    with Image.open(src) as im:
        img = im.convert("RGBA")
    scale = min(400 / img.width, 240 / img.height)
    nw, nh = max(1, round(img.width * scale)), max(1, round(img.height * scale))
    img = img.resize((nw, nh), Image.LANCZOS)
    flat = Image.new("RGBA", img.size, (0, 0, 0, 255))
    flat.alpha_composite(img)
    canvas = Image.new("RGB", (512, 256), (0, 0, 0))
    canvas.paste(flat.convert("RGB"), (0, 0))
    canvas.save(png)


def _fade_tail(data: bytes) -> bytes:
    """Short fade-out so a clip cut at its cap does not click."""
    a = bytearray(data)
    nfade = min(len(a) // 2, 441)
    for i in range(nfade):
        k = len(a) // 2 - nfade + i
        v = int.from_bytes(a[k * 2:k * 2 + 2], "little", signed=True)
        a[k * 2:k * 2 + 2] = int(v * (1 - (i + 1) / nfade)).to_bytes(2, "little", signed=True)
    return bytes(a)


def convert_scene(tools, level_dir: Path, which: str, out: Path, issues: Issues) -> dict | None:
    cs = level_dir / "cutscene"
    lines = _cfg(cs / which / "script.cfg", "data", "lines") or []
    if not isinstance(lines, list) or not lines:
        return None
    if len(lines) == 1 and isinstance(lines[0], dict) and lines[0].get("dialogue") == "Sample Dialogue" \
            and not lines[0].get("src") and not lines[0].get("audio"):
        # the cutscene editor's untouched template (one "Sample Character: Sample Dialogue" panel, no art or sound)
        issues.add("ignored", "cutscene_placeholder", f"{which} cutscene is the editor's empty template; not converted")
        return None
    tracks =_cfg(cs / which / "global_tracks.cfg", "main", "data") or []
    shots = [DirIndex(cs / "shots"), DirIndex(cs / which / "shots")]
    audio = [DirIndex(cs / "audio"), DirIndex(cs / which / "audio")]
    fallback_used: set[str] = set()

    def find(indexes: list[DirIndex], name: str, kind: str) -> Path | None:
        for i, idx in enumerate(indexes):
            p = idx.find(name)
            if p:
                if i:
                    fallback_used.add(kind)
                return p
        return None

    colors: dict[str, tuple[int, int, int]] = {}
    for e in _cfg(cs / "cutscene_cache.cfg", "main", "data") or []:
        c = e.get("character") if isinstance(e, dict) else None
        if isinstance(c, dict) and c.get("name") and isinstance(c.get("color"), list):
            colors[str(c["name"])] = tuple(int(max(0.0, min(1.0, float(x))) * 255) for x in c["color"][:3])

    roles: dict[str, str] = {}
    srcs: dict[str, Path] = {}

    def want(path: Any, role: str) -> str | None:
        if not isinstance(path, str) or not path:
            return None
        f = find(audio, path, "audio")
        if f is None:
            issues.add("warning", "cutscene_missing_asset", f"{which} cutscene: audio {path!r} not found (silent)")
            return None
        n = clip_name(path)
        if roles.get(n) is None or CAP[role] > CAP[roles[n]]:
            roles[n] = role
        srcs[n] = f
        return n

    panels, images, img_idx = [], [], {}
    for ln in lines[:MAX_PANELS]:
        if not isinstance(ln, dict):
            continue
        src, idx = str(ln.get("src", "") or ""), -1
        if src:
            sp = find(shots, src, "shots")
            if sp is None:
                issues.add("warning", "cutscene_missing_asset", f"{which} cutscene: image {src!r} not found (blank panel)")
            else:
                if sp not in img_idx:
                    img_idx[sp] = len(images)
                    images.append(sp)
                idx = img_idx[sp]
        sfx, voice = [], ""
        raw = ln.get("audio", [])
        for a in [raw] if isinstance(raw, (str, dict)) else (raw if isinstance(raw, list) else []):
            a = {"path": a} if isinstance(a, str) else a
            if not isinstance(a, dict) or not a.get("path"):
                continue
            is_voice = a.get("bus", "SFX") == "Voice"
            n = want(a["path"], "voice" if is_voice else "sfx")
            if n:
                if is_voice and not voice:
                    voice = n
                else:
                    sfx.append(n)
        vl = want(ln.get("voice_line", ""), "voice")
        if vl:
            voice = vl
        if len(sfx) > MAX_SFX:
            issues.add("warning", "cutscene_limit", f"{which} cutscene: a panel has {len(sfx)} sounds; the runtime plays {MAX_SFX}")
        text = names.ascii_text(strip_tags(str(ln.get("dialogue", "") or "")))
        if len(text) > MAX_TEXT:
            issues.add("warning", "cutscene_limit", f"{which} cutscene: dialogue over {MAX_TEXT} characters is cut")
            text = text[:MAX_TEXT]
        panels.append((idx, names.ascii_text(str(ln.get("character", "") or ""), 31), text, voice, sfx[:MAX_SFX],
                       names.ascii_text(str(ln.get("transition", "") or ""), 15)))
    if len(lines) > MAX_PANELS:
        issues.add("warning", "cutscene_limit", f"{which} cutscene: {len(lines)} panels, the runtime shows the first {MAX_PANELS}")

    segs = []
    for t in tracks if isinstance(tracks, list) else []:
        tr = t.get("track", t) if isinstance(t, dict) else None
        if not isinstance(tr, dict) or not tr.get("path"):
            continue
        n = want(tr["path"], "track")
        if not n:
            continue
        bus = str(tr.get("bus", "Music") or "Music")
        if isinstance(tr.get("segments"), list):
            for s in tr["segments"]:
                if isinstance(s, dict):
                    segs.append((n, bus, int(s.get("start", 0)), int(s.get("end", 0)), s.get("resume") == "restart",
                                 bool(s.get("fade_in")), bool(s.get("fade_out"))))
        else:
            st = max(0, int(tr.get("start_panel", 0) or 0))
            en = max(st, int(tr.get("end_panel", 0) or 0))
            pause, resume, fade = int(tr.get("pause_panel", 0) or 0), int(tr.get("resume_panel", 0) or 0), bool(tr.get("fade", False))
            if st < pause <= en:
                segs.append((n, bus, st, pause - 1, False, fade, False))
                if pause < resume <= en:
                    segs.append((n, bus, resume, en, False, False, False))
            else:
                segs.append((n, bus, st, en, False, fade, False))
    if len(segs) > MAX_SEGS:
        issues.add("warning", "cutscene_limit", f"{which} cutscene: {len(segs)} track segments, the runtime keeps {MAX_SEGS}")
        segs = segs[:MAX_SEGS]
    if fallback_used:
        issues.add("warning", "cutscene_scene_folder", f"{which} cutscene: {' and '.join(sorted(fallback_used))} found in "
                   f"cutscene/{which}/ rather than the shared cutscene/ folders the game loads from")

    # audio bank
    table, blob = [], bytearray()
    for n in sorted(roles)[:MAX_CLIPS]:
        data = tools.run(tools.ffmpeg, "-v", "error", "-i", str(srcs[n]), "-t", f"{CAP[roles[n]]}", "-ac", "1",
                         "-ar", str(RATE[roles[n]]), "-f", "s16le", "-")
        if roles[n] != "track" and len(data) > 2000:
            data = _fade_tail(data)
        while len(blob) % 64:                 # NDSP wants aligned sample data
            blob += bytes(1)
        table.append((n, len(blob), len(data) // 2, RATE[roles[n]]))
        blob += data
    if len(roles) > MAX_CLIPS:
        issues.add("warning", "cutscene_limit", f"{which} cutscene: {len(roles)} sounds, the runtime loads {MAX_CLIPS}")
    bank = bytearray(b"BBCA") + struct.pack("<II", 1, len(table))
    for n, off, fr, rate in table:
        bank += n.encode("ascii").ljust(32, bytes(1)) + struct.pack("<III", off, fr, rate)
    (out / f"{which}.bbca").write_bytes(bytes(bank + blob))

    # images (encoded in parallel like every other page)
    from .media import encode_async, wait_all
    jobs = []
    for i, src in enumerate(images):
        png = out / f"{which}_{i}.png"
        fit_image(src, png)
        jobs.append(encode_async(tools, png, out / f"{which}_{i}.t3x", "etc1"))
    wait_all(jobs)

    # script
    txt = [f"H|{len(panels)}"]
    for name, (r, g, b) in sorted(colors.items())[:MAX_CHARS]:
        txt.append(f"C|{names.ascii_text(name, 31)}|{r}|{g}|{b}")
    for idx, ch, text, voice, sfx, tr in panels:
        txt.append("P|" + "|".join([str(idx), ch, text, voice, ";".join(sfx), tr])[:MAX_LINE - 2])
    for n, bus, st, en, restart, fi, fo in segs:
        txt.append(f"T|{n}|{bus}|{st}|{en}|{int(restart)}|{int(fi)}|{int(fo)}")
    (out / f"{which}.txt").write_text("\n".join(txt) + "\n", encoding="ascii", errors="replace")
    return {"panels": len(panels), "images": len(images), "clips": len(table), "pcm_kib": len(blob) // 1024, "segments": len(segs)}
