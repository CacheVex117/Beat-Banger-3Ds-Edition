"""External tools (ffmpeg, ffprobe, tex3ds) and the image/audio/video transforms, matching native3ds.

Sprite pages: frames split by sheet_data, premultiplied-alpha Lanczos resample to --frame-width,
packed row-major into power-of-two pages of at most 1024 px (convert_sprites.py).
Audio: 32 kHz PCM16; song stereo WAV, clips mono (convert_sfx.py / convert_stage.py).
Video: every frame cover-fit to 400x240 into 512x256 ETC1 cells, 8 per 1024x1024 page (convert_video.py).
"""
from __future__ import annotations

import math
import os
import shutil
import subprocess
import tempfile
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

from . import limits

Image.MAX_IMAGE_PIXELS = None   # mod sheets are routinely > 89 Mpx (e.g. 8x7 grids of 1080p frames)

# tex3ds encodes are independent subprocesses: run them on every core (pages are encoded one per call)
ENCODE_WORKERS = max(1, os.cpu_count() or 1)
_encode_pool: ThreadPoolExecutor | None = None


def encode_pool() -> ThreadPoolExecutor:
    global _encode_pool
    if _encode_pool is None:
        _encode_pool = ThreadPoolExecutor(max_workers=ENCODE_WORKERS, thread_name_prefix="tex3ds")
    return _encode_pool


def encode_async(tools: "Tools", png: Path, t3x: Path, fmt: str, quality: str = "medium") -> Future:
    """Encode in the pool, then delete the intermediate PNG; .result() re-raises a tex3ds failure."""
    def job() -> None:
        try:
            tools.tex3ds_encode(png, t3x, fmt, quality)
        finally:
            png.unlink(missing_ok=True)
    return encode_pool().submit(job)


def wait_all(futures: list[Future]) -> None:
    for f in futures:
        f.result()


BYTES_PER_PIXEL = {"rgba8": 4.0, "rgba4": 2.0, "rgba5551": 2.0, "rgb565": 2.0, "etc1a4": 1.0, "etc1": 0.5}


class ToolError(RuntimeError):
    pass


@dataclass
class Tools:
    ffmpeg: str
    ffprobe: str
    tex3ds: str

    @classmethod
    def find(cls, ffmpeg: str | None = None, ffprobe: str | None = None, tex3ds: str | None = None) -> "Tools":
        def pick(explicit: str | None, name: str, *fallbacks: str) -> str:
            for cand in ([explicit] if explicit else []) + [shutil.which(name)] + list(fallbacks):
                if cand and Path(cand).is_file():
                    return str(cand)
            raise ToolError(f"{name} not found; pass --{name.split('.')[0]} PATH")
        from . import paths
        ffmpeg = ffmpeg or paths.bundled_tool("ffmpeg")      # the portable exe carries its own tools
        ffprobe = ffprobe or paths.bundled_tool("ffprobe")
        tex3ds = tex3ds or paths.bundled_tool("tex3ds")
        return cls(
            ffmpeg=pick(ffmpeg, "ffmpeg", "C:/Tools/ffmpeg/bin/ffmpeg.exe"),
            ffprobe=pick(ffprobe, "ffprobe", "C:/Tools/ffmpeg/bin/ffprobe.exe"),
            tex3ds=pick(tex3ds, "tex3ds", "C:/devkitPro/tools/bin/tex3ds.exe",
                        os.path.join(os.environ.get("DEVKITPRO", "/opt/devkitpro"), "tools", "bin", "tex3ds")),
        )

    def run(self, *cmd: str) -> bytes:
        try:
            return subprocess.run(list(cmd), check=True, capture_output=True).stdout
        except subprocess.CalledProcessError as exc:
            raise ToolError(f"{Path(cmd[0]).name} failed: {exc.stderr.decode(errors='replace').strip()[:400]}") from exc

    def tex3ds_encode(self, png: Path, t3x: Path, fmt: str, quality: str = "medium") -> None:
        cmd = [self.tex3ds, "-f", fmt, "-z", "auto", "-o", str(t3x), str(png)]
        if fmt.startswith("etc1"):
            cmd[1:1] = ["-q", quality]
        self.run(*cmd)

    def decode_pcm(self, src: Path, channels: int, rate: int = limits.SAMPLE_RATE) -> bytes:
        return self.run(self.ffmpeg, "-v", "error", "-i", str(src), "-vn", "-ac", str(channels), "-ar", str(rate), "-f", "s16le", "-")

    def song_wav(self, src: Path, dst: Path, rate: int = limits.SAMPLE_RATE) -> None:
        self.run(self.ffmpeg, "-v", "error", "-y", "-i", str(src), "-vn", "-ac", "2", "-ar", str(rate), "-c:a", "pcm_s16le", str(dst))

    def wav_clip(self, src: Path, dst: Path, start_s: float, length_s: float, rate: int = limits.SAMPLE_RATE) -> None:
        self.run(self.ffmpeg, "-v", "error", "-y", "-ss", f"{start_s:.3f}", "-t", f"{length_s:.3f}", "-i", str(src),
                 "-vn", "-ac", "2", "-ar", str(rate), "-c:a", "pcm_s16le", str(dst))

    def has_audio(self, src: Path) -> bool:
        return bool(self.run(self.ffprobe, "-v", "error", "-select_streams", "a", "-show_entries", "stream=index",
                             "-of", "csv=p=0", str(src)).strip())

    def estimate_frames(self, src: Path) -> int:
        """Frame count from container metadata (ogv streams rarely carry nb_frames: duration x fps)."""
        out = self.run(self.ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries",
                       "stream=nb_frames,duration:format=duration", "-of", "default=nw=1", str(src)).decode()
        vals = dict(line.split("=", 1) for line in out.splitlines() if "=" in line)
        if vals.get("nb_frames", "N/A").isdigit():
            return int(vals["nb_frames"])
        dur = next((float(v) for k, v in vals.items() if k == "duration" and v not in ("N/A", "")), 0.0)
        return math.ceil(dur * self.fps(src))

    def fps(self, src: Path) -> float:
        out = self.run(self.ffprobe, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=r_frame_rate",
                       "-of", "csv=p=0", str(src)).decode().strip()
        num, _, den = out.partition("/")
        return float(num) / float(den or 1)


