# OO-1 WebUI

The OO-1 (One Off One) in the browser, after mockup C ("Workbench") as
redesigned in October 2026: a CRT with the serial terminal on it, a status
strip, the PDP-11 style front panel in its rack, and a drawer at the right
that Settings and the debugger share. It talks to the machine only through `MachinePort`
(`../../../docs/MACHINE_PORT.md`); today that is the emulator compiled to
WebAssembly, running in the page.

## Run it

```sh
../../../tools/web_build.sh build    # the core: build/web/apps/web/oo1.mjs + oo1.wasm (Docker)
npm install
npm run dev                          # http://localhost:5173
```

`npm run build` writes a static site to `dist/` (relative paths: serve it
from anywhere). `OO1_CORE_DIR` points at a core built elsewhere. Node 22 or
later is recommended (Vite 8 and Vitest 5 ask for it); 20.19 works.

Click the screen and type: the BIOS boots to the Woz Monitor (`\`).
`C680.C684` shows the north bridge's windows. CAPS is on by default, since
Woz only knows capitals. Run, Halt and Step work through the front panel's
HALT switch and CONT, as the real panel does. The toolbar is one row: back
to the project page, Run, Halt, Step inst, Step cycle and the clock; then
CAPS, Sound, Settings and Debugger. Narrower than 1280px the steps and the
clock go under More, so it stays one row. On a phone it keeps Run or Halt
(the one the machine isn't doing), Sound and Settings, and the rest go under
More; the front panel scales to the screen's width.

The strip under the monitor has the power light, the machine's state
(`OO-1 · VGA 640×480 · RUNNING · 7.16 MHz`), and a pill for each thing
loaded: D1 and D2 with the drives' lights, SD with the card's; a click on
one opens Settings at its row. A tune playing has its own pill (song n/m,
previous, next, stop), a long paste has Stop typing, and Apple II mode has
the IIe's Reset and Reboot. With nothing loaded it says so, with a link to
Settings. The pills stay while the power is off, their lights out. A BIOS
of your own that prints nothing for five seconds gets a note there pointing
to Use built-in.

The front panel is the Console card's (hw/design.md, "Front Panel"). The
lamps glow with the share of bus cycles each line was high and freeze on the
latched cycle when halted; the readouts under them spell it out. To look at
memory: HALT down, set the switch register to a physical address, LOAD
ADRS, EXAM (again to step on); set bits 7-0 and DEP to store. The keyswitch
turns OFF (power off), RUN, LOCK (the panel is ignored). The switch register
is remembered between visits.

Settings and the debugger open in one drawer, at the right of the bench
(over it, as a modal dialog, on a window 980px wide or less; the whole
screen on a phone); opening one closes the other, the toolbar's buttons
toggle them, and Escape closes the drawer, focus going back to whatever
opened it. The debugger starts closed on a first visit and opens again
beside the page if it was left open. It has four working tabs. CPU: registers and flags, live. Bus trace: the latest cycles, as the
bus spy writes them, recorded only while the tab is open. Breakpoints: add a
label or address (`ECHO`, `C968`, or five hex digits for a physical one),
arm one (the Console card has one comparator), choose fetch, read or write
and halt or /NMI; a hit shows the cycle the card latched. Breakpoints are
the card's BPADDR/BPCTRL, written over its SPI registers, so they work the
same way on the real board.

## Publish it

The page is published at
[oneoffendeavors.com/projects/oo-1/emulator/](https://oneoffendeavors.com/projects/oo-1/emulator/);
the site's `make emulator` builds it from this checkout. To build it for
somewhere else:

```sh
VITE_HOME_URL=/projects/oo-1/ npm run build -- --outDir /path/to/site/emulator
```

`VITE_HOME_URL` makes the toolbar's back arrow a link to that page (unset: none).
The core keeps its names, so the page asks for `core/oo1.mjs?v=<hash>` and
the same for `oo1.wasm`: a new core is a new URL, never an old one from a
cache. Everything else Vite already names by hash. On a touch screen the page
says once that the terminal needs a keyboard.

The monitor's switch is its input select. **Terminal** is the serial
terminal; **Display** is the video card's output: VGA, which the BIOS fills
with the same Woz Monitor session in the IBM VGA font, or in Apple II mode
the IIe's picture. The phosphor (Settings, Display) is Colour as the card
makes it, or P1/P3/P4 as a mono monitor shows it. The switch follows the
machine: entering Apple II mode, or a program giving the video card a
picture of its own (`C803R`'s demo, say: anything but a text screen on
layer 0 alone, as a BIOS sets one up), puts it on Display; leaving it
(START, the demo's end) puts it back on Terminal. A choice made by hand
holds until the machine's video mode next changes, and is remembered for
the next visit until then; the machine's own switches are not. On Terminal the keyboard
is the serial terminal's; on Display it and the mouse are the USB card's, as
HID usage codes from the physical keys (the card cooks them in its own US
layout, with its own Caps Lock, so CAPS is off there). On Terminal, a character AltGr makes
(or Option, on the Mac) is typed as it is. Ctrl+V and Shift+Insert paste
(Cmd+V on the Mac, where Ctrl+V is the machine's ^V). Tab and Escape are
the machine's, so F6 takes the keyboard off the screen to what follows it,
Shift+F6 to what comes before. Pause/Break breaks into the monitor. Type `C803R` for the video demo (tiles, sprites, scrolling, a
palette fade); any key goes back. The Devices tab in the debugger decodes the
card's registers, and the north bridge's real-time clock, which the page sets
from this computer's clock when it opens the machine.

Settings, in sections:

- **ROMs.** The **BIOS**: the one built in, or **Load…** a build of your
  own (up to 512 KB, the flash): it is kept in this browser's localStorage
  and the page restarts the machine on it, its version read from its
  greeting; **Use built-in** forgets it and restarts. Either asks first if
  the restart would lose the SD card's unsaved writes or a drive's. Then Apple II mode's
  ROMs, a row each, each with Load… and Eject: the owner's **Apple IIe ROM**
  (16 KB, or the CD and EF halves; the character ROM and the PROMs may be
  picked with it), the **IIe character ROM**, and the Disk II's **boot
  PROM** and **sequencer PROM** (these need the IIe ROM in first). They are
  told apart by size and contents, not names, plugged into the cartridge,
  and kept in this browser, never uploaded. Each Load or Eject plugs a new
  cartridge, which restarts the machine: in Apple II mode it asks first.
  Ejecting the IIe ROM keeps the other parts for the next one. With the IIe ROM in, its row has
  **Start Apple IIe**: START, then `C806R` at the monitor. Apple II mode, by
  the button or `C806R` typed, shows the IIe's 560x384 picture filling the
  screen, softened since it is not whole pixels, lights AMODE, and the
  keyboard is the USB card's (Ctrl-Delete is Reset; the left GUI or Alt is
  Open Apple, the right Closed Apple). The strip's **Reset** and **Reboot**
  (Ctrl-Open Apple-Reset) type the IIe's resets.
- **Disk II.** Drives 1 and 2, each with its light (the drive's motor),
  the disk's name, size and format, **Protect** (the disk's notch),
  **Save** (as `.woz` if it no longer reads as plain sectors) and
  **Eject**, or **Insert…** for a `.woz`, `.dsk`, `.do` or `.po`. The IIe
  boots drive 1 on Start. The disks are kept in this browser (IndexedDB),
  with what was written to them, never uploaded.
- **SD card.** A card image of your own in the mass storage card's slot:
  whole 512-byte sectors, raw or FAT as a PC formats it. `C809R` at the
  monitor lists the card's root, and `C80CR` boots its `/BOOT.BIN`. The
  light is on while it works on a command; Protect is the card's switch,
  Save downloads it with what the machine wrote, Eject takes it out. The
  card stays in the page only: never uploaded, and not kept in this browser
  (a card is megabytes), so once it has been written to the note says so
  and leaving the page asks first.
- **Sound.** **Play .sid…** puts a C64 tune of your own (PSID or RSID) on
  the sound card, turning Sound on; the strip's pill changes the song and
  stops it, and the note says what the card can't give a tune (a third
  SID; an RSID's VIC-II, without which most stay silent). The file stays in
  the page: not kept, never uploaded.
- **Display.** The phosphor.
- **About.** The credits: the screen font is VileR's (CC BY-SA 4.0); Inter,
  JetBrains Mono and VT323 are under the SIL Open Font License, bundled
  into the build (`@fontsource`), so the page looks the same everywhere and
  fetches no fonts from elsewhere. **About and credits**, under the panel,
  opens it.

A paste goes to whichever keyboard the screen has: on Display it is typed
on the USB keyboard (US layout; letters as the card's Caps Lock has them),
and the card holds each character until the machine has taken the last,
so a BASIC program pasted into Applesoft arrives whole. A long one takes a
while at that pace: **Stop typing**, in the strip while it types, drops the
rest.

**Sound**, in the toolbar, plays the machine's sound through the
computer's speakers: the sound card's two SIDs, and the USB card's speaker,
which is the IIe's `$C030` in Apple II mode (Web Audio, at the device's own
rate; the meter in the button is the level). It is remembered; left on, it plays from
the first click or key after a reload, as browsers require. Halting the
machine silences it: the emulator's time is the CPU's. So does a
background tab, where the browser stops the animation frames the machine
runs on: a tune pauses until the tab is in front again.

## Test it

```sh
npm test         # vitest: terminal, keyboard, panel state, the speaker's queue, and WasmPort against the real module in node
npm run e2e      # build, then Playwright in headless Chromium and WebKit (Safari's engine) against `vite preview`
OO1_BASE_URL=https://staging.oneoffendeavors.com/projects/oo-1/emulator/ npx playwright test   # the same tests against the published page
```

Both need the core; without it they fail rather than skip. Playwright needs
its browsers (`npx playwright install chromium webkit`, and on a bare Linux
box `npx playwright install-deps chromium webkit`). Screenshots of the boot, the
monitor, VGA, Apple II and a phone's width land in `test-results/`. The Apple II
tests use a stand-in ROM (src/port/fake-iie.ts); `e2e/iie.spec.ts` runs the
real enhanced IIe only where the owner's ROMs are in
`test/host/fixtures/apple2e` (or `OO1_IIE_DIR`), and skips elsewhere.

## Layout

```
src/main.ts           the page: toolbar, monitor, strip, drawer, wiring
src/port/             MachinePort, WasmPort (the WebAssembly core), FakePort (for tests)
src/crt/              Terminal (80x30 text model), Crt (canvas renderer), keyboards
src/panel/            the front panel: lamps, switches, keyswitch, readouts
src/debugger/         the pane: registers, bus trace, breakpoints (the card's comparator)
src/ui/               toolbar, drawer, Settings and its rows (BIOS, cartridge, drives, SD, tune), the strip,
                      the screen's follow logic, panel state (every physical control, one path to the port), DOM helpers
src/styles/           mockup C's tokens, toolbar and monitor
e2e/                  Playwright
scripts/copy-core.mjs puts the core in public/core
```
