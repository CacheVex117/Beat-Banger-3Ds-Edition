"""mod2ds command line.

    python -m mod2ds scan                              every installed mod: kind, levels, convertibility (no media work)
    python -m mod2ds inspect "Kat's Audition"          one pack in detail, every issue per level (no media work)
    python -m mod2ds convert "Kat's Audition" --out out --game-dir "C:/Beat Banger"
    python -m mod2ds convert --all --out out --fit-runtime
    python -m mod2ds verify out                       read the output back with the runtime loaders' rules

Mod folders are only ever read. Outputs go to --out (see SPEC.md for the layout).
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from . import __version__, convert, discover, limits, paths, report
from .model import LevelError, build_level, check_runtime_limits

DEFAULT_MODS = Path(os.environ.get("APPDATA", "~")).expanduser() / "Godot" / "app_userdata" / "Beat Banger" / "mods"
DEFAULT_GAME = Path("C:/Beat Banger")


def _packs(args) -> list[discover.ModPack]:
    if getattr(args, "all", False) or not args.packs:
        return discover.scan(args.mods)
    return [discover.load_pack(discover.resolve_pack_arg(Path(p), args.mods)) for p in args.packs]


def _assert_outside(out: Path, sources: list[Path]) -> None:
    o = out.resolve()
    for s in sources:
        s = s.resolve()
        if o == s or s in o.parents or o in s.parents:
            raise SystemExit(f"--out {out} overlaps the read-only source {s}")


def _game_dir(args) -> Path | None:
    if args.no_game_dir:
        return None
    if args.game_dir:
        return args.game_dir
    for cand in (DEFAULT_GAME, *paths.steam_game_dirs()):
        if (cand / "data" / "_common").is_dir():
            return cand
    return None


def _ask_game_dir() -> Path | None:
    """oneshot (MOD2DS.exe) without a known install: ask once; Enter skips (mods that borrow the game's shared clips
    then miss those sounds)."""
    print("Some mods use sounds from the game itself. Drag your Beat Banger folder (the one with beatbanger.exe) here and")
    print("press Enter, or just press Enter to skip:")
    while True:
        raw = input("> ").strip().strip('"')
        if not raw:
            return None
        p = Path(raw)
        if p.is_file():
            p = p.parent
        if (p / "data" / "_common").is_dir():
            return p
        print("No data/_common folder there. Try again, or press Enter to skip.")


def cmd_scan(args) -> int:
    packs = _packs(args)
    game = _game_dir(args)
    totals = {"ready": 0, "over": 0, "error": 0}
    for p in packs:
        print(f"{p.path.name}  [{p.kind}]  {len(p.levels)} level(s)")
        for prob in p.problems:
            print(f"    ! {prob}")
        for ref in p.levels:
            try:
                lv = build_level(ref, p, "scan", game, args.framing)
            except LevelError as exc:
                totals["error"] += 1
                print(f"    x {ref.folder}: {exc}")
                continue
            check_runtime_limits(lv, {n: min(s.total, limits.MAX_FRAMES_PER_SHEET) for n, s in lv.sheets.items()} if args.fit_runtime else None)
            lim = [i for i in lv.issues.items if i.severity == "limit"]
            uns = sorted({i.code for i in lv.issues.items if i.severity == "unsupported"})
            totals["over" if lim else "ready"] += 1
            mark = "~" if lim else "+"
            print(f"    {mark} {ref.folder} -> {lv.stage_id}  charts={len(lv.charts)} sheets={len(lv.sheets)} clips={len(lv.clips)}"
                  f"{'  limits: ' + '; '.join(i.message for i in lim) if lim else ''}"
                  f"{'  unsupported: ' + ', '.join(uns) if uns else ''}")
    print(f"\n{len(packs)} folder(s): {totals['ready']} level(s) within runtime limits, {totals['over']} over limits, "
          f"{totals['error']} not convertible")
    return 0


def cmd_inspect(args) -> int:
    game = _game_dir(args)
    for p in _packs(args):
        print(f"== {p.path.name}  [{p.kind}]  act={p.act.get('act_name')!r}")
        for prob in p.problems:
            print(f"   ! {prob}")
        for ref in p.levels:
            print(f"  - {ref.folder}")
            try:
                lv = build_level(ref, p, "inspect", game, args.framing)
            except LevelError as exc:
                print(f"      ERROR {exc}")
                continue
            check_runtime_limits(lv)
            print(f"      stage_id {lv.stage_id}  title {lv.title!r}  character {lv.character!r}  "
                  f"framing {lv.framing} (canvas x{lv.canvas_k:g})")
            print(f"      charts: " + ", ".join(f"{c['name']!r}({len(c['notes'])})" for c in lv.charts)
                  + "  slots: " + ", ".join(f"{s}->{i}" for s, i in lv.slots.items()))
            print(f"      loops {len(lv.loops)}  sheets {len(lv.sheets)}  camera {len(lv.cameras)}  shutters {len(lv.shutters_us)}  "
                  f"clips {len(lv.clips)}  sound_loop {len(lv.sound_loops)}  oneshot {len(lv.oneshots)}  voice {len(lv.voice_banks)}  "
                  f"video {len(lv.videos)}")
            for sh in lv.sheets.values():
                print(f"        {sh.name:32s} {sh.h}x{sh.v} total {sh.total:3d}  frame {sh.frame_px[0]}x{sh.frame_px[1]}"
                      f"{f' scale {sh.scale:g}' if sh.scale != 1 else ''}{f' offset {sh.offset}' if any(sh.offset) else ''}"
                      f"  <- {sh.source.name}")
            for i in lv.issues.as_list():
                n = f" (x{i['count']})" if i["count"] > 1 else ""
                print(f"      [{i['severity']}] {i['message']}{n}")
    return 0


def cmd_convert(args) -> int:
    packs = _packs(args)
    _assert_outside(args.out, [args.mods] + [p.path for p in packs] + ([args.game_dir] if args.game_dir else []))
    from . import media
    try:
        tools = media.Tools.find(args.ffmpeg, args.ffprobe, args.tex3ds)
    except media.ToolError as exc:
        raise SystemExit(str(exc))
    steps = tuple(s for s in args.steps.split(",") if s)
    bad = [s for s in steps if s not in convert.STEPS]
    if bad:
        raise SystemExit(f"unknown step(s) {bad}; choose from {','.join(convert.STEPS)}")
    o = convert.Options(out=args.out, steps=steps, game_dir=_game_dir(args), tools=tools, frame_width=args.frame_width,
                        sprite_format=args.sprite_format, quality=args.quality, fit_runtime=args.fit_runtime,
                        keep_oversize=args.keep_oversize, level_filter=args.level or [], framing=args.framing,
                        video_fps=args.video_fps, video_res=args.video_res)
    args.out.mkdir(parents=True, exist_ok=True)
    skipped = []
    for p in packs:
        convert.convert_pack(p, o)
        if p.kind != "act":
            skipped.append({"folder": p.path.name, "kind": p.kind, "problems": p.problems})
    rows = convert.write_index(args.out)
    rep = report.write(args.out, skipped)
    print(f"\n[OK] {len(rows)} stage(s) in {args.out / 'stages' / 'index.txt'}; report {rep}")
    if len(rows) > limits.MAX_STAGES:
        print(f"[!] {len(rows)} stages: the runtime lists at most {limits.MAX_STAGES} mod levels")
    return 0


def cmd_oneshot(args) -> int:
    from . import media, oneshot
    if not args.inputs:
        if not (sys.stdin and sys.stdin.isatty()):
            raise SystemExit("oneshot: give a mod folder or .zip")
        print("Drag a Beat Banger mod folder or .zip onto this window, then press Enter:")
        raw = input("> ").strip().strip('"')
        if not raw:
            return 1
        args.inputs = [raw]
    preset = args.preset or (oneshot.ask_preset() if sys.stdin and sys.stdin.isatty() else "standard")
    try:
        tools = media.Tools.find(args.ffmpeg, args.ffprobe, args.tex3ds)
    except media.ToolError as exc:
        raise SystemExit(str(exc))
    game = _game_dir(args)
    if game is None and not args.no_game_dir and sys.stdin and sys.stdin.isatty():
        game = _ask_game_dir()
    o = convert.Options(out=args.library, game_dir=game, tools=tools, framing=args.framing)
    return oneshot.run([Path(p) for p in args.inputs], preset=preset, out_root=args.out, options=o)


def cmd_install(args) -> int:
    from . import install, media
    try:
        tools = media.Tools.find(args.ffmpeg, args.ffprobe, args.tex3ds)
    except media.ToolError as exc:
        raise SystemExit(str(exc))
    o = convert.Options(out=args.library, game_dir=_game_dir(args), tools=tools, fit_runtime=not args.no_fit,
                        framing=args.framing)
    return install.run([Path(p) for p in args.inputs], sd=args.sd, options=o, include_over_limit=args.include_over_limit)


def cmd_verify(args) -> int:
    from . import verify
    results = verify.verify(args.out, args.sd_root)
    for sid, errs in results.items():
        print(f"{'OK ' if not errs else 'BAD'} {sid}")
        for e in errs:
            print(f"      {e}")
    bad = sum(1 for e in results.values() if e)
    print(f"\n{len(results)} stage(s), {bad} with problems")
    return 1 if bad else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="mod2ds", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", action="version", version=f"mod2ds {__version__}")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(sp, packs_help: str):
        sp.add_argument("packs", nargs="*", help=packs_help)
        sp.add_argument("--mods", type=Path, default=DEFAULT_MODS, help=f"mods root (default {DEFAULT_MODS})")
        sp.add_argument("--game-dir", type=Path, help="Beat Banger install, read-only (shared sounds, data/_common/audio); "
                                                     f"default {DEFAULT_GAME} when present")
        sp.add_argument("--no-game-dir", action="store_true", help="do not use any game install files")
        sp.add_argument("--framing", default="auto", choices=["auto", "game", "legacy"],
                        help="sprite placement: game = current game (true frame size), legacy = Godot 3 rules "
                             "(scale_multiplier/position_offset); auto = legacy when a level carries those fields")
        sp.add_argument("--fit-runtime", action="store_true",
                        help=f"decimate sprite sheets to {limits.MAX_FRAMES_PER_SHEET} frames (runtime limit; timing kept)")

    sp = sub.add_parser("scan", help="summarize every mod pack (no media conversion)")
    common(sp, "pack folders or names (default: all)")
    sp.set_defaults(func=cmd_scan)
    sp = sub.add_parser("inspect", help="detailed per-level report (no media conversion)")
    common(sp, "pack folders or names (default: all)")
    sp.set_defaults(func=cmd_inspect)
    sp = sub.add_parser("convert", help="convert packs into a MOD2DS package")
    common(sp, "pack folders or names")
    sp.add_argument("--all", action="store_true", help="convert every pack under --mods")
    sp.add_argument("--out", type=Path, required=True, help="output folder (copy it to sdmc:/3ds/bb_mods/)")
    sp.add_argument("--level", action="append", help="only this level folder / level_name (repeatable)")
    sp.add_argument("--steps", default=",".join(convert.STEPS), help=f"comma list from {','.join(convert.STEPS)}")
    sp.add_argument("--frame-width", type=int, default=512)
    sp.add_argument("--sprite-format", default="etc1a4", choices=["etc1a4", "rgba4", "rgba5551", "rgba8"])
    sp.add_argument("--quality", default="medium", choices=["low", "medium", "high"])
    sp.add_argument("--video-fps", type=float, default=30.0, help="cap video frame rate (0 = keep the source rate)")
    sp.add_argument("--video-res", default="auto", choices=["auto", "full", "half"],
                    help="video page layout: full 400x240, half 200x120 (a quarter of the SD reads); auto = half for "
                         "levels with more than 20 s of video")
    sp.add_argument("--keep-oversize", action="store_true",
                    help="also build videos the runtime cannot play (over the page or video-count limit)")
    sp.add_argument("--ffmpeg")
    sp.add_argument("--ffprobe")
    sp.add_argument("--tex3ds")
    sp.set_defaults(func=cmd_convert)
    sp = sub.add_parser("install", help="convert mod folders / .zip files and copy them onto a 3DS SD card (drag and drop)")
    sp.add_argument("inputs", nargs="+", help="mod pack folders, bbmods .zip files, or a whole mods folder")
    sp.add_argument("--sd", type=Path, help="SD card root, its 3ds folder, or a bb_mods folder (default: detect)")
    sp.add_argument("--library", type=Path, default=paths.library_dir(), help="where converted packs are cached")
    sp.add_argument("--include-over-limit", action="store_true", help="also install levels over the runtime limits")
    sp.add_argument("--no-fit", action="store_true", help="do not fit sprite sheets to the runtime limits")
    sp.add_argument("--game-dir", type=Path)
    sp.add_argument("--no-game-dir", action="store_true")
    sp.add_argument("--framing", default="auto", choices=["auto", "game", "legacy"])
    sp.add_argument("--ffmpeg")
    sp.add_argument("--ffprobe")
    sp.add_argument("--tex3ds")
    sp.set_defaults(func=cmd_install)
    sp = sub.add_parser("oneshot", help="one mod in, one SD-ready 3DS package out (MOD2DS.exe)")
    sp.add_argument("inputs", nargs="*", help="a mod pack folder or bbmods .zip (asked for when omitted)")
    sp.add_argument("--preset", choices=["standard", "larger"],
                    help="standard: most mods; larger: stronger downscaling for heavy video or animation mods (asked when omitted)")
    sp.add_argument("--out", type=Path, default=paths.output_dir(), help="where the package folder is written")
    sp.add_argument("--library", type=Path, default=paths.library_dir(), help="where converted packs are cached")
    sp.add_argument("--game-dir", type=Path)
    sp.add_argument("--no-game-dir", action="store_true")
    sp.add_argument("--framing", default="auto", choices=["auto", "game", "legacy"])
    sp.add_argument("--ffmpeg")
    sp.add_argument("--ffprobe")
    sp.add_argument("--tex3ds")
    sp.set_defaults(func=cmd_oneshot)
    sp = sub.add_parser("verify", help="read an output folder back with the runtime's acceptance rules")
    sp.add_argument("out", type=Path)
    sp.add_argument("--sd-root", default="sdmc:/3ds/bb_mods", help="where the folder will live on the SD card")
    sp.set_defaults(func=cmd_verify)

    args = ap.parse_args(argv)
    if args.cmd == "convert" and not args.packs and not args.all:
        ap.error("convert: name one or more packs, or pass --all")
    try:
        return args.func(args)
    except FileNotFoundError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