# -- sprites -----------------------------------------------------------------------------------------

def next_pow2(n: int) -> int:
    return 1 << max(3, (n - 1).bit_length())


def split_frames(sheet: Image.Image, h: int, v: int, total: int) -> list[Image.Image]:
    """Godot's hframes / vframes cells are sheet size / count, which can be fractional (1601 / 2 = 800.5): each frame
    starts on the first whole pixel row / column of its cell, so the last line of the frame before stays out."""
    fw, fh = sheet.width // h, sheet.height // v

    def start(i: int, size: int, n: int) -> int:
        return -(-i * size // n)   # ceil(i * size / n)
    return [sheet.crop((start(i % h, sheet.width, h), start(i // h, sheet.height, v),
                        start(i % h, sheet.width, h) + fw, start(i // h, sheet.height, v) + fh)) for i in range(total)]


def resample(frame: Image.Image, w: int, h: int) -> Image.Image:
    return frame.convert("RGBa").resize((w, h), Image.LANCZOS).convert("RGBA")


def fit_frame_size(src_w: int, src_h: int, frame_w: int, max_page: int) -> tuple[int, int]:
    """Native rule (width = --frame-width, height by aspect), shrunk when a frame would not fit one page."""
    fw = frame_w
    fh = max(1, round(fw * src_h / src_w))
    if fh > max_page:
        fw = max(1, math.floor(fw * max_page / fh))
        fh = max(1, round(fw * src_h / src_w))
    return fw, fh


def pack_pages(fw: int, fh: int, count: int, max_page: int) -> list[dict[str, int]]:
    cols, rows = max_page // fw, max_page // fh
    per_page = cols * rows
    pages, idx = [], 0
    while idx < count:
        n = min(per_page, count - idx)
        pages.append({"first": idx, "count": n, "cols": cols,
                      "tex_w": next_pow2(min(cols, n) * fw), "tex_h": next_pow2(math.ceil(n / cols) * fh)})
        idx += n
    return pages


def decimate(count: int, keep: int) -> list[int]:
    """Indices of `keep` frames spread evenly over `count` (first and last kept). The game tweens frame
    0 -> total-1 across the loop duration, so fewer frames keep the timing and only lose smoothness."""
    if count <= keep:
        return list(range(count))
    return [round(i * (count - 1) / (keep - 1)) for i in range(keep)]


def place_frame(frame: Image.Image, cell_w: int, cell_h: int, canvas_k: float, scale: float,
                offset: tuple[float, float]) -> Image.Image | None:
    """One runtime cell (cell_w x cell_h texels = 1280k x 720k character units) with the frame drawn where the game
    draws it: frame px * scale units, centered on the canvas center + offset. Out-of-canvas parts are clipped."""
    texel = cell_w / (1280.0 * canvas_k)                    # texels per character-space unit
    w, h = frame.width * scale * texel, frame.height * scale * texel
    if w < 0.5 or h < 0.5:
        return None
    x0 = cell_w / 2 + offset[0] * texel - w / 2
    y0 = cell_h / 2 + offset[1] * texel - h / 2
    cell = Image.new("RGBA", (cell_w, cell_h), (0, 0, 0, 0))
    iw, ih = max(1, round(w)), max(1, round(h))
    cell.paste(resample(frame, iw, ih), (round(x0), round(y0)))
    return cell


def build_sheet(tools: Tools, src: Path, h: int, v: int, total: int, *, sheet_no: int, out_dir: Path, frame_w: int,
                max_page: int, fmt: str, quality: str, max_frames: int | None, max_pages: int | None = None,
                canvas_k: float = 1.0, scale: float = 1.0, offset: tuple[float, float] = (0.0, 0.0)) -> dict:
    """Split a sheet, place every frame on its 16:9 runtime cell (see place_frame), pack the cells into t3x pages."""
    with Image.open(src) as im:
        img = im.convert("RGBA")
    frames = split_frames(img, h, v, total)
    del img
    if max_frames:
        frames = [frames[i] for i in decimate(len(frames), max_frames)]
    cw = min(frame_w, max_page)
    ch = max(1, round(cw * 9 / 16))
    pages = pack_pages(cw, ch, len(frames), max_page)
    while max_pages and len(pages) > max_pages and cw > 64:   # shrink cells until the sheet fits the page budget
        cw -= 16
        ch = max(1, round(cw * 9 / 16))
        pages = pack_pages(cw, ch, len(frames), max_page)
    page_recs, frame_recs, vram, jobs = [], [], 0, []
    for pi, pg in enumerate(pages):
        canvas = Image.new("RGBA", (pg["tex_w"], pg["tex_h"]), (0, 0, 0, 0))
        for k in range(pg["count"]):
            x, y = (k % pg["cols"]) * cw, (k // pg["cols"]) * ch
            cell = place_frame(frames[pg["first"] + k], cw, ch, canvas_k, scale, offset)
            if cell is not None:
                canvas.paste(cell, (x, y))
            frame_recs.append((pi, x, y))
        png, t3x = out_dir / f"sheet{sheet_no}_{pi}.png", out_dir / f"sheet{sheet_no}_{pi}.t3x"
        canvas.save(png)
        jobs.append(encode_async(tools, png, t3x, fmt, quality))
        page_recs.append({"t3x": t3x.name, "tex_w": pg["tex_w"], "tex_h": pg["tex_h"]})
        vram += int(pg["tex_w"] * pg["tex_h"] * BYTES_PER_PIXEL[fmt])
    wait_all(jobs)
    return {"frame_w": cw, "frame_h": ch, "pages": page_recs, "frames": frame_recs, "vram_bytes": vram,
            "source_frames": total, "frame_count": len(frames)}


def build_cover_texture(tools: Tools, src: Path, t3x: Path, *, w: int = 400, h: int = 240, tex_w: int = 512, tex_h: int = 256,
                        fmt: str = "etc1") -> None:
    """Cover-fit an image onto black inside a power-of-two texture (game over art: GameOver.tscn FailedGraphic)."""
    with Image.open(src) as im:
        rgba = im.convert("RGBA")
    base = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
    base.alpha_composite(rgba)
    img = base.convert("RGB")
    scale = max(w / img.width, h / img.height)
    nw, nh = round(img.width * scale), round(img.height * scale)
    img = img.resize((nw, nh), Image.LANCZOS).crop(((nw - w) // 2, (nh - h) // 2, (nw - w) // 2 + w, (nh - h) // 2 + h))
    tex = Image.new("RGB", (tex_w, tex_h), (0, 0, 0))
    tex.paste(img, (0, 0))
    png = t3x.with_suffix(".png")
    tex.save(png)
    tools.tex3ds_encode(png, t3x, fmt)
    png.unlink()


def build_thumb(src: Path, dst_png: Path, size: int = 96) -> None:
    """Square centre-cropped thumbnail PNG (the runtime packs thumbs into its own atlas)."""
    with Image.open(src) as im:
        img = im.convert("RGBA")
    s = min(img.width, img.height)
    img = img.crop(((img.width - s) // 2, (img.height - s) // 2, (img.width - s) // 2 + s, (img.height - s) // 2 + s))
    img.resize((size, size), Image.LANCZOS).save(dst_png)


def build_round_thumb(tools: Tools, thumb_png: Path, t3x: Path, size: int = 96) -> None:
    """menu/thumb.t3x as the runtime's Mods screens draw it (native3ds mods_ui / tools/install_mods.py): the square
    portrait masked to a circle with an anti-aliased edge, RGBA8."""
    with Image.open(thumb_png) as im:
        img = im.convert("RGBA").resize((size, size), Image.LANCZOS)
    ss = 4
    mask = Image.new("L", (size * ss, size * ss), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, size * ss - 1, size * ss - 1), fill=255)
    mask = mask.resize((size, size), Image.LANCZOS)
    r, g, b, a = img.split()
    tmp = t3x.with_name(t3x.stem + "_round.png")
    Image.merge("RGBA", (r, g, b, ImageChops.multiply(a, mask))).save(tmp)
    tools.tex3ds_encode(tmp, t3x, "rgba8")
    tmp.unlink()


# -- video -------------------------------------------------------------------------------------------

# Page layouts (one 1024x1024 ETC1 page each; the runtime reads frame/cell/grid from the BBVD header and always draws the
# frame stretched to the 400x240 top screen):
#   full  400x240 frames in 512x256 cells, 2x4 =  8 frames per page
#   half  200x120 frames in 256x128 cells, 4x8 = 32 frames per page (a quarter of the SD reads)
VIDEO_LAYOUTS = {"full": (400, 240, 512, 256, 2, 4), "half": (200, 120, 256, 128, 4, 8)}


def video_frames_per_page(res: str) -> int:
    l = VIDEO_LAYOUTS[res]
    return l[4] * l[5]


def build_video(tools: Tools, src: Path | None, tag: str, out_dir: Path, quality: str = "medium", max_fps: float = 30.0,
                res: str = "full", audio_src: Path | None = None, own_audio: bool = True) -> dict:
    """max_fps: clips above it are resampled to it. res: page layout (VIDEO_LAYOUTS). On the console an SD card delivers
    about 1.5 MB/s, and every page is ~250 KB: full-res 30 fps video (a page every 267 ms) saturates it, half-res does not."""
    W, H, CW, CH, COLS, ROWS = VIDEO_LAYOUTS[res]
    if src is None:   # sound only (an outro final_audio without a final_video): no pages, a 1 ms "picture"
        pcm = tools.decode_pcm(audio_src, channels=2) if audio_src is not None else b""
        (out_dir / f"{tag}.pcm").write_bytes(pcm)
        return {"fps": 1000.0, "frames": 1, "pages": [], "audio_name": f"{tag}.pcm", "audio_frames": len(pcm) // 4,
                "layout": {"frame_w": W, "frame_h": H, "cell_w": CW, "cell_h": CH, "cols": COLS, "rows": ROWS}}
    frames_dir = Path(tempfile.mkdtemp(prefix=f"mod2ds_{tag}_"))
    try:
        src_fps = tools.fps(src)
        fps = min(src_fps, max_fps) if max_fps and max_fps > 0 else src_fps
        rate = f"fps={fps:g}," if fps < src_fps else ""
        tools.run(tools.ffmpeg, "-v", "error", "-i", str(src), "-fps_mode", "passthrough",
                  # cover-fit; same pixels as native's scale=-2:240,crop=400:240 for >= 5:3 sources, and it also
                  # handles narrower (portrait) mod videos that the native filter cannot crop
                  "-vf", f"{rate}scale={W}:{H}:force_original_aspect_ratio=increase:flags=lanczos,crop={W}:{H}",
                  str(frames_dir / "%05d.png"))
        frames = sorted(frames_dir.glob("*.png"))
        if not frames:
            raise ToolError(f"ffmpeg produced no frames for {src.name}")
        per_page, pages, jobs = COLS * ROWS, [], []
        for p0 in range(0, len(frames), per_page):
            page = Image.new("RGB", (1024, 1024), (0, 0, 0))
            for k, f in enumerate(frames[p0:p0 + per_page]):
                with Image.open(f) as fr:
                    page.paste(fr.convert("RGB"), ((k % COLS) * CW, (k // COLS) * CH))
            name = f"{tag}_{p0 // per_page}"
            png, t3x = out_dir / f"{name}.png", out_dir / f"{name}.t3x"
            page.save(png)
            jobs.append(encode_async(tools, png, t3x, "etc1", quality))
            pages.append(t3x.name)
        wait_all(jobs)
        # picture-only clips (e.g. a full-motion mod whose sound is the song) get an empty soundtrack: audio_frames 0
        if audio_src is not None:
            pcm = tools.decode_pcm(audio_src, channels=2)
        else:
            pcm = tools.decode_pcm(src, channels=2) if own_audio and tools.has_audio(src) else b""
        (out_dir / f"{tag}.pcm").write_bytes(pcm)
        return {"fps": fps, "frames": len(frames), "pages": pages, "audio_name": f"{tag}.pcm", "audio_frames": len(pcm) // 4,
                "layout": {"frame_w": W, "frame_h": H, "cell_w": CW, "cell_h": CH, "cols": COLS, "rows": ROWS}}
    finally:
        shutil.rmtree(frames_dir, ignore_errors=True)
