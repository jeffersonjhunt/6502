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
- [`test/cpmtools.sh`](test/cpmtools.sh), [`test/sfcb.py`](test/sfcb.py): what `check_diskdefs.sh` and
  `interop.sh` share, and interop's own reading of the date stamps.
- [`ldr/`](ldr/): OOLDR, the loader (`/BOOT.BIN`), and a test OODOS.SYS; [`sys/cpmread.s`](sys/cpmread.s), the
  read-only CP/M reader OOLDR links (phase 2, below).
- [`sys/`](sys/): OODOS.SYS: common memory, the BDOS (phase 3a: the file system), the stand-in CCP, the BIOS;
  below.
- [`test/cpm3model.py`](test/cpm3model.py), [`test/modelscript.py`](test/modelscript.py),
  [`test/modeltest.py`](test/modeltest.py), [`test/driver/`](test/driver/): the BDOS's model test (phase 3a,
  below).


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
| `build/NAME.prl` | the `.PRL`: `mkprl.py` from three links with `cfg/prl.cfg`, `NAME.a` (page 3, zero page `$00`), `NAME.b` (page 4, `$02`) and `NAME.c` (page 5, `$05`) |
| `build/NAME.map`, `.lbl` | ld65's map and labels for the `.COM` (`.a.map`, `.a.lbl` for the `.PRL`) |

Segments: `ZEROPAGE` (BSS only, `$00`–`$EF`: initialized data there is a build error; a `.PRL`'s is at most `$EB`
bytes, as its third link puts it at `$05`), `CODE`, `RODATA`, `DATA` and `BSS` (above the image, not in the file,
not cleared). Only `CODE` is disassembled by the rules check, so keep data out of it. Any ld65 warning fails the
build.

