#!/usr/bin/env python3
"""OpenTBBlue's icon art: a dark rounded key with the four stripes across
its corner. Writes opentbblue.png (512), opentbblue-64.png (the Amiga
badge), opentbblue.ico (Windows, 16 to 256), and the Amiga badges for the
drawers the first start makes: sdcard-64.png (a card, for drives/ and
each card in it) and media-64.png (the downloads, a box with the stripes).
Needs Pillow.
MIT, Copyright (c) 2026 AmiCradle Team."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
STRIPES = [(222, 24, 24), (240, 206, 0), (24, 190, 40), (0, 196, 228)]


def art(size: int) -> Image.Image:
    s = 4 * size                                    # drawn large, then shrunk smooth
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    pad, r = s // 16, s // 6
    d.rounded_rectangle([pad, pad, s - pad, s - pad], r, fill=(28, 30, 38, 255))
    # the stripes, corner to corner, clipped to the key
    stripes = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ds = ImageDraw.Draw(stripes)
    w = s // 10
    start = int(s * 1.02)                           # bands of x + y = c across the lower right
    for i, c in enumerate(STRIPES):
        lo, hi = start + i * w, start + (i + 1) * w
        ds.polygon([(lo + s, -s), (hi + s, -s), (-s, hi + s), (-s, lo + s)], fill=c + (255,))
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([pad, pad, s - pad, s - pad], r, fill=255)
    clipped = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    clipped.paste(stripes, (0, 0), mask)
    im = Image.alpha_composite(im, clipped)
    # TB, for TBBlue, in the free corner
    font = ImageFont.load_default(size=int(s * 0.30))
    ImageDraw.Draw(im).text((int(s * 0.17), int(s * 0.12)), "TB", font=font, fill=(240, 242, 248, 255))
    # a light rim
    ImageDraw.Draw(im).rounded_rectangle([pad, pad, s - pad, s - pad], r, outline=(210, 214, 224, 255), width=s // 64)
    return im.resize((size, size), Image.LANCZOS)


def sdcard(size: int) -> Image.Image:
    """An SD card: the cut corner, the contacts, the stripes on its label."""
    s = 4 * size
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    l, t, r, b, cut = int(s * 0.18), int(s * 0.06), int(s * 0.82), int(s * 0.94), int(s * 0.16)
    body = [(l + cut, t), (r, t), (r, b), (l, b), (l, t + cut)]
    d.polygon(body, fill=(28, 30, 38, 255), outline=(210, 214, 224, 255), width=s // 64)
    cw = (r - l - cut) // 9                         # the contacts along the top
    for i in range(8):
        x = l + cut + cw // 2 + i * cw
        d.rectangle([x, t + s // 32, x + cw * 2 // 3, t + s // 7], fill=(232, 190, 64, 255))
    lt, lb = int(s * 0.42), int(s * 0.86)           # the label: the four stripes
    bw = (lb - lt) // 4
    for i, c in enumerate(STRIPES):
        d.rectangle([l + s // 16, lt + i * bw, r - s // 16, lt + (i + 1) * bw - 1], fill=c + (255,))
    return im.resize((size, size), Image.LANCZOS)


def media(size: int) -> Image.Image:
    """The downloads kept as they came: a box, the stripes on its side."""
    s = 4 * size
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    l, t, r, b, depth = int(s * 0.10), int(s * 0.30), int(s * 0.74), int(s * 0.92), int(s * 0.16)
    d.polygon([(l, t), (l + depth, t - depth), (r + depth, t - depth), (r, t)], fill=(70, 74, 90, 255))
    d.polygon([(r, t), (r + depth, t - depth), (r + depth, b - depth), (r, b)], fill=(44, 46, 58, 255))
    d.rectangle([l, t, r, b], fill=(28, 30, 38, 255), outline=(210, 214, 224, 255), width=s // 64)
    bw = (r - l) // 8
    for i, c in enumerate(STRIPES):
        x = l + (i + 2) * bw
        d.rectangle([x, t + s // 16, x + bw - s // 64, b - s // 16], fill=c + (255,))
    return im.resize((size, size), Image.LANCZOS)


if __name__ == "__main__":
    sdcard(64).save(HERE / "sdcard-64.png")
    media(64).save(HERE / "media-64.png")
    big = art(512)
    big.save(HERE / "opentbblue.png")
    art(64).save(HERE / "opentbblue-64.png")
    big.save(HERE / "opentbblue.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("wrote", ", ".join(p.name for p in sorted(HERE.glob("opentbblue*"))))
