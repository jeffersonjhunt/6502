# The machine port

The WebUI (`apps/web/ui`) reaches the machine only through one interface,
`MachinePort` (`apps/web/ui/src/port/machine-port.ts`). Today one backend
implements it, `WasmPort`: the emulator compiled to WebAssembly and running
in the page. The interface is shaped so that a second backend, a WebSocket
to the native emulator or to the real board's ESP32, can replace it without
touching the UI.

## What is in it, and why

Everything in the port is something the real board can also do:

| Group | Calls | On the board |
|---|---|---|
| Physical panel | `setSwitches`, `setPanel` (HALT/ENABLE, S INST/S BUS CYCLE), `setLock`, `press` (LOAD ADRS, EXAM, DEP, CONT, START), `setPower` | the Console card's switches and the keyswitch |
| Card registers | `cardRead`, `cardWrite` | the card's SPI header, which the ESP32 plugs into |
| Serial | `sendSerial`, `FrameState.serialOut` | the serial card |
| Inspection | `readMemory`, `physical`, `resolve`, `readTrace`, `FrameState.lights`, `FrameState.regs` | memory and the latched cycle over SPI; the lamps; the bus spy's trace |
| Video | `watchVideo`, `FrameState.video` | the video card's VGA output (a capture device, for the board) |
| USB | `sendKey`, `sendMouse` | the USB card's keyboard and mouse (the board: a USB keyboard and mouse plugged into it) |
| Pacing | `setClock`, `onFrame` | the clock select; the ESP32's report rate |

The UI never calls the emulator's debugger. Breakpoints are the Console
card's BPADDR/BPCTRL comparator, written through `cardWrite`; Run, Halt and
Step are the HALT switch and CONT. So whatever works in the browser works
the same way against the board.

`caps` says what a backend cannot do. The emulator has no bus-cycle step
(`busCycleStep: false`: it runs whole instructions, so S BUS CYCLE steps an
instruction). The board cannot show the CPU's registers (`registers: false`
there), and needs a capture device on its VGA port for `video`.

## Calls and frames

Calls that only send (`setPanel`, `press`, `sendSerial`, ...) are fire and
forget. Reads return promises, because over a socket they are round trips;
`WasmPort` resolves them at once.

State arrives as a `FrameState` about once per animation frame: cycles,
power, halted, stopped (STP), the /IRQ line, the measured speed, the serial bytes since the
last frame, and, only while asked for (`setLights`, `watchRegisters`,
`watchVideo`), lamp duty, registers and the video card's frames, so a closed
debugger or another scene costs nothing. `video` is the card's last finished
frame (640x480, RGBA), sent only when it is a new one; `setClock` also tells
the card the CPU clock, so it keeps 60 frames a second at any speed.

Lamps are 0 to 1: the share of bus cycles each line was high since the last
frame (weighted by wait states), or exactly 0/1 while halted, when they
show the latched cycle, or what EXAM/DEP last touched.

## WasmPort

- **Pacing:** one budget per animation frame, clock rate × the time since
  the last frame, capped at 100 ms so a tab back from the background does
  not race to catch up. "Max" runs 50,000-cycle slices until 10 ms of the
  frame are used.
- **Files:** the module has `backplane.machine`, the BIOS and its labels
  built in, so `connect()` needs no fetches beyond the module itself.
- **Strings are bytes** (Latin-1), as a serial line is.
- **Speed:** ~21 MHz under node; real time at 7.16 MHz is about a third of a
  core. If that is ever too much for the main thread, the same interface
  can front a Web Worker.

## A WebSocket backend (later)

The messages follow the calls one for one; frames are pushed by the server
at its own rate. For the board, the ESP32 would also need to set the switch
register over SPI (SWR and PANEL are read-only physical switches today), so
a remote LOAD ADRS or DEP works. That is a small hw/design.md addition to
make when the bridge is built.
