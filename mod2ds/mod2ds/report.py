"""REPORT.md / report.json for a MOD2DS output folder, rebuilt from packs/*.json on every run."""
from __future__ import annotations

import json
from collections import Counter
from pathlib import Path
from typing import Any

from . import __version__

STATUS_TEXT = {"ok": "ready", "over_runtime_limits": "converted, over runtime limits", "error": "not converted"}


def collect(out: Path) -> list[dict[str, Any]]:
    return [json.loads(p.read_text(encoding="utf-8")) for p in sorted((out / "packs").glob("*.json"))]


def write(out: Path, skipped: list[dict[str, Any]] | None = None) -> Path:
    packs = collect(out)
    stages = [s for p in packs for s in p["stages"]]
    status = Counter(s["status"] for s in stages)
    codes: Counter[tuple[str, str]] = Counter()
    for s in stages:
        for i in s.get("issues", []):
            codes[(i["severity"], i["code"])] += 1
    data = {"tool": f"mod2ds {__version__}", "packs": len(packs), "stages": len(stages), "status": dict(status),
            "issue_codes": {f"{sev}:{code}": n for (sev, code), n in codes.most_common()},
            "not_converted_packs": skipped or []}
    (out / "report.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")

    md = [f"# MOD2DS conversion report", "",
          f"`mod2ds {__version__}` - {len(packs)} pack(s), {len(stages)} level(s): "
          + ", ".join(f"{n} {STATUS_TEXT.get(k, k)}" for k, n in status.items()), "",
          "Severity: **limit** = over a current 3DS runtime limit (stage will not load as-is); "
          "**unsupported** = used by the game, no 3DS format yet (kept in level.json, dropped from binaries); "
          "**warning** = source data problem handled like the game does; **ignored** = the game ignores it too.", ""]
    if codes:
        md += ["## Issues across all levels", "", "| severity | code | levels |", "|---|---|---|"]
        md += [f"| {sev} | `{code}` | {n} |" for (sev, code), n in codes.most_common()]
        md.append("")
    for p in packs:
        md += [f"## {p['act_name_ascii']} (`{p['pack_id']}`)", "", f"Source: `{p['source']['folder']}`", ""]
        for prob in p.get("problems", []):
            md.append(f"- {prob}")
        if p.get("problems"):
            md.append("")
        if p["stages"]:
            md += ["| level | stage id | status | limits | unsupported |", "|---|---|---|---|---|"]
            for s in p["stages"]:
                iss = s.get("issues", [])
                lim = "; ".join(i["message"] for i in iss if i["severity"] == "limit") or "-"
                uns = ", ".join(sorted({i["code"] for i in iss if i["severity"] == "unsupported"})) or "-"
                state = STATUS_TEXT.get(s["status"], s["status"]) + (f": {s['error']}" if s.get("error") else "")
                md.append(f"| {s.get('level_name') or s['folder']} | `{s.get('stage_id', '-')}` | {state} | {lim} | {uns} |")
            md.append("")
    if skipped:
        md += ["## Folders not converted", ""] + [f"- `{s['folder']}` ({s['kind']}): {'; '.join(s['problems'])}" for s in skipped] + [""]
    path = out / "REPORT.md"
    path.write_text("\n".join(md), encoding="utf-8")
    return path
