# Appendix F: Building with ca65 and cc65

How to build a program: oodos.inc, the link configs, the rules gate, and running it in the emulator. The tested
starting point is [docs/examples](../examples/).

OODOS programs are written in ca65, cc65's assembler, and linked with ld65. Nothing more is needed but python3, for
the rules gate and mkprl.py. The design record: [oodos/README.md, "Building a program"](../../oodos/README.md#building-a-program).

## A program

[`docs/examples/writestr.s`](../examples/writestr.s), function 9's example, is the smallest whole program:

<!-- listing: ../examples/writestr.s -->
```
; WRITESTR: prints a line through BDOS function 9, C_WRITESTR, then ends by
; the warm boot. The Programmer's Guide's example for function 9.

        .include "oodos.inc"

        OODOS_HEADER                    ; once per program

        .code
start:  lda     #<msg                   ; the string's address: low in A,
        ldx     #>msg                   ; high in X
        ldy     #C_WRITESTR             ; the function in Y
        jsr     BDOS
        jmp     WBOOT                   ; back to the CCP

        .rodata
msg:    .byte   "Written by function 9", 13, 10, '$'
```

- **[`oodos.inc`](../../oodos/include/oodos.inc)** has `OODOS_HEADER`, the base page's addresses (`BDOS`, `WBOOT`,
  `BP_FCB`, `BP_DMA`, ...) and every function's name. Call the BDOS by name (`C_WRITESTR`), not by number.
  [`cmd.inc`](../../oodos/include/cmd.inc) has console helpers, for a program that is a command.
- **`OODOS_HEADER`** goes in one module, once.
- **The entry** is `$0300`, the first byte of `CODE`.
- **The end** is `JMP WBOOT`.

## The segments

[`cfg/com.cfg`](../../oodos/cfg/com.cfg) links a `.COM`:

| Segment | Where | What |
|---|---|---|
| `HEADER` | the file's first 16 bytes | `OODOS_HEADER`'s, in the file only |
| `ZEROPAGE` | `$00-$EF` | the program's zero page: BSS only (`.res`) |
| `CODE` | from `$0300` | the program; the only segment the rules gate reads |
| `RODATA`, `DATA` | after `CODE` | constants and initialized data |
| `BSS` | after the image | in memory, not in the file, and not cleared |

Initialized data in `ZEROPAGE` is dropped by ld65 with a warning, and the build fails on any ld65 warning. The
image and BSS must end below `$BFFD`. [`cfg/prl.cfg`](../../oodos/cfg/prl.cfg) is the same, with the link page and
zero page base as symbols, for a `.PRL`'s three links ([chapter 6](06-program-formats.md#prl-files)); a `.PRL`'s
zero page may be `$EB` bytes at most, as its third link puts it at `$05`.

## Building

oodos's [Makefile](../../oodos/Makefile) assembles, links, and runs the rules gate. From the repository's root, for
`myprog/name.s`:

```
make -C oodos SRC=$PWD/myprog PROGRAMS=name BUILD=$PWD/build $PWD/build/name.com
```

Name the target: without it, make builds `all`, which is every program in `PROGRAMS` as a `.COM` and a `.PRL`, and
every utility. Don't give a program a utility's name (one of `oodos/utils/*.s`): the Makefile builds the utility
instead. `$PWD/build/name.prl` builds the `.PRL`.

The build fails on:

- any ld65 warning, as on an error;
- a break of the 65C816 rules ([chapter 6](06-program-formats.md#rules-for-programs-the-65c816)), found by
  `oodos/tools/rules.py` in `CODE`;
- for a `.PRL`, a byte that doesn't move between the three links as nothing, a page or the zero page does, or a
  16-bit pointer to the program's zero page, found by `oodos/tools/mkprl.py`.

## Running it

**In the emulator's tests.** [`emulator/test/host/oodos_rig.h`](../../emulator/test/host/oodos_rig.h)'s `CcpRig`
boots OODOS.SYS from the test card. `put_m()` puts a `.COM` on `M:`, the RAM disk, before it is logged in, and
`cmd("m:name")` types the command at the prompt and returns what it printed, up to the next `A>`.
[`test_docs_examples.cpp`](../../emulator/test/host/test_docs_examples.cpp) is the smallest suite that does this.

**A book's example** goes in [docs/examples/](../examples/) as `NAME.s`, with `NAME.out`, what it prints. Its
[Makefile](../examples/Makefile) builds each through oodos's rules, `test_docs_examples` runs each from the CCP
and compares, and the book shows it with a listing marker, which `make -C docs check` holds to the file
([docs/README.md, "Examples"](../README.md#examples)).

**On a card.** An OODOS drive is a CP/M 3 file system in an image file on the SD card's FAT partition, so cpmtools
put a program on it from Linux: `cpmcp -f oodos-8m A.IMG build/name.com 0:NAME.COM`, with OODOS's formats from
[`oodos/diskdefs`](../../oodos/diskdefs) (its first lines say how to point cpmtools at it). Then `NAME` at the
prompt runs it.
