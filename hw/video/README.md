# OO-1 Video Card — hardware design files

Nov 20, 2026 · @Jefferson

The video card's schematics, pin maps and FPGA constraints. The design itself, and the reasons for it, are in
[../video-card-hw.md](../video-card-hw.md); the register map is [../video-rev2.md](../video-rev2.md).

**Status: bootstrap (2026-11-20).** The prototype carrier's schematic is complete as a netlist (every part placed,
every pin labelled, checked below) but not yet arranged or wired by hand. The direct board is a skeleton.

## What's here

| Path | What it is |
| --- | --- |
| `kicad/proto-carrier/` | **Board 1, the prototype carrier:** an iCESugar-Pro module (LFE5U-25F, 32 MB SDRAM) in a DDR2 SODIMM-200 socket, with the OO-1 bus interface, DVI and VGA, and the clocks. Sheets: module, bus, video, clocks, power |
| `kicad/direct/` | **Board 2, the direct board:** the ECP5 and two SDRAMs (32-bit) soldered down. The bus, video and clock sheets are generated the same as the carrier's; the FPGA, SDRAM and power sheets are placeholders |
| `kicad/lib/oo_video.kicad_sym` | The project's symbols: KiCad library symbols copied in (hidden pins made visible), and five drawn here: the module, the 98-contact edge, the SN74LVC16T245, the 74LVC07A and the ADV7123 |
| `pinmap/icesugar-pro-sodimm.csv` | The module's 200 edge pins: pad, ball, bank, clock function. Read from Muse Lab's schematic (v1.3) and checked against Project Trellis's ball map |
| `pinmap/carrier-assign.csv` | The carrier's signal on each module pin, with its RTL port name |
| `fpga/icesugar_carrier.lpf` | nextpnr-ecp5 constraints for the carrier, from the assignment |
| `tools/gen_kicad.py` | The generator for all of the above (with `kisch.py` and `sexp.py`) |

**Why a subdirectory and not its own repository:** the RTL will be checked against the emulator's `VideoCard` and
its golden frames, and the card's design record lives in `hw/` beside design.md and video-rev2.md. Keeping the
boards here keeps the hardware, the emulator and the docs in one history, as `hw/expansion/` does for OO-AP. If the
card is ever published on its own, `git subtree split --prefix=hw/video` makes a repository of it with its history.

## The generator, and when to stop running it

```
python3 hw/video/tools/gen_kicad.py
```

It writes the library, both KiCad projects, the assignment and the LPF from `pinmap/icesugar-pro-sodimm.csv`. Each
part is placed once and every pin has a stub ending in a global label, a power symbol or a no-connect flag, so the
schematic is correct but not yet readable as a drawing. **Once a sheet is arranged by hand in KiCad, KiCad owns it:
don't run the generator over it again.** Change pin assignments in KiCad and the LPF together from then on.

The files are KiCad 7 format (version 20230121); KiCad 8 and 9 open them and save them in their own format.

## Checked so far

- `kicad-cli` (7.0.11) loads both projects and exports their netlists.
- In the carrier's netlist, all 106 signal pins of the module land on their assigned nets, the power and ground pins
  on +5V_MOD and GND, and no net has a single node (no dangling labels): 75 parts, 290 nets.
- The SN74LVC16T245 symbol follows TI's DGG pin table (SCES636B); the ADV7123 follows ADI's table 8 (Rev. D).
- The module: every bank's VCCIO is 3.3 V; its 5 V comes in on SODIMM pins 2-12; its serial port (PT38A, PT42A)
  and SDRAM are on balls that aren't on the edge.

## To check before layout

- **Footprints** that are guesses: the edge contact footprint (`Connector_PCBEdge:BUS_PCIexpress_x8`: its pad names
  must be A1-A49 and B1-B49), and the SODIMM-200 socket, which has none yet (pick the part, e.g. TE 1473005-1, and
  match its footprint's pin numbering to the module's).
- **The edge's pin positions** follow the B6 draft in [../modules.md](../modules.md), which leaves every position to
  the owner.
- **TFP410 straps** (ISEL 0, BSEL 1, DSEL 0, EDGE 1, VREF 3.3 V) against the datasheet's table.
- **ESD:** the carrier has none on the HDMI socket; the final card adds a TPD12S016.
- **Power:** the carrier's 3.3 V is an AMS1117 (about 0.4 A, 0.7 W); the final card uses a buck converter.
- **Pin choice:** the assignment fills the module's pins in order (video first, then the bus) and keeps bank 8's five
  pins (PB4A/B, PB6A/B, PB13A, used during configuration) as spares on the debug header. Layout may want pins swapped
  for routing; swap them in the CSV or in KiCad and the LPF together.

## Next

1. Arrange the carrier's sheets by hand, then lay out the 4-layer board.
2. Phase P1 on Muse Lab's own breakout: the timing generator and the SDRAM controller at 100.7 MHz.
3. The direct board's FPGA sheet needs an LFE5U-25F caBGA-256 symbol (KiCad 7's library has only the 85F), built
   from Lattice's pinout; then the two SDRAMs and the power sheet.
