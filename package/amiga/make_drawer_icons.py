#!/usr/bin/env python3
"""GlowIcons for the drawers OpenTBBlue's first start makes on the Amiga.

    python3 package/amiga/make_drawer_icons.py AMICRADLE

AMICRADLE is an AmiCradle checkout (DalsinAI/amicradle): its
scripts/port_icons.py draws the Team's drawer with a badge at its foot, as
for the OpenTBBlue drawer itself. Writes package/amiga/files/Icons/:

  OpenTBBlue-data.info  the program's home (PROGDIR:OpenTBBlue-data)
  drives.info           the Next's SD cards
  card.info             each card in drives/ (SD0, SD1, and an .img's)
  media.info            the downloads, kept as they came
  drawer.info           any other drawer it makes (pi/, the NextPi's)

The program copies them in beside each drawer that has no icon yet
(src/setup.c, otb_drawer_icons). The badges are package/icons' art
(make_art.py). MIT, Copyright (c) 2026 AmiCradle Team."""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ICONS = HERE.parent / "icons"
OUT = HERE / "files" / "Icons"
WANT = {
    "OpenTBBlue-data.info": "opentbblue-64.png",
    "drives.info": "sdcard-64.png",
    "card.info": "sdcard-64.png",
    "media.info": "media-64.png",
    "drawer.info": "opentbblue-64.png",
}


def main() -> int:
    sys.path.insert(0, str(Path(sys.argv[1]).resolve() / "scripts"))
    import amiga_icon
    import port_icons as pi
    OUT.mkdir(parents=True, exist_ok=True)
    for name, art in WANT.items():
        meta = {"type": pi.TYPES["drawer"], "defaultTool": None, "toolTypes": [], "toolWindow": None,
                "stack": 4096, "x": amiga_icon.NO_POSITION, "y": amiga_icon.NO_POSITION,
                "drawer": {"left": 80, "top": 60, "width": 360, "height": 160,
                           "flags": 0, "viewModes": 0, "currentX": 0, "currentY": 0}}
        p = pi._write(OUT / name, meta, pi.frames("drawer", pi.Picture.load(ICONS / art)))
        print(f"{p.name}: {p.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
