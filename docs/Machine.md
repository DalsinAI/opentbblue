# How the emulated machine works

OpenTBBlue emulates a ZX Spectrum Next: the TBBlue FPGA core, plus the
parts around it on the board. The core is written from the FPGA's own VHDL
(the SpectrumNext ZX_Spectrum_Next_FPGA sources: `zxnext.vhd` and the
`device/`, `video/`, `audio/` and `serial/` modules). It models what the
hardware does, not what a program expects. So the official firmware and
NextZXOS boot and run as they are, from the real System/Next distribution.

This page follows the machine from power on, then goes through it part by
part. File names are under `src/`.

## Power on to NextZXOS

1. **Boot ROM** (`next/bootrom.c`). The FPGA's small boot program is
   mapped at 0000-3FFF. It sets the SD card up and loads `TBBLUE.FW` from
   the card's root.
2. **Firmware** (`TBBLUE.FW`, from the card). It runs in *config mode*
   (nextreg 0x03 = 7), where 0000-3FFF can be any 16K bank of SRAM. It
   reads `machines/next/config.ini` and `menu.def`, and copies the ROMs for
   the chosen machine into the bottom 64K of SRAM:
   - the four NextZXOS ROMs
   - the divMMC ROM (`enNxtmmc.rom`) at 0x010000
   - the multiface ROM (`enNextMf.rom`) at 0x014000
   - the alternate ROMs at 0x018000

   It also sets the machine type and timing (nextreg 0x03), the peripherals
   (0x05, 0x06, 0x08, 0x0A) and the port enables (0x82-0x89), then leaves
   config mode with a soft reset.
3. **NextZXOS** starts from ROM 0. It finds the SD card again through
   divMMC and esxDOS-style calls, and brings up the menu.

OpenTBBlue writes `timing=0` into `config.ini` on the first boot, so the
firmware skips the monitor test card. Holding Space while it boots opens
the firmware's own menu, as on a real Next.

## The program loop

`opentbblue.c` runs the machine one video frame at a time, 50 a second:

- `zx_run_frame` (in `next/next.c`) runs the CPU until the frame's last
  clock, handling memory, I/O, interrupts, DMA, the copper and the
  devices as it goes.
- `video.c` converts the lines of the picture that changed to the
  window's 16-bit format, uploads them, and queues the frame's sound.
- **Pacing.** The sound queue paces the program: it keeps about three
  frames of sound queued and waits while there is more.
- **Frame skipping.** When the PC falls behind, up to four frames in a row
  are left undrawn (`OTB_MAX_SKIP`), and the sound keeps the pace.

`setup.c` finds the home folder, reads `opentbblue.ini`, downloads
System/Next on the first run (with `fetch.c`, libcurl built in) and unpacks
it with its own tar reader. `cards.c` is the card panel: updates, and which
card is in each slot. `fatimg.c` turns a card image into a folder.

## CPU

`z80/z80.c` is a Z80 with the Next's extra instructions (Z80N): `NEXTREG`,
`PUSH nn` (big-endian operand), `MUL`, `SWAPNIB`, `MIRROR`, `TEST`, the
`LDIX`/`LDDX`/`LDPIRX`/`LDWS` block moves, `OUTINB`, `BSLA` and the barrel
shifts, `JP (C)` and the rest. Every instruction is timed to the T-state
and keeps the undocumented flags and MEMPTR.

- **Speed.** It runs at 3.5, 7, 14 or 28 MHz (nextreg 0x07). The core
  counts in 28 MHz master clocks, so a T-state is 8, 4, 2 or 1 of them. At
  28 MHz each memory access to SRAM gets the extra wait state the real
  board adds.
- **Contention.** ULA contention follows the machine's timing (48K, 128K,
  +3 or Pentagon), and is only applied at 3.5 MHz, as on the Next.
- **Interrupts.** IM 1 and IM 2 both work. The Next's *hardware IM 2*
  (nextreg 0xC0 bit 0) is also there, with a vector per source: line
  interrupt, UARTs, CTC channels and ULA, each with its own priority and
  status bits (0xC4-0xCE).
- **NMI.** NMIs come from the multiface or divMMC buttons, or from software
  through nextreg 0x02. There is one NMI sequencer (idle, fetch, hold,
  end), and the first device to ask gets it.
  - The return address goes to nextregs 0xC2/0xC3.
  - With *stackless NMI* (0xC0 bit 3) nothing is pushed, and RETN takes its
    address from those two registers.

## Memory and paging

