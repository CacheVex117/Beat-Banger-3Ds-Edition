"""Read one mod level into a normalized, runtime-neutral model (the MOD2DS level IR).

Every rule here follows the Godot 4.7 game (C:/Beat Banger/src), not the editor:
  Config.store_config_data       asset/keyframes/meta/notes/settings must load; mod.cfg/toys.cfg optional
  Config.round_note_timestamps   note + hold-end timestamps snapped to 0.001 s
  Config.normalize_loop_variations   bare-string variations get weight 50 (DEFAULT_VARIATION_WEIGHT)
  TimeStampEvent (timestamp_component.gd)  the active keyframe is items[count(ts < now) - 1]: list order,
                                 not timestamp order, picks the payload (see game_order)
  Difficulty.get_current_chart   difficulty slot i plays charts[i], or charts[0] when i >= len(charts)
  AssetManager.get_sheet_geometry  a sheet used by several loops is split with the LAST loop's sheet_data
  game_character_root            camera = position, scale, speed, interpolation (default TRANS_EXPO)
Fields the game reads but the 3DS formats cannot carry are kept in the IR and reported "unsupported";
fields the game never reads are reported "ignored".
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from decimal import ROUND_HALF_UP, Decimal
from pathlib import Path
from typing import Any

from PIL import Image

Image.MAX_IMAGE_PIXELS = None   # mod sheets are routinely > 89 Mpx

from . import godot_cfg, limits, names
from .discover import LevelRef, ModPack

REQUIRED_CONFIGS = ("asset", "keyframes", "meta", "notes", "settings")
OPTIONAL_CONFIGS = ("mod", "toys")
TRANS_EXPO = 5
DEFAULT_VARIATION_WEIGHT = 50.0
SEVERITIES = ("error", "limit", "unsupported", "warning", "ignored")


# BBVD start_us / hide_us markers for the asset.cfg outro (native3ds video.c: start = the chart's last note + 0.3 s)
OUTRO_START = -(1 << 63)
NO_PICTURE = -(1 << 63)

class LevelError(Exception):
    """The game itself could not play this level (missing/unloadable required config, no song...)."""


@dataclass
class Issue:
    severity: str
    code: str
    message: str
    count: int = 1

    def as_dict(self) -> dict[str, Any]:
        return {"severity": self.severity, "code": self.code, "message": self.message, "count": self.count}


class Issues:
    def __init__(self) -> None:
        self.items: list[Issue] = []
        self._by_key: dict[tuple[str, str, str], Issue] = {}

    def add(self, severity: str, code: str, message: str) -> None:
        assert severity in SEVERITIES, severity
        key = (severity, code, message)
        if key in self._by_key:
            self._by_key[key].count += 1
            return
        issue = Issue(severity, code, message)
        self._by_key[key] = issue
        self.items.append(issue)

    def has(self, severity: str) -> bool:
        return any(i.severity == severity for i in self.items)

    def as_list(self) -> list[dict[str, Any]]:
        order = {s: n for n, s in enumerate(SEVERITIES)}
        return [i.as_dict() for i in sorted(self.items, key=lambda i: order[i.severity])]


# -- small helpers -----------------------------------------------------------------------------------

def sec_to_us(seconds: Any) -> int:
    return int((Decimal(str(float(seconds))) * 1_000_000).quantize(Decimal(1), rounding=ROUND_HALF_UP))


def snapped_ms(seconds: float) -> float:
    """Godot snappedf(x, 0.001) = floor(x / 0.001 + 0.5) * 0.001."""
    return math.floor(seconds / 0.001 + 0.5) * 0.001


def num(v: Any, default: float | None = None) -> float | None:
    if isinstance(v, bool):
        return float(v)
    if isinstance(v, (int, float)) and math.isfinite(v):
        return float(v)
    if isinstance(v, str):
        try:
            f = float(v)
            return f if math.isfinite(f) else default
        except ValueError:
            return default
    return default


def game_order(entries: list[dict[str, Any]], issues: Issues, what: str) -> list[dict[str, Any]]:
    """Re-time a keyframe list so a sorted reader sees what TimeStampEvent shows.

    The game shows items[k-1] once k items have timestamp < now. For a sorted list that is the usual
    "latest keyframe"; for an unsorted list the k-th smallest timestamp switches to the k-th item in
    list order. Returning (sorted timestamps zipped with payloads in list order) reproduces that exactly.
    """
    times = [e["_t"] for e in entries]
    if times == sorted(times):
        return entries
    issues.add("warning", "unsorted_keyframes",
               f"keyframes.{what} is not in time order; re-timed to match the game's index-based lookup")
    out = []
    for t, e in zip(sorted(times), entries):
        e = dict(e)
        e["_t"] = t
        out.append(e)
    return out


class DirIndex:
    """Case-insensitive file lookup inside one folder (Windows-authored mods mix case freely)."""

    def __init__(self, folder: Path):
        self.folder = folder
        self.files: dict[str, Path] = {}
        if folder.is_dir():
            for p in folder.iterdir():
                if p.is_file():
                    self.files.setdefault(p.name.lower(), p)

    def find(self, name: str) -> Path | None:
        return self.files.get(Path(name).name.lower())


# -- the IR ------------------------------------------------------------------------------------------

@dataclass
class Sheet:
    name: str              # canonical char[32] name used by BBAN sheets and BBCH loop names
    source: Path
    h: int
    v: int
    total: int
    frame_px: tuple[int, int] = (1280, 720)   # source frame size (image size // grid)
    scale: float = 1.0                         # legacy framing: scale_multiplier
    offset: tuple[float, float] = (0.0, 0.0)   # legacy framing: position_offset, in character-space units


@dataclass
class Clip:
    name: str
    source: Path
    origin: str            # "level" | "game_common"


@dataclass
class Level:
    stage_id: str
    pack_id: str
    ref: LevelRef
    title: str
    character: str
    level_index: int
    display: dict[str, Any]
    song: Path | None
    song_offset_us: int
    note_offset_us: int
    post_song_delay_s: float
    bpm_changes: list[dict[str, int]]
    shutters_us: list[int]
    loops: list[dict[str, Any]]                  # {timestamp_us, sheet, variations:[{sheet, weight}]}
    sheets: dict[str, Sheet]                     # canonical name -> sheet
    cameras: list[dict[str, Any]]
    clips: dict[str, Clip]                       # source file name (lower) -> clip
    sound_loops: list[dict[str, Any]]
    oneshots: list[dict[str, Any]]
    voice_banks: list[dict[str, Any]]
    videos: list[dict[str, Any]]                 # {start_us, hide_us, source}
    charts: list[dict[str, Any]]                 # every authored chart, normalized
    slots: dict[str, int]                        # difficulty slot -> chart index (Difficulty.get_current_chart)
    game_over: Path | None
    thumb: Path | None
    splash: Path | None
    extras: dict[str, Any]                       # game-used data the 3DS formats cannot carry yet
    framing: str = "game"                        # "game" (Godot 4.7) or "legacy" (Godot 3 character.gd)
    canvas_k: float = 1.0                        # sheet canvas = 1280k x 720k character units (camera scales x k)
    issues: Issues = field(default_factory=Issues)


# -- builder -----------------------------------------------------------------------------------------

def _load_configs(level_dir: Path, issues: Issues) -> dict[str, dict[str, Any]]:
    cfg: dict[str, dict[str, Any]] = {}
    for name in REQUIRED_CONFIGS + OPTIONAL_CONFIGS:
        path = level_dir / "config" / f"{name}.cfg"
        data: Any = {}
        if path.is_file():
            try:
                data = godot_cfg.load_data(path, default={})
            except godot_cfg.CfgError as exc:
                if name in REQUIRED_CONFIGS:
                    raise LevelError(f"{name}.cfg does not load, the game refuses this level: {exc}") from exc
                issues.add("warning", "config_unloadable", f"{name}.cfg does not load and is ignored, as in the game: {exc}")
                data = {}
        if not isinstance(data, dict):
            data = {}
        if name in REQUIRED_CONFIGS and not data:
            raise LevelError(f"{name}.cfg is missing or empty, the game refuses this level (Config.store_config_data)")
        cfg[name] = data
    return cfg


def _timed(raw: Any, what: str, issues: Issues) -> list[dict[str, Any]]:
    """Dict keyframes with a usable timestamp, tagged with '_t' (seconds) and re-timed per game_order."""
    if raw is None:
        return []
    if not isinstance(raw, list):
        issues.add("warning", "bad_keyframes", f"keyframes.{what} is not a list; ignored")
        return []
    out = []
    for e in raw:
        if not isinstance(e, dict):
            issues.add("warning", "bad_keyframe", f"keyframes.{what}: non-dictionary entry skipped")
            continue
        t = num(e.get("timestamp", 0.0))
        if t is None:
            issues.add("warning", "bad_keyframe", f"keyframes.{what}: entry with a non-numeric timestamp skipped")
            continue
        e = dict(e)
        e["_t"] = t
        out.append(e)
    return game_order(out, issues, what)


def _bpm_changes(modifiers: Any, issues: Issues, fallback_bpm: Any = None) -> tuple[list[dict[str, int]], list[dict[str, Any]]]:
    """Same tempo-map math as native convert_stage.normalize_modifiers (timestamps are beat positions)."""
    if not isinstance(modifiers, list) or not modifiers or not isinstance(modifiers[0], dict):
        modifiers = [{}]
    if num(modifiers[0].get("bpm")) is None:
        # GameManager reads modifiers[0]["bpm"] and errors without it (notes still play: they are in seconds).
        # BBCH needs a tempo, so take the closest authored one: a mistyped bpm key, then download.cfg info.bpm.
        stray = next((v for k, v in modifiers[0].items() if "bpm" in str(k).lower() and num(v)), None)
        bpm = num(stray) or num(fallback_bpm)
        if not bpm or bpm <= 0:
            raise LevelError("no tempo: keyframes.modifiers[0].bpm is missing and no other BPM is authored")
        issues.add("warning", "tempo_fallback", f"keyframes.modifiers[0].bpm missing; using {bpm:g} BPM from "
                   + ("a mistyped modifier key" if num(stray) else "download.cfg"))
        modifiers = [dict(modifiers[0], bpm=bpm)] + list(modifiers[1:])
    raw: list[dict[str, Any]] = []
    for i, m in enumerate(modifiers):
        bpm = num(m.get("bpm")) if isinstance(m, dict) else None
        beat = num(m.get("timestamp", 0.0)) if isinstance(m, dict) else None
        if bpm is None or bpm <= 0 or beat is None or beat < 0:
            issues.add("warning", "bad_tempo", f"keyframes.modifiers[{i}] has no usable bpm/timestamp; skipped")
            continue
        if raw and beat < raw[-1]["timestamp"]:
            issues.add("warning", "bad_tempo", f"keyframes.modifiers[{i}] is out of order; skipped")
            continue
        raw.append({"bpm": bpm, "timestamp": beat})
    if raw[0]["timestamp"] != 0:
        issues.add("warning", "bad_tempo", "first tempo modifier does not start at beat 0; treated as beat 0")
        raw[0]["timestamp"] = 0.0
    out, elapsed, prev = [], Decimal(0), None
    for m in raw:
        bpm, beat = Decimal(str(m["bpm"])), Decimal(str(m["timestamp"]))
        if prev is not None:
            elapsed += (beat - prev[1]) * Decimal(60_000_000) / prev[0]
        out.append({"timestamp_us": int(elapsed.quantize(Decimal(1), rounding=ROUND_HALF_UP)), "beat": int(beat),
                    "bpm_milli": int((bpm * 1000).quantize(Decimal(1), rounding=ROUND_HALF_UP))})
        prev = (bpm, beat)
    return out, raw


def _beat_to_us(beat: float, raw: list[dict[str, Any]]) -> int:
    active, prev_beat, elapsed = raw[0]["bpm"], 0.0, Decimal(0)
    for m in raw[1:]:
        if beat <= m["timestamp"]:
            break
        elapsed += Decimal(str(m["timestamp"] - prev_beat)) * 60 / Decimal(str(active))
        prev_beat, active = m["timestamp"], m["bpm"]
    elapsed += Decimal(str(beat - prev_beat)) * 60 / Decimal(str(active))
    return int((elapsed * 1_000_000).quantize(Decimal(1), rounding=ROUND_HALF_UP))


def _notes(chart: dict[str, Any], label: str, issues: Issues) -> list[dict[str, Any]]:
    raw = chart.get("notes", [])
    if not isinstance(raw, list):
        issues.add("warning", "bad_chart", f"chart {label}: 'notes' is not a list; treated as empty")
        raw = []
    notes = []
    for i, n in enumerate(raw):
        if not isinstance(n, dict):
            issues.add("warning", "bad_note", f"chart {label}: non-dictionary note skipped")
            continue
        t = num(n.get("timestamp"))
        it, mod = n.get("input_type", 0), n.get("note_modifier", 0)
        if t is None or not isinstance(it, int) or isinstance(it, bool) or not 0 <= it <= 3 \
                or not isinstance(mod, int) or isinstance(mod, bool) or not 0 <= mod <= 3:
            issues.add("warning", "bad_note", f"chart {label}: note {i} has an invalid timestamp/input_type/note_modifier; skipped")
            continue
        t = snapped_ms(t)                                   # Config.round_note_timestamps
        ts = sec_to_us(t)
        hold = 0
        if mod == 3:                                        # HoldNote: hold_end_timestamp defaults to timestamp
            end = num(n.get("hold_end_timestamp", t), t)
            hold = sec_to_us(snapped_ms(end)) - ts
            if hold <= 0:
                hold = 1                                    # BBCH: hold length is non-zero iff HOLD (native rule)
        if ts < -5000:   # chart time starts at -song_offset, so these play during the lead-in (BBCH times are signed)
            issues.add("ignored", "negative_note", f"chart {label}: notes before 0 s (lead-in); kept as authored")
        if n.get("ignore_animation"):
            issues.add("unsupported", "note_ignore_animation",
                       "notes with ignore_animation=true (character/voice/sound do not react); BBCH has no field, they will react on 3DS")
        if "horny" in n:
            issues.add("ignored", "note_horny", "note 'horny' data (pre-release horny mode; the current game ignores it)")
        notes.append({"timestamp_us": ts, "input_type": it, "modifier": mod,
                      "trigger_voice": 1 if n.get("trigger_voice", False) else 0, "hold_duration_us": hold})
    if any(a["timestamp_us"] > b["timestamp_us"] for a, b in zip(notes, notes[1:])):
        issues.add("warning", "unsorted_notes", f"chart {label}: notes not in time order; sorted (stable)")
        notes.sort(key=lambda x: x["timestamp_us"])
    return notes


CHAR_W, CHAR_H = 1280.0, 720.0   # the runtime's character rect (game_view.c), in character-space units


def framing_canvas(sheets, cameras: list[dict[str, Any]], whole_frames: bool = False) -> float:
    """Smallest k >= 1 so a (1280k x 720k) canvas holds every placed frame, capped at what any camera state can show.
    whole_frames: no cap (Legacy mods: the 3DS book layout shows the whole portrait frame, e.g. 600 x 800 > 720)."""
    sheets = list(sheets)
    need_x = max((abs(sh.offset[0]) + sh.frame_px[0] * sh.scale / 2 for sh in sheets), default=0.0)
    need_y = max((abs(sh.offset[1]) + sh.frame_px[1] * sh.scale / 2 for sh in sheets), default=0.0)
    vis_x, vis_y = (float("inf"), float("inf")) if whole_frames else (CHAR_W / 2, CHAR_H / 2)   # default camera: pos 0, scale 1
    for c in cameras:
        sc = c["scale"][0]
        if sc > 0:
            vis_x = max(vis_x, (CHAR_W / 2 + abs(c["pos"][0])) / sc)
            vis_y = max(vis_y, (CHAR_H / 2 + abs(c["pos"][1])) / sc)
    k = max(1.0, min(need_x, vis_x) / (CHAR_W / 2), min(need_y, vis_y) / (CHAR_H / 2))
    return math.ceil(k * 1000) / 1000


def build_level(ref: LevelRef, pack: ModPack, pack_id: str, game_dir: Path | None = None, framing: str = "auto") -> Level:
    issues = Issues()
    cfg = _load_configs(ref.path, issues)
    asset, kf, meta, notes_cfg, settings = (cfg[k] for k in REQUIRED_CONFIGS)
    mod = cfg["mod"]

    act_id = str(pack.act.get("act_id", pack.path.name))
    level_name = str(meta.get("level_name") or ref.folder)
    stage_id = names.stage_id(level_name, act_id, str(meta.get("level_id", "")), ref.folder)
    title = names.ascii_text(level_name, limits.TITLE_MAX) or stage_id
    character = names.ascii_text(str(meta.get("character") or level_name), limits.CHARACTER_MAX) or "Mod"
    if title != level_name:
        issues.add("warning", "title_ascii", f"title shortened/transliterated for the 3DS font: {level_name!r} -> {title!r}")

    audio, images, video_dir = DirIndex(ref.path / "audio"), DirIndex(ref.path / "images"), DirIndex(ref.path / "video")
    common = DirIndex(game_dir / "data" / "_common" / "audio") if game_dir else None

    # song (Level.get_song_stream: audio/<asset.song_path>)
    song_name = str(asset.get("song_path", ""))
    song = audio.find(song_name) if song_name else None
    if song is None:
        raise LevelError(f"song {song_name!r} not found in audio/ (asset.cfg song_path); the game cannot start this level")

    def setting_us(key: str) -> int:
        v = num(settings.get(key, 0.0), 0.0)
        return sec_to_us(v)

    bpm_changes, tempo_raw = _bpm_changes(kf.get("modifiers"), issues, pack.download.get("bpm"))

    # shutters: {timestamp: s} or [beat] (built-in style)
    shutters = []
    for i, e in enumerate(kf.get("shutter", []) or []):
        if isinstance(e, dict) and num(e.get("timestamp")) is not None:
            shutters.append(sec_to_us(num(e["timestamp"])))
        elif isinstance(e, list) and len(e) == 1 and num(e[0]) is not None:
            shutters.append(_beat_to_us(num(e[0]), tempo_raw))
        else:
            issues.add("warning", "bad_keyframe", f"keyframes.shutter[{i}] unreadable; skipped")
    if shutters != sorted(shutters):
        issues.add("warning", "unsorted_keyframes", "keyframes.shutter is not in time order; sorted (every shutter fires once)")
        shutters.sort()

    # loops + sheets
    loops_raw = _timed(kf.get("loops"), "loops", issues)
    geometry: dict[str, dict[str, int]] = {}
    for lp in loops_raw:   # AssetManager.get_sheet_geometry: last definition wins
        sd = lp.get("sheet_data")
        anims = lp.get("animations") if isinstance(lp.get("animations"), dict) else {}
        if not isinstance(sd, dict) or not sd:
            continue
        g = {k: int(num(sd.get(k, 1), 1)) for k in ("h", "v", "total")}
        files = [anims.get("normal")] + [v.get("path") if isinstance(v, dict) else v for v in anims.get("variations", []) or []]
        for f in files:
            if isinstance(f, str) and f:
                key = Path(f).name.lower()
                if key in geometry and geometry[key] != g:
                    issues.add("warning", "sheet_geometry_conflict",
                               f"sheet {Path(f).name!r} used with different sheet_data; the game splits it with the last one")
                geometry[key] = g

    # Framing. The runtime draws every frame as a 1280x720 rect of character space (game_view.c draw_character;
    # x CHAR_SCALE 1.5 = the 1920x1080 screen). The game draws a frame at its source pixel size, centered:
    #   game   (Godot 4.7 game_character.gd): size = frame px; loop scale_multiplier/position_offset are not read
    #   legacy (Godot 3 Backport character.gd, what mods carrying those fields were tuned for):
    #          size = frame px * scale_multiplier (sticky across loops, starts at 1.0),
    #          center = screen center + position_offset (screen px; / 1.5 in character units)
    # Each frame is composed onto a 16:9 canvas at that place, so the fixed rect never distorts it.
    legacy_params = any(k in lp for lp in loops_raw for k in ("scale_multiplier", "position_offset"))
    framing_mode = framing if framing in ("game", "legacy") else ("legacy" if legacy_params else "game")

    sheets: dict[str, Sheet] = {}
    by_key: dict[tuple, str | None] = {}

    def sheet_for(fname: Any, scale: float, offset: tuple[float, float]) -> str | None:
        if not isinstance(fname, str) or not fname:
            return None
        fkey = Path(fname).name.lower()
        key = (fkey, round(scale, 4), round(offset[0], 2), round(offset[1], 2))
        if key in by_key:
            return by_key[key]
        src = images.find(fname)
        g = geometry.get(fkey)
        if src is None:
            issues.add("warning", "missing_asset", f"sprite sheet {fname!r} not in images/ (the game shows nothing for it)")
            by_key[key] = None
            return None
        if g is None or g["h"] < 1 or g["v"] < 1 or g["total"] < 1:
            issues.add("warning", "bad_sheet_data", f"sprite sheet {fname!r} has no usable sheet_data; skipped")
            by_key[key] = None
            return None
        try:
            with Image.open(src) as im:
                frame_px = (max(1, im.width // g["h"]), max(1, im.height // g["v"]))
        except OSError as exc:
            issues.add("warning", "bad_image", f"sprite sheet {fname!r} cannot be read ({exc}); skipped")
            by_key[key] = None
            return None
        cname = names.asset_name("s", len(sheets), src.name)
        sheets[cname] = Sheet(cname, src, g["h"], g["v"], min(g["total"], g["h"] * g["v"]), frame_px, scale, offset)
        if g["total"] > g["h"] * g["v"]:
            issues.add("warning", "bad_sheet_data", f"sheet {fname!r}: total {g['total']} > h*v; clamped")
        by_key[key] = cname
        return cname

    loops = []
    sticky_scale = 1.0
    for lp in loops_raw:
        anims = lp.get("animations") if isinstance(lp.get("animations"), dict) else {}
        scale, offset = 1.0, (0.0, 0.0)
        if framing_mode == "legacy":
            if "scale_multiplier" in lp:
                sticky_scale = num(lp["scale_multiplier"], sticky_scale) or sticky_scale
            po = lp.get("position_offset")
            if isinstance(po, dict):
                offset = (float(num(po.get("x"), 0.0)) / 1.5, float(num(po.get("y"), 0.0)) / 1.5)
            scale = sticky_scale
            if num(lp.get("manual_speed_multiplier", 1.0), 1.0) != 1.0:
                issues.add("unsupported", "loop_manual_speed",
                           "loop manual_speed_multiplier (legacy animation speed; the 3DS runtime times loops by notes only)")
        else:
            for k in ("position_offset", "scale_multiplier", "manual_speed_multiplier"):
                if k in lp:
                    issues.add("ignored", f"loop_{k}", f"loop '{k}' (not read by the current game; --framing legacy uses it)")
        normal = sheet_for(anims.get("normal"), scale, offset)
        variations = []
        for v in anims.get("variations", []) or []:
            vpath = v.get("path") if isinstance(v, dict) else v
            weight = num(v.get("weight", DEFAULT_VARIATION_WEIGHT), DEFAULT_VARIATION_WEIGHT) if isinstance(v, dict) else DEFAULT_VARIATION_WEIGHT
            vs = sheet_for(vpath, scale, offset)
            if vs:
                variations.append({"sheet": vs, "weight": weight})
        if anims.get("horny"):
            issues.add("ignored", "loop_horny", "loop 'horny' sheets (pre-release horny mode; the current game ignores them)")
        loops.append({"timestamp_us": sec_to_us(lp["_t"]), "sheet": normal or "", "variations": variations,
                      "source": anims.get("normal")})

    cameras = []
    for c in _timed(kf.get("camera"), "camera", issues):
        pos, scl = c.get("position"), c.get("scale")
        if not (isinstance(pos, list) and len(pos) >= 2 and isinstance(scl, list) and len(scl) >= 2):
            issues.add("warning", "bad_keyframe", "keyframes.camera entry without position/scale skipped")
            continue
        interp = int(num(c.get("interpolation", TRANS_EXPO), TRANS_EXPO))
        if interp != TRANS_EXPO:
            issues.add("unsupported", "camera_interpolation",
                       f"camera interpolation {interp} (3DS runtime always eases EXPO-out)")
        for k in ("rotation", "skew", "ease", "trans"):
            if k in c:
                issues.add("ignored", f"camera_{k}", f"camera '{k}' (not read by game_character_root)")
        cameras.append({"timestamp_us": sec_to_us(c["_t"]), "pos": [float(num(pos[0], 0)), float(num(pos[1], 0))],
                        "scale": [float(num(scl[0], 1)), float(num(scl[1], 1))],
                        "speed": float(num(c.get("speed"), 0.0) or 0.0), "interpolation": interp})

    canvas_k = framing_canvas(sheets.values(), cameras, whole_frames=bool(pack.act.get("act_legacy")))
    if canvas_k > 1.0:
        # the canvas spans k x the runtime's rect, so every camera scale grows by k (positions are not scaled:
        # screen = center + 1.5 * (pos + scale * local)); a zero-length key before any chart time sets the start state
        for c in cameras:
            c["scale"] = [c["scale"][0] * canvas_k, c["scale"][1] * canvas_k]
        if not cameras or cameras[0]["timestamp_us"] > -3_600_000_000:
            cameras.insert(0, {"timestamp_us": -3_600_000_000, "pos": [0.0, 0.0], "scale": [canvas_k, canvas_k],
                               "speed": 0.0, "interpolation": TRANS_EXPO, "added": "canvas"})

    # sounds
    clips: dict[str, Clip] = {}

    def clip_for(fname: Any, what: str) -> str | None:
        if not isinstance(fname, str) or not fname:
            return None
        key = Path(fname).name.lower()
        if key in clips:
            return key
        src, origin = audio.find(fname), "level"
        if src is None and common is not None:
            src, origin = common.find(fname), "game_common"
        if src is None:
            issues.add("warning", "missing_asset", f"{what} clip {fname!r} not found (silent in the game too)")
            return None
        clips[key] = Clip(names.asset_name("c", len(clips), src.name), src, origin)
        if origin == "game_common":
            issues.add("warning", "common_audio", f"{what} clip {fname!r} taken from the game's data/_common/audio")
        return key

    sound_loops = []
    for e in _timed(kf.get("sound_loop"), "sound_loop", issues):
        paths = e.get("path") if isinstance(e.get("path"), list) else [e.get("path")]
        ids = [c for c in (clip_for(p, "sound_loop") for p in paths) if c]
        if num(e.get("volume_db", 0.0), 0.0) != 0.0:
            issues.add("unsupported", "sound_loop_volume", "sound_loop volume_db (BBSF has no gain field; plays at 0 dB)")
        if not ids:
            issues.add("warning", "empty_sound_loop", "sound_loop keyframe with no playable clip dropped")
            continue
        sound_loops.append({"timestamp_us": sec_to_us(e["_t"]), "clips": ids})
    oneshots = []
    for e in _timed(kf.get("sound_oneshot"), "sound_oneshot", issues):
        c = clip_for(e.get("path"), "sound_oneshot")
        if c:
            oneshots.append({"timestamp_us": sec_to_us(e["_t"]), "clip": c})
    voice_banks = []
    for e in _timed(kf.get("voice_bank"), "voice_bank", issues):
        vp = e.get("voice_paths", [])
        ids = [c for c in (clip_for(p, "voice_bank") for p in (vp if isinstance(vp, list) else [])) if c]
        if "frequency" in e:
            issues.add("ignored", "voice_frequency", "voice_bank 'frequency' (not read by game_voice)")
        voice_banks.append({"timestamp_us": sec_to_us(e["_t"]), "clips": ids})

    # videos (keyframes.video; a later loop keyframe hides the picture -- game_outro_video / native convert_video)
    loop_times = sorted(lp["timestamp_us"] for lp in loops)
    videos = []
    for e in _timed(kf.get("video"), "video", issues):
        src = video_dir.find(str(e.get("path", "")))
        if src is None:
            issues.add("warning", "missing_asset", f"video {e.get('path')!r} not in video/; skipped")
            continue
        start = sec_to_us(e["_t"])
        later = [t for t in loop_times if t > start]
        videos.append({"start_us": start, "hide_us": later[0] if later else 0x7FFFFFFFFFFFFFFF, "source": src})

    # asset.cfg final_video / final_audio (game_outro_video.gd): once the song passes the chart's last note + 0.3 s the game
    # plays final_video full screen with its own sound stripped (ffmpeg -an) and final_audio beside it, and passes the level
    # when the video and the song have ended. One more video whose soundtrack is final_audio; the runtime sets its start per
    # chart (OUTRO_START), and NO_PICTURE marks a final_audio without a video.
    fv_name, fa_name = str(asset.get("final_video", "") or ""), str(asset.get("final_audio", "") or "")
    fv = video_dir.find(fv_name) if fv_name else None
    fa = audio.find(fa_name) if fa_name else None
    if fv_name and fv is None:
        issues.add("warning", "missing_asset", f"final_video {fv_name!r} not in video/ (the game shows nothing either)")
    if fa_name and fa is None:
        issues.add("warning", "missing_asset", f"final_audio {fa_name!r} not in audio/ (the game plays nothing either)")
    if fv is not None or fa is not None:
        videos.append({"start_us": OUTRO_START, "hide_us": 0x7FFFFFFFFFFFFFFF if fv is not None else NO_PICTURE,
                       "source": fv, "audio_source": fa, "outro": True})

    # charts + difficulty slots
    charts_raw = notes_cfg.get("charts")
    if not isinstance(charts_raw, list) or not charts_raw:
        raise LevelError("notes.cfg has no 'charts' list; the game has nothing to play")
    charts = []
    for ci, c in enumerate(charts_raw):
        if not isinstance(c, dict):
            raise LevelError(f"notes.cfg charts[{ci}] is not a dictionary")
        label = str(c.get("name", f"#{ci}"))
        charts.append({"index": ci, "name": label, "name_ascii": names.ascii_text(label, 23) or f"chart{ci}",
                       "rating": int(num(c.get("rating", 0), 0)), "notes": _notes(c, label, issues)})
        if "speed" in c:
            issues.add("ignored", "chart_speed", "chart 'speed' (note speed comes from the player's setting)")
    slots = {slot: (i if i < len(charts) else 0) for i, slot in enumerate(limits.DIFFICULTY_SLOTS)}
    if len(charts) > len(limits.DIFFICULTY_SLOTS):
        issues.add("unsupported", "extra_charts",
                   f"{len(charts) - 3} chart(s) beyond the 3 difficulty slots ({', '.join(c['name'] for c in charts[3:])}); "
                   "converted but not selectable on 3DS")
    if len(charts) < 3:
        issues.add("warning", "chart_slots",
                   f"{len(charts)} chart(s): slots without a chart play chart 0, as Difficulty.get_current_chart does")

    # game-used data with no 3DS representation yet
    extras: dict[str, Any] = {}
    for key, why in (("background", "background layers"), ("effects", "effect overlays"), ("flags", "gameplay flags"),
                     ("toy", "toy intensity"), ("vibe_control", "vibration control")):
        val = kf.get(key)
        if isinstance(val, list) and val:
            extras[key] = val
            issues.add("unsupported", f"keyframes_{key}", f"keyframes.{key}: {len(val)} {why} (no 3DS format yet)")
    for key in ("game_over_sound", "level_start_sound"):
        if str(asset.get(key, "") or ""):
            extras[key] = asset[key]
            issues.add("unsupported", f"asset_{key}", f"asset.cfg {key} = {asset[key]!r} (no 3DS format yet)")
    if str(asset.get("cutscene_song_path", "") or ""):   # neither the Godot 4.7 game nor the Godot 3 backport reads it
        issues.add("ignored", "asset_cutscene_song_path", f"asset.cfg cutscene_song_path = {asset['cutscene_song_path']!r} (not read by the game)")
    if (ref.path / "cutscene").is_dir():   # converted by the cutscene step (mod2ds/cutscene.py)
        extras["cutscene_dir"] = True
        if not any((ref.path / "cutscene" / w / "script.cfg").is_file() for w in ("pre", "post")):
            issues.add("ignored", "cutscene_empty", "cutscene/ folder without a pre/ or post/ script.cfg (the game plays none)")
    if cfg["toys"]:
        extras["toys"] = cfg["toys"]
    if pack.act.get("act_legacy"):   # converted pre-release mod (legacy.py): native3ds offers the sideways LEGACY LAYOUT
        extras["legacy"] = True

    display = {
        "level_name": level_name, "character": str(meta.get("character", "")),
        "color": meta.get("color"), "level_id": meta.get("level_id"),
        "creator": mod.get("creator"), "song_title": mod.get("song_title"),
        "song_author": mod.get("song_author") or mod.get("song_creator"), "description": mod.get("description"),
        "preview_timestamp_s": num(mod.get("preview_timestamp"), None),
    }
    def maybe(name: str) -> Path | None:
        return DirIndex(ref.path).find(name)

    return Level(
        stage_id=stage_id, pack_id=pack_id, ref=ref, title=title, character=character,
        level_index=int(num(meta.get("level_index", 0), 0)), display=display, song=song,
        song_offset_us=setting_us("song_offset"), note_offset_us=setting_us("note_offset"),
        post_song_delay_s=float(num(settings.get("post_song_delay", 5.0), 5.0)),
        bpm_changes=bpm_changes, shutters_us=shutters, loops=loops, sheets=sheets, cameras=cameras,
        clips=clips, sound_loops=sound_loops, oneshots=oneshots, voice_banks=voice_banks, videos=videos,
        charts=charts, slots=slots, game_over=images.find("game_over.png"), thumb=maybe("thumb.png"),
        splash=maybe("splash.png"), extras=extras, issues=issues, framing=framing_mode, canvas_k=canvas_k,
    )


def check_runtime_limits(lv: Level, frames_per_sheet: dict[str, int] | None = None) -> None:
    """Flag everything the current native runtime would reject or truncate (severity 'limit')."""
    add = lv.issues.add
    L = limits
    for c in lv.charts:
        if c["index"] in lv.slots.values() and len(c["notes"]) > L.MAX_NOTES:
            add("limit", "notes", f"chart {c['name']!r}: {len(c['notes'])} notes > {L.MAX_NOTES}")
        if c["index"] in lv.slots.values() and not c["notes"]:
            add("limit", "notes", f"chart {c['name']!r} has no notes")
    if len(lv.shutters_us) > L.MAX_SHUTTERS:
        add("limit", "shutters", f"{len(lv.shutters_us)} shutters > {L.MAX_SHUTTERS}")
    if len(lv.loops) > L.MAX_LOOPS:
        add("limit", "loops", f"{len(lv.loops)} loop keyframes > {L.MAX_LOOPS}")
    if len(lv.bpm_changes) > L.MAX_BPM_CHANGES:
        add("limit", "bpm_changes", f"{len(lv.bpm_changes)} tempo changes > {L.MAX_BPM_CHANGES}")
    if len(lv.sheets) > L.MAX_SHEETS:
        add("limit", "sheets", f"{len(lv.sheets)} sprite sheets > {L.MAX_SHEETS}")
    for name, sh in lv.sheets.items():
        n = (frames_per_sheet or {}).get(name, sh.total)
        if n > L.MAX_FRAMES_PER_SHEET:
            add("limit", "frames", f"sheet {sh.source.name!r}: {n} frames > {L.MAX_FRAMES_PER_SHEET} (use --fit-runtime)")
    var_count: dict[str, set[str]] = {}
    for lp in lv.loops:
        for v in lp["variations"]:
            var_count.setdefault(lp["sheet"], set()).add(v["sheet"])
    for s, vs in var_count.items():
        if len(vs) > L.MAX_VARIATIONS_PER_SHEET:
            add("limit", "variations", f"sheet {s}: {len(vs)} variations > {L.MAX_VARIATIONS_PER_SHEET}")
    if len(lv.cameras) > L.MAX_CAMERA_KEYS:
        add("limit", "camera", f"{len(lv.cameras)} camera keys > {L.MAX_CAMERA_KEYS}")
    if len(lv.clips) + 5 > L.MAX_CLIPS:
        add("limit", "clips", f"{len(lv.clips)} sound clips (+5 shared) > {L.MAX_CLIPS}")
    if len(lv.sound_loops) > L.MAX_SOUND_LOOPS:
        add("limit", "sound_loops", f"{len(lv.sound_loops)} sound_loop keyframes > {L.MAX_SOUND_LOOPS}")
    if len(lv.oneshots) > L.MAX_ONESHOTS:
        add("limit", "oneshots", f"{len(lv.oneshots)} sound_oneshot keyframes > {L.MAX_ONESHOTS}")
    if len(lv.voice_banks) > L.MAX_VOICE_BANKS:
        add("limit", "voice_banks", f"{len(lv.voice_banks)} voice_bank keyframes > {L.MAX_VOICE_BANKS}")
    for e in lv.sound_loops:
        if len(e["clips"]) > L.MAX_LOOP_CLIPS:
            add("limit", "loop_clips", f"a sound_loop has {len(e['clips'])} clips > {L.MAX_LOOP_CLIPS}")
    for e in lv.voice_banks:
        if len(e["clips"]) > L.MAX_VOICE_CLIPS:
            add("limit", "voice_clips", f"a voice_bank has {len(e['clips'])} clips > {L.MAX_VOICE_CLIPS}")
    if len(lv.videos) > L.MAX_VIDEOS:
        add("limit", "videos", f"{len(lv.videos)} videos > {L.MAX_VIDEOS}")
