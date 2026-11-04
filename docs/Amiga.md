# OpenTBBlue on AmigaOS (68040)

The Amiga build is the same program with the same SDL2 window. It needs
AmigaOS 3.2, a 68040 and an RTG screen. The first-run download uses an
Amiga `curl` in the path (OpenUp installs one in `C:`), or copy a card made
by another OpenTBBlue into `OpenTBBlue-data/drives/SD0`. The files go in
`PROGDIR:OpenTBBlue-data`, beside the program, so put it on your fastest
disk.

On Linux (x86-64, glibc 2.38 or later), one script fetches the compiler
(OpenAmigaGCC 16.2.0-open6), the OpenNDK headers and the GFOpenGPU SDL2
link library at pinned versions, builds `opentbblue.68k` and packs the
LhA with its Installer:

```sh
sh package/amiga/build-ci.sh 0.1.0 dist
```

With an m68k-amigaos-gcc of your own and an SDL2 for it:

```sh
make amiga AMIGA_CC=/path/to/m68k-amigaos-gcc AMIGA_SDL=/path/to/sdl2-prefix
```

The SDL2 link library loads GFOpenGPU's `SDL2.module` at run time, which
the Installer copies into `LIBS:` if it isn't there. The Next's wifi uses `bsdsocket.library`, so any
TCP/IP stack works; with none running, the Next just sees no network.

It's built 68040-only, soft-float (the compiler picks the soft-float libraries
for `-m68040`). The machine core has no floating point
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

Round 2, checked the same way (and with `-DOTB_CHECK_MIX`):

- The plain Spectrum screen is drawn a byte (8 pixels) at a time: the
  pixel and attribute bytes and the ink and paper colours are looked up
  once per byte, not once per half-pixel.
- Events: the earliest due event's tick is kept in `ev_next`, so most bus
  cycles make one compare. Port and nextreg writes, the line interrupt,
  the copper, the UARTs, resets and each new frame clear it.
- Opcode fetches at no automap entry point skip the automap call.
- The video position maths is 32-bit, so there's no 64-bit divide (a
  library call on the 68040) on each video catch-up.
- A silent AY (all three volumes 0, no envelope) moves its counters on in
  bulk instead of a tick at a time. Turbosound has three of them.

| Run | Before round 2 | After round 2 |
| --- | --- | --- |
| Five scenarios (x86-64) | 57 s | 34 s |
| NextZXOS, 1500 frames, no window (x86-64) | 5.01 s (300 fps) | 2.29 s (654 fps) |
| NextZXOS, 1500 frames, no window (68040 lab) | 31.98 s (46.9 fps, 94%) | 18.77 s (79.9 fps, 160%) |
| NextZXOS, 500 frames, no window (68040 lab) | 9.39 s (53.3 fps, 107%) | 5.73 s (87.3 fps, 175%) |

In the window on the 68040 lab, round 2 holds 49.4 frames a second (real
time is 50), drawing only the frames that changed.

Next:

1. Measure on a real 68040. In the lab SDL2 draws through WritePixelArray
   (its bitmap lock fails there), the slower path.
2. The Z80N's clock is 64-bit; on a 68040 every add and compare on it is
   two instructions. A 32-bit clock inside the frame is the next step.
