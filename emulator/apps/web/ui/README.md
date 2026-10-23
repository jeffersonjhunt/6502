# OO-1 WebUI

The OO-1 (One Off One) in the browser, after mockup C ("Workbench"): a CRT
with the serial terminal on it, the PDP-11 style front panel in its rack,
and, in a later step, a debugger. It talks to the machine only through `MachinePort`
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

`?demo=scenes` shows the mockup's pictures on the Tiles and Apple II scenes
instead of NO SIGNAL (the video card is not emulated yet).

## Test it

```sh
npm test         # vitest: terminal, keyboard, panel state, and WasmPort against the real module in node
npm run e2e      # build, then Playwright in headless Chromium and WebKit (Safari's engine) against `vite preview`
```

Both need the core; without it they fail rather than skip. Playwright needs
its browsers (`npx playwright install chromium webkit`, and on a bare Linux
box `npx playwright install-deps chromium webkit`). Screenshots of the boot, the
monitor and NO SIGNAL land in `test-results/`.

## Layout

```
src/main.ts           the page: toolbar, monitor, wiring
src/port/             MachinePort, WasmPort (the WebAssembly core), FakePort (for tests)
src/crt/              Terminal (80x30 text model), Crt (canvas renderer), keyboard, demo scenes
src/panel/            the front panel: lamps, switches, keyswitch, readouts
src/ui/               toolbar, panel state (every physical control, one path to the port), DOM helpers
src/styles/           mockup C's tokens, toolbar and monitor
e2e/                  Playwright
scripts/copy-core.mjs puts the core in public/core
```
