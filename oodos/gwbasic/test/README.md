# GW-BASIC's math tests

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

    make -C oodos/gwbasic test BUILD=...                # the image, run6502, check.py
    make -C oodos/gwbasic test BUILD=... CHECK=--timing # the cycles instead
    python3 -I check.py --runner R --image I --seed 7 --count 1500   # more vectors
    python3 -I check.py --runner R --image I --programs             # the spike's programs too, on the stub PRINT

ctest's `test_gwbasic_math` builds `run6502` and the image and runs `check.py` (about 5 s). The harness's vector
layout and operations are at the top of `harness.s`.

`oracle/math.out` is PC-BASIC's output for `oracle/math.bas`, made by `oodos/gwbasic/tools/oracle.sh` (PC-BASIC
2.0.7, its wheel by sha256, in a throwaway `python:3.12-slim` container by digest:
`emulator/test/host/gwbasic/README.md` has the details). `check.py --write-oracle` writes the program again; run the
oracle again only when it changes, and look at the difference. PC-BASIC agrees with the model on the arithmetic,
the conversions, number input and output and RND's generator; on the transcendental functions it computes its own
results and is often a unit off in the last digit, so `math.differ` lists those, each with why, and the check
fails on any difference it doesn't list.

The project's own: `flat.cfg`, `harness.s`, `run6502.cpp`, `check.py`, this file and `oracle/`.
