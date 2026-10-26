# OODOS #

The One Off DOS: CP/M 3 for the OO-1. The plan and the decisions are in [hw/oodos.md](../hw/oodos.md); this
directory holds the work.

- [`diskdefs`](diskdefs): OODOS's disk formats for cpmtools.
- [`test/check_diskdefs.sh`](test/check_diskdefs.sh): checks them against cpmtools.
- [`spike/gwbasic/`](spike/gwbasic/): the GW-BASIC size spike (phase 0).
- [`Makefile`](Makefile), [`cfg/`](cfg/), [`include/oodos.inc`](include/oodos.inc), [`tools/`](tools/): building
  programs, below.
- [`examples/`](examples/): programs, built by `make`.
- [`test/check_tools.py`](test/check_tools.py): `make check`, the tools' tests.


## Building a program ##

Needs cc65 (ca65 and ld65) and python3. An OODOS program is a `.COM`, linked to run at `$0300`, or a `.PRL`,
relocatable by page and zero page; both start with OODOS's 16-byte header ([hw/oodos.md](../hw/oodos.md#the-program-header)).
Write it in ca65 with `oodos.inc`, which has the header macro, the base page and the BDOS's function numbers:

```
        .include "oodos.inc"
        OODOS_HEADER                    ; once per program

        .zeropage
ptr:    .res    2                       ; the program's own zero page

        .code                           ; the first byte of CODE is the entry
start:  lda     #<msg
        ldx     #>msg
        ldy     #C_WRITESTR             ; the function in Y, the parameter in A and X
        jsr     BDOS
        jmp     WBOOT

        .rodata
msg:    .byte   "Hello", 13, 10, '$'
```

Put it in `examples/NAME.s` and add NAME to `PROGRAMS` in the Makefile (or `make PROGRAMS=NAME`, or
`make SRC=dir BUILD=dir PROGRAMS=NAME` for a program elsewhere). `make` builds:

| File | What |
|---|---|
| `build/NAME.com` | the `.COM`: `cfg/com.cfg`, the image at `$0300` |
| `build/NAME.prl` | the `.PRL`: `mkprl.py` from two links with `cfg/prl.cfg`, `NAME.a` (page 3, zero page `$00`) and `NAME.b` (page 4, `$02`) |
| `build/NAME.map`, `.lbl` | ld65's map and labels for the `.COM` (`.a.map`, `.a.lbl` for the `.PRL`) |

Segments: `ZEROPAGE`, `CODE`, `RODATA`, `DATA` and `BSS` (above the image, not in the file, not cleared). Only
`CODE` is disassembled by the rules check, so keep data out of it.

**The 65C816 rules** ([hw/oodos.md](../hw/oodos.md#rules-for-programs-the-65c816)) are checked on every link by
`tools/rules.py`, and a violation fails the build: no RMB, SMB, BBR or BBS; zero page only through zero page
operands (pass a zero page address in X, as `LDX #ptr`, never as a 16-bit pointer); no absolute access to the
stack page; no JSR or JMP out of the program but to `BDOS` (`$0203`) and `WBOOT` (`$0200`).

**What a `.PRL` may contain:** addresses and their high and low bytes, and zero page addresses. mkprl.py refuses
any byte that differs between the two links other than by a page (1) or the zero page offset (2), and says where;
an expression like `>(label*3)` is refused, but one that happens to move by 1 or 2 (`>(label*2)`) can't be seen.

**Relocating by hand,** as OODOS's loader will: `python3 -I tools/mkprl.py --load build/NAME.prl --page 0x80 --zp
0xF4 -o NAME.img` writes the image as it would be at `$8000` with its zero page at `$F4`.

```
make            # build/*.com, build/*.prl
make check      # test/check_tools.py: ends PASS 47
make clean
```


## Drives on Linux ##

An OODOS drive is an image file on the SD card's FAT volume. cpmtools makes and reads them, given OODOS's
`diskdefs`: it reads `./diskdefs` before its own, so run it here or copy the file beside your images.

```
cd oodos
mkfs.cpm -f oodos-8m -t -L OODOS A.IMG      # -t: date stamps, as OODOS keeps them
printf '\061' | dd of=A.IMG bs=1 seek=12 conv=notrunc   # label mode 0x31: creation and update stamps
cpmcp -f oodos-8m A.IMG HELLO.TXT 0:        # copy in, user 0
cpmls -f oodos-8m -F A.IMG                  # the directory with its stamps, as DIR [FULL]
cpmcp -f oodos-8m A.IMG 0:HELLO.TXT .       # copy out
fsck.cpm -f oodos-8m -n A.IMG               # check, changing nothing
```

| Format | Size | For |
|---|---|---|
| `oodos-8m` | 8 MB | the usual SD drive |
| `oodos-32m` | 32 MB | a large SD drive |
| `oodos-ram` | 128 KB | M:, the RAM disk (for tests) |
| `apple-do` | 140 KB | Apple CP/M disks (the SoftCard's), cpmtools's own |

Make drives with `-t`. Without it there are no SFCBs, so no date stamps, and OODOS's INITDIR would have to add
them. `-t` alone keeps creation stamps only (the directory label's mode byte, entry 0's byte 12, is 0x11);
OODOS's drives keep creation and update stamps, 0x31, so set it after `mkfs.cpm` as above.


## Checking the formats ##

`test/check_diskdefs.sh` checks each format's DPB arithmetic (directory blocks, DSM, EXM), makes it with date
stamps, sets and checks the label's mode (0x31), checks the directory label and an SFCB in every fourth entry,
copies files in and out (users 0 and 3), checks the stamps cpmtools wrote, and runs `fsck.cpm`. It
installs cpmtools, so run it in a throwaway container:

```
docker run --rm -v "$(hostpath oodos):/o:ro" debian:bookworm sh /o/test/check_diskdefs.sh
```

It ends `PASS 18`. Checked with cpmtools 2.23 (Debian bookworm), 2026-10-26.
