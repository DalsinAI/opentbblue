#!/usr/bin/env python3
"""OpenTBBlue's icon art: a dark rounded key with the four stripes across
its corner. Writes opentbblue.png (512), opentbblue-64.png (the Amiga
badge), opentbblue.ico (Windows, 16 to 256). Needs Pillow.
MIT, Copyright (c) 2026 AmigaChrome Team."""
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


if __name__ == "__main__":
    big = art(512)
    big.save(HERE / "opentbblue.png")
    art(64).save(HERE / "opentbblue-64.png")
    big.save(HERE / "opentbblue.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("wrote", ", ".join(p.name for p in sorted(HERE.glob("opentbblue*"))))
