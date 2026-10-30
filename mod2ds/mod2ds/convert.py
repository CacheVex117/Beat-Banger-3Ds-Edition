"""Convert mod packs into a MOD2DS package (see SPEC.md): one stage folder per level, laid out exactly
like native3ds romfs:/stages/<id>/, plus level.json (full IR), pack manifests and a merged index.txt.
"""
from __future__ import annotations

import functools
from concurrent.futures import ThreadPoolExecutor
import json
import shutil
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from . import __version__, cutscene, formats, limits, media, names
from .discover import ModPack
from .model import Level, LevelError, build_level, check_runtime_limits

STEPS = ("charts", "sprites", "sfx", "song", "video", "menu", "cutscene")
SHEET_WORKERS = 4
# issues raised while emitting (not by the model), keyed to the step that raises them
STEP_ISSUES = {"cutscene_missing_asset": "cutscene", "cutscene_limit": "cutscene", "cutscene_scene_folder": "cutscene",
               "sheet_pages": "sprites", "video_pages": "video", "videos": "video", "bbanim_size": "sprites", "bbsfx_size": "sfx"}
PREVIEW_LEN_S = 24.0          # native build_previews.py clip length


@dataclass
class Options:
    out: Path
    steps: tuple[str, ...] = STEPS
    game_dir: Path | None = None
    tools: media.Tools | None = None
    frame_width: int = 512
    max_page: int = 1024
    sprite_format: str = "etc1a4"
    quality: str = "medium"
    fit_runtime: bool = False
    keep_oversize: bool = False
    framing: str = "auto"
    video_fps: float = 30.0
    video_res: str = "auto"
    half_res_over_s: float = 20.0      # auto: a level with more video than this (full-motion mods) gets half-res pages
    level_filter: list[str] = field(default_factory=list)
    log: Any = functools.partial(print, flush=True)


def _json(path: Path, data: Any) -> None:
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False, default=str) + "\n", encoding="utf-8")


def _rel(p: Path | None, base: Path) -> str | None:
    if p is None:
        return None
    try:
        return p.relative_to(base).as_posix()
    except ValueError:
        return str(p)


# -- per-stage emitters -----------------------------------------------------------------------------

def emit_charts(lv: Level, dst: Path) -> dict[str, Any]:
    d = dst / "charts"
    d.mkdir(parents=True, exist_ok=True)
    loops = [{"timestamp_us": lp["timestamp_us"], "name": lp["sheet"]} for lp in lv.loops]
    written: dict[str, Any] = {}

    def write(chart: dict[str, Any], fname: str) -> dict[str, Any]:
        blob = formats.pack_bbchart(stage_id=lv.stage_id, title=lv.title, difficulty=chart["name_ascii"], rating=chart["rating"],
                                    song_offset_us=lv.song_offset_us, note_offset_us=lv.note_offset_us, notes=chart["notes"],
                                    shutters_us=lv.shutters_us, loops=loops, bpm_changes=lv.bpm_changes)
        (d / fname).write_bytes(blob)
        return {"file": f"charts/{fname}", "chart_index": chart["index"], "name": chart["name"], "notes": len(chart["notes"]), "bytes": len(blob)}

    for slot, ci in lv.slots.items():
        written[slot] = write(lv.charts[ci], f"{lv.stage_id}_{slot}.bbchart")
    for c in lv.charts[len(limits.DIFFICULTY_SLOTS):]:
        written[f"c{c['index']}"] = write(c, f"{lv.stage_id}_c{c['index']}.bbchart")
    return written