**The 65C816 rules** ([hw/oodos.md](../hw/oodos.md#rules-for-programs-the-65c816)) are checked by
`tools/rules.py` in `CODE`, the `.COM`'s and the `.PRL`'s first link's (only `CODE`), and a violation fails the
build: no RMB, SMB, BBR or BBS; zero page only through zero page operands; no absolute access to the stack page;
no JSR or JMP out of the program but to `BDOS` (`$0203`) and `WBOOT` (`$0200`). Pass a zero page address in X, as
`LDX #ptr`, never as a 16-bit pointer: mkprl.py refuses a zero page address anywhere but as the operand of a zero
page or immediate instruction in `CODE` (`.word ptr` in data, for one). It checks pointers to the program's own
`ZEROPAGE` segment only: pointers to an overlay's zero page (imported, so it doesn't move) or to absolute zero page
addresses, in `CODE` or the data segments, rely on the author.

**What a `.PRL` may contain:** addresses and their high and low bytes, and zero page addresses. mkprl.py refuses
any byte that doesn't move between the three links as nothing does, as a page does (1, then 2) or as the zero page
does (2, then 5), and says where; so an expression like `>(label*2)`, which moves by 2 and 4, is refused.

**Loading by hand,** by the loader's steps and refusals ([hw/oodos.md](../hw/oodos.md#the-prl-file)):

```
python3 -I tools/mkprl.py --load build/NAME.prl --page 0x80 --zp 0x40 -o NAME.img  # at $8000, zero page $40
python3 -I tools/mkprl.py --load RSX.PRL --rsx --page 0xA0 --zp 0xF8 -o RSX.img    # an RSX, its slice at $F8
python3 -I tools/mkprl.py --load OVL.PRL --overlay 0x6000 -o OVL.img               # an overlay (function 59)
```

```
make            # build/*.com, build/*.prl
make check      # test/check_tools.py build: ends PASS 99
make clean
```

`make check` checks every program in the build, then the tools with its own hello and test programs. It runs
hello on `spike/gwbasic/run6502` (the emulator's W65C02S), so it needs a C++ compiler too.

And phase 1's tooling:

- [`tools/make_card.sh`](tools/make_card.sh): makes the test card, from [`card/`](card/).
- [`test/interop.sh`](test/interop.sh): checks a card with cpmtools, the committed one or one the OO-1 wrote.


## The system: OODOS.SYS ##

Phase 2's system ([hw/oodos.md](../hw/oodos.md#the-system-phase-2)) with phase 3a's BDOS
([hw/oodos.md](../hw/oodos.md#the-bdos-phase-3)), in [`sys/`](sys/):

| File | What |
|---|---|
| `common.s` | common memory, `$F800–$FFFF` in block 62: the BIOS's jump table, the BDOS stub, the warm boot, FARCALL, IRQ, Break and BRK, the vectors |
| `bdos.s`, `bdosfile.s`, `bdosdisk.s`, `bdos.inc` | the BDOS, block 60: the entry, the console and errors; the file functions; the drives, buffers, directory hash, allocation vectors and stamps |
| `bios.s`, `crt.s` | the BIOS, block 61: drives, M:, TIME, MOVE, the devices; the text screen's VT52 subset |
| `cold.s`, `ccp.s` | block 62's code window: cold boot (OODOS.SYS's entry) and the stand-in CCP, with `GO` |
| `sys.inc`, `sys.cfg` | the system block's layout and the SCB; the one ld65 link, a file for each block's part |
| `mksys.py` | makes OODOS.SYS of those parts (its header: hw/oodos.md), and reads one as OOLDR will |
| `test/` | the test programs (`.COM`s: BIOSTEST, STUBTEST, FARTEST, TIMETEST, LINETEST, WPTEST, IRQTEST, VT52TEST, BANKTEST, HOLETEST; the BDOS's FSTEST, DIRTEST, ERRTEST, STAMPTEST, LOGINTEST, RULETEST (the review's findings against the Programmer's Guide, and the model comparison's), with `fs.inc` and `fszp.inc`; WRITEOO1, on the card), `check_mksys.py`, `make_mdisk.sh` |

```
make -C sys             # sys/build/OODOS.SYS and the test programs, sys/build/NAME.com
make -C sys sizes       # its segments: mksys.py --check
make -C sys check       # test/check_mksys.py: ends PASS 55
```

The native test runs it all on the emulator; from `emulator/`, after `cmake --build --preset dev` (which runs the
Makefile above into the build tree, with `ldr/`'s and this directory's for `test_oodos_boot`; configuring fails
without cc65, python3 and make, as ctest has no skips):

```
ctest --preset dev -R test_oodos_sys
./build/dev/test/host/test_oodos_sys     # the cases; the stub's, FARCALL's, the jump table's and the BDOS's cycles
```

At the stand-in CCP, `NAME` runs `NAME.COM` from drive A, user 0, loaded through the BDOS (`HELLO` on the test
card). To run something by hand instead, put a `.COM`'s header at `$0280` and its image at `$0300` and type `GO`,
as test_oodos_sys does; `GO M` gives the program the tail ` M` (FSTEST and STAMPTEST take their drive so).

M:'s format is checked against what cpmtools makes, kept as `emulator/test/host/sd/oodos-ram.sparse`; it was made,
and is made again, with Docker:

```
sys/test/make_mdisk.sh      # mkfs.cpm -f oodos-ram -t at 2026-10-27 12:00, the mode set to 0x31
```


## The BDOS's model test (phase 3a) ##

A model of CP/M 3's file system, ours, in Python (standard library only), that seeded random scripts of file
operations run on, compared with what the BDOS does on the emulator ([hw/oodos.md](../hw/oodos.md#testing)):

| File | What |
|---|---|
| [`test/cpm3model.py`](test/cpm3model.py) | the model: functions 13–37, 40, 46, 48, 98, 99 on drive images; its header lists what it models and its choices where the manual is open |
| [`test/modelscript.py`](test/modelscript.py) | the script and result formats (its header), the reference driver, the generator, the comparison |
| [`test/modeltest.py`](test/modeltest.py) | the commands, below |
| [`test/driver/`](test/driver/) | DRIVER.COM, which runs `A:SCRIPT.BIN` through the BDOS and writes `A:RESULT.BIN`; its own Makefile |
| [`test/test_model.py`](test/test_model.py) | the model's tests: hand-worked CP/M 3 cases, the formats, the generator, the comparison |
| [`test/model_cpmtools.sh`](test/model_cpmtools.sh) | the model's drives checked by cpmtools (Docker) |

```
make -C test/driver             # test/driver/build/driver.com, through the rules gate
make -C test/driver check       # test_model.py: ends PASS 194
python3 -I test/modeltest.py prepare --seed 7 --format oodos-8m --dir /tmp/m7 --driver test/driver/build/driver.com
python3 -I test/modeltest.py show --dir /tmp/m7       # the script, an operation a line
python3 -I test/modeltest.py predict --dir /tmp/m7    # the model's run: A.model.IMG, T.model.IMG, RESULT.model.BIN
python3 -I test/modeltest.py check --dir /tmp/m7      # against A.out.IMG and T.out.IMG, what DRIVER left
```

`prepare` writes the seed's `SCRIPT.BIN`, an `oodos-8m` A: holding it and DRIVER.COM (`A.IMG`), and the drive
under test, empty (`T.IMG`): `oodos-ram` is M:, the others B:. `check` prints the seed, each operation that
differs and how (the first ten), the drive's entries and records that differ, and PASS or FAIL. `--compare
exact` compares the drive byte for byte, for a BDOS that allocates as the model (`--alloc lowest`, the
default, or `nearest`); `renumber`, the default, up to the blocks' numbering. `--loose-x` leaves X alone where
the model's is 0.

`test_oodos_model`, in ctest, runs DRIVER.COM with the model as its BDOS, for 9 seeds, and with the model made
wrong on purpose (`serve --fault`), and DRIVER under OODOS.SYS. **The comparison with OODOS's own BDOS** is on
(the CMake option `OODOS_MODEL_BDOS`; a build tree configured before it was on keeps OFF in its cache, so pass
`-DOODOS_MODEL_BDOS=ON` once; off, its case says it's skipped and ctest counts the test skipped) for seeds 1, 4, 6,
10, 14, 24, 41 and 45 (`modeltest.py seeds`: between them a full disk, a full directory on M: and on B:, a read-only
drive), about 6 minutes under ASan. More seeds by hand,
from `emulator/`: `OODOS_MODEL_SEEDS` takes a list and ranges, `OODOS_MODEL_ONLY=1` runs the comparison alone, so
seeds can run a process each, and `OODOS_MODEL_CUT=N` runs only a seed's first N operations, to find where a
difference starts (a release build is about ten times faster than ASan's). `model_seeds.sh` runs a process a seed
and counts a seed passed only if it exited 0 and printed its PASS line; it exits non-zero unless all did.
`OODOS_MODEL_COMPARE`, `OODOS_MODEL_ALLOC`, `OODOS_MODEL_CUT` and `OODOS_MODEL_SEEDS` take only their values (any
other fails the run), and `OODOS_MODEL_ONLY=1` with the comparison off fails:

```
cmake --preset dev -DOODOS_MODEL_BDOS=ON && cmake --build --preset dev && ctest --preset dev -R test_oodos_model
OODOS_MODEL_BDOS=0 ./build/dev/test/host/test_oodos_model        # without the comparison, for one run
OODOS_MODEL_SEEDS=4,7-9 OODOS_MODEL_COMPARE=exact OODOS_MODEL_LOOSE_X=1 ./build/dev/test/host/test_oodos_model
sh ../oodos/test/model_seeds.sh -j 16 ./build/dev/test/host/test_oodos_model 1-600    # ends 600/600 seeds pass
OODOS_MODEL_ONLY=1 OODOS_MODEL_SEEDS=14 OODOS_MODEL_CUT=213 ./build/dev/test/host/test_oodos_model
```

The seeds' directories stay in the build tree (`build/dev/test/host/oodos_model/oodos-N`), with what DRIVER
left, for `modeltest.py show` and `check` by hand. The cpmtools cross-check, at verify (the model's empty drives
against `mkfs.cpm -t`'s, then each seed's drive by `fsck.cpm`, `cpmls -F` and `cpmcp`):

```
docker run --rm -v "$(hostpath oodos):/o:ro" \
    debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61 \
    sh /o/test/model_cpmtools.sh 1 2 3 4 5 6 7 8 9 10 11 12      # ends PASS 15
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
docker run --rm -v "$(hostpath oodos):/o:ro" \
    debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61 sh /o/test/check_diskdefs.sh
```

It ends `PASS 18`; `fsck.cpm` must print no Error or Warning line. Checked with cpmtools 2.23 (Debian bookworm),
2026-10-26. The image is pinned by digest, as everywhere here (`make_card.sh` has it).


## The test card ##

`tools/make_card.sh` makes the card the emulator's tests use, `emulator/test/host/sd/oodos.sparse` (committed,
389 KB; `sd_images.h` expands it): a 32 MB card with an MBR and FAT16 in partition 1, holding

| On the card | What |
|---|---|
| `/BOOT.BIN` | OOLDR, [`ldr/ooldr.s`](ldr/ooldr.s) (phase 2, below) |
| `/OODOS/DRIVES.TXT` | [`card/DRIVES.TXT`](card/DRIVES.TXT): `A: /OODOS/A.IMG`, `B: /OODOS/B.IMG` |
| `/OODOS/A.IMG` | `oodos-8m`, all 8 MB, label mode 0x31: `0:README.TXT`, `0:PATTERN.BIN` (36 KB, two directory entries), `3:USER3.TXT`, `3:SMALL.BIN`, `0:OODOS.SYS` (OODOS, [`sys/`](sys/)), `0:TESTSYS.SYS`, OOLDR's test system ([`ldr/testsys.s`](ldr/testsys.s): 260 records, two entries), `0:HELLO.COM`, `0:HELLO.PRL` and `0:WRITEOO1.COM` ([`sys/test/writeoo1.s`](sys/test/writeoo1.s), below) |
| `/OODOS/B.IMG` | `oodos-8m`, all 8 MB, label mode 0x31, empty |

It runs everything (make, ca65 and ld65 for OOLDR and OODOS.SYS, mkfs.cpm, cpmcp, mkfs.fat, mtools, sfdisk) in a
throwaway `debian:bookworm`, pinned by digest, under faketime, every stamp 2026-10-26 12:00, with fixed volume and
disk ids, so the same sources make the same card; it prints the packages' versions:

```
oodos/tools/make_card.sh
```

Run it after changing anything on the card: `test_oodos_boot` builds OOLDR, both systems, HELLO.COM, HELLO.PRL
and WRITEOO1.COM from the sources and fails, with "the card is stale: run oodos/tools/make_card.sh", unless the card
holds exactly those (and `card/DRIVES.TXT`).

`mkfs.cpm` writes only the directory, and the file grows as `cpmcp` writes; an SD unit is its file's size, so the
script makes A.IMG 8 MB first (`truncate`), the unused blocks zeros. mkfs.fat's boot code is zeroed, as
`emulator/tools/make_sd_images.sh` does. A binary file's byte i is `((i >> 7) * 31 + i * 7 + seed) & 0xFF`; the
script, `test_oodos_card.cpp` and `interop.sh` all know it.


## Interoperability ##

Both ways, in two parts: `test_oodos_card` in ctest, which needs nothing outside the build, and `test/interop.sh`,
which needs Docker and is run by hand, and at each phase's verify.

**cpmtools to the OO-1.** `test_oodos_card` loads the committed card into the emulated SD card, reads
`DRIVES.TXT` (to its size, by LIST) and parses it by its grammar, mounts A.IMG as unit 1 with the card's own MOUNT,
as OOLDR will, and finds the label, the SFCBs, each file's entries, stamps and records in the n-th sector of block
b, LBA = b × (4096 / 512) + n, by the DPB it works out from `diskdefs`.

**The OO-1 to cpmtools.** In the same test OODOS writes `0:OO1.BIN` (260 records, two entries, made 2026-10-25
13:45, updated 2026-10-26 15:30): the card booted in a Session as its owner would (C80CR, OOLDR, OODOS.SYS) and
`WRITEOO1` typed at the prompt, which sets the clock by function 104 for each stamp and writes through the BDOS;
with `OODOS_CARD_OUT` set the test saves the card there. `interop.sh` takes A.IMG off the card's FAT with mtools and checks every file entry's stamp order
(`sfcb.py`), `fsck.cpm -n` (no Error or Warning lines, but the one below), the label's mode, `cpmls -F`'s two
stamps for every file, and every file's contents by `cpmcp`; with `--written`, `0:OO1.BIN` too, and its two SFCB
slots byte for byte: the stamps in its first entry's, 0 in its second's (CP/M 3 keeps a file's stamps in its
extent-zero entry only; cpmtools stamps every entry, and `sfcb.py` takes either). From the worktree's root:

```
cmake --build emulator/build/dev --target test_oodos_card
mkdir -p oodos/build
OODOS_CARD_OUT=$PWD/oodos/build/written.img emulator/build/dev/test/host/test_oodos_card
bookworm=debian:bookworm@sha256:854dd8ab51d62866dbd8f5431d15ce77b12983a2a4a1e1fe43a2395dc7ef9f61
docker run --rm -v "$(hostpath oodos):/o:ro" -v "$(hostpath oodos/build):/c:ro" \
    $bookworm sh /o/test/interop.sh /c/written.img --written      # ends PASS 9
docker run --rm -v "$(hostpath oodos):/o:ro" -v "$(hostpath emulator/test/host/sd):/c:ro" \
    $bookworm sh /o/test/interop.sh /c/oodos.sparse               # the committed card: PASS 6
```

`OODOS_CARD_FAULT=alloc` (a block README.TXT has), `=stamp` (each entry's stamps in the next one's slot, the first
entry's update a minute later, so no slot is right) or `=stamp-zero` (the same in an SFCB of zeros, not $E5s)
has phase 1's stand-in writer make the file instead, wrong on purpose: the test then fails, and `interop.sh` fails
on the card it saved.

Two things cpmtools 2.23 does that OODOS needn't copy: `cpmcp` leaves an SFCB slot's password mode byte at $E5 (no
matter with passwords off; `cpmls -F` shows it as "Read" protection), and `fsck.cpm` compares the stamps in an
SFCB's first slot backwards: an entry there updated on a later day than it was made draws "Warning: Modification
date earlier than creation date (extent=S/E)", and one updated on an earlier day draws nothing (same-day stamps are
compared right). `interop.sh` checks the order with `sfcb.py` first, then lets that warning through for those
entries only
([hw/oodos.md](../hw/oodos.md#testing)).


## The loader (phase 2) ##

OOLDR is `/BOOT.BIN` on an OODOS card: the ROM BIOS's `C80CR` loads it at `$1000`, and it reads
`/OODOS/DRIVES.TXT`, mounts the drives and loads OODOS.SYS from A: ([hw/oodos.md](../hw/oodos.md#the-system-phase-2)).
[`ldr/`](ldr/) builds it, with its own Makefile, and the test system the card carries as TESTSYS.SYS:

```
make -C ldr            # ldr/build/BOOT.BIN (OOLDR) and ldr/build/OODOS.SYS (testsys.s)
make -C ldr clean
```

| File | What |
|---|---|
| [`ldr/ooldr.s`](ldr/ooldr.s), [`ldr/ldr.cfg`](ldr/ldr.cfg) | OOLDR: at `$1000`, everything below `$4000`, zero page `$00–$1F` |
| [`sys/cpmread.s`](sys/cpmread.s) | the read-only CP/M reader: CR_INIT, CR_FIND, CR_SEEK, CR_READ; its zero page and buffers are the `CRZP` and `CRBSS` segments, placed by whoever links it (OOLDR; OODOS.SYS's CCP loads through the BDOS since phase 3a) |
| [`ldr/testsys.s`](ldr/testsys.s), [`ldr/testsys.cfg`](ldr/testsys.cfg) | the test OODOS.SYS: six segments in block 31 and blocks 60–62, one from block 60 into 61, a cold boot that writes the windows and P it found to `$7FF00` and stops |

It builds with the host's cc65 as with the container's (Debian's 2.19 both), byte for byte. To try it: `make -C ldr`,
`tools/make_card.sh`, then in the emulator with the card in the slot, `C80CR` from the monitor. Each refusal says
what, DRIVES.TXT's with the line's number, and goes back to the monitor.

`test_oodos_boot`, in ctest, boots the committed card and checks where every segment landed and the entry's
windows and flags, then makes each refusal on cards it builds in memory (a changed DRIVES.TXT, a changed A.IMG):

```
cmake --build emulator/build/dev --target test_oodos_boot
emulator/build/dev/test/host/test_oodos_boot      # ends "15 cases, 0 failed"
```
