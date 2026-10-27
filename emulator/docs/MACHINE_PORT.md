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
| Apple II mode | `insertCartridge`, `readApple`, `FrameState.apple` | the cartridge (ROM B) with the owner's IIe ROM in it, and the Disk II card's PROMs; the north bridge's MODE and the IIe's switches, read without a bus cycle; the AMODE lamp |
| Disk II | `insertDisk`, `ejectDisk`, `readDiskInfo`, `saveDisk`, `setWriteProtect`, `readDisk2`, `FrameState.disk` | the Floppy Emu in the drive's place: an image in, out, written back; the drive's light |
| SD card | `insertSd`, `ejectSd`, `readSdInfo`, `saveSd`, `setSdWriteProtect`, `FrameState.sd` | the card in the mass storage card's slot: an image in, out, saved with the machine's writes; the card's light |
| Real-time clock | `readClock` | the north bridge's DS3231 and its registers, for the debugger; the emulator sets it from the page's clock at `connect`, as an owner sets a board's |
| Sound card | `readSound`, and its registers through `readMemory(0xC500, 0x80, "io")` | the card's 6510 and its SIDs, for the debugger |
| Sound | `listen`, `FrameState.audio` | the sound card's audio and the USB card's speaker, mixed (emulator only: the board's cards play through their own jacks) |
| A tune | `playSid`, `playSong`, `stopSid`, `FrameState.soundRunning` | a `.sid` file onto the sound card (emulator only, `caps.sid`: on the board a program on the CPU would load it) |
| Pacing | `setClock`, `onFrame` | the clock select; the ESP32's report rate |

The UI never calls the emulator's debugger. Breakpoints are the Console
card's BPADDR/BPCTRL comparator, written through `cardWrite`; Run, Halt and
Step are the HALT switch and CONT. So whatever works in the browser works
the same way against the board.

`caps` says what a backend cannot do. The emulator has no bus-cycle step
(`busCycleStep: false`: it runs whole instructions, so S BUS CYCLE steps an
instruction). The board cannot show the CPU's registers (`registers: false`
there), and needs a capture device on its VGA port for `video`. A backend
that cannot take a cartridge says `cartridge: false`; one whose sound comes
out of the machine itself, as the board's does, says `audio: false`, and
one that can't load a tune onto the sound card without a program says
`sid: false`.

`readMemory` reads CPU addresses (through the windows), physical ones, or
`"io"`: the cards' registers at the addresses the native map gives them
($C000-$CFFF), which Apple II mode hides from the CPU behind the Apple's
own. The Devices tab reads the cards that way, in either mode.

The page never ships Apple's ROMs. `insertCartridge` takes the owner's
(16 KB for $C000-$FFFF, and optionally the 4 or 8 KB character ROM) as
plugging a cartridge in with the power off: the machine starts afresh.
`WasmPort` writes it into the module's own memory file system and no
further; the page keeps it in the browser's localStorage for next time
(ui/cartridge.ts), and Eject forgets it. Disks are the same: the page keeps
the last one in IndexedDB (ui/disk-store.ts), with what was written to it,
and never uploads it. A disk stays in its drive through power cycles.

## Calls and frames

Calls that only send (`setPanel`, `press`, `sendSerial`, ...) are fire and
forget. Reads return promises, because over a socket they are round trips;
`WasmPort` resolves them at once.

State arrives as a `FrameState` about once per animation frame: cycles,
power, halted, stopped (STP), the /IRQ line, the measured speed, the clock
the mode fixes and whether it is Apple II mode (both as the frame's run
left them), the serial bytes since the
last frame, and, only while asked for (`setLights`, `watchRegisters`,
`watchVideo`), lamp duty, registers and the video card's frames, so a closed
debugger or another scene costs nothing. `video` is the card's last finished
frame (640x480, RGBA), sent only when it is a new one; `setClock` also tells
the card the CPU clock, so it keeps 60 frames a second at any speed.
`audio`, while `listen(rate)` asks for it, is the machine's samples since
the last frame at that rate, left and right interleaved, -1 to 1: SID A
left, SID B right, and the speaker (the IIe's `$C030`) in both; `listen` is kept through power cycles, and `listen(0)` stops the card
making them at all. Time is the CPU's, so a halted or stopped machine is a
silent one (the board's card would play on). `playSid` starts a `.sid`
file's song on the card as hw/design.md's player would (the tune at its
load address, the stub, the clock and the chips' model from the header),
writing the card's registers itself; `soundRunning` says the card's 6510
still runs it, so a page sees a power cycle or START's /RESET stop it.

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