def emit_sprites(lv: Level, dst: Path, o: Options) -> dict[str, Any]:
    gfx = dst / "gfx"
    gfx.mkdir(parents=True, exist_ok=True)
    sheets, report = [], []
    def build(no: int, sh) -> dict:
        return media.build_sheet(o.tools, sh.source, sh.h, sh.v, sh.total, sheet_no=no, out_dir=gfx, frame_w=o.frame_width,
                                 max_page=o.max_page, fmt=o.sprite_format, quality=o.quality,
                                 max_frames=limits.MAX_FRAMES_PER_SHEET if o.fit_runtime else None,
                                 max_pages=limits.MAX_PAGES_PER_SHEET if o.fit_runtime else None,
                                 canvas_k=lv.canvas_k, scale=sh.scale, offset=sh.offset)

    # a few sheets at a time: splitting a big mod sheet holds a few hundred MB, the encodes themselves use every core
    with ThreadPoolExecutor(max_workers=SHEET_WORKERS) as pool:
        futures = [pool.submit(build, no, sh) for no, sh in enumerate(lv.sheets.values())]
        results = [f.result() for f in futures]
    for sh, rec in zip(lv.sheets.values(), results):
        sheets.append({"name": sh.name, **rec})
        report.append({"name": sh.name, "source": sh.source.name, "grid": [sh.h, sh.v], "source_frames": sh.total,
                       "frames": rec["frame_count"], "frame": [rec["frame_w"], rec["frame_h"]],
                       "pages": [p["t3x"] for p in rec["pages"]], "vram_bytes": rec["vram_bytes"]})
        if len(rec["pages"]) > limits.MAX_PAGES_PER_SHEET:
            lv.issues.add("limit", "sheet_pages", f"sheet {sh.source.name!r}: {len(rec['pages'])} texture pages > "
                          f"{limits.MAX_PAGES_PER_SHEET} (use --fit-runtime)")
        o.log(f"      sheet {sh.name}: {rec['frame_count']}/{sh.total} frames {rec['frame_w']}x{rec['frame_h']} -> {len(rec['pages'])} page(s)")
    variations = []
    seen = set()
    for lp in lv.loops:
        for v in lp["variations"]:
            key = (lp["sheet"], v["sheet"])
            if lp["sheet"] and key not in seen:
                seen.add(key)
                variations.append((lp["sheet"], v["sheet"], float(v["weight"])))
    blob = formats.pack_bbanim(sheets, lv.cameras, variations)
    (gfx / f"{lv.stage_id}.bbanim").write_bytes(blob)
    if len(blob) > limits.ANIM_FILE_MAX:
        lv.issues.add("limit", "bbanim_size", f".bbanim is {len(blob)} bytes > {limits.ANIM_FILE_MAX}")
    if lv.game_over:
        media.build_cover_texture(o.tools, lv.game_over, gfx / "game_over.t3x")
    vram = sum(s["vram_bytes"] for s in report)
    return {"file": f"gfx/{lv.stage_id}.bbanim", "sheets": report, "vram_bytes": vram,
            "all_resident": vram <= limits.ANIM_RESIDENT_BUDGET, "game_over": bool(lv.game_over)}


def emit_sfx(lv: Level, dst: Path, o: Options) -> dict[str, Any]:
    d = dst / "sfx"
    d.mkdir(parents=True, exist_ok=True)
    clips: list[tuple[str, bytes]] = []
    index: dict[str, int] = {}
    # the shared miss / damage / glass sounds are not in mod banks: the app plays them from romfs:/audio/common.bbsfx
    for rate in limits.SFX_RATES:   # the whole bank sits in linear memory: a bank too big is rebuilt at a lower rate
        clips, index = [], {}
        dropped = []
        for key, clip in lv.clips.items():
            try:
                pcm = o.tools.decode_pcm(clip.source, channels=1, rate=rate)
            except media.ToolError:
                pcm = b""
            if not pcm:   # Godot loads such a file as an empty/null stream: the game plays nothing for it
                dropped.append(clip.source.name)
                continue
            index[key] = len(clips)
            clips.append((clip.name, pcm))
        blob = formats.pack_bbsfx(
            clips,
            [(e["timestamp_us"], 0, ids) for e in lv.sound_loops if (ids := [index[c] for c in e["clips"] if c in index])],
            [(e["timestamp_us"], index[e["clip"]]) for e in lv.oneshots if e["clip"] in index],
            [(e["timestamp_us"], [index[c] for c in e["clips"] if c in index]) for e in lv.voice_banks],
            rate=rate,
        )
        if len(blob) <= limits.SFX_FILE_MAX:
            break
    for name in dropped:
        lv.issues.add("warning", "undecodable_audio", f"clip {name!r} has no decodable audio; dropped (silent in the game too)")
    if rate != limits.SAMPLE_RATE and len(blob) <= limits.SFX_FILE_MAX:
        lv.issues.add("warning", "sfx_rate", f"sound effects at {rate} Hz so the bank fits the 3DS ({len(blob) / 1048576:.1f} MiB)")
    (d / f"{lv.stage_id}.bbsfx").write_bytes(blob)
    if len(blob) > limits.SFX_FILE_MAX:
        lv.issues.add("limit", "bbsfx_size", f".bbsfx is {len(blob) / 1048576:.1f} MiB > {limits.SFX_FILE_MAX // 1048576} MiB")
    return {"file": f"sfx/{lv.stage_id}.bbsfx", "clips": len(clips), "bytes": len(blob), "rate": rate,
            "clip_names": {c.name: c.source.name for c in lv.clips.values()}}


