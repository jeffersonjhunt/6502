# OO-1 WebUI

The OO-1 (One Off One) in the browser, after mockup C ("Workbench"): a CRT
with the serial terminal on it, the PDP-11 style front panel in its rack,
and a debugger beside them. It talks to the machine only through `MachinePort`
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
HALT switch and CONT, as the real panel does.

The front panel is the Console card's (hw/design.md, "Front Panel"). The
lamps glow with the share of bus cycles each line was high and freeze on the
latched cycle when halted; the readouts under them spell it out. To look at
memory: HALT down, set the switch register to a physical address, LOAD
ADRS, EXAM (again to step on); set bits 7-0 and DEP to store. The keyswitch
turns OFF (power off), RUN, LOCK (the panel is ignored). The switch register
is remembered between visits.

The debugger (toggle it from the toolbar; it remembers) has three working
tabs. CPU: registers and flags, live. Bus trace: the latest cycles, as the
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

`VITE_HOME_URL` makes the badge a link back to that page (unset: no link).
The core keeps its names, so the page asks for `core/oo1.mjs?v=<hash>` and
the same for `oo1.wasm`: a new core is a new URL, never an old one from a
cache. Everything else Vite already names by hash. On a touch screen the page
says once that the terminal needs a keyboard.

The monitor's scene buttons are its input select. **Text** is the serial
terminal; **VGA** is the video card's output, which the BIOS fills with the
same Woz Monitor session in the IBM VGA font: in Colour as the card makes
it, on P1/P3/P4 as a mono monitor shows it. On Text the keyboard is the
serial terminal's; on VGA it and the mouse are the USB card's, as HID usage
codes from the physical keys (the card cooks them in its own US layout, with
its own Caps Lock, so CAPS is off there). Pause/Break breaks into the monitor. Type `C803R` for the video demo (tiles, sprites, scrolling, a
palette fade); any key goes back. The Devices tab in the debugger decodes the
card's registers.

**Apple II** is Apple II mode as an Apple monitor shows it: the video card's
560x384 IIe picture filling the screen, softened since it is not whole
pixels, and the keyboard is the USB card's (Ctrl-Delete is Reset; the left
GUI or Alt is Open Apple, the right Closed Apple). It needs the owner's IIe
ROM: **Insert IIe ROM…** under the monitor takes the files (16 KB, or the CD
and EF halves, and the character ROM; told apart by size and contents, not
names), plugs them into the cartridge, and keeps them in this browser's
localStorage, never uploaded; **Eject** forgets them. **Start Apple IIe** is
START, then `C806R` at the monitor; in Apple II mode **Ctrl-Reset** and
**Reboot** (Ctrl-Open Apple-Reset) type the IIe's resets. Entering Apple II
mode, by the button or `C806R` typed, switches to the Apple II scene and
lights AMODE. With the Disk II's two PROMs among those files (or added
later, alone), the drive under the monitor takes a `.woz`, `.dsk`, `.do` or
`.po`; the IIe boots it on Start. Its light is the drive's motor;
Write-protect is the disk's notch; Save downloads it (as `.woz` if it no
longer reads as plain sectors); Eject forgets it. The last disk is kept in
this browser (IndexedDB), with what was written to it, never uploaded. A paste goes to whichever keyboard the screen has: on VGA and
Apple II it is typed on the USB keyboard (US layout; letters as the card's
Caps Lock has them), and the card holds each character until the machine
has taken the last, so a BASIC program pasted into Applesoft arrives whole.
**Sound**, under the monitor at the right, plays the sound card through
the computer's speakers (Web Audio, at the device's own rate; the meter in
the button is the card's level). It is remembered; left on, it plays from
the first click or key after a reload, as browsers require. Halting the
machine silences it: the emulator's time is the CPU's.
The screen font is VileR's (CC BY-SA 4.0), and the page
credits it under the front panel.

## Test it

```sh
npm test         # vitest: terminal, keyboard, panel state, the speaker's queue, and WasmPort against the real module in node
npm run e2e      # build, then Playwright in headless Chromium and WebKit (Safari's engine) against `vite preview`
```

Both need the core; without it they fail rather than skip. Playwright needs
its browsers (`npx playwright install chromium webkit`, and on a bare Linux
box `npx playwright install-deps chromium webkit`). Screenshots of the boot, the
monitor, VGA, NO SIGNAL and Apple II land in `test-results/`. The Apple II
tests use a stand-in ROM (src/port/fake-iie.ts); `e2e/iie.spec.ts` runs the
real enhanced IIe only where the owner's ROMs are in
`test/host/fixtures/apple2e` (or `OO1_IIE_DIR`), and skips elsewhere.

## Layout

```
src/main.ts           the page: toolbar, monitor, wiring
src/port/             MachinePort, WasmPort (the WebAssembly core), FakePort (for tests)
src/crt/              Terminal (80x30 text model), Crt (canvas renderer), keyboards
src/panel/            the front panel: lamps, switches, keyswitch, readouts
src/debugger/         the pane: registers, bus trace, breakpoints (the card's comparator)
src/ui/               toolbar, panel state (every physical control, one path to the port), the Apple bar and cartridge, DOM helpers
src/styles/           mockup C's tokens, toolbar and monitor
e2e/                  Playwright
scripts/copy-core.mjs puts the core in public/core
```
