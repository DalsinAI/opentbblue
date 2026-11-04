# OpenTBBlue on AmigaOS (68040)

The Amiga build is the same program with the same SDL2 window, built with
OpenNDK's m68k-amigaos-gcc against [gfopensdl](https://github.com/DalsinAI/gfopensdl)
(SDL 2 on OpenRTG). It needs AmigaOS 3.2, a 68040 and an RTG screen. The first-run
download uses `curl` from [gfopencurl](https://github.com/DalsinAI/gfopencurl).
The files go in `PROGDIR:OpenTBBlue-data`, beside the program, so put it on
your fastest disk.

```sh
make amiga AMIGA_SDL=/path/to/gfopensdl/prefix     # gives opentbblue.68k
```

It's built 68040-only and soft-float. The machine core has no floating point
at all, so soft-float costs nothing. `-O3 -fomit-frame-pointer` frees a
register for the CPU loop.

## Speed

A real Next runs a 3.5 to 28 MHz Z80N and mixes four video layers at 14 MHz
for every pixel. That's a lot of work for a 68040, so the Amiga build gets
these:

Already in (all platforms):

- The picture goes to a 16-bit texture, not a 32-bit one, which halves what's moved.
- Only lines that changed since the last drawn frame are converted and
  uploaded. A still NextZXOS screen costs nearly nothing to draw.
- A frame that changed nothing isn't presented.
- When the emulation falls behind, frames are skipped (up to 4 in a row)
  while the sound keeps the pace. Timing inside the machine is untouched.
- `-b` prints frames a second, and how many frames were drawn, skipped and
  converted, so each change can be measured.

Profile of the core (x86-64, gprof, NextZXOS booting and at its menu, 1500
frames):

| Function | Share | What it is |
| --- | --- | --- |
| `mix` (next/video.c) | 48% | the layer mixer, run for each of the 640 x 256 half-pixels |
| `automap_fetch` | 17% | the divMMC automap check on every opcode fetch |
| `events` | 12% | the per-cycle check for due events |
| `b_fetch` | 8% | the bus's opcode fetch |
| `z80_step` | 5% | the Z80N itself |

Done, each one checked frame for frame with `tests/compare.sh` (five
scenarios: NextBASIC, the tilemap demo, Warhawk, show512 and Spectron
2084, with every 50th picture and all of the sound hashed). Building with
`-DOTB_CHECK_MIX` also checks the mixer's fast path against the full mixer
on every pixel.

| Change | NextZXOS menu, 3000 frames (x86-64) |
| --- | --- |
| Before | 16.9 s (177 frames a second) |
| Mixer: ULA-only lines skip the other layers; one ULA pixel for both halves unless hi-res or half-pixel scroll | |
| divMMC automap: fetches at no entry point skip the trigger checks | 10.1 s (296 frames a second) |

That's 40% less time, with the same pictures and sound in all five
scenarios. Across all five scenarios, 75 s went down to 54 s.

Next:

1. A ULA line renderer, a byte (8 pixels) at a time, for the plain
   Spectrum screen. `ula_pixel` is now the largest cost.
2. Events: keep the next due event's tick in one variable, so most cycles
   make one compare.
3. Measure on the AmiCradle 68040 lab and then on a real 68040, and post the
   table with each change.