def emit_song(lv: Level, dst: Path, o: Options) -> dict[str, Any]:
    d = dst / "audio"
    d.mkdir(parents=True, exist_ok=True)
    wav = d / f"{lv.stage_id}.wav"
    o.tools.song_wav(lv.song, wav)
    return {"file": f"audio/{wav.name}", "source": lv.song.name, "bytes": wav.stat().st_size}


def emit_video(lv: Level, dst: Path, o: Options) -> list[dict[str, Any]]:
    if not lv.videos:
        return []
    d = dst / "video"
    d.mkdir(parents=True, exist_ok=True)
    out = []
    res = o.video_res
    if res == "auto":   # long video streams for a long time: console SD reads (~1.5 MB/s) cannot feed full-res pages
        total_s = sum(o.tools.estimate_frames(v["source"]) / max(1.0, min(o.tools.fps(v["source"]), o.video_fps or 1e9))
                      for v in lv.videos if v["source"] is not None)
        res = "half" if total_s > o.half_res_over_s else "full"
    per_page = media.video_frames_per_page(res)
    max_frames = limits.MAX_VIDEO_PAGES * per_page
    for vi, v in enumerate(lv.videos):
        tag = f"{lv.stage_id}_v{vi}"
        vname = (v["source"] or v["audio_source"]).name
        if not o.keep_oversize:
            if vi >= limits.MAX_VIDEOS:
                lv.issues.add("limit", "videos", f"video {vname!r} not built: the runtime only plays "
                              f"_v0.._v{limits.MAX_VIDEOS - 1} (--keep-oversize builds it anyway)")
                continue
            src_fps = o.tools.fps(v["source"]) if v["source"] is not None else 1000.0
            est = o.tools.estimate_frames(v["source"]) if v["source"] is not None else 1
            if o.video_fps and src_fps > o.video_fps:
                est = int(est * o.video_fps / src_fps) + 1
            if est > max_frames:
                lv.issues.add("limit", "video_pages", f"video {vname!r} not built: ~{est} frames > {max_frames} "
                              f"({limits.MAX_VIDEO_PAGES} pages; --keep-oversize builds it anyway)")
                continue
        rec = media.build_video(o.tools, v["source"], tag, d, o.quality, o.video_fps, res,
                                audio_src=v.get("audio_source"), own_audio=not v.get("outro"))
        (d / f"{tag}.bbvid").write_bytes(formats.pack_bbvid(start_us=v["start_us"], hide_us=v["hide_us"], fps=rec["fps"],
                                                           frames=rec["frames"], pages=rec["pages"],
                                                           audio_frames=rec["audio_frames"], audio_name=rec["audio_name"],
                                                           **rec["layout"]))
        if len(rec["pages"]) > limits.MAX_VIDEO_PAGES:
            lv.issues.add("limit", "video_pages", f"video {vname!r}: {len(rec['pages'])} pages "
                          f"({rec['frames']} frames) > {limits.MAX_VIDEO_PAGES} ({max_frames} frames)")
        out.append({"file": f"video/{tag}.bbvid", "source": v["source"].name if v["source"] is not None else None,
                    "audio": v["audio_source"].name if v.get("audio_source") is not None else None,
                    "outro": bool(v.get("outro")), "frames": rec["frames"], "fps": rec["fps"], "res": res,
                    "pages": len(rec["pages"]), "start_us": v["start_us"], "hide_us": v["hide_us"]})
        o.log(f"      video {tag}: {rec['frames']} frames @ {rec['fps']:g} fps, {res} res -> {len(rec['pages'])} page(s)")
    return out


