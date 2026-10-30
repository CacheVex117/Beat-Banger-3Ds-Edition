"""Reader for Godot 4 ConfigFile text (`[section]` + `key=<variant>`), as written by Beat Banger.

The native3ds converters parse configs with a regex + json.loads shortcut. That is fine for the
shipped stages but not for community mods, which carry raw newlines inside strings
(act_description), unicode, Godot constructors (Color(...), Vector2(...)), StringNames and
trailing commas. This module implements the subset of Godot's VariantParser grammar that can
appear in a ConfigFile, so nothing has to be guessed.

Mapping to Python:
    null -> None, true/false -> bool, int -> int, float/inf/nan -> float, "str" / &"name" / ^"path" -> str
    [..] / Array[T]([..]) -> list, {k: v} / Dictionary[K, V]({..}) -> dict
    Vector2/3/4(i), Color, Rect2(i), Packed*Array, Quaternion, Plane, ... -> list of their arguments
    any other Ctor(args) -> GodotObject(name, args)  (never produced by Beat Banger mods seen so far)
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


class CfgError(ValueError):
    """Malformed ConfigFile text. Message carries file:line:col."""


@dataclass
class GodotObject:
    name: str
    args: list[Any] = field(default_factory=list)


_FLAT_CTORS = {
    "Vector2", "Vector2i", "Vector3", "Vector3i", "Vector4", "Vector4i", "Color", "Rect2", "Rect2i",
    "Quaternion", "Plane", "AABB", "Basis", "Transform2D", "Transform3D", "Projection",
    "PackedByteArray", "PackedInt32Array", "PackedInt64Array", "PackedFloat32Array", "PackedFloat64Array",
    "PackedStringArray", "PackedVector2Array", "PackedVector3Array", "PackedVector4Array", "PackedColorArray",
}
_ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "b": "\b", "f": "\f", "a": "\a", "v": "\v", '"': '"', "\\": "\\", "'": "'", "/": "/"}


class _Reader:
    def __init__(self, text: str, origin: str):
        self.s = text
        self.i = 0
        self.origin = origin

    # -- error helpers ---------------------------------------------------------------------------
    def fail(self, msg: str) -> CfgError:
        line = self.s.count("\n", 0, self.i) + 1
        col = self.i - (self.s.rfind("\n", 0, self.i) + 1) + 1
        return CfgError(f"{self.origin}:{line}:{col}: {msg}")

    def peek(self) -> str:
        return self.s[self.i] if self.i < len(self.s) else ""

    def skip_ws(self) -> None:
        s, n = self.s, len(self.s)
        while self.i < n:
            c = s[self.i]
            if c in " \t\r\n﻿":
                self.i += 1
            elif c == ";" or (c == "#" and self._at_line_start()):
                while self.i < n and s[self.i] != "\n":
                    self.i += 1
            else:
                break

    def _at_line_start(self) -> bool:
        j = self.i - 1
        while j >= 0 and self.s[j] in " \t":
            j -= 1
        return j < 0 or self.s[j] == "\n"

    def expect(self, ch: str) -> None:
        self.skip_ws()
        if self.peek() != ch:
            raise self.fail(f"expected {ch!r}, got {self.peek() or 'EOF'!r}")
        self.i += 1

    # -- values ----------------------------------------------------------------------------------
    def value(self) -> Any:
        self.skip_ws()
        c = self.peek()
        if c == "":
            raise self.fail("unexpected end of input")
        if c == "{":
            return self.dict_()
        if c == "[":
            return self.array()
        if c == '"':
            return self.string()
        if c in "&^" and self.s[self.i + 1:self.i + 2] == '"':
            self.i += 1
            return self.string()
        if c in "-+.0123456789":
            return self.number()
        if c.isalpha() or c == "_":
            return self.ident_value()
        raise self.fail(f"unexpected character {c!r}")

    def string(self) -> str:
        self.i += 1  # opening quote
        out: list[str] = []
        s, n = self.s, len(self.s)
        while True:
            if self.i >= n:
                raise self.fail("unterminated string")
            c = s[self.i]
            if c == '"':
                self.i += 1
                return "".join(out)
            if c == "\\":
                e = s[self.i + 1:self.i + 2]
                if e in _ESCAPES:
                    out.append(_ESCAPES[e])
                    self.i += 2
                elif e in ("u", "U"):
                    width = 4 if e == "u" else 6
                    hexs = s[self.i + 2:self.i + 2 + width]
                    try:
                        cp = int(hexs, 16)
                    except ValueError:
                        raise self.fail(f"bad \\{e} escape") from None
                    self.i += 2 + width
                    # join UTF-16 surrogate pairs (Godot writes non-BMP chars that way)
                    if 0xD800 <= cp < 0xDC00 and s[self.i:self.i + 2] == "\\u":
                        lo = int(s[self.i + 2:self.i + 6], 16)
                        if 0xDC00 <= lo < 0xE000:
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00)
                            self.i += 6
                    out.append(chr(cp))
                else:
                    out.append(e)  # Godot keeps unknown escapes as the bare character
                    self.i += 2
            else:
                out.append(c)
                self.i += 1

    def number(self) -> int | float:
        s, n, j = self.s, len(self.s), self.i
        if s[j] in "+-":
            j += 1
        if s.startswith("inf", j):  # -inf
            sign = -1.0 if s[self.i] == "-" else 1.0
            self.i = j + 3
            return sign * math.inf
        while j < n and (s[j].isalnum() or s[j] in "._+-"):
            # stop a trailing sign that is not an exponent sign
            if s[j] in "+-" and s[j - 1] not in "eE":
                break
            j += 1
        tok = s[self.i:j]
        self.i = j
        try:
            if tok.lower().lstrip("+-").startswith("0x"):
                return int(tok, 16)
            if any(ch in tok for ch in ".eE"):
                return float(tok)
            return int(tok, 10)
        except ValueError:
            raise self.fail(f"bad number {tok!r}") from None

    def ident(self) -> str:
        j = self.i
        while j < len(self.s) and (self.s[j].isalnum() or self.s[j] == "_"):
            j += 1
        tok = self.s[self.i:j]
        self.i = j
        return tok

    def ident_value(self) -> Any:
        start = self.i
        name = self.ident()
        if name == "null":
            return None
        if name in ("true", "false"):
            return name == "true"
        if name == "nan":
            return math.nan
        if name == "inf":
            return math.inf
        if name == "inf_neg":
            return -math.inf
        self.skip_ws()
        if self.peek() == "[" and name in ("Array", "Dictionary"):
            # typed container: Array[int]([1, 2]) / Dictionary[String, int]({...})
            depth = 0
            while True:
                c = self.peek()
                if c == "":
                    raise self.fail("unterminated type parameter")
                self.i += 1
                if c == "[":
                    depth += 1
                elif c == "]":
                    depth -= 1
                    if depth == 0:
                        break
            self.expect("(")
            inner = self.value()
            self.expect(")")
            return inner
        if self.peek() != "(":
            self.i = start
            raise self.fail(f"unknown identifier {name!r}")
        self.i += 1
        args = self.list_until(")")
        if name in _FLAT_CTORS:
            return args
        return GodotObject(name, args)

    def list_until(self, close: str) -> list[Any]:
        items: list[Any] = []
        while True:
            self.skip_ws()
            if self.peek() == close:
                self.i += 1
                return items
            items.append(self.value())
            self.skip_ws()
            c = self.peek()
            if c == ",":
                self.i += 1
            elif c != close:
                raise self.fail(f"expected ',' or {close!r}, got {c or 'EOF'!r}")

    def array(self) -> list[Any]:
        self.i += 1
        return self.list_until("]")

    def dict_(self) -> dict[Any, Any]:
        self.i += 1
        out: dict[Any, Any] = {}
        while True:
            self.skip_ws()
            if self.peek() == "}":
                self.i += 1
                return out
            key = self.value()
            if isinstance(key, list):
                key = tuple(key)
            self.expect(":")
            out[key] = self.value()
            self.skip_ws()
            c = self.peek()
            if c == ",":
                self.i += 1
            elif c != "}":
                raise self.fail(f"expected ',' or '}}', got {c or 'EOF'!r}")

    # -- ConfigFile structure --------------------------------------------------------------------
    def config(self) -> dict[str, dict[str, Any]]:
        """Godot's VariantParser::parse_tag_assign_eof as ConfigFile drives it. Between assignments every character above
        space is collected as the next key until '=': whitespace and newlines are dropped, a "quoted" string replaces what
        was collected, ';' comments to the end of the line ('#' is not a comment), '[' opens a section only when nothing is
        collected. What is still collected when the file ends is dropped and the file loads: mods carry notes such as
        '//Change what's in "" ...' after their data, and the game reads them fine (checked with Godot 3.5's ConfigFile)."""
        sections: dict[str, dict[str, Any]] = {}
        current: dict[str, Any] | None = None
        s, n = self.s, len(self.s)
        if s.startswith("﻿"):
            self.i = 1
        what = ""
        while self.i < n:
            c = s[self.i]
            if c == ";":
                while self.i < n and s[self.i] != "\n":
                    self.i += 1
            elif c == "[" and not what:
                end = s.find("]", self.i)
                if end < 0:
                    raise self.fail("unterminated section header")
                current = sections.setdefault(s[self.i + 1:end].strip(), {})
                self.i = end + 1
            elif c == '"':
                what = self.string()
            elif c == "=":
                self.i += 1
                if current is None:
                    current = sections.setdefault("", {})
                current[what] = self.value()
                what = ""
            else:
                if ord(c) > 32:
                    what += c
                self.i += 1
        return sections


def parse_text(text: str, origin: str = "<text>") -> dict[str, dict[str, Any]]:
    return _Reader(text, origin).config()


def parse_value(text: str, origin: str = "<value>") -> Any:
    r = _Reader(text, origin)
    v = r.value()
    r.skip_ws()
    if r.peek():
        raise r.fail("trailing data after value")
    return v


def load(path: Path) -> dict[str, dict[str, Any]]:
    raw = path.read_bytes()
    try:
        text = raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        text = raw.decode("latin-1")
    return parse_text(text, str(path))


def load_data(path: Path, section: str = "main", key: str = "data", default: Any = None) -> Any:
    """Beat Banger stores each config as `[main] data=<value>` (Config.load_config_data)."""
    return load(path).get(section, {}).get(key, default)
