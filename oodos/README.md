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

And phase 1's tooling:

- [`tools/make_card.sh`](tools/make_card.sh): makes the test card, from [`card/`](card/).
- [`test/interop.sh`](test/interop.sh): checks a card with cpmtools, the committed one or one the OO-1 wrote.


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


## The test card ##

`tools/make_card.sh` makes the card the emulator's tests use, `emulator/test/host/sd/oodos.sparse` (committed,
184 KB; `sd_images.h` expands it): a 16 MB card with an MBR and FAT16 in partition 1, holding

| On the card | What |
|---|---|
| `/BOOT.BIN` | [`card/boot.s`](card/boot.s): says it's an OODOS card and returns to the monitor, until OOLDR (phase 2) |
| `/OODOS/DRIVES.TXT` | [`card/DRIVES.TXT`](card/DRIVES.TXT): `A: /OODOS/A.IMG` |
| `/OODOS/A.IMG` | `oodos-8m`, all 8 MB, label mode 0x31: `0:README.TXT`, `0:PATTERN.BIN` (36 KB, two directory entries), `3:USER3.TXT`, `3:SMALL.BIN` |

It runs everything (ca65, mkfs.cpm, cpmcp, mkfs.fat, mtools, sfdisk) in a throwaway `debian:bookworm` under
faketime, every stamp 2026-10-26 12:00, with fixed volume and disk ids, so the same sources make the same card:

```
oodos/tools/make_card.sh
```

`mkfs.cpm` writes only the directory, and the file grows as `cpmcp` writes; an SD unit is its file's size, so the
script makes A.IMG 8 MB first (`truncate`), the unused blocks zeros. mkfs.fat's boot code is zeroed, as
`emulator/tools/make_sd_images.sh` does. A binary file's byte i is `((i >> 7) * 31 + i * 7 + seed) & 0xFF`; the
script, `test_oodos_card.cpp` and `interop.sh` all know it.


## Interoperability ##

Both ways, in two parts: `test_oodos_card` in ctest, which needs nothing outside the build, and `test/interop.sh`,
which needs Docker and is run by hand, and at each phase's verify.

**cpmtools to the OO-1.** `test_oodos_card` loads the committed card into the emulated SD card, reads
`DRIVES.TXT` and mounts A.IMG as unit 1 with the card's own MOUNT, as OOLDR will, and finds the label, the SFCBs,
each file's entries, stamps and records at LBA = track × 16 + sector, by the DPB it works out from `diskdefs`.

**The OO-1 to cpmtools.** In the same test a stand-in for the BDOS writes `0:OO1.BIN` (260 records, two entries,
made 2026-10-26 13:45, updated 15:30) through the card's WRITE; with `OODOS_CARD_OUT` set it saves the card there.
`interop.sh` takes A.IMG off the card's FAT with mtools and checks it with `fsck.cpm -n` (no Error or Warning
lines), the label's mode, `cpmls -F`'s two stamps for every file, and every file's contents by `cpmcp`; with
`--written`, `0:OO1.BIN` too. From the worktree's root:

```
cmake --build emulator/build/dev --target test_oodos_card
mkdir -p oodos/build
OODOS_CARD_OUT=$PWD/oodos/build/written.img emulator/build/dev/test/host/test_oodos_card
docker run --rm -v "$(hostpath oodos):/o:ro" -v "$(hostpath oodos/build):/c:ro" \
    debian:bookworm sh /o/test/interop.sh /c/written.img --written      # ends PASS 7
docker run --rm -v "$(hostpath oodos):/o:ro" -v "$(hostpath emulator/test/host/sd):/c:ro" \
    debian:bookworm sh /o/test/interop.sh /c/oodos.sparse               # the committed card: PASS 5
```

`OODOS_CARD_FAULT=alloc` (a block README.TXT has) or `=stamp` (each entry's stamps in the next one's slot) makes
the stand-in wrong on purpose: the test then fails, and `interop.sh` fails on the card it saved.

**Phase 3** replaces the stand-in: `write_on_the_oo1()` in `test_oodos_card.cpp` is the seam. It gets the card
with A: mounted and must leave `0:OO1.BIN` on it; the BDOS will do that in a Session, a test program calling make,
write and close with the DS3231 set to the stamps' times, and the checks on both sides stay as they are.

Two things cpmtools 2.23 does that OODOS needn't copy: `cpmcp` leaves an SFCB slot's password mode byte at $E5 (no
matter with passwords off; `cpmls -F` shows it as "Read" protection), and `fsck.cpm` compares the days of an
SFCB's first slot mixed up, so a file whose two stamps are on different days can draw a false warning there.