def emit_cutscene(lv: Level, dst: Path, o: Options) -> dict[str, Any]:
    d = dst / "cutscene"
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True)
    rec = {}
    for which in ("pre", "post"):
        info = cutscene.convert_scene(o.tools, lv.ref.path, which, d, lv.issues)
        if info:
            rec[which] = info
            o.log(f"      cutscene {which}: {info['panels']} panels, {info['images']} images, {info['clips']} sounds")
    if not rec:
        d.rmdir()
    return rec


def emit_menu(lv: Level, dst: Path, o: Options) -> dict[str, Any]:
    d = dst / "menu"
    d.mkdir(parents=True, exist_ok=True)
    rec: dict[str, Any] = {}
    art = lv.thumb or lv.splash
    if art:
        media.build_thumb(art, d / "thumb.png")
        media.build_round_thumb(o.tools, d / "thumb.png", d / "thumb.t3x")
        rec["thumb"] = "menu/thumb.png"
        rec["thumb_t3x"] = "menu/thumb.t3x"
    start = lv.display.get("preview_timestamp_s")
    start = 30.0 if start is None else max(0.0, start)    # BaseLevelNode default preview point (64 beats @ 128)
    o.tools.wav_clip(lv.song, d / "preview.wav", start, PREVIEW_LEN_S)
    rec["preview"] = "menu/preview.wav"
    rec["preview_start_s"] = start
    return rec


# -- level / pack ------------------------------------------------------------------------------------

def level_ir(lv: Level, outputs: dict[str, Any]) -> dict[str, Any]:
    base = lv.ref.path
    return {
        "format": "mod2ds-level", "version": 1, "tool": f"mod2ds {__version__}",
        "stage_id": lv.stage_id, "pack_id": lv.pack_id, "title": lv.title, "character": lv.character,
        "level_index": lv.level_index, "display": lv.display,
        "source": {"level_dir": str(base), "folder": lv.ref.folder},
        "runtime_compatible": not lv.issues.has("limit") and not lv.issues.has("error"),
        "timing": {"song_offset_us": lv.song_offset_us, "note_offset_us": lv.note_offset_us,
                   "post_song_delay_s": lv.post_song_delay_s, "bpm_changes": lv.bpm_changes, "shutters_us": lv.shutters_us},
        "song": _rel(lv.song, base),
        "difficulty_slots": {s: lv.charts[i]["name"] for s, i in lv.slots.items()},
        "charts": [{k: c[k] for k in ("index", "name", "rating")} | {"note_count": len(c["notes"])} for c in lv.charts],
        "framing": {"mode": lv.framing, "canvas_k": lv.canvas_k},
        "loops": [{"timestamp_us": lp["timestamp_us"], "sheet": lp["sheet"], "source": lp["source"], "variations": lp["variations"]}
                  for lp in lv.loops],
        "sheets": {n: {"source": _rel(s.source, base), "h": s.h, "v": s.v, "total": s.total, "frame_px": list(s.frame_px),
                       "scale": s.scale, "offset": list(s.offset)} for n, s in lv.sheets.items()},
        "camera": lv.cameras,
        "clips": {c.name: {"source": _rel(c.source, base), "origin": c.origin} for c in lv.clips.values()},
        "sound_loop": [{"timestamp_us": e["timestamp_us"], "clips": [lv.clips[c].name for c in e["clips"]]} for e in lv.sound_loops],
        "sound_oneshot": [{"timestamp_us": e["timestamp_us"], "clip": lv.clips[e["clip"]].name} for e in lv.oneshots],
        "voice_bank": [{"timestamp_us": e["timestamp_us"], "clips": [lv.clips[c].name for c in e["clips"]]} for e in lv.voice_banks],
        "video": [{"start_us": v["start_us"], "hide_us": v["hide_us"], "source": _rel(v["source"], base),
                   "audio_source": _rel(v.get("audio_source"), base), "outro": bool(v.get("outro"))} for v in lv.videos],
        "extras_not_in_3ds_formats": lv.extras,
        "outputs": outputs,
        "issues": lv.issues.as_list(),
    }


