# GW-BASIC's math and program tests

The port's arithmetic (`res/math1.s`, `res/math2.s`, `res/mathfar.s` and block P1's `p1/*.s`) checked bit for bit
against `mbf.py`, a Python model of the 1983 source's MATH1.ASM and MATH2.ASM (hw/oodos.md, "The plan, phase 7":
testing). From the size spike's tests (`oodos/spike/gwbasic/`), for the port.

| File | What |
|---|---|
| `mbf.py` | the model: written from the 8086's algorithms, each step as it does it (derived: Microsoft's notice) |
| `basic.py` | the spike's tokeniser and reference reader for test programs (derived, likewise) |
| `flat.cfg`, `harness.s` | the flat test image: every module with `-D FLAT` (a far call is a JSR) at $0300, the harness first |
| `run6502.cpp` | runs the image on the emulator's W65C02S core, answering the BDOS's console calls |
| `check.py` | writes the vectors, runs them, compares; the tables; the model against exact arithmetic and against PC-BASIC |
| `oracle/` | `math.bas`, PC-BASIC's `math.out` for it, and `math.differ`: where the two differ, and why |
| `programs.py` | runs `programs/*.bas` on the flat image and compares each with PC-BASIC's output |
| `programs/` | the variables, arrays and strings: `NAME.bas`, PC-BASIC's `NAME.out`, and `NAME.differ` where the 1983 source and PC-BASIC differ |

    make -C oodos/gwbasic test BUILD=...                # the image, run6502, check.py
    make -C oodos/gwbasic test BUILD=... CHECK=--timing # the cycles instead
    python3 -I check.py --runner R --image I --seed 7 --count 1500   # more vectors
    python3 -I check.py --runner R --image I --programs             # the spike's programs too, on the stub PRINT
    python3 -I programs.py --runner R --image I [NAME...]           # programs/, against PC-BASIC

ctest's `test_gwbasic_math` builds `run6502` and the image and runs `check.py` (about 5 s). The harness's vector
layout and operations are at the top of `harness.s`.

`oracle/math.out` is PC-BASIC's output for `oracle/math.bas`, made by `oodos/gwbasic/tools/oracle.sh` (PC-BASIC
2.0.7, its wheel by sha256, in a throwaway `python:3.12-slim` container by digest:
`emulator/test/host/gwbasic/README.md` has the details). `check.py --write-oracle` writes the program again; run the
oracle again only when it changes, and look at the difference. PC-BASIC agrees with the model on the arithmetic,
the conversions, number input and output and RND's generator; on the transcendental functions it computes its own
results and is often a unit off in the last digit, so `math.differ` lists those, each with why, and the check
fails on any difference it doesn't list.

## The programs

ctest's `test_gwbasic_programs` runs `programs.py`: each `programs/NAME.bas`, tokenised by `basic.py`, runs
in the harness (`harness.s` sets the workspace up as RUN would: the variables past the program, the
string space down from `$9FEE`, or smaller where a program says `REM @memsize N`) through NEWSTT and
the stand-in PRINT (numbers and strings, `;` between). What it prints must be `NAME.out`, made by
`tools/oracle.sh` from the same file. GW-BASIC's own error printer (step I1) prints an error as PC-BASIC
does ("Type mismatch in 20", on a line of its own); then READY's "Ok", which PC-BASIC's `-n` run doesn't
print, is dropped, and so are the `$FF` marks PC-BASIC writes after a message (the OO-1's console doesn't
show them). Where the 1983 source and PC-BASIC disagree, the port keeps the source's behaviour and
`NAME.differ` says why and what it prints:

- a 41-character name and `DEFINT C-A` are Syntax errors in the source; PC-BASIC takes them;
- the source has 10 string temporaries (NUMTMP), and the 11th is String formula too complex; PC-BASIC
  has no limit.

Known, not tested against PC-BASIC (it would differ): `DIM X(20000)` is Subscript out of range in the
source (UMULT's 16-bit size overflows), Out of memory in PC-BASIC; `VAL("&H1F")` needs `OCTCNS`,
which is still a stand-in (Illegal function call).

`gc.bas` is the garbage collector under pressure: 1,000 passes in 1,500 bytes, thousands of strings
made and dropped, the kept ones checked at the end; breaking the collector (one descriptor not moved)
fails it.

## Typed lines

`run6502 --input FILE` answers function 10 with FILE's lines, one a call, echoed as function 10 echoes
(with the characters the buffer holds first, as OODOS's preload), and stops when they run out. With the
control byte 2 the harness starts GW-BASIC itself at READY (INITSA), its workspace at `$9001`: so a
transcript of lines typed at GW-BASIC's prompt can be tried in a second, without OODOS. (The real test of
typed lines is the emulator's `test_gwbasic_core`, against `emulator/test/host/gwbasic/`.)

    printf '\x02\0\0\0\0\0\0\0' > ctrl.vec
    run6502 flat.bin --load 9FF0:ctrl.vec --input lines.txt

The project's own: `flat.cfg`, `harness.s`, `run6502.cpp`, `check.py`, `programs.py`, this file, `oracle/` and
`programs/`.