The Next has 1 MB or 2 MB of SRAM, addressed in 8K pages. Every 8K slot of
the Z80's 64K comes from one of the MMU registers (nextregs 0x50-0x57).
Each memory access goes through a slot table (`nx_slot`) that holds where
reads and writes go. `remap()` in `next/next.c` rebuilds that table
whenever something that changes it is written.

- **The old ports still work.**
  - 7FFD, 1FFD, DFFD and EFF7 set the 128K and +3 banking and the +3
    all-RAM modes, and update the MMU registers the way the MMU process in
    `zxnext.vhd` does.
  - Nextreg 0x8E is the same thing in one write.
  - Nextreg 0x8F picks Pentagon or Profi style mappings.
- **ROMs.** MMU value 0xFF in slots 0 and 1 means "the ROM". Which of the
  four ROMs is used comes from 7FFD bit 4 and 1FFD bit 2, as on a +3.
- **Alternate ROM** (nextreg 0x8C). Bit 7 swaps in a second copy of the
  ROM. Bit 6 selects whether the swap applies to reads or writes, and bits
  5 and 4 lock it to the 48K or 128K ROM. NextZXOS uses this to call
  between its ROMs.
- **Layer 2 paging** (port 123B) maps the Layer 2 screen over 0000-BFFF for
  reads, writes or both.
- **Bank 5 and bank 7.** These are the ULA screens. Writes to them go to
  the video too. The picture is brought up to the present first, so mid-line
  changes land on the right pixel.

## divMMC and the multiface

These are the two "hidden" memories in the bottom 16K.

**divMMC** is the SD card interface that esxDOS and NextZXOS's file system
are built on.

- Its 8K ROM and 128K of RAM page in over 0000-3FFF in two ways:
  - when the program writes port E3 (CONMEM), or
  - by *automap*, when the CPU fetches an instruction at one of the entry
    points.
- The entry points are RST 0/8/10/18/20/28/30/38 as chosen by nextregs
  0xB8-0xBA, plus 3Dxx, 1FF8-1FFF, the tape traps 04C6/0562/04D7/056A
  and 0066 for its NMI (0xBB).
- Instant points page in for the fetch itself, and delayed points for the
  next instruction. RETN pages it out.

**The multiface.** NextZXOS uses it to start snapshots (`.sna`, `.snx`)
and for its NMI menu and debugger.

- Its ROM is at SRAM 0x014000 and its RAM is at 0x016000.
- It appears over 0000-3FFF when its NMI is taken (at the fetch at 0066),
  or when its enable port is read while it is visible.
- Its disable port and RETN hide it again.
- The ports are 3F/BF for +3 mode, BF/3F for 128 mode and 9F/1F for 48
  mode (nextreg 0x0A).
- In +3 mode its enable port also reads back the paging registers.

**Starting a snapshot.** The SPECTRUM command loads the snapshot into
memory. It then writes nextreg 0x02 bit 3, which raises a multiface NMI.
The multiface code checks that the NMI came from the loader (the return
address in 0xC2/0xC3), restores every register from the snapshot's header
and jumps in. An `.snx` is an `.sna` that leaves its file open on handle 0,
so the program can go on reading its own data.

## Video

`next/video.c` draws the picture lazily: up to "now" whenever something
that changes it is about to change. That is what makes raster effects, the
copper and border stripes land on the right pixel.

- **The picture.** It is 320x256 at the window's resolution. Each 7 MHz
  pixel is two 14 MHz half pixels, so the framebuffer is 640 wide and
  hi-res modes keep every pixel.
- **The layers:**
  - **ULA:** the Spectrum screen, with Timex hi-colour and hi-res, ULAnext
    and ULA+. This is also where the border is.
  - **LoRes:** 128x96 in 256 colours.
  - **Tilemap:** 40 or 80 columns of 8x8 tiles, with per-tile attributes.
  - **Layer 2:** 256x192, 320x256 or 640x256, in 256 or 16 colours, with
    scrolling and clipping.
  - **Sprites:** 128 hardware sprites, 4 or 8 bit, anchored and relative,
    scaled, rotated and mirrored. The limit per line is as on the FPGA.
- **The mixer** (nextreg 0x15) combines them in any of the six priority
  orders, plus the two blend modes. Transparency is by colour (0x14) and by
  index. Each layer has its own clip window and palette, with 9-bit colour
  (RRRGGGBBB).
- **The copper** (nextregs 0x60-0x64) is a little co-processor that
  writes nextregs at chosen beam positions. It runs in step with the beam.