def _replace_dir(src: Path, dst: Path, attempts: int = 8) -> None:
    """Move a finished stage into place. Windows briefly locks fresh files (indexer, antivirus): retry."""
    for i in range(attempts):
        try:
            shutil.rmtree(dst, ignore_errors=True)
            src.rename(dst)
            return
        except PermissionError:
            if i == attempts - 1:
                raise
            time.sleep(0.25 * (i + 1))


def convert_level(lv: Level, o: Options) -> dict[str, Any]:
    stages = o.out / "stages"
    final = stages / lv.stage_id
    work = stages / f".{lv.stage_id}.partial"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    keep_from_previous = final.is_dir() and set(o.steps) != set(STEPS)
    if keep_from_previous:                     # partial rebuild: start from the previous output
        shutil.copytree(final, work, dirs_exist_ok=True)
    outputs: dict[str, Any] = {}
    prev_ir = final / "level.json"
    if keep_from_previous and prev_ir.is_file():
        prev = json.loads(prev_ir.read_text(encoding="utf-8"))
        outputs = prev.get("outputs", {})
        for i in prev.get("issues", []):           # findings of steps not re-run this time
            if STEP_ISSUES.get(i["code"]) not in (None, *o.steps):
                for _ in range(i.get("count", 1)):
                    lv.issues.add(i["severity"], i["code"], i["message"])
    t0 = time.time()
    if "charts" in o.steps:
        outputs["charts"] = emit_charts(lv, work)
    if "song" in o.steps:
        outputs["song"] = emit_song(lv, work, o)
    if "sfx" in o.steps:
        outputs["sfx"] = emit_sfx(lv, work, o)
    if "sprites" in o.steps:
        outputs["sprites"] = emit_sprites(lv, work, o)
    if "video" in o.steps:
        outputs["video"] = emit_video(lv, work, o)
    if "menu" in o.steps:
        outputs["menu"] = emit_menu(lv, work, o)
    if "cutscene" in o.steps:
        outputs["cutscene"] = emit_cutscene(lv, work, o)
    frames = {s["name"]: s["frames"] for s in outputs.get("sprites", {}).get("sheets", [])}
    check_runtime_limits(lv, frames or None)
    _json(work / "level.json", level_ir(lv, outputs))
    if lv.extras.get("legacy"):   # native3ds main.c stage_is_legacy: the LEGACY LAYOUT modifier (sideways highway, notes from the right)
        # "art u0 u1": the widest frame's span of the 1280k-wide cell, so the book layout crops the empty sides
        half = max((abs(sh.offset[0]) + sh.frame_px[0] * sh.scale / 2 for sh in lv.sheets.values()), default=300.0)
        span = min(0.5, half / (1280.0 * lv.canvas_k))
        (work / "legacy.txt").write_text(f"pre-release (Legacy) mod\nart {0.5 - span:.4f} {0.5 + span:.4f}\n", encoding="utf-8")
    _replace_dir(work, final)
    o.log(f"    -> stages/{lv.stage_id} in {time.time() - t0:.1f}s")
    return outputs


