"""Generate the launcher icon BMPs used by PalmFedi.rcp (needs Pillow).

The generated files are committed, so this only needs to run when the
artwork changes:  python3 make_icons.py
"""
from PIL import Image, ImageDraw

MAGENTA = (255, 0, 255)  # transparent colour in the .rcp
BLUE = (51, 51, 153)
WHITE = (255, 255, 255)


def bubble(size, scale):
    """Speech bubble with three dots, drawn at size*scale pixels."""
    w, h = size[0] * scale, size[1] * scale
    img = Image.new("RGB", (w, h), MAGENTA)
    d = ImageDraw.Draw(img)
    pad = scale
    body_bottom = int(h * 0.78)
    d.rounded_rectangle([pad, pad, w - pad - 1, body_bottom], radius=4 * scale, fill=BLUE)
    d.polygon([(int(w * 0.28), body_bottom - 1), (int(w * 0.48), body_bottom - 1),
               (int(w * 0.22), h - 1)], fill=BLUE)
    r = max(1, int(scale * 1.5)) if size[0] > 16 else max(1, scale)
    cy = (pad + body_bottom) // 2
    for i in range(3):
        cx = int(w * (0.30 + 0.20 * i))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=WHITE)
    return img


def mono(img):
    """1-bit version: bubble black, everything else white."""
    out = Image.new("1", img.size, 1)
    px, opx = img.load(), out.load()
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            if px[x, y] == BLUE:
                opx[x, y] = 0
    return out


for name, size in (("icon", (22, 22)), ("sicon", (15, 9))):
    lo = bubble(size, 1) if size[0] > 16 else bubble(size, 1)
    hi = bubble(size, 2)
    mono(lo).save(f"{name}-1.bmp")
    lo.save(f"{name}-8.bmp")
    hi.save(f"{name}-8hd.bmp")
