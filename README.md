# OpenTBBlue

A ZX Spectrum Next (TBBlue) emulator in one window, for Linux, Windows,
macOS and AmigaOS 3.2 (68040).

There's nothing to set up. On the first run OpenTBBlue downloads the latest
official Next software distribution
([System/Next](https://gitlab.com/thesmog358/tbblue): firmware, NextZXOS,
ROMs, apps, games and demos) and unpacks it into a folder on your computer.
That folder becomes the Next's SD card, and the Next boots straight into
NextZXOS.

The Next is modelled on its official FPGA sources: the Z80N at 3.5, 7, 14
and 28 MHz, memory and paging, every nextreg, the ULA, LoRes, Timex modes,
the tilemap, Layer 2 and sprites with the mixer, the copper, DMA, CTC, IM2,
divMMC automap, three AYs, the DACs, both UARTs, a Kempston mouse and
joystick, ESP8266 wifi through your computer's network, and a Raspberry Pi
(NextPi) with its files in a host folder. The machine core comes from
[ZxNextEmu](https://github.com/DalsinAI/ZxNextEmu).

**Download:** [the latest release](https://github.com/DalsinAI/opentbblue/releases)
(now 0.1.0-rc1, the first release candidate) has an installer for each system.

It is quick on small machines too: on a 68040 Amiga it runs NextZXOS at 160%
of a real Next's speed with no window, and at full speed in a window
(figures in [docs/Amiga.md](docs/Amiga.md)).

## Installing

Each release has an installer for every system it runs on:

| System | Get | Then |
| --- | --- | --- |
| Windows 10 and 11 | `OpenTBBlue-…-windows-setup.exe` | run it; OpenTBBlue is in the Start menu (no administrator needed) |
| macOS 11 and later (Apple silicon) | `OpenTBBlue-…-macos-arm64.dmg` | open it and drag OpenTBBlue to Applications. It isn't signed by Apple yet, so the first time, right-click it and choose Open |
| Debian, Ubuntu, Mint (x86-64) | `OpenTBBlue-…-linux-x86_64.deb` | `sudo apt install ./OpenTBBlue-…-linux-x86_64.deb`; it's in the games menu. Other Linuxes: the `.tar.gz` |
| AmigaOS 3.2, 68040 or better, RTG | `OpenTBBlue-…-amiga-68040.lha` | unpack it and run Install-OpenTBBlue, which puts the OpenTBBlue drawer in Work: (or where you choose) and copies GFOpenGPU's SDL2.module if it isn't there. The first download needs a `curl` (OpenUp installs one). Updating keeps your OpenTBBlue-data |

Then start it. The first start shows a small window while it downloads
System/Next (about 100 MB) and writes it to the SD card, and then the Next
boots. Every start after that goes straight to the Next.

## Running

```sh
make            # needs a C compiler and SDL2
./opentbblue
```

| Option | Does |
| --- | --- |
| `-c PATH` | SD card: a folder, or an image (`.img`), see below |
| `--sd1 PATH` | the second SD card slot, the same way |
| `-p PATH` | the Raspberry Pi's folder |
| `-z N` | window scale (default 3) |
| `-F` | full screen |
| `--update` | refresh the card with the latest distribution (your own files stay) |
| `--fetch` / `--no-fetch` | download now / never download |
| `--home` | show where OpenTBBlue keeps its files |
| `-H N [-o pic.ppm]` | no window: run N frames (tests) |
| `-t TEXT [-d N]` | type TEXT (`\n` is ENTER) after N frames |
| `-b` | print the speed |

### Keys

The PC keyboard works like a PS/2 keyboard on a Next. Shift is CAPS SHIFT, and
Ctrl or Alt is SYMBOL SHIFT. Punctuation types the symbol printed on the key.
Backspace is DELETE, the arrow keys move the cursor, Esc is BREAK, Tab is
EXTEND MODE and Home is EDIT. Game controllers act as a Kempston joystick,
and the mouse is a Kempston mouse.

F1 is a hard reset (back through the firmware), F4 a soft reset, F9 runs at
full speed, F11 switches full screen, and F10 quits.

## Where the files go

| System | Folder |
| --- | --- |
| Linux | `~/.local/share/opentbblue` (or `$XDG_DATA_HOME/opentbblue`) |
| macOS | `~/Library/Application Support/OpenTBBlue` |
| Windows | `%APPDATA%\OpenTBBlue` |
| AmigaOS | `PROGDIR:OpenTBBlue-data` |

`OPENTBBLUE_HOME` overrides the location (on AmigaOS, set it with `SetEnv`
or `Set`). Inside it, laid out the way
AmiCradle keeps its machines:

| Inside | Is |
| --- | --- |
| `opentbblue.ini` | the settings: which card is in each slot, window size, full screen, fetching. Written with the defaults on the first run; options on the command line win over it |
| `drives/SD0/` | the Next's SD card, as ordinary files. Copy `.nex`, `.tap`, `.sna` or anything else in and NextZXOS's Browser can see it. What the Next saves appears there about a second later. `.opentbblue` records which distribution commit is on it |
| `drives/<name>/` | a card made from an image (below) |
| `media/` | the media repository: the official downloads, kept just as they came, each with a `.json` note of where it came from. The System/Next archive is kept here, so `--update` and new cards don't download it again |
| `pi/` | the Raspberry Pi's files |

Drives are folders. An image (`.img`) given with `-c`, `--sd1` or in the
settings is copied into `media/` unchanged, and OpenTBBlue's own FAT reader
(FAT12, FAT16 and FAT32, long names) writes its files into
`drives/<image name>/`, which is then the card. That happens once; after
that the folder is used, with whatever the Next has saved in it. A card
from an older OpenTBBlue in `sd/` moves to `drives/SD0/` by itself.

For example, to use a card image you already have in the second slot:

```sh
opentbblue --sd1 ~/Downloads/mycard.img
```

or put `sd1=mycard.img` in `opentbblue.ini` after copying the image into the
folder. The settings file the first run writes looks like this:

```ini
sd0=drives/SD0
sd1=
pi=pi
scale=3
fullscreen=0
fetch=1
```

The first boot writes `machines/next/config.ini` on the card with `timing=0` (VGA at
50 Hz), so it skips the monitor test card. Change it in the firmware's editor
(hold Space while it boots, then press E) like on a real Next.

The download uses your system's `curl`. Windows 10 and later, macOS and Linux
include it. On AmigaOS, any `curl` in the path works (OpenUp installs one in `C:`).

## The distribution

OpenTBBlue doesn't include any of the Next's software. The distribution is
downloaded whole from its own project and is used under the Next License in
`LICENSE.md` on the card, which allows the whole distribution to run on other
hardware. Its parts keep their own licences.

## Building

| Target | Builds |
| --- | --- |
| `make` | `opentbblue` |
| `make test` | the tar reader, image reader and first-boot config tests |
| `make amiga` | `opentbblue.68k` for AmigaOS 3.2 on a 68040, with the OpenGPU SDL2. See [docs/Amiga.md](docs/Amiga.md) |

Any SDL2 2.x builds it (on macOS, Homebrew's `sdl2` is fine for a local
build). The release builds are made by `.github/workflows/release.yml`: it
builds and install-tests all four installers on every pull request that
touches the program or the packages, and publishes them when it runs with
a version (a `v*` tag, or run by hand with `version`, such as `v0.1.0`; a
version with a suffix such as `-rc1` is published as a pre-release). The
macOS app has SDL2 built in, so it carries no libraries; the Amiga build
uses the Team's OpenAmigaGCC, OpenNDK and the GFOpenGPU SDL2, all pinned in
`package/amiga/build-ci.sh`.

## Licence

GPL-3.0-or-later (see `LICENSE`). The Next's boot ROM in `src/next/bootrom.c`
comes from the GPL-3 SpectrumNext FPGA sources.

Designed, tested and delivered by The AmigaChrome Team.