def convert_pack(pack: ModPack, o: Options) -> dict[str, Any]:
    act_id = str(pack.act.get("act_id", pack.path.name))
    pid = names.pack_id(pack.name, act_id)
    manifest: dict[str, Any] = {
        "format": "mod2ds-pack", "version": 1, "tool": f"mod2ds {__version__}", "pack_id": pid,
        "source": {"path": str(pack.path), "folder": pack.path.name, "kind": pack.kind},
        "act": {k: pack.act.get(k) for k in ("act_name", "act_id", "act_index", "act_description", "act_author", "author") if k in pack.act},
        "act_name_ascii": names.ascii_text(pack.name, limits.ACT_NAME_MAX) or pid,
        "download": {k: pack.download.get(k) for k in ("id", "title", "author", "rating", "game_version", "mod_version",
                                                       "song_title", "song_creator", "bpm", "media_type") if k in pack.download},
        "problems": list(pack.problems), "stages": [], "index_lines": [],
    }
    o.log(f"== {pack.path.name}  [{pack.kind}] -> {pid}")
    if pack.kind != "act":
        for p in pack.problems:
            o.log(f"   ! {p}")
        return manifest
    for order, ref in enumerate(pack.levels):   # index level_index = position in the game's level order (unique per pack)
        if o.level_filter and ref.folder not in o.level_filter and str(ref.meta.get("level_name")) not in o.level_filter:
            continue
        o.log(f"  - {ref.folder}")
        entry: dict[str, Any] = {"folder": ref.folder, "level_name": ref.meta.get("level_name")}
        try:
            lv = build_level(ref, pack, pid, o.game_dir, o.framing)
        except LevelError as exc:
            entry.update(status="error", error=str(exc))
            o.log(f"    ! not convertible: {exc}")
            manifest["stages"].append(entry)
            continue
        entry["stage_id"] = lv.stage_id
        try:
            convert_level(lv, o)
        except (media.ToolError, OSError, ValueError) as exc:
            shutil.rmtree(o.out / "stages" / f".{lv.stage_id}.partial", ignore_errors=True)
            entry.update(status="error", error=f"conversion failed: {exc}")
            o.log(f"    ! conversion failed: {exc}")
            manifest["stages"].append(entry)
            continue
        ok = not lv.issues.has("limit")
        entry.update(status="ok" if ok else "over_runtime_limits", title=lv.title, issues=lv.issues.as_list())
        manifest["stages"].append(entry)
        manifest["index_lines"].append(formats.index_line(
            lv.stage_id, lv.title, lv.character, pid, manifest["act_name_ascii"],
            int(pack.act.get("act_index", 0) or 0), order))
    packs = o.out / "packs"
    packs.mkdir(parents=True, exist_ok=True)
    prev = packs / f"{pid}.json"
    if o.level_filter and prev.is_file():          # keep the pack's other, previously converted levels
        old = json.loads(prev.read_text(encoding="utf-8"))
        done = {s["folder"] for s in manifest["stages"]}
        new_ids = {line.split("|")[0] for line in manifest["index_lines"]}
        kept = [s for s in old.get("stages", []) if s.get("folder") not in done]
        kept_ids = {s.get("stage_id") for s in kept}
        manifest["stages"] = kept + manifest["stages"]
        manifest["index_lines"] = [ln for ln in old.get("index_lines", [])
                                   if ln.split("|")[0] in kept_ids and ln.split("|")[0] not in new_ids] + manifest["index_lines"]
    _json(prev, manifest)
    return manifest


def write_stage_rows(stages: Path, rows: list[str]) -> None:
    """Each stage folder also carries its own index row (index_row.txt). The 3DS game lists stage folders that have one even
    when index.txt does not name them, so a package copied onto a card never has to merge (or overwrite) its index.txt."""
    for r in rows:
        d = stages / r.split("|", 1)[0]
        if d.is_dir():
            (d / "index_row.txt").write_text(r + "\n", encoding="utf-8")


def write_index(out: Path) -> list[str]:
    """Merge every pack manifest's rows into <out>/stages/index.txt (romfs index format)."""
    rows: list[str] = []
    for mf in sorted((out / "packs").glob("*.json")):
        data = json.loads(mf.read_text(encoding="utf-8"))
        rows += [r for r in data.get("index_lines", []) if (out / "stages" / r.split("|")[0]).is_dir()]
    rows.sort(key=lambda r: (r.split("|")[3], int(r.split("|")[6]), r.split("|")[0]))
    (out / "stages").mkdir(parents=True, exist_ok=True)
    (out / "stages" / "index.txt").write_text("".join(r + "\n" for r in rows), encoding="utf-8")
    write_stage_rows(out / "stages", rows)
    return rows
