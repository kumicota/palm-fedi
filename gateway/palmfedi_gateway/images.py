"""Fetch remote images and transcode them into the PFI1 Palm bitmap format."""

from __future__ import annotations

import io
import struct
import threading
from collections import OrderedDict

from PIL import Image, ImageChops, ImageOps

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


def _load(data: bytes) -> Image.Image:
    img = Image.open(io.BytesIO(data))
    img.seek(0)  # first frame of animations
    img = ImageOps.exif_transpose(img)
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
    return encode(_resize(_load(data), w, h, crop), bpp, density)


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
