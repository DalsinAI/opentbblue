#!/usr/bin/env python3
"""boot_check.py PICTURE.ppm RRGGBB MIN: at least MIN pixels of that colour.

CI boots the Next from the fetched distribution and checks the picture
with this (NextZXOS's menu has a cyan bar and a rainbow stripe)."""
import sys


def main(path, colour, least):
    data = open(path, "rb").read()
    parts = data.split(maxsplit=4)
    if parts[0] != b"P6" or parts[3] != b"255":
        sys.exit(f"{path}: not a P6 PPM")
    w, h, px = int(parts[1]), int(parts[2]), parts[4]
    want = bytes.fromhex(colour)
    n = sum(1 for i in range(0, w * h * 3, 3) if px[i:i + 3] == want)
    print(f"{path}: {n} pixels of #{colour} (need {least})")
    return 0 if n >= least else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2], int(sys.argv[3])))
