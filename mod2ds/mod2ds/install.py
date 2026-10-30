"""One-step install: mod folders / bbmods .zip files -> converted -> copied onto a 3DS SD card (or Azahar's).

Used by "Drop mods here.bat" (drag and drop) and `python -m mod2ds install`.

    inputs   a mod pack folder (has act.cfg), a .zip downloaded from bbmods, or a folder of packs (the game's mods folder)
    library  converted packs are cached in <MOD2DS>/library (re-dropping an unchanged mod skips the conversion)
    target   <SD>/3ds/bb_mods: a drive with a "Nintendo 3DS" folder (a 3DS SD card) or Azahar's sdmc; asked if several
    rule     only levels inside every runtime limit are installed (--include-over-limit installs the rest anyway)
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import string
import sys
import zipfile
from pathlib import Path
from typing import Any

from . import convert, cutscene, discover, legacy, limits, paths, report
from .model import LevelError, build_level, check_runtime_limits

LIBRARY = paths.library_dir()
AZAHAR_SD = Path(os.environ.get("APPDATA", "~")).expanduser() / "Azahar" / "sdmc"
BLOCKED_EXT = {"exe", "dll", "so", "dylib", "bat", "cmd", "com", "msi", "scr", "ps1", "sh", "bash", "app", "jar",
               "gd", "gdc", "gde", "gdextension"}   # Files.BLOCKED_MOD_EXTENSIONS
ZIP_MARKER = ".mod2ds_zip"
ORDER_FILE = ".mod2ds_order.json"


# -- inputs ------------------------------------------------------------------------------------------

def extract_zip(zip_path: Path, dest_root: Path) -> Path | None:
    """Files.extract_all_from_zip: the folder holding the first act.cfg (else chart.cfg) is the mod root.
    __MACOSX/.DS_Store are skipped; absolute, drive or '..' paths and executable/script files are refused."""
    with zipfile.ZipFile(zip_path) as zf:
        names = [n.replace("\\", "/") for n in zf.namelist()]
        cfg = next((n for n in names if n.rsplit("/", 1)[-1] == "act.cfg"), None) \
            or next((n for n in names if n.rsplit("/", 1)[-1] == "chart.cfg"), None)
        if cfg is None:
            return None
        base = cfg.rsplit("/", 1)[0] if "/" in cfg else ""
        out = dest_root / (base.rsplit("/", 1)[-1] if base else zip_path.stem)
        st = zip_path.stat()
        stamp = f"{zip_path.resolve()}|{st.st_size}|{int(st.st_mtime)}"
        marker = out / ZIP_MARKER
        if marker.is_file() and marker.read_text(encoding="utf-8") == stamp:
            return out                                   # same zip already extracted: keep it (and its cache key)
        shutil.rmtree(out, ignore_errors=True)
        out.mkdir(parents=True)
        root = out.resolve()
        for raw, name in zip(zf.namelist(), names):
            if name.startswith("__MACOSX/") or name.endswith(".DS_Store"):
                continue
            if base and not name.startswith(base + "/"):
                continue
            rel = name[len(base) + 1:] if base else name
            if not rel or rel.startswith("/") or (len(rel) > 1 and rel[1] == ":"):
                continue
            target = (out / rel).resolve()
            if target != root and root not in target.parents:
                continue
            if name.endswith("/"):
                target.mkdir(parents=True, exist_ok=True)
                continue
            if target.suffix.lower().lstrip(".") in BLOCKED_EXT:
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with zf.open(raw) as src, open(target, "wb") as dst:
                shutil.copyfileobj(src, dst)
        marker.write_text(stamp, encoding="utf-8")
        return out


def collect_packs(inputs: list[Path], work: Path, log) -> list[Path]:
    packs: list[Path] = []
    for item in inputs:
        if item.is_file() and item.suffix.lower() == ".zip":
            out = extract_zip(item, work / "_zips")
            if out is None:
                log(f"  ! {item.name}: no act.cfg or chart.cfg inside; not a Beat Banger mod")
            else:
                packs.append(out)
        elif item.is_dir() and (item / "act.cfg").is_file():
            packs.append(item)
        elif item.is_dir() and any((c / "act.cfg").is_file() or (c / "chart.cfg").is_file() or next(c.glob("*/chart.cfg"), None)
                                   for c in item.iterdir() if c.is_dir()):
            packs += sorted(c for c in item.iterdir() if c.is_dir())      # a mods folder: every pack inside
        elif item.is_dir() and next(item.rglob("chart.cfg"), None) is not None:
            packs.append(item)                                             # a pre-release (Legacy) mod: reported
        elif item.is_dir() and (item / "config" / "meta.cfg").is_file() and (item.parent / "act.cfg").is_file():
            packs.append(item.parent)                                      # a level folder: its pack
        else:
            log(f"  ! {item}: not a mod folder, a mods folder or a .zip")
    seen, out = set(), []
    for p in packs:
        if p.resolve() not in seen:
            seen.add(p.resolve())
            out.append(p)
    return out


def source_signature(pack_dir: Path) -> str:
    h = hashlib.sha1()
    for f in sorted(pack_dir.rglob("*")):
        if f.is_file() and f.name != ZIP_MARKER:
            st = f.stat()
            h.update(f"{f.relative_to(pack_dir).as_posix()}|{st.st_size}|{int(st.st_mtime)}\n".encode("utf-8"))
    return h.hexdigest()


# -- SD target ---------------------------------------------------------------------------------------

def normalize_target(path: Path) -> Path:
    """Accept the SD root, its 3ds folder, or the bb_mods folder itself."""
    if path.name.lower() == "bb_mods":
        return path
    if path.name.lower() == "3ds":
        return path / "bb_mods"
    if (path / "3ds").is_dir() or (path / "Nintendo 3DS").is_dir() or path.parent == path:
        return path / "3ds" / "bb_mods"
    return path


def find_targets() -> list[tuple[str, Path]]:
    found = []
    if os.name == "nt":
        for letter in string.ascii_uppercase:
            root = Path(f"{letter}:/")
            try:
                if (root / "Nintendo 3DS").is_dir():
                    found.append((f"3DS SD card ({letter}:)", root / "3ds" / "bb_mods"))
            except OSError:
                pass
    if AZAHAR_SD.is_dir():
        found.append(("Azahar emulator", AZAHAR_SD / "3ds" / "bb_mods"))
    return found


def choose_target(explicit: Path | None, log) -> Path:
    if explicit is not None:
        return normalize_target(explicit)
    found = find_targets()
    if not found:
        raise SystemExit("No 3DS SD card (a drive with a 'Nintendo 3DS' folder) or Azahar found. "
                         "Insert the SD card, or pass --sd <SD card or folder>.")
    if len(found) == 1:
        log(f"Installing to {found[0][0]}: {found[0][1]}")
        return found[0][1]
    if not sys.stdin or not sys.stdin.isatty():
        raise SystemExit("Several targets found (" + ", ".join(n for n, _ in found) + "); pass --sd to pick one.")
    print("Where should the mods go?")
    for i, (name, path) in enumerate(found, 1):
        print(f"  {i}. {name}   {path}")
    while True:
        ans = input(f"Type 1-{len(found)} and press Enter: ").strip()
        if ans.isdigit() and 1 <= int(ans) <= len(found):
            return found[int(ans) - 1][1]


# -- install -----------------------------------------------------------------------------------------

def merge_index(target: Path, new_rows: list[str]) -> list[str]:
    """Merge rows into <target>/stages/index.txt, keeping other installs; rows whose folder is gone are dropped.

    level_index drives unlocking (bb_save_level_unlocked: level k opens once k-1 is beaten), so the levels actually
    on the card are numbered 0..n-1 per pack, in the pack's own order. That order (the level's position in the
    whole pack) is kept in stages/.mod2ds_order.json, because skipped levels leave gaps the index must not show."""
    idx = target / "stages" / "index.txt"
    order_file = target / "stages" / ORDER_FILE
    order: dict[str, int] = json.loads(order_file.read_text(encoding="utf-8")) if order_file.is_file() else {}
    rows: dict[str, str] = {}
    if idx.is_file():
        for line in idx.read_text(encoding="utf-8").splitlines():
            if line.strip():
                rows[line.split("|", 1)[0]] = line
    for r in new_rows:
        sid = r.split("|", 1)[0]
        rows[sid] = r
        order[sid] = int(r.split("|")[6])
    keep = [r for sid, r in rows.items() if (target / "stages" / sid).is_dir()]
    keep.sort(key=lambda r: (r.split("|")[3], order.get(r.split("|")[0], int(r.split("|")[6])), r.split("|")[0]))
    per_pack: dict[str, int] = {}
    for i, r in enumerate(keep):
        f = r.split("|")
        f[6] = str(per_pack.get(f[3], 0))
        per_pack[f[3]] = int(f[6]) + 1
        keep[i] = "|".join(f)
    idx.write_text("".join(r + "\n" for r in keep), encoding="utf-8")
    convert.write_stage_rows(target / "stages", keep)
    order_file.write_text(json.dumps({r.split("|")[0]: order.get(r.split("|")[0], 0) for r in keep}, indent=1), encoding="utf-8")
    return keep


def run(inputs: list[Path], *, sd: Path | None, options: convert.Options, include_over_limit: bool = False,
        log=print, package: bool = False) -> int:
    """package: sd is a fresh package folder (oneshot.py) rather than a card; only the summary wording changes."""
    library = options.out
    library.mkdir(parents=True, exist_ok=True)
    target = choose_target(sd, log)
    pack_dirs = collect_packs(inputs, library, log)
    if not pack_dirs:
        log("Nothing to install.")
        return 1

    installed, skipped, problems = [], [], []
    rows: list[str] = []
    for pdir in pack_dirs:
        pack = discover.load_pack(pdir)
        if pack.kind == "legacy":   # rewritten the way the game's LegacyConverter does it, then converted like any act
            try:
                pdir = legacy.to_act(pdir, library / "_legacy")
                pack = discover.load_pack(pdir)
                log(f"\n== {pdir.name}: pre-release (Legacy) mod, converted with the game's Legacy rules")
            except (ValueError, OSError, legacy.godot_cfg.CfgError) as exc:
                problems.append((pdir.name, f"Legacy mod could not be converted: {exc}"))
                log(f"\n== {pdir.name}: Legacy mod could not be converted ({exc})")
                continue
        if pack.kind != "act":
            problems.append((pdir.name, "; ".join(pack.problems)))
            log(f"\n== {pdir.name}: not convertible ({'; '.join(pack.problems)})")
            continue
        pid = convert.names.pack_id(pack.name, str(pack.act.get("act_id", pack.path.name)))
        # cheap pre-check from the configs (no media work): only levels that can run get converted
        wanted = []
        for ref in pack.levels:
            label = f"{pack.name} / {ref.meta.get('level_name') or ref.folder}"
            try:
                lv = build_level(ref, pack, pid, options.game_dir, options.framing)
            except LevelError as exc:
                problems.append((label, str(exc)))
                continue
            fit = {n: min(sh.total, limits.MAX_FRAMES_PER_SHEET) for n, sh in lv.sheets.items()} if options.fit_runtime else None
            check_runtime_limits(lv, fit)
            over = [i.message for i in lv.issues.items if i.severity == "limit"]
            if over and not include_over_limit:
                skipped.append((label, over))
            else:
                wanted.append(ref.folder)
        if not wanted:
            log(f"\n== {pdir.name}: nothing in this pack can run on the 3DS yet")
            continue
        sig = source_signature(pdir)
        manifest_path = library / "packs" / f"{pid}.json"
        manifest = json.loads(manifest_path.read_text(encoding="utf-8")) if manifest_path.is_file() else None
        have = {s["folder"]: s for s in (manifest or {}).get("stages", [])}
        if manifest and manifest.get("source_signature") == sig and same_options(manifest.get("options"), options,
                                                    any((library / "stages" / s["stage_id"] / "video").is_dir()
                                                        for s in manifest["stages"] if s.get("stage_id")),
                                                    any(cutscene.has_cutscene(r.path) for r in pack.levels)) and \
                all(f in have and have[f].get("stage_id") and (library / "stages" / have[f]["stage_id"]).is_dir() for f in wanted):
            log(f"\n== {pdir.name}: already converted, using the library copy")
        else:
            prev_filter = options.level_filter
            options.level_filter = wanted
            try:
                manifest = convert.convert_pack(pack, options)
            finally:
                options.level_filter = prev_filter
            manifest["source_signature"] = sig
            manifest["options"] = _opts_sig(options)
            manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        for s in manifest["stages"]:
            if s["folder"] not in wanted:
                continue
            label = f"{pack.name} / {s.get('title') or s.get('level_name') or s['folder']}"
            if s["status"] == "error":
                problems.append((label, s.get("error", "")))
                continue
            if s["status"] != "ok" and not include_over_limit:   # limits only visible after conversion (video length, bank size)
                why = [i["message"] for i in s.get("issues", []) if i["severity"] == "limit"]
                skipped.append((label, why))
                continue
            src = library / "stages" / s["stage_id"]
            dst = target / "stages" / s["stage_id"]
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(src, dst, ignore=shutil.ignore_patterns("level.json", "thumb.png"))
            installed.append(label)
        rows += [r for r in manifest.get("index_lines", [])
                 if any(r.startswith(s["stage_id"] + "|") and (target / "stages" / s["stage_id"]).is_dir()
                        for s in manifest["stages"] if s.get("stage_id"))]

    all_rows = merge_index(target, rows)
    convert.write_index(library)
    report.write(library)

    log("\n" + "=" * 70)
    log(f"{'Packaged' if package else 'Installed'} {len(installed)} level(s) to {target}")
    for name in installed:
        log(f"  + {name}")
    if skipped:
        log(f"\nSkipped {len(skipped)} level(s) that the 3DS game cannot run yet (over its limits):")
        for name, why in skipped:
            log(f"  - {name}: {'; '.join(why[:3])}{' ...' if len(why) > 3 else ''}")
    if problems:
        log(f"\nCould not convert {len(problems)}:")
        for name, why in problems:
            log(f"  x {name}: {why}")
    if package:
        return 0 if installed else 1
    log(f"\nThe SD card now lists {len(all_rows)} mod level(s).")
    if len(all_rows) > limits.MAX_STAGES:
        log(f"  ! The game lists at most {limits.MAX_STAGES} mod levels; the rest will not show up. Remove some from the SD card.")
    packs = {r.split("|")[3] for r in all_rows}
    if len(packs) > limits.MAX_PROGRESS_PACKS:
        log(f"  ! {len(packs)} mod packs: the game records level unlocks for {limits.MAX_PROGRESS_PACKS} packs; in the others "
            f"only the first level opens.")
    return 0 if installed or not (skipped or problems) else 1


# options added after a pack was converted count as their default, so a new setting does not force re-converting the library
# what conversions made before these settings existed used
OPTION_DEFAULTS = {"video_fps": 30.0, "video_res": "full", "cutscenes": False}
VIDEO_OPTIONS = ("video_fps", "video_res")


def same_options(saved: dict[str, Any] | None, o: convert.Options, has_video: bool = True, has_cutscene: bool = True) -> bool:
    """Video and cutscene settings only matter for packs that have them, so a new setting does not re-convert everything."""
    if saved is None:
        return False
    a, b = {**OPTION_DEFAULTS, **saved}, _opts_sig(o)
    drop = (VIDEO_OPTIONS if not has_video else ()) + (("cutscenes",) if not has_cutscene else ())
    return {k: v for k, v in a.items() if k not in drop} == {k: v for k, v in b.items() if k not in drop}


def _opts_sig(o: convert.Options) -> dict[str, Any]:
    return {"fit_runtime": o.fit_runtime, "framing": o.framing, "frame_width": o.frame_width,
            "sprite_format": o.sprite_format, "quality": o.quality, "video_fps": o.video_fps, "video_res": o.video_res, "cutscenes": "cutscene" in o.steps,
            "tool": convert.__version__}
