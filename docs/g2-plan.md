# G2: the plan for the OO-1 Technical Reference

Draft, 2026-11-05, for the owner's approval. Nothing below is written until it is approved. G2 writes the
[Technical Reference](oo1-technical-reference/README.md) for what the emulator implements
([README.md, "Phases"](README.md#phases)). The emulator is the truth: where hw/design.md and the code disagree, the
book says what the code does and the difference goes on the list in [chapter 14](#the-chapters) and in the
questions [below](#questions-for-the-owner).

Nothing is on a real card yet, so no section or entry is **built**. Every section is **emulated** or **proposed**.

## What the emulator has, by card

These counts come from the emulator's register decode (`emulator/components/devices/*`), compared with
[`backplane.inc`](../backplane/bios/backplane.inc). "Entries now" is the 77 anchors in
[tools/unwritten.txt](tools/unwritten.txt). "Decoded, no equate" is what G2 adds if the owner takes option A
(below).

| Module | Card | Entries now | Decoded, no equate |
|---|---|---|---|
| 6 | serial: the 6551 at `$C300`, the VIA at `$C340` | 4 | VIA: 16 (ORB, ORA, DDRB, DDRA, T1CL, T1CH, T1LL, T1LH, T2CL, T2CH, SR, ACR, PCR, IFR, IER, ORA without handshake) |
| 7 | storage (SD) | 10 | 2: BUFPTR `$C38D`, ISR `$C391` |
| 8 | DMA | 13 | 11: channels 1 and 2 (ADDR, COUNT, MODE, CTRL, DEST each), channel 0's DEST `$C408` |
| 9 | video | 19 | 7: ISR `$C485`, LINE `$C486`, layer 1's MAP, TILE, HSCROLL, VSCROLL, PALOFS |
| A | sound | 6 | 4: SID B `$C520`, STATUS `$C541`, MBOUT `$C546`, MBIN `$C547` |
| B | USB | 5 | 22: MODS, EVCOUNT, EVENT, LEDS, REPEAT, the mouse's 7 (`$C590-$C59A`), the game port's 7 (`$C5A0-$C5A6`, SPKR among them), ISR, DEVICES |
| C | Disk II | 2 | none (native mode decodes only the ID) |
| D | north bridge | 22 | 4: WAIT0-3 `$C68A-$C68D` |
| E | console | 2 | 13: SWR, PANEL, KEYS, DISPLAY, CTRL, BPADDR, BPCTRL, STATUS, LADDR, LDATA, LSTAT, PADDR, ISR |

That makes about 156 entries with option A, against 77 without it.

## The chapters

Each chapter keeps G0's sections. A register entry goes under the chapter's `## Registers`, in address order, from
[templates/register.md](templates/register.md). Each section's tag is listed here; where the plan changes a G0
tag, the change is marked.

1. **Overview.** The backplane and its signals, the slots and module IDs (`$C000 + n × $80`, the ID byte at
   +`$7F`, `$n1` for revision 1), and the clocks: 7.16 MHz native, about 1.02 MHz in Apple II mode, and the cards'
   own clocks. Status: emulated. The ÷1 turbo mode is a proposed paragraph. No entries.
2. **Memory and banking.** 1 MB in 64 blocks, the four windows, ROM and RO bits, VEC, the I/O hole
   `$C000-$C7FF`, and who uses zero page and low RAM (from backplane.inc's table). Emulated. Its entries are
   chapter 11's, linked from here.
3. **Boot.** Reset from the flash, the copy to block 63, /BOOTSEL (emulated as the loader's option; the ESP32 that
   drives it is proposed), the entry points `$C800-$C80F`, the monitor and its keys, Break and BRK (what they
   reset and what they leave), and the SD boot with BIOS 0.10's auto-boot. Listings of `bios.s`, with line ranges.
   Emulated.
4. **Interrupts.** /IRQ and /NMI, IRQL/IRQH (0 means interrupting) and the bit each card has: IRQL 6 serial
   (the 6551 and the VIA share it), IRQL 7 SD, IRQH 0 DMA, 1 video, 2 sound, 3 USB, 5 RTC, 6 console. VEC, and
   Pause/Break and the console's breakpoint as NMI. Emulated. The serial card's INTSTAT is proposed.
5. **DMA.** The four channels, their priority, the modes, memory to memory, a card's DRQ (0 SD, 2 sound RAM),
   BURST, ABORT, and how the BIOS uses channels 0 and 3. Emulated, TC included: the card sees terminal count with
   its last byte. Entries:
   `$C400-$C442`, `$C47F` (13 now, 24 with option A).
6. **Video.** The text, tile, bitmap 256, bitmap 512 and mono layers, CONFIG's bits, sprites (64 entries of
   8 bytes at SPRBASE), the palette (256 × 12 bits at PALBASE), video memory in banks 8-B, latching at vertical
   blank, the interrupts, and the splash. Emulated. G0's tags stand. Entries: `$C480-$C4A7`, `$C4FF`.
   CONFIG types 6 and 7 are unassigned.
7. **Sound.** The two SIDs (`$C500`, `$C520`), the card's 6510 and its 64 KB, RAMADDR/RAMDATA, the mailboxes,
   NTSC/PAL, and `.sid` playback. Emulated. STATUS's BUSY bit is proposed: the emulator never sets it. Each SID is
   one entry, with its 29 registers in a table in our own words.
8. **USB.** The keyboard (cooked KEY, the raw event FIFO, MODS, LEDS, REPEAT, KBCFG), the mouse, and the game port
   with the speaker. Emulated: G0's three sections stand, because the emulator has the mouse and the game port.
   Layouts other than US, and a gamepad, are proposed.
9. **Serial and the VIA.** The TL16C552A's COM1, COM2 and LPT, the card's ID and INTSTAT: **proposed**, in prose,
   with no entries until they have equates. The 6551 stand-in: emulated, mirrored through the block. The VIA at
   `$C340` with the joysticks on PA0-4 and PB0-4 (0 = pressed): emulated. The user port: proposed, since the
   joysticks take both ports. A note on [hw/oolib.md's "The VIA is shared"](../hw/oolib.md#the-via-is-shared).
10. **Storage.** The SD card's commands (`$20`, `$30`, `$A0-$A2`, `$E7`, `$EC`), errors 1-9, FAT mounts and
    units, DMA by channel 0, and how the BIOS waits. Emulated. **The Disk II:** [below](#the-disk-ii).
11. **The north bridge and the clock.** WIN0-3, VEC, MODE (APPLE, TURBO kept but not modelled), STATUS, WAIT0-3,
    IRQL/IRQH, RTCCTL and SEC-YEAR, RTCREG/RTCDATA, BUSY. Emulated, with TURBO proposed. Entries: `$C680-$C698`,
    `$C6FF`.
12. **The front panel.** The console card's switches, keys, lights, breakpoint and latched cycle, as a program
    sees them. Emulated. The design's handling of AMODE and the BE lamp, and a true bus-cycle step, are a
    proposed section. Entries: `$C700-$C719`, `$C77F`.
13. **Apple II mode.** Entering it (MODE's APPLE, `$C806`, only a hard reset leaves), the memory map in our words
    (banks 0-1, the language card in banks 2-5, the IIe ROM image in bank E), the switches the OO-1 implements, the
    keyboard, game port and speaker through the USB card, the Disk II in slot 6, and what isn't there (slots 1-5
    and 7, the cassette, the floating bus, the native cards). Emulated. No register entries: Apple's addresses
    aren't backplane.inc's, and this chapter isn't Apple's manual.
14. **Emulator notes.** The machine files (`backplane`, `apple1`, `apple1-65c02`, `eater`, `eater-lcd8`), the
    terminal app (its `--clock` is 1 MHz unless set) and the WebUI, running a ROM or a program, and **what
    differs**, from the inventory:
    - the 6551 stand-in isn't reset by /RESET, so the pulse that enters Apple II mode keeps its CMD: with its
      receive interrupt on, it can pull /IRQ in the mode (design.md:1095). Every other native card, and the DMA
      controller, is reset by that pulse, so they are quiet in the mode as the design wants (round 1's review);
    - `$C060` reads 0, where the design says `$FF`;
    - MODE reads back (the design has it write-only), and WAIT applies at ÷2;
    - Apple II mode's clock is an even 1.0205 MHz, with no long cycle;
    - the console's step is an instruction, not a bus cycle, and its ISR is set only while IEN is;
    - Pause/Break's NMI reaches the CPU in the WebUI only.

## The Disk II

Chapter 10's "The Disk II" section has two parts:

- **In Apple II mode** (emulated): slot 6, its switches at `$C0E0-$C0EF`, the boot PROM at `$C600`, the
  sequencer, the motor's run-on, and the image formats. Chapter 13 links here.
- **In native mode:** the ID at `$C67F` (`DISK2_ID`, `$C1`), emulated; the rest of the block reads `$FF`. The
  switches at `$C600-$C60F` and the 224-cycle write loop are a **proposed** subsection, written from
  hw/design.md, which says the emulator doesn't do them yet.

Entries: `$C600` `DISK2` (the block; in Apple II mode slot 6's PROM) and `$C67F` `DISK2_ID`. G2 doesn't touch the
emulator's Disk II code, backplane.inc's Disk II block, or hw/oodos.md's "Apple II disks". OODOS phase 6, on its
own branch, adds the native switches. Whichever branch merges second puts phase 6's new anchors on
unwritten.txt, and their pages follow after both are in. Writing entries for `$C600` and `$C67F` needs one line in
check_docs.py: `MODULES` has no name for module C yet. G2 adds `0xC: "Disk II"`.

## Open for G2: registers with no equate

The check refuses an entry whose address no equate in backplane.inc names. About 79 decoded registers have none.

- **A. Add the equates to backplane.inc (recommended).** Each card's block gets its names, as the emulator and
  hw/design.md call them (`VIDEO_ISR`, `VIDEO_LINE`, `VIDEO_L1_MAP`, `USB_MX`, `VIA_T1CL`, `CONSOLE_SWR`...). The
  check, the index and the template stay as they are, and there is still one source. The BIOS, OOLIB and drivers
  will want these names anyway. The cost is about 80 equate lines in a file the ROM includes. They are names only,
  so the ROM is unchanged, and G2 checks that the image is byte for byte the same.
- **B. A second include the checker also reads,** for example `backplane/include/registers.inc` holding the full
  map, with backplane.inc keeping what the BIOS uses. It is one more file for the checker and for agents, and the
  same name could be in both unless the check forbids it. It suits a full map kept apart from the BIOS's needs, and
  that isn't a need here.
- **C. A list in check_docs.py,** like `PLANNED`. This is a third copy of each address, which docs/README.md's
  "Indexes" argues against.
- **D. No entries:** the registers go in tables in the chapter prose. registers.json misses them, so an agent
  can't find them, and nothing holds them to the code.

The SIDs' 29 registers each, and the joysticks' bits, are tables inside their entries, not equates. The VIA
gets equates, because OOLIB will program its timers. Proposed hardware (the TL16C552A, INTSTAT, the serial card's
ID, the native Disk II switches) gets **no** equates until the emulator has it: an equate invites code to use a
register that doesn't answer.

## Checks

- Every commit: `make -C docs check` (the check and its 49 cases); `make -C docs index` after any entry.
- The equates commit: the ROM built before and after, compared byte for byte (`cmp`), and `test_check_docs` with
  a case for the new module name.
- Before each review round: the quick tier, detached, from the worktree's root (`tools/check.sh quick`), with
  `test_docs`, `test_docs_check` and `test_docs_examples` among it.
- **Facts from the code:** each entry's Access, Bits and Reset are taken from the device's `read`/`peek`/`write`/
  `reset` and checked against its test (`test_video`, `test_dma`, `test_rtc`, ...). A scratch script, not committed,
  lists every offset each device decodes against the entries, so a register can't be left out.
- **Copyright:** no SID, WDC or Apple text. The VIA's and the SIDs' registers are described in our own words, from
  the emulator's code. Apple II mode names the switches by their usual names, as identifiers, and says what each
  does in our words. Nothing under `emulator/test/host/fixtures/` is opened. No screenshots of third-party
  software.

## Commits and review rounds

| Step | Content | Review |
|---|---|---|
| 0 | this plan | the owner approves it, or changes it |
| 1 | the equates (option A), `MODULES` with Disk II, the new anchors on unwritten.txt, the index | round 0, small: the names |
| 2 | chapters 1-5 and 11: the machine (backplane, memory, boot, interrupts, DMA, the north bridge and the clock), about 60 entries | round 1 |
| 3 | chapters 6-10 and 12: the cards, about 95 entries | round 2 |
| 4 | chapters 13 and 14, the README's "Written so far", docs/README.md's "Open for G2" settled, AGENTS.md if it needs it | round 3, a read of the whole book |

**Size.** G1 was about 6,700 lines across 27 files, in three rounds. G2 is about the same: roughly 156 entries at
20-30 lines each (3,500-4,500 lines), about 1,500 lines of chapter prose, and about 80 equates. That is the same
three rounds, plus round 0.

## Questions for the owner

1. **Registers with no equate:** A, B, C or D (above)? *Recommended: A.*
2. **The new equates' names:** the card prefix and the emulator's or hw/design.md's name (`USB_MBTN`, `VIA_IFR`)?
   One entry for a two-byte register (LINE, MX, BPADDR), but separate entries for the VIA's timer bytes, whose
   high byte starts the timer? *Recommended: yes to both, reviewed in round 0.*
3. **Design and code disagree** (the list under chapter 14): the book describes the code and notes the
   difference. Should any of these be fixed in the emulator instead, before G2 documents them? The 6551's interrupt
   in Apple II mode and `$C060` look like bugs rather than choices. (Apple II mode's IRQ gating and DMA idle were on
   this list; round 1's review found the /RESET pulse that enters the mode resets the DMA controller and every
   card but the 6551.) *Recommended: document them as they are now, and put them on the emulator's backlog. G2
   doesn't change the emulator.*
4. **hw/design.md's out-of-date tags** ("Proposed" on the layers and the splash, design.md:499 and 639; "starting
   with the text layer", design.md:1451; channel 2 "USB" against the sound card's DRQ, design.md:477). Fix them in
   G2's last commit, tags and those lines only? *Recommended: yes.*
5. **Tags for what is partly emulated** (KBCFG's layout bits, the console's step, sound STATUS's BUSY):
   *Recommended:* the entry is **emulated**, with a Note saying what isn't. A whole feature that is missing is a
   **proposed** section.
6. **Examples.** The register template has no Example. Chapters can list the BIOS's source, held by the listing
   check. Should G2 also add `.COM` examples that read hardware (the clock, a joystick)? *Recommended: no. A
   program under OODOS shouldn't touch that hardware until OOLIB exists (AGENTS.md); the BIOS's own code serves.*
7. **Proposed hardware:** prose in proposed sections only, with no equates or entries until the emulator has it?
   *Recommended: yes.*
8. **Phase 6 and the Disk II:** phase 6's native-mode equates go on unwritten.txt when the branches meet, and their
   pages are a follow-up after both merge. Is that right, or should phase 6 write its own entries, as phases will
   after G3? *Recommended: a follow-up, as the brief has it, so neither branch waits on the other.*
