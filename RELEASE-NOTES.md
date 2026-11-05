OpenTBBlue 0.1.0: the ZX Spectrum Next in one window.

- First run downloads the latest System/Next distribution and boots NextZXOS with no setup.
- Installers: a Windows setup, a macOS disk image, a Debian/Ubuntu package and an Amiga LhA with its Installer script and GlowIcons. The first start shows its download in a small window, so it works straight from an icon.
- Drives are folders: the card is `drives/SD0`, a second card can go in the second slot, and official downloads are kept unchanged in a `media` repository. An SD card image (`.img`) is kept there too and its files written into a drive folder with OpenTBBlue's own FAT reader. Defaults are in `opentbblue.ini`.
- The SD card is a folder on your computer. OpenTBBlue checks for a newer System/Next itself and offers to install it over the card or write a new card you can pick from the bar; `--update` does the same from the command line.
- Full screen on F11, or the Help key on an Amiga.
- PC keyboard as on a Next's PS/2 port, Kempston joystick from game controllers, Kempston mouse, sound, wifi through the host, NextPi.
- Fast: on a 68040 it runs NextZXOS at about four times the speed of a real Next with no window, and 28 MHz games such as Warhawk at a full 50 frames a second in a window. Every speed change is checked to give the same pictures and sound.
- All four are attached below. The AmigaOS 68040 build is made with the Team's OpenAmigaGCC and the GFOpenGPU SDL2 (docs/Amiga.md).
