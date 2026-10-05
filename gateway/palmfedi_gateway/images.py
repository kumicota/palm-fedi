"""Fetch remote images and transcode them into the PFI1 Palm bitmap format."""

from __future__ import annotations

import io
import logging
import shutil
import struct
import subprocess
import threading
from collections import OrderedDict

from PIL import Image, ImageChops, ImageOps

log = logging.getLogger("palmfedi")

# Optional decoders for formats older Pillow builds can't read (AVIF, HEIC).
try:
    import pillow_avif  # noqa: F401  (registers itself)
except ImportError:
    pass
try:
    import pillow_heif
    pillow_heif.register_heif_opener()
    if hasattr(pillow_heif, "register_avif_opener"):
        pillow_heif.register_avif_opener()
except Exception:
    pass

FFMPEG = shutil.which("ffmpeg")
FFMPEG_TIMEOUT = 30

MAGIC = b"PFI1"
DENSITY_DOUBLE = 144
DENSITY_LOW = 72
MAX_SIDE = 1024  # native pixels; the LifeDrive screen is 320x480
MAX_SOURCE_BYTES = 16 * 1024 * 1024

Image.MAX_IMAGE_PIXELS = 40_000_000


def palette() -> list[tuple[int, int, int]]:
    """The fixed 256-colour PalmFedi palette (mirrored in palm/src/image.c)."""
    levels = [0, 51, 102, 153, 204, 255]
    pal = [(levels[r], levels[g], levels[b]) for r in range(6) for g in range(6) for b in range(6)]
    pal += [(round(i * 255 / 41),) * 3 for i in range(1, 41)]
    return pal


_PALETTE_IMAGE = Image.new("P", (1, 1))
_PALETTE_IMAGE.putpalette([c for rgb in palette() for c in rgb])


class ImageError(Exception):
    """The data isn't an image we can decode."""


def _ffmpeg_frame(source: str, data: bytes | None = None) -> bytes:
    """First video frame (or the picture itself) as PNG, via ffmpeg.

    *source* is ``"pipe:0"`` (decode *data*) or an http(s) URL, which lets
    ffmpeg fetch only what it needs from a large video.
    """
    if not FFMPEG:
        raise ImageError("no ffmpeg")
    cmd = [FFMPEG, "-nostdin", "-hide_banner", "-loglevel", "error",
           "-protocol_whitelist", "pipe,http,https,tcp,tls,crypto",
           "-i", source, "-frames:v", "1", "-f", "image2pipe", "-c:v", "png", "pipe:1"]
    try:
        res = subprocess.run(cmd, input=data, capture_output=True, timeout=FFMPEG_TIMEOUT)
    except (OSError, subprocess.TimeoutExpired) as e:
        raise ImageError(f"ffmpeg failed: {e}") from None
    if res.returncode != 0 or not res.stdout:
        raise ImageError("ffmpeg: " + res.stderr.decode("utf-8", "replace").strip()[-200:])
    return res.stdout


def video_frame(url: str) -> bytes:
    """Preview frame of a remote video, as PNG (needs ffmpeg)."""
    return _ffmpeg_frame(url)


def _open(data: bytes) -> Image.Image:
    try:
        img = Image.open(io.BytesIO(data))
        img.seek(0)  # first frame of animations
        img.load()
        return img
    except Image.DecompressionBombError:
        raise ImageError("image too large") from None
    except Exception as e:  # UnidentifiedImageError, truncated files, codec errors
        if not FFMPEG:
            raise ImageError(str(e) or e.__class__.__name__) from None
    # Pillow can't read it (e.g. AVIF/HEIC on an older Pillow, or a video
    # served as a "preview"): let ffmpeg turn it into a PNG.
    try:
        return Image.open(io.BytesIO(_ffmpeg_frame("pipe:0", data)))
    except ImageError:
        raise
    except Exception as e:
        raise ImageError(str(e)) from None


def _load(data: bytes) -> Image.Image:
    img = _open(data)
    try:
        img = ImageOps.exif_transpose(img)
    except Exception:
        pass  # broken EXIF; keep the image as is
    if img.mode in ("RGBA", "LA", "P", "PA"):
        img = img.convert("RGBA")
        bg = Image.new("RGB", img.size, (255, 255, 255))
        bg.paste(img, mask=img.getchannel("A"))
        return bg
    return img.convert("RGB")


def _resize(img: Image.Image, w: int, h: int, crop: bool) -> Image.Image:
    w = max(1, min(w, MAX_SIDE))
    h = max(1, min(h, MAX_SIDE * 3))
    if crop:
        return ImageOps.fit(img, (w, h), Image.Resampling.LANCZOS)
    img = img.copy()
    img.thumbnail((w, h), Image.Resampling.LANCZOS)  # never upscales
    return img


def _rgb565(img: Image.Image) -> bytes:
    """RGB565 big endian: hi byte RRRRRGGG, lo byte GGGBBBBB."""
    r, g, b = img.split()
    hi = ImageChops.add(Image.eval(r, lambda v: v & 0xF8), Image.eval(g, lambda v: v >> 5))
    lo = ImageChops.add(Image.eval(g, lambda v: ((v >> 2) & 7) << 5), Image.eval(b, lambda v: v >> 3))
    return Image.merge("LA", (hi, lo)).tobytes()


def encode(img: Image.Image, bpp: int, density: int = DENSITY_DOUBLE) -> bytes:
    w, h = img.size
    if bpp == 8:
        q = img.quantize(palette=_PALETTE_IMAGE, dither=Image.Dither.FLOYDSTEINBERG)
        raw = q.tobytes()
        row_bytes = w + (w & 1)
        if w & 1:
            raw = b"".join(raw[y * w:(y + 1) * w] + b"\0" for y in range(h))
    else:
        bpp = 16
        raw = _rgb565(img)
        row_bytes = w * 2
    header = MAGIC + struct.pack(">HHBBH", w, h, bpp, density, row_bytes)
    return header + raw


def transcode(data: bytes, w: int, h: int, bpp: int = 16, crop: bool = False,
              density: int = DENSITY_DOUBLE) -> bytes:
    """Decode *data* and return a PFI1 image; raises ImageError if it can't."""
    try:
        return encode(_resize(_load(data), w, h, crop), bpp, density)
    except ImageError:
        raise
    except Exception as e:
        raise ImageError(str(e) or e.__class__.__name__) from None


class ByteLRU:
    """Tiny thread-safe LRU bounded by total byte size."""

    def __init__(self, max_bytes: int):
        self.max_bytes = max_bytes
        self._d: OrderedDict = OrderedDict()
        self._size = 0
        self._lock = threading.Lock()

    def get(self, key):
        with self._lock:
            v = self._d.get(key)
            if v is not None:
                self._d.move_to_end(key)
            return v

    def put(self, key, value: bytes) -> None:
        if len(value) > self.max_bytes:
            return
        with self._lock:
            old = self._d.pop(key, None)
            if old is not None:
                self._size -= len(old)
            self._d[key] = value
            self._size += len(value)
            while self._size > self.max_bytes:
                _, v = self._d.popitem(last=False)
                self._size -= len(v)
