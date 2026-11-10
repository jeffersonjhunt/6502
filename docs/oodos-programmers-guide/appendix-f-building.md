# Appendix F: Building with ca65 and cc65

How to build a program: oodos.inc, the link configs, the rules gate, a program in C, and running it in the emulator.
The tested starting point is [docs/examples](../examples/).

OODOS programs are written in ca65, cc65's assembler, and linked with ld65. Nothing more is needed but python3, for the
rules gate and mkprl.py. A program can also be written in C and compiled by cc65 ([A program in C](#a-program-in-c)). The design record:
[oodos/README.md, "Building a program"](../../oodos/README.md#building-a-program).

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

## A program in C

cc65 compiles C for the 65C02, and a C program links as a `.COM` with OODOS's start and cc65's own C library.
[`docs/examples/cargs.c`](../examples/cargs.c):

<!-- listing: ../examples/cargs.c -->
```
/*
 * CARGS: a program in C. It asks the BDOS for its version through bdos(), then prints its
 * arguments, which the CCP gives in capitals. The Programmer's Guide's example in appendix F.
 */

#include <stdio.h>

#include "oodos.h"

int main(int argc, char **argv)
{
    int i;

    printf("BDOS version %02X\n", bdos(S_BDOSVER, 0) & 0xFF);
    printf("%d arguments\n", argc - 1);
    for (i = 1; i < argc; ++i)
        printf("  %s\n", argv[i]);
    return 0;
}
```

With no arguments, it prints:

<!-- listing: ../examples/cargs.out -->
```
BDOS version 31
0 arguments
```

Build it as `CPROGRAMS`, not `PROGRAMS`, for `myprog/name.c`:

```
make -C oodos SRC=$PWD/myprog CPROGRAMS=name BUILD=$PWD/build $PWD/build/name.com
```

The Makefile compiles it (`cc65 -t none --cpu 65c02 -O`), assembles cc65's output, and links it with
[`cfg/c.cfg`](../../oodos/cfg/c.cfg), `oodosc.lib` (OODOS's part, built from
[`oodos/c/`](../../oodos/c/)) and cc65's `none.lib`. The rules gate reads the program's code and the library's.
A cc65 warning fails the build, as an ld65 warning does.

**What the program has:**

- **`main(argc, argv)`**, from the command tail. The words are split at spaces and are in capitals, as the CCP
  leaves them. `argv[0]` is `""`, since OODOS doesn't keep a program's name, and `argv[argc]` is `NULL`. At most 16
  words are taken, counting `argv[0]`.
- **stdin, stdout and stderr** are the console. A write sends each byte by function 2, a `'\n'` as CR LF. A read
  takes a line by function 10, with OODOS's line editing, and gives it with a `'\n'` at its end. A line holding
  only ^Z is the end of the file. Any other file descriptor is refused with `EBADF`. Files through `fopen` are a
  later step.
- **`bdos(fn, arg)`**, from [`oodos/include/oodos.h`](../../oodos/include/oodos.h), calls any function. It follows
  the rule `JSR BDOS` follows: the function in Y, `arg` in A and X, the result in A and X. A function that returns a
  byte gives it in the result's low byte. `oodos.h` has the functions' and the base page's names, written from
  `oodos.inc` by `oodos/tools/oodosh.py`, and the build checks that they are the same.
- **The end:** `main`'s return, or `exit(n)`, ends the program by the warm boot. A status other than 0 sets the
  return code ([function 108](07-function-reference.md#bdos-108)) to `$FF00`, so a `:` command after it doesn't
  run.
- **Memory:** cc65's runtime takes 26 bytes of zero page from `$00`, and a program's own zero page follows. The C
  stack runs down from the top of the TPA. The heap (`malloc`) runs up from the end of `BSS` and stops 2 KB below
  the stack's start. With less than those 2 KB between the end of `BSS` and the top of the TPA (a large program,
  or RSXs resident below the BDOS), the program prints `Not enough memory for the C stack` and ends as a failure,
  before `main`. For another size, link with `CLDFLAGS="-D __STACKSIZE__=N"`.

**`cfg/c.cfg`'s segments** are `cfg/com.cfg`'s, with two of cc65's added after `HEADER` and `ZEROPAGE`:
`STARTUP`, the start ([`oodos/c/crt0.s`](../../oodos/c/crt0.s)), which comes first at `$0300`; and `ONCE`, the code
that runs once before `main`. The constructor tables go in `RODATA`, so `STARTUP`, `ONCE` and `CODE` hold only code,
and the rules gate reads all three.

**cc65's `printf`.** cc65's library breaks rule 2 in one place: `_printf.o` saves its registers with `LDA regbank,Y`,
an absolute operand in zero page. `oodosc.lib` has an altered copy of it,
[`oodos/c/_printf.s`](../../oodos/c/_printf.s), which keeps cc65's licence and marks its changed lines `OODOS`. The
link takes the two libraries as a group (`--start-group`, `--end-group`), so ld65 takes `oodosc.lib`'s modules ahead
of `none.lib`'s. `oodos/test/check_tools.py` links the stock module on purpose and checks that the rules gate
refuses it.

**Not yet:** files through stdio, a `.PRL` in C, and C headers for OOLIB.

## Running it

**In the emulator's tests.** [`emulator/test/host/oodos_rig.h`](../../emulator/test/host/oodos_rig.h)'s `CcpRig`
boots OODOS.SYS from the test card. `put_m()` puts a `.COM` on `M:`, the RAM disk, before it is logged in, and
`cmd("m:name")` types the command at the prompt and returns what it printed, up to the next `A>`.
[`test_docs_examples.cpp`](../../emulator/test/host/test_docs_examples.cpp) is the smallest suite that does this.

**A book's example** goes in [docs/examples/](../examples/) as `NAME.s`, or `NAME.c` in C, with `NAME.out`, what it prints. Its
[Makefile](../examples/Makefile) builds each through oodos's rules, `test_docs_examples` runs each from the CCP
and compares, and the book shows it with a listing marker, which `make -C docs check` holds to the file
([docs/README.md, "Examples"](../README.md#examples)).

**On a card.** An OODOS drive is a CP/M 3 file system in an image file on the SD card's FAT partition, so cpmtools
put a program on it from Linux: `cpmcp -f oodos-8m A.IMG build/name.com 0:NAME.COM`, with OODOS's formats from
[`oodos/diskdefs`](../../oodos/diskdefs) (its first lines say how to point cpmtools at it). Then `NAME` at the
prompt runs it.
