# Bus trace format

One text line per CPU cycle, the same from the emulator (`emulator --trace
FILE`) and from the ESP32 bus spy on the backplane (`hw/modules.md`), so
the two can be diffed with `tools/trace_diff.py` when the board and the
emulator disagree.

The emulator defines the format because the spy's firmware does not exist
yet. Version 1 is below; the spy should write exactly this.

## Lines

```
# ooe-bus-trace 1
# cpu w65c02s
1 00000 00 R -- 0
...
6 0FFFC 00 R -V 4
7 0FFFD FF R -V 4
8 0FF00 D8 R S- 4
```

Lines starting with `#` are comments. The first line must be
`# ooe-bus-trace 1`; any other comment is free-form (the emulator records
the CPU model and the machine file). Every other line has six fields,
separated by single spaces:

| # | field | meaning |
|---|---|---|
| 1 | cycle | decimal cycle number. Only differences matter: the diff compares each trace against its own first line |
| 2 | address | A19..A0, five upper-case hex digits |
| 3 | data | D7..D0, two upper-case hex digits: what was read or written |
| 4 | R/W | `R` read (RWB high), `W` write |
| 5 | signals | two characters: `S` if SYNC is high (an opcode fetch) else `-`, then `V` if VPB is low (a vector read) else `-` |
| 6 | module | PS3..PS0 as one hex digit, or `-` if the access selects no module |

Any field except the cycle may be `?` for "not captured". It matches
anything in a diff, so a spy that cannot see PS0..PS3, or a build that
leaves SYNC unwired, still diffs on the rest.

## What a cycle is

Every 6502 cycle is a bus access, including the dummy reads and the NMOS
read-modify-write write-back, so one line per PHI2 cycle. The emulator's
core makes those accesses at the addresses the silicon uses (checked per
cycle against Tom Harte's tests), so a trace from a correct board and from
the emulator match line for line.

Three gaps:

- **WAI and STP.** The W65C02S holds the bus while waiting or stopped, and
  the emulator does not log those idle cycles. They show as a jump in the
  cycle numbers, and the diff checks that both traces jump by the same
  amount.
- **The debugger.** Its fetch and store are not CPU cycles, so they are
  not traced; neither is anything while it is halted.
- **RDY.** The emulator does not model wait states. A board run with RDY
  pulled low (the console's single-step) repeats the held cycle; the spy
  should log a cycle only on PHI2 edges where RDY is high.

## Module IDs and A16..A19

The address is the 20-bit backplane address, after the north bridge. Until
its A16..A19 rule is decided (EMULATOR_PLAN.md §5), the emulator drives
them low, so its addresses all start with `0`. The module is the mapping's
`module=` in the machine file; the Apple-1 machines set none, so they show
`-`.

## Diffing

```
python3 tools/trace_diff.py board.trace emu.trace
python3 tools/trace_diff.py --from FF00 --symbols ibasic.lbl board.trace emu.trace
```

`--from ADDR` starts each trace at its first opcode fetch (SYNC) at that
address, so a capture that began mid-program lines up with an emulator run
from reset. The report gives the first differing cycle with a few lines
of context from both, labelled with symbols if given. Exit status: 0 if
the traces match for the length of the shorter one (it says if one ran
longer), 1 at the first difference, 2 for a malformed trace.
