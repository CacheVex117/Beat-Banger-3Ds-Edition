"""Build the 3D HOME Menu banner model (banner.cgfx) for tools/build_cia.py.

    python tools/cia/make_banner3d.py --pycgfx <pycgfx checkout> --logo <png> --out generated/cia/banner.cgfx

The smallest scene that still turns and pulses: one node, one mesh of two quads (front, and a back that reads the right way round;
back-face culling draws one at a time), one cut-out material and one 256x128 RGBA4 texture. The HOME Menu turns the banner on its own
(one turn per 10 s). The node pulses with the menu logo's beat curve (`bb_ui_beat_pulse`: 1 + A * (1 - f)^3) at The Boss's 128 BPM;
32 beats = a 15 s loop, so the pulse stays on the beat. (The first version stacked 16 cut-out edge layers for depth and froze the
HOME Menu on a New 2DS XL; see SPRINT_HANDOFF 3AB.)

pycgfx (https://github.com/skyfloogle/pycgfx, tested at 1f78850) converts glTF to CGFX; it has no licence that allows copying it here,
so pass a checkout. Its `gltflib` import is served by a small stand-in below (it uses four names), so no other package is needed.
The run-time fixes follow what another port needed on hardware (GabeSud0/pokeplatinum-3ds tools/banner3d, CBC-for-YANBF):
unlit materials (texture x material colour), each mesh names its bone, the bone lookup tree is rebuilt after pycgfx reorders bones,
every curve repeats, and the file stays well under the 512 KiB CGFX limit (their largest working scene was ~431 KB).
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import struct
import sys
import types
from pathlib import Path

from PIL import Image, ImageFilter

BPM = 128.0                   # The Boss (claire_e1 meta.cfg)
LOOP_BEATS = 32               # 32 beats at 128 BPM = 15 s exactly = 900 frames at pycgfx's 60 fps
PULSE = 0.06                  # the menu logo uses 0.035; a little more so it reads at banner size
WIDTH = 25.0                  # world units; the HOME banner camera shows ~10 px per unit at z = 0 (pycgfx banner-camera.gltf)
CENTER_Y = 0.4                # middle of the visible band between the HOME Menu bars (about y 40..212 of 240)
GAP = 0.02                    # front and back faces this far apart (only one is ever drawn)
OUTLINE = (20, 18, 32)        # dark rim around the logo so the white letters read on a light HOME theme
TEX_W, TEX_H = 256, 128


def make_texture(logo_path: Path, out_png: Path) -> tuple[float, float]:
    """The logo with a dark rim, fitted into a 256x128 RGBA4-dithered texture; returns the used (u, v) range."""
    src = Image.open(logo_path).convert("RGBA")
    pad = max(2, round(src.width / TEX_W * 3))
    big = Image.new("RGBA", (src.width + 2 * pad, src.height + 2 * pad), (0, 0, 0, 0))
    big.paste(src, (pad, pad))
    rim = Image.new("RGBA", big.size, OUTLINE + (0,))
    rim.putalpha(big.getchannel("A").filter(ImageFilter.MaxFilter(2 * pad + 1)))
    big = Image.alpha_composite(rim, big)
    k = min(TEX_W / big.width, TEX_H / big.height)
    w, h = round(big.width * k), round(big.height * k)
    small = big.resize((w, h), Image.LANCZOS)
    tex = Image.new("RGBA", (TEX_W, TEX_H), OUTLINE + (0,))
    tex.paste(small, (0, 0))
    bayer = (0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5)
    px = tex.load()
    for y in range(TEX_H):
        for x in range(TEX_W):
            r, g, b, a = px[x, y]
            if a < 128:
                px[x, y] = OUTLINE + (0,)     # cut away; keep a dark colour so bilinear filtering does not fringe
                continue
            d = (bayer[(y & 3) * 4 + (x & 3)] + 0.5) / 16.0 - 0.5
            q = tuple(min(15, max(0, round(c / 17.0 + d))) * 17 for c in (r, g, b))
            px[x, y] = q + (255,)
    tex.save(out_png)
    return w / TEX_W, h / TEX_H


class _Buf:
    def __init__(self):
        self.data = bytearray()
        self.views: list[dict] = []
        self.accessors: list[dict] = []

    def add(self, fmt: str, rows: list[tuple], gl_type: str, comp: int, minmax: bool = False) -> int:
        while len(self.data) % 4:
            self.data.append(0)
        off = len(self.data)
        for r in rows:
            self.data += struct.pack("<" + fmt * len(r), *r)
        self.views.append({"buffer": 0, "byteOffset": off, "byteLength": len(self.data) - off})
        acc = {"bufferView": len(self.views) - 1, "byteOffset": 0, "componentType": comp, "count": len(rows), "type": gl_type}
        if minmax:
            acc["min"] = [min(r[i] for r in rows) for i in range(len(rows[0]))]
            acc["max"] = [max(r[i] for r in rows) for i in range(len(rows[0]))]
        self.accessors.append(acc)
        return len(self.accessors) - 1


def make_gltf(work: Path, u_max: float, v_max: float) -> Path:
    w = WIDTH / 2.0
    h = WIDTH * (v_max * TEX_H) / (u_max * TEX_W) / 2.0
    buf = _Buf()
    pos, nrm, uv, idx = [], [], [], []
    for z, nz in ((GAP / 2, 1.0), (-GAP / 2, -1.0)):      # front, then the back (u mirrored so it reads from behind)
        base = len(pos)
        for (x, y, u, v) in ((-w, -h, 0.0, v_max), (w, -h, u_max, v_max), (w, h, u_max, 0.0), (-w, h, 0.0, 0.0)):
            pos.append((x, y, z))
            nrm.append((0.0, 0.0, nz))
            uv.append((u if nz > 0 else u_max - u, v))
        idx += [(base + i,) for i in ((0, 1, 2, 0, 2, 3) if nz > 0 else (0, 2, 1, 0, 3, 2))]
    attrs = {"POSITION": buf.add("f", pos, "VEC3", 5126, minmax=True), "NORMAL": buf.add("f", nrm, "VEC3", 5126),
             "TEXCOORD_0": buf.add("f", uv, "VEC2", 5126)}
    meshes = [{"name": "logo", "primitives": [{"attributes": attrs, "indices": buf.add("H", idx, "SCALAR", 5123),
                                                "material": 0, "mode": 4}]}]
    nodes = [{"name": "logo", "mesh": 0, "translation": [0.0, CENTER_Y, 0.0]}]

    beat = 60.0 / BPM
    times, scales = [], []
    for b in range(LOOP_BEATS):
        for f in (0.0, 0.15, 0.4):
            times.append((b + f) * beat)
            scales.append(1.0 + PULSE * (1.0 - f) ** 3)
        times.append((b + 1) * beat - 1.0 / 60.0)    # back to rest one frame before the next beat, then snap up on it
        scales.append(1.0)
    times.append(LOOP_BEATS * beat)
    scales.append(1.0 + PULSE)
    t_acc = buf.add("f", [(x,) for x in times], "SCALAR", 5126, minmax=True)
    s_acc = buf.add("f", [(s, s, s) for s in scales], "VEC3", 5126)

    (work / "banner3d.bin").write_bytes(bytes(buf.data))
    gltf = {
        "asset": {"version": "2.0", "generator": "Beat Banger 3DS make_banner3d.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": nodes, "meshes": meshes,
        "materials": [{"name": "logo", "alphaMode": "MASK", "alphaCutoff": 0.5, "doubleSided": False,
                       "pbrMetallicRoughness": {"baseColorFactor": [1.0, 1.0, 1.0, 1.0], "baseColorTexture": {"index": 0},
                                                "roughnessFactor": 1.0, "metallicFactor": 0.0}}],
        "textures": [{"source": 0, "sampler": 0}], "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"name": "logo", "uri": "banner3d_logo.png"}],
        "animations": [{"name": "pulse", "channels": [{"sampler": 0, "target": {"node": 0, "path": "scale"}}],
                        "samplers": [{"input": t_acc, "output": s_acc, "interpolation": "LINEAR"}]}],
        "buffers": [{"uri": "banner3d.bin", "byteLength": len(buf.data)}],
        "bufferViews": buf.views, "accessors": buf.accessors,
    }
    out = work / "banner3d.gltf"
    out.write_text(json.dumps(gltf, indent=1))
    return out


def _gltflib_standin() -> types.ModuleType:
    """The four gltflib names pycgfx uses: JSON objects with attribute access (missing fields read as None)."""
    class Obj:
        def __init__(self, d=None, **kw):
            for k, v in {**(d or {}), **kw}.items():
                setattr(self, k, Obj._wrap(v))

        def __getattr__(self, name):
            if name.startswith("__"):
                raise AttributeError(name)
            return None

        @staticmethod
        def _wrap(v):
            if isinstance(v, dict):
                return Obj(v)
            if isinstance(v, list):
                return [Obj._wrap(x) for x in v]
            return v

    class Resource:
        def __init__(self, data: bytes):
            self.data = data

    class GLTF:
        def __init__(self, path: Path):
            self.base = path.parent
            self.model = Obj(json.loads(path.read_text()))

        @classmethod
        def load(cls, path, load_file_resources=True):
            return cls(Path(path))

        def get_resource(self, uri):
            return Resource((self.base / uri).read_bytes())

        def get_glb_resource(self):
            raise RuntimeError("only .gltf with external files is supported")

    mod = types.ModuleType("gltflib")
    mod.GLTF, mod.Accessor, mod.Sampler, mod.Material, mod.PBRMetallicRoughness = GLTF, Obj, Obj, Obj, Obj
    return mod


def convert(pycgfx: Path, gltf_path: Path, out: Path) -> int:
    sys.modules["gltflib"] = _gltflib_standin()
    sys.path.insert(0, str(pycgfx))
    spec = importlib.util.spec_from_file_location("pycgfx_main", pycgfx / "main.py")
    pm = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(pm)
    from cgfx.canm import FloatAnimationCurve, RepeatMethod
    from cgfx.mtob import ConstantColorSource, MTOBFlag

    gltf = sys.modules["gltflib"].GLTF.load(gltf_path)
    cgfx = pm.convert_gltf(gltf)
    cmdl = cgfx.data.models["COMMON"]
    cmdl.skeleton.bones.dict.regenerate()            # pycgfx reorders the bones after building the lookup tree
    for m in cmdl.materials:                         # unlit: texture x material colour, then pass through
        mtob = cmdl.materials[m]
        mtob.flags = MTOBFlag(0)
        tc = mtob.fragment_shader.texture_combiners
        tc[0].constant = ConstantColorSource.Diffuse
        tc[0].src_rgb = tc[0].src_alpha = 0x0E3
        tc[0].combine_rgb = tc[0].combine_alpha = 1
        for c in tc[1:]:
            c.src_rgb = c.src_alpha = 0xFFF
            c.combine_rgb = c.combine_alpha = 0
    mesh_bone = {m["name"]: n["name"] for n in json.loads(gltf_path.read_text())["nodes"] if "mesh" in n
                 for m in [json.loads(gltf_path.read_text())["meshes"][n["mesh"]]]}
    for sm in cmdl.meshes.data.contents:             # each mesh names its node's bone
        sm.mesh_node_name = next(b for name, b in mesh_bone.items() if sm.name.startswith(name + "_"))
    for a in cgfx.data.skeletal_animations:          # every curve repeats over its own keys
        canm = cgfx.data.skeletal_animations[a]
        canm.looping = True
        for b in canm.member_animations_data:
            bone = canm.member_animations_data[b]
            for k in ("scale_x", "scale_y", "scale_z", "rot_x", "rot_y", "rot_z", "pos_x", "pos_y", "pos_z"):
                cv = getattr(bone, k)
                if isinstance(cv, FloatAnimationCurve) and cv.segments:
                    cv.start_frame = min(s.start_frame for s in cv.segments)
                    cv.end_frame = max(s.end_frame for s in cv.segments)
                    cv.pre_repeat_method = cv.post_repeat_method = RepeatMethod.Repeat
    data = pm.write(cgfx)
    if len(data) > 400 * 1024:
        raise SystemExit(f"banner.cgfx is {len(data)} bytes; keep it under ~400 KB (HOME Menu limit)")
    out.write_bytes(data)
    return len(data)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pycgfx", type=Path, required=True)
    ap.add_argument("--logo", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    work = args.out.parent
    work.mkdir(parents=True, exist_ok=True)
    u_max, v_max = make_texture(args.logo, work / "banner3d_logo.png")
    n = convert(args.pycgfx, make_gltf(work, u_max, v_max), args.out)
    print(f"[OK] {args.out} ({n} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