- **Timing.** 48K, 128K, +3 and Pentagon frame timings, at 50 or 60 Hz
  (nextreg 0x05). The line interrupt is set by 0x22/0x23.

## Sound

- **Three AY-3-8912s** ("TurboSound"), each with its stereo mode and
  channel enables.
- **The 8-bit DACs** (Soundrive, Covox, SpecDrum and the rest, on their
  usual ports).
- **The beeper and tape** (port FE).

They are mixed per sample at 44.1 kHz, as on the Next.

## The machine's other parts

- **zxnDMA** (`next/dma.c`), on ports 6B and 0B. It copies memory to
  memory, memory to port, or port to memory, in continuous or burst mode,
  with the prescaler for timed sample playback. The CPU stops while it has
  the bus.
- **CTC** (in `next/next.c`): four counter/timer channels at 28 MHz. They
  can raise interrupts and drive DMA.
- **UARTs** (`next/uart.c`), with FIFOs and the baud rate prescaler:
  - **UART 0** is the ESP8266 wifi module (`devices/esp8266.c`). Its AT
    commands go to your computer's network, so `.uart` and the wifi
    programs work.
  - **UART 1** is the Raspberry Pi Zero (`devices/nextpi.c`). This is not
    an emulated Pi running Linux. It is NextPi's supervisor shell and the
    `nextpi-*` commands that the Next's dot commands use (`.pisend`,
    `.piput`, `.piget`), over a host folder (`pi/`).
- **Keyboard** (port FE and nextregs 0xB0/0xB1). The PC keyboard becomes
  the Spectrum matrix plus the Next's extra keys. Shifted punctuation types
  its own symbol.
- **Kempston joystick and mouse.** The joystick is on port 1F (and 37),
  and comes from a game controller, or from the arrow keys with Left Ctrl
  as fire. The mouse is the Kempston mouse on FBDF/FFDF/FADF, and has a
  wheel.
- **I2C real-time clock**, set from your computer's clock.

## SD cards and drives

The Next has two SD card slots (port E7 selects one, and EB transfers a
byte over SPI). `next/sd.c` behaves as an SDHC card in SPI mode, which is
what the boot ROM, the firmware and NextZXOS expect.

Behind each card is a *block device* (`storage/`). That is either an image
file, or a **folder on your computer shown as a FAT32 volume**
(`storage/vfat.c`):

- **Building the card.** When the card is attached, the folder is read and
  a FAT32 volume is built in memory: the partition table, boot sector,
  FSInfo, both FATs and the directories, with long file names and the 8.3
  names next to them. Each file's clusters point back at the host file, so
  the data is read from it when the Next asks for those sectors.
- **Writes come back.** What the Next writes (new files, changes, deletes,
  renames, new folders) is worked out from the FAT and directory sectors
  it writes. About a second after it has stopped writing, that is put back
  into the folder. So what you copy into `drives/SD0` is on the card, and
  what the Next saves appears there.
- **Size.** A card has a fixed size, 32 GB by default (`card_mb`), and the
  partition table, boot sector and FAT all say so. The Next never reads
  past the end, and its free space counts down as the folder fills. The
  memory it costs is the per-cluster tables: about 20 MB for 32 GB.

`drives/SD0` is the boot card. `drives/SD1` is the second card, empty to
start with, and NextZXOS sees it as a second drive.

## Accuracy and limits

- The core follows the VHDL closely enough that the official firmware,
  NextZXOS, the NMI menu, games, demos, `.nex`, `.tap`, `.sna`, `.snx` and
  `.z80` all run unchanged. Five scenarios in `tests/compare.sh` (NextBASIC,
  the tilemap demo, Warhawk, show512, Spectron 2084) are hashed frame by
  frame, so every change can be checked against the last.
- **Timing is to the instruction and the pixel.** The FPGA's
  sub-instruction pipeline is folded out. Very tight cycle-counted effects
  that depend on where inside an instruction a write lands may differ by a
  pixel.
- **Not emulated:**
  - the expansion bus and anything plugged into it
  - the real Raspberry Pi: only NextPi's shell and file transfer are there,
    with no Linux programs
  - HDMI/VGA specifics beyond the picture itself
  - the Next's Wi-Fi radio: the network is your computer's
- **The Amiga build** is the same core. It is tuned for a 68040: no
  floating point, a smaller video conversion, and skipped frames. See
  [Amiga.md](Amiga.md).

Designed, tested and delivered by The AmigaChrome Team.
