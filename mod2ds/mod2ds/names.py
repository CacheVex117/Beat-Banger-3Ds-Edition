"""Canonical, runtime-safe names.

The 3DS runtime keys everything by short ASCII names (char[32] fields, 64-byte path buffers, 23-char
save ids). Mod names are free text: spaces, apostrophes, Unicode, 40+ characters, and duplicates once
truncated. Every id here is ASCII [a-z0-9_], length-bounded, and stable: the same source always maps
to the same id, so re-converting a mod does not orphan save data.
"""
from __future__ import annotations

import hashlib
import re
import unicodedata

_TRANSLIT = {"’": "'", "‘": "'", "“": '"', "”": '"', "–": "-", "—": "-", "…": "...",
             "ß": "ss", "æ": "ae", "Æ": "AE", "ø": "o", "Ø": "O", "ł": "l", "Ł": "L",
             "♡": "<3", "♥": "<3", "❤": "<3"}


def ascii_text(s: str, limit: int | None = None) -> str:
    """Printable ASCII rendering of display text (titles, act names). Never empty unless the input is."""
    s = "".join(_TRANSLIT.get(c, c) for c in str(s))
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode("ascii")
    s = re.sub(r"[\x00-\x1f\x7f|]+", " ", s)       # control chars and the index.txt separator
    s = re.sub(r"\s+", " ", s).strip()
    if limit is not None:
        s = s[:limit].rstrip()
    return s


def slug(s: str, limit: int) -> str:
    t = ascii_text(s).lower()
    t = re.sub(r"[^a-z0-9]+", "_", t).strip("_")
    t = t[:limit].rstrip("_")
    return t or "x"


def short_hash(*parts: str, n: int = 6) -> str:
    return hashlib.sha1("\x1f".join(parts).encode("utf-8")).hexdigest()[:n]


def stage_id(level_name: str, *identity: str) -> str:
    """<slug<=13>_<hash6>: <= 20 chars (save ids keep 23; video page names need id <= 21)."""
    return f"{slug(level_name, 13)}_{short_hash(*identity)}"


def pack_id(act_name: str, *identity: str) -> str:
    """<slug<=16>_<hash4>: <= 21 chars, used as the index.txt act field (23 max)."""
    return f"{slug(act_name, 16)}_{short_hash(*identity, n=4)}"


def asset_name(prefix: str, index: int, source_name: str, limit: int = 31) -> str:
    """Name for a char[32] field: '<prefix><nn>_<slug>' -- the index keeps it unique after truncation."""
    head = f"{prefix}{index:02d}"
    stem = source_name.rsplit(".", 1)[0]
    return f"{head}_{slug(stem, limit - len(head) - 1)}"
