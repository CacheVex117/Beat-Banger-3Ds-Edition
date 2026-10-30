"""Find mod packs and their levels exactly the way Beat Banger does.

GameManager.get_act_datatree: an act (mod pack) is an immediate sub-folder of the mods root that holds
an act.cfg ConfigFile that loads; its data is [main] data. Level.get_level_list: a level is an
immediate sub-folder of the act holding a loadable config/meta.cfg; levels sort by meta level_index.
Folders with a chart.cfg but no act.cfg are pre-release ("Legacy") mods that the game converts at play
time through LegacyConverter; MOD2DS reports them but does not convert them.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from . import godot_cfg


@dataclass
class LevelRef:
    path: Path
    meta: dict[str, Any]

    @property
    def folder(self) -> str:
        return self.path.name


@dataclass
class ModPack:
    path: Path
    kind: str                      # "act" | "legacy" | "invalid"
    act: dict[str, Any] = field(default_factory=dict)
    download: dict[str, Any] = field(default_factory=dict)
    levels: list[LevelRef] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)

    @property
    def name(self) -> str:
        return str(self.act.get("act_name") or self.download.get("title") or self.path.name)


def _load_section(path: Path, section: str, key: str | None = None) -> tuple[Any, str | None]:
    try:
        sec = godot_cfg.load(path).get(section, {})
    except (godot_cfg.CfgError, OSError) as exc:
        return None, str(exc)
    return (sec.get(key) if key else sec), None


def find_levels(act_dir: Path, problems: list[str]) -> list[LevelRef]:
    levels: list[LevelRef] = []
    for sub in sorted(p for p in act_dir.iterdir() if p.is_dir()):
        meta_path = sub / "config" / "meta.cfg"
        if not meta_path.is_file():
            continue
        meta, err = _load_section(meta_path, "main", "data")
        if err:
            problems.append(f"level '{sub.name}' skipped: meta.cfg does not load ({err})")
            continue
        levels.append(LevelRef(sub, meta if isinstance(meta, dict) else {}))
    # Level.get_level_list sorts by level_index; folder name breaks ties deterministically
    levels.sort(key=lambda lv: (_int(lv.meta.get("level_index", 0)), lv.folder.lower()))
    return levels


def _int(v: Any) -> int:
    try:
        return int(v)
    except (TypeError, ValueError):
        return 0


def load_pack(path: Path) -> ModPack:
    act_cfg = path / "act.cfg"
    if not act_cfg.is_file():
        legacy = next(path.rglob("chart.cfg"), None)
        if legacy is not None:
            return ModPack(path, "legacy", problems=[
                f"pre-release (Legacy) mod: {legacy.relative_to(path)} with no act.cfg; the game converts these at "
                "play time (LegacyConverter), and install / oneshot (MOD2DS.exe) convert them the same way"])
        return ModPack(path, "invalid", problems=["no act.cfg: the game does not list this folder"])
    act, err = _load_section(act_cfg, "main", "data")
    if err:
        return ModPack(path, "invalid", problems=[f"act.cfg does not load, the game skips this pack ({err})"])
    pack = ModPack(path, "act", act=act if isinstance(act, dict) else {})
    dl_path = path / "download.cfg"
    if dl_path.is_file():
        dl, derr = _load_section(dl_path, "info")
        if derr:
            pack.problems.append(f"download.cfg ignored ({derr})")
        else:
            pack.download = dl or {}
    pack.levels = find_levels(path, pack.problems)
    if not pack.levels:
        pack.problems.append("act has no level folder with a loadable config/meta.cfg")
    return pack


def scan(mods_root: Path) -> list[ModPack]:
    if not mods_root.is_dir():
        raise FileNotFoundError(f"mods folder not found: {mods_root}")
    return [load_pack(p) for p in sorted(mods_root.iterdir(), key=lambda p: p.name.lower()) if p.is_dir()]


def resolve_pack_arg(arg: Path, mods_root: Path | None) -> Path:
    """Accept a pack folder path, or a pack folder name under the mods root."""
    if arg.is_dir():
        return arg
    if mods_root is not None and (mods_root / arg).is_dir():
        return mods_root / arg
    raise FileNotFoundError(f"mod pack not found: {arg}")
