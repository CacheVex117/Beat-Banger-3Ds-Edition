#!/usr/bin/env python3
"""Reproducible chart analysis for a Beat Banger level's notes.cfg (read-only).

Usage: python -I analyze_chart.py <path to notes.cfg> [--json]

notes.cfg is a Godot ConfigFile whose `data=` value is a JSON-compatible
dictionary with a `charts` array.  Reports, per chart: note count, input_type
and note_modifier distributions, hold_end_timestamp validity and ordering.
"""
import argparse
import collections
import json
import sys


def load_notes_cfg(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    marker = "data="
    start = text.index(marker) + len(marker)
    return json.loads(text[start:])


def analyze_chart(chart):
    notes = chart.get("notes", [])
    mods = collections.Counter(n.get("note_modifier", 0) for n in notes)
    lanes = collections.Counter(n.get("input_type") for n in notes)
    holds = [n for n in notes if n.get("note_modifier", 0) == 3]
    with_end = [n for n in notes if "hold_end_timestamp" in n]
    invalid = [n for n in with_end if not n["hold_end_timestamp"] > n["timestamp"]]
    holds_missing_end = [n for n in holds if "hold_end_timestamp" not in n]
    stray_end = [n for n in with_end if n.get("note_modifier", 0) != 3]
    ts = [n["timestamp"] for n in notes]
    return {
        "name": chart.get("name"),
        "rating": chart.get("rating"),
        "notes": len(notes),
        "sorted_by_timestamp": ts == sorted(ts),
        "last_timestamp": max(ts) if ts else None,
        "note_modifier": dict(sorted(mods.items())),
        "input_type": dict(sorted(lanes.items())),
        "hold_modifier_notes": len(holds),
        "hold_end_present": len(with_end),
        "hold_end_valid": len(with_end) - len(invalid),
        "hold_end_invalid_duration": len(invalid),
        "hold_modifier_missing_end": len(holds_missing_end),
        "hold_end_on_non_hold": len(stray_end),
        "min_hold_len": min((n["hold_end_timestamp"] - n["timestamp"] for n in with_end), default=None),
    }


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("notes_cfg")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    data = load_notes_cfg(args.notes_cfg)
    report = [analyze_chart(c) for c in data["charts"]]
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        for r in report:
            print(f"== {r['name']} (rating {r['rating']})")
            for k, v in r.items():
                if k not in ("name", "rating"):
                    print(f"  {k}: {v}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
