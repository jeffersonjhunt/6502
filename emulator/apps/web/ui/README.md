# OO-1 WebUI

The OO-1 (One Off One) in the browser, after mockup C ("Workbench"): a CRT
with the serial terminal on it, and, in later steps, the PDP-11 style front
panel and a debugger. It talks to the machine only through `MachinePort`
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

`?demo=scenes` shows the mockup's pictures on the Tiles and Apple II scenes
instead of NO SIGNAL (the video card is not emulated yet).

## Test it

```sh
npm test         # vitest: terminal, keyboard, panel state, and WasmPort against the real module in node
npm run e2e      # build, then Playwright in headless Chromium against `vite preview`
```

Both need the core; without it they fail rather than skip. Playwright needs
its Chromium (`npx playwright install chromium`, and on a bare Linux box
`npx playwright install-deps chromium`). Screenshots of the boot, the
monitor and NO SIGNAL land in `test-results/`.

## Layout

```
src/main.ts           the page: toolbar, monitor, wiring
src/port/             MachinePort, WasmPort (the WebAssembly core), FakePort (for tests)
src/crt/              Terminal (80x30 text model), Crt (canvas renderer), keyboard, demo scenes
src/ui/               toolbar, panel state (the switches Run/Halt/Step move), DOM helpers
src/styles/           mockup C's tokens, toolbar and monitor
e2e/                  Playwright
scripts/copy-core.mjs puts the core in public/core
```
