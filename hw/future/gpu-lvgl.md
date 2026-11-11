# Parked: an LVGL GPU for OO-2 or OO-3

Nov 10, 2026 · @Jefferson

**Status: parked (2026-11-10).** For OO-1 the toolkit is a 6502 library on the existing layers instead
([../ooui.md](../ooui.md)), and the video card's memory model is changing ([../video-rev2.md](../video-rev2.md)).
This plan is kept as the starting point for a GPU on a future machine. It was written against the snooped card
(command ring in banks 8–B); on a private-memory card the ring would live in card memory instead.

## Goal and scope

A native UI toolkit that makes apps easier, more consistent and less error-prone to write, implemented as LVGL v9
running on a coprocessor on the video card and driven by commands from the 6502. The 6502 never draws widgets.

- **The product:** one documented toolkit API that apps call to create windows, widgets and dialogs and to receive
  events.
- **Order of work:** API first, emulator first, hardware last.
- **The proving app:** OO-Amp.

## Decisions reached

| Topic | Decision |
| --- | --- |
| Base library | LVGL v9 (MIT): widest widget set, MCU-sized, runtime XML layouts, data binding, builds for RP2350 and WebAssembly |
| Where it runs | A 32-bit coprocessor on the video card (RP2350 assumed), never the 6502 |
| CPU/card coupling | None. Commands plus a commit; the card applies each batch atomically at its own frame boundary. The 6502 never waits on vblank |
| Display modes | MODE 0 = 640×480@60 (reset value). MODE 1 = VESA 1024×768@60, 65 MHz. 800×600 and stretched timings dropped |
| GUI surface | New layer type 6: full-screen, card-private memory, ARGB4444 direct colour, double-buffered, composited in the layer order |
| Bus role | The coprocessor never masters the bus |
| Apple II mode | Must keep working unchanged |

## Architecture

```
  6502 + OODOS ──ring + COMMIT──▶ FPGA (snoop path, registers, compositing, timing) ──▶ VGA
       ▲                              │ commands              ▲ scanout
       └──────── events, ISR bit 3 ───┤                       │
                                      ▼                       │
                                 RP2350: LVGL v9 ──draws──▶ SDRAM: front + back surfaces
```

## Draft register map (snooped card)

| Address | Name | Purpose |
| --- | --- | --- |
| `$C4B0` | GMODE | 0 = 640×480, 1 = 1024×768; /RESET sets 0 |
| `$C4B1`, `$C4B2` | RINGBASE, RINGSIZE | Command ring location and length |
| `$C4B3`–`$C4B4` | COMMIT | New head offset; writing the high byte commits the batch |
| `$C4B5`–`$C4B6` | TAIL | Card's read offset |
| `$C4B7` | GSTATUS | BUSY, FULL, ERROR, PARKED, EVENTS pending |
| `$C4B8`–`$C4B9` | FRAME | Card frame counter, for pacing only |
| `$C4BA`, `$C4BB` | EVDATA, EVCOUNT | Event FIFO |
| `$C4BC`–`$C4C0` | PTRX, PTRY, PTRBTN | Pointer, written by OODOS each frame |
| `$C4C1` | GKEY | Focus and navigation keys |
| `$C4C2` | GERR | Last error code |

ISR and IEN gain bit 3, GUI. OODOS owns IEN, so it services this bit and turns events into `EK_GUI` queue entries.

## Command protocol

Byte records in the ring: opcode, length, arguments, little-endian. Objects are named by 16-bit handles the 6502
chooses, so creating a widget needs no round trip. Strings are length-prefixed UTF-8.

| Family | Examples |
| --- | --- |
| Lifecycle | create(type, parent, handle), delete, clean |
| Geometry | set_pos, set_size, align, set_layout (flex, grid) |
| Content | set_text, set_value, set_range, add_option, set_cell |
| Style | set_style(prop, value, selector), add_style, set_theme |
| State | add_flag, clear_flag, add_state, clear_state, focus |
| Screens | load_screen(handle, animation) |
| Events | subscribe(handle, event mask) |
| Subjects | subject_create, subject_set, bind |
| XML | xml_load(component), xml_create(name, parent, handle) |
| Assets | font_load, image_load, image_from_vram |
| File bridge | dir_begin, dir_entry, dir_end (OODOS supplies listings; LVGL never reads the SD card) |
| Sync | fence(id), query(handle, prop) |

## Apple II mode guarantees

1. /RESET parks the GPU: stops command processing, clears handles and events, hides layer 6, forces GMODE 0.
2. It stays parked until a hard reset.
3. The coprocessor never masters the bus.
4. The Apple snoop path keeps its timing and stays first in the FPGA.
5. `$C019`, the picture and the character ROM are unchanged.
6. No firmware reboot on /RESET: LVGL state resets in place.

## Emulator and firmware notes

- Vendor LVGL v9 in `emulator/third_party/lvgl`, pinned, MIT licence in `licenses/`.
- A shared interpreter library compiled by both the emulator and the RP2350 firmware; only tick, flush and link
  differ.
- LVGL ticked from emulated time, never the host clock; a BUSY/latency model so software can't rely on instant
  completion.
- A custom theme and bitmap font, or LVGL looks like a phone.
- Later: an LVGL draw unit handing fills, blits and glyphs to the FPGA.

## Phases (as planned)

| Phase | Work | Gate |
| --- | --- | --- |
| 0 | Toolkit API spec | API reviewed against OO-Amp |
| 1 | Emulated GPU | Golden frames and Apple suites pass |
| 2 | Toolkit library and OO-Amp port | OO-Amp much smaller: go / no-go for hardware |
| 3 | Card hardware | Board passes Apple mode tests |
| 4 | Firmware bring-up | OO-Amp runs on real hardware |
| 5 | MODE 1 and draw unit | Smooth scrolling at 1024×768 |

## Open questions left

1. Which FPGA part, with an SDRAM or HyperRAM interface?
2. In MODE 1, do legacy layers work, or is it a desktop mode?
3. ARGB4444 direct colour for layer 6 (accepted in discussion as the practical choice).
4. Does the LVGL heap survive Break?
5. How is the RP2350 linked to the FPGA?
