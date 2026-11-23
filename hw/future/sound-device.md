# OO-1 Sound Card Rev 3 — The Sound Card as a Device

Draft, Nov 21, 2026, for the owner to edit and approve. It builds nothing.

**Status: draft.** Everything here is **Proposed** unless it says **Decided**. Decided means the owner's decisions of
2026-11-21 (recorded in [the appendix](#appendix-the-owners-decisions-2026-11-21), to be copied into hw/design.md when this is approved). Register offsets,
command numbers, record layouts and sizes are proposals for the owner to change. [hw/sound-rev2.md](../sound-rev2.md)
stays the record of revision 2 and isn't edited. The layouts this draft leaves open, and function 82's
renumbering (the owner's answer of 2026-11-23), are in [the interface details](#appendix-interface-details-proposed-for-s1-2026-11-23)
at the end.

## Summary

| Area | Revision 2 (today) | Revision 3, the device | Status |
|---|---|---|---|
| What software sees | the SIDs' registers, the 6510's RAM, a mailbox, a PCM FIFO, the ES5503's port, EQ and vis registers | one device: commands in, data in by DMA, events out. Nothing inside is reachable | **Decided** |
| Who runs the chips | the host, or code the host loads onto the 6510 | the card's own logic ("the firmware" below). The 6510 runs only `.sid` code, PSID and RSID | **Decided** |
| What implements it | an RP2350 | open: a microcontroller, an FPGA (as the video card's), or both, with the 6510, the ES5503 and the SIDs possibly in one package. This spec says what the card does | **Decided** (the part is open) |
| Loading code onto the card | OODOS's `SN_LOAD`, `SN_START`; OOLIB's music driver and `.sid` stub | none at boot: the card's firmware lives in its own flash. A firmware update through OODOS is a feature | **Decided** |
| Window into card memory | RAMADDR/RAMDATA, DOCADDR/DOCDATA | none | **Decided** |
| Sharing | one part, one owner; PCM and the ES5503 exclude each other | the card shares the chips: a tune on one SID while effects play on the other SID or the ES5503 | **Decided** (sharing); the rules Proposed |
| Card memory | 64 KB 6510 RAM, 128 KB wave RAM, 128 KB FIFO | 64 KB and 128 KB as before, plus a store of 2, 4 or 8 MB, its size in a register, as the video card's | **Decided** |
| The SIDs' sound | analog, mixed after the card's digital path | through the codec's ADC into the digital path: mixed, EQ'd, in the vis page, under the master volume | **Decided** |
| Streams | fed through a 128 KB FIFO: 0.74 s of CD audio | gapless chunked streams, the card asking for the next chunk, underruns reported | **Decided** |
| Sound bank | none | SID effects and ES5503 samples loaded once, "play effect X", voices managed across both chips | **Decided** |
| Music | OOLIB's driver on the 6510, a mailbox byte at a time, 16-bit lengths | a block of notes in one transfer, 32-bit lengths, one done signal, note counts | **Decided** |
| Beep | the USB card's speaker, timed by the CPU | a sound card command; GW-BASIC's BEEP and the console's bell go there. No card, no sound, not even a beep | **Decided** |
| SID noise seed | BIOS 0.18 `$C818` `$A2`, `SN_NOISE` | gone (its own branch, ahead of this) | **Decided** |
| Old software | | may break: nothing is placed to keep it working | **Decided** |
| ID | `$A2` | `$A3` | **Decided** ([Q1](#the-drafts-questions-decided)) |
| DMA | channel 2, a standing request at DRQSEL 0 and 2 | channel 2, asking only while a transfer is open and the card has room | Proposed |
| Interrupt | IRQH bit 2: mailbox byte, ES5503 | IRQH bit 2: events waiting | Proposed |

## Goals

- **Decided.** The card presents itself as one device with features. The caller never knows what is inside it.
- **Decided.** Data goes in by DMA. Everything is played and controlled by commands. Events come back: done,
  markers, counts, underruns, requests for data.
- **Decided.** No raw mode. OODOS loads nothing onto the card at boot (the firmware lives in the card's own flash),
  and there is no host window into card memory. Updating that firmware through OODOS is a feature.
- **Decided.** The 6510 stays, hidden, running `.sid` tune code, RSID as well as PSID, with the card managing it. RSIDs
  play as today at first; fuller C64 support comes later.
- **Decided.** Songs bigger than card memory play gaplessly, fed in chunks.
- **Proposed.** Every `.sid` tune that plays today plays identically: the 61,201-tune corpus is the acceptance test.
- **Proposed.** The interface follows the video card's conventions: registers from the start of the card's block,
  the ID at `$7F`, little-endian multi-byte fields, a data port served by DMA, ISR/IEN interrupts on the card's IRQH
  bit, a size register for card memory, sizes in 24 or 32 bits. It follows the storage card where the storage card
  already fronts a controller: commands run on the card, an ERROR code, READY after start-up, a reset strobe, and
  objects that survive /RESET.
- **Proposed.** One program's view: `SN_` operations of function 82, OOLIB's `MU_`, `PB_` and `SID_` calls, and
  GW-BASIC's SOUND, PLAY and BEEP all sit on the same device commands. Bare-metal code (WOZMON, the ROM) uses the
  command records directly: they are the card's documented interface (**Decided**).
- **Proposed.** Results don't depend on the host's speed or the emulator's audio rate, as revision 2's don't.

**Out of scope:** the real card's schematic and the part that implements it, OO-Amp's port (its own repo; a step for
the owner to schedule), a MOD player (codec 2 stays reserved), Apple II mode sound, and the random-number plan that
replaces the noise seed.

## The card's logic

**Decided (the owner, 2026-11-21):** the spec says what the card does, not the part: "treat it like the Video Card
here as well, its good to know the specs, but assume the hardware can be built to support those specs." The part is
open: a microcontroller, an FPGA as the video card and OO-AP have, or both, doing the 6510, the ES5503 and the SIDs
possibly all in one package. In this spec **"the firmware"** means the card's logic, whatever implements it, and a
**firmware update** replaces it.

**What the card does (Proposed, as specs the hardware meets):**

- **The backplane:** the registers below, answered within the bus's read budget, with /WAIT where a card may pull it
  ([hw/design.md, wait states](../design.md#wait-states-and-the-read-budget)); the DATA port taking a byte a cycle (or a
  word with WIDE) for as long as a transfer lasts.
- **The 6510:** cycle-exact at the SID clock (PAL 985,248 Hz or NTSC 1,022,727 Hz) with its C64-like map, as
  revision 2's.
- **The SIDs:** two, as hw/design.md has them (6581 or 8580 behaviour), each register write reaching the chip on the
  6510's cycle.
- **The ES5503:** 32 oscillators and 128 KB of wave RAM, as revision 2 specified it.
- **The digital path at 48 kHz:** the ES5503's output, raw PCM, MP3 decoding, the tone generator, the SIDs from the
  ADC, then the EQ, the vis tap, the volumes and the mix, all at once, out through the codec.
- **The sequencers:** music, effects and the voice manager, stepped each millisecond.
- **Time:** every timed thing on the card's own clock, never the host's.

### An example build

*One way to meet the spec, not a choice: the part stays open. Part names, PSRAM bandwidth and latency are from memory,
to be checked at build time.*

- **A microcontroller build.** An RP2350B-class MCU runs the firmware straight from the card's QSPI flash
  (execute-in-place through its cache) and keeps its working state (the FIFOs, the voice and queue tables, the
  decoder's state, the output buffers) in its own on-chip SRAM, about 520 KB.
- **Where an MCU gets the store.** One QSPI PSRAM part on the RP2350's second QMI chip-select, memory-mapped through
  the same cache: 2, 4 or 8 MB parts exist, and the interface addresses up to 16 MB. The part fitted sets MEMSIZE. The
  host's DMA lands in the store through the card's DATA port.
- **Why PSRAM is fast enough for the store.** CD-quality PCM is about 176 KB/s and the host bus's DMA a few MB/s at
  most; a QSPI PSRAM moves tens of MB/s, far above both.
- **Why the 6510's RAM and the wave RAM aren't in it.** The 6510 (64 KB) and the ES5503 (128 KB wave RAM) read their
  memory at their own clock, cycle by cycle, with no room for PSRAM's latency. Each has its own fast SRAM (or an
  FPGA's block RAM or SRAM). The store feeds them: a tune is copied into the 6510's RAM at play, a sample into wave
  RAM when an effect needs it.
- **An FPGA build** (the owner's alternative, as the video card and OO-AP have): the FPGA carries the 6510, the SIDs
  and the ES5503, with the store in SRAM or PSRAM beside it, as the video card carries its 2 MB; a small MCU or a soft
  core may run the command parser, the sequencers and the MP3 decoder.

### The SIDs and the codec

- **Decided:** two SIDs on the card, driven only by the card (for the 6510's writes and the card's own voices).
- **Decided (the owner: "SIDs through the codec and ADC: yes"):** a stereo **codec with an ADC** instead of revision
  2's DAC alone: the DAC plays the digital path, and the ADC takes the two SIDs' outputs back in, so the SIDs go
  through the same mix, EQ, vis tap and master volume as everything else. So the vis page shows the SIDs too, a mono
  tune is heard in both ears without writing SID B, and the master volume no longer scales `$D418`'s nibble (which
  changed a 6581's samples). The path's delay is the same for every SID sound, so tunes stay in time with themselves.

### Flash and the update

- **Decided:** the firmware is in the card's own flash.
- **Proposed:** the flash holds two firmware images (A and B), a settings page, and the built-in sounds: the boot
  sound's samples (today 80 KB in the system flash, loaded at every boot), the console's bell, and a small default
  sound bank.
- **Decided (the owner):** "OODOS loads nothing onto the card" meant that OODOS shouldn't have to load firmware at
  every boot when it can live on the device's own storage. So the firmware lives in the card's flash, and updating it
  through OODOS is simply a feature: an update command set. `FW_BEGIN` opens a transfer of the image through DATA and `FW_COMMIT`
  ends it. The card writes the image into the slot it isn't running from, checks its CRC-32 and its header (the card
  type and a version), and only then marks it the one to boot; the next power-on or `FWRESTART` boots it. A bad image
  leaves the old one booting. A utility, `SNDFW.COM`, sends the file through function 82 (`SN_FIRMWARE`, below).
- **Proposed:** a recovery path for a card whose images are both bad is the hardware's own (a programming header), not
  software's.
- **The firmware's version** is in IDENTIFY's reply and in FWVER (below), so the BIOS, SYSINFO and the diagnostics can
  show it.

## Card memory

### What the card holds

| Memory | Size | Holds |
|---|---|---|
| The 6510's RAM | 64 KB | the running tune, as today |
| Wave RAM | 128 KB | the ES5503's samples in use (the chip's bank bit allows 128 KB, as revision 2) |
| Working memory | the card's own | DMA's landing buffer, the decoder's state, the output buffers, the voice and queue tables, the FIFOs |
| **The store** | **2, 4 or 8 MB** (**Decided**: as the video card, a size register; 8 MB the most) | loaded tunes (the files themselves), the sound bank's samples and effect tables, music blocks, and each stream's ring of chunks |

MEMSIZE says what is fitted, in 64 KB units (32, 64 or 128), as the video card's does; software reads it and never
assumes a size. Wave RAM is a cache of the store: a sample in the bank is copied into wave RAM when an effect needs it,
so the bank can be bigger than 128 KB. A tune's file stays in the store, and its image is put into the 6510's RAM at
each play, so a tune can be restarted or a song changed without the host sending it again.

### The streaming arithmetic

| Stream | Bytes a second | 128 KB (rev 2's FIFO) | 2 MB | 4 MB | 8 MB |
|---|---|---|---|---|---|
| PCM 44.1 kHz, 16-bit, stereo | 176,400 | 0.74 s | 11.9 s | 23.8 s | 47.6 s |
| PCM 48 kHz, 16-bit, stereo | 192,000 | 0.68 s | 10.9 s | 21.8 s | 43.7 s |
| PCM 22.05 kHz, 8-bit, mono | 22,050 | 5.9 s | 95 s | 3.2 min | 6.3 min |
| MP3 128 kbit/s | 16,000 | 8.2 s | 2.2 min | 4.4 min | 8.7 min |
| MP3 320 kbit/s | 40,000 | 3.3 s | 52 s | 1.7 min | 3.5 min |

(The store isn't all the stream's: the bank and the tunes share it. The table is the stream alone.)

On the bus a chunk goes in at a byte a cycle by demand DMA: 176,400 bytes a second is about 2.5% of the 7.16 MHz
bus. Under OODOS a chunk goes in pieces of 4 KB with the CPU free between them, as today, so that no millisecond tick
is lost ([chapter 5](../../docs/oo1-technical-reference/05-dma.md#under-oodos)): 43 pieces a second for CD audio, as
revision 2's measured stream already does. Even 2 MB holds 12 s of CD audio, sixteen times revision 2's FIFO, so a
program that reads its file in 32 KB chunks has seconds in hand at any size.

## The host interface

### The registers

The card keeps module A and its block, `$C500–$C57F`. **Proposed**, laid out from the block's start as the video
card's are; nothing of revisions 1 and 2 is kept (old software may break: **Decided**). Every offset not named reads 0
and ignores writes.

| Offset | Address | Name | Access | What it is |
|---|---|---|---|---|
| `$00` | `$C500` | CTRL | R/W | bit 0 DRQ: DATA asks for DMA while a transfer is open and there is room. Bit 6 FWRESTART, a strobe: the firmware starts again (READY clear until it is up). Bit 7 RESET, a strobe: see [Reset](#boot-reset-and-apple-ii-mode). The rest read 0 |
| `$01` | `$C501` | STATUS | R | bit 0 READY: the firmware is up. Bit 1 CMDRDY: the command FIFO has room for a whole record (256 bytes). Bit 2 EVT: an event record is waiting. Bit 3 XFER: a transfer is open. Bit 4 DATA: DATA can take a byte now. Bit 5 EVLOST: events were lost since the last LOSTCLR command. Bit 6 ERR: the last record was refused before it ran (ERROR says why). Bit 7: the card is interrupting (ISR AND IEN) |
| `$02` | `$C502` | ERROR | R | why the last record was refused at the door: 0 none, or a code from [Errors](#errors) |
| `$03` | `$C503` | CMD | W | a command record's bytes, in order. A record runs once its last byte is in |
| `$04` | `$C504` | CMDFREE | R | the command FIFO's room in bytes, 255 meaning 255 or more |
| `$05` | `$C505` | EVT | R | the event FIFO's next byte, which a read takes. With none waiting it reads 0 (kind 0, "none") |
| `$06` | `$C506` | EVTLEN | R | the bytes waiting in the event FIFO, 255 meaning 255 or more |
| `$07` | `$C507` | DATA | W | the open transfer's bytes. DMA channel 2 serves it. A byte with no transfer open, or no room, is dropped |
| `$08–$0A` | `$C508` | XLEFT | R | the bytes the open transfer still wants, 24 bits, low byte first; reading `$08` latches all three |
| `$0B` | `$C50B` | XTAG | R | the tag of the command that opened the transfer |
| `$0C` | `$C50C` | IEN | R/W | the interrupts on, the same bits as ISR |
| `$0D` | `$C50D` | ISR | R; write 1 to clear | bit 0 EVT, a level: set while an event waits (writing it does nothing). Bit 1 XDONE: a transfer took its last byte. Bit 2 CMDROOM: the command FIFO had been full and has room again. Bit 3 READY: the firmware came up. Bits 1–3 are set whether IEN has them or not, and cleared by writing 1 |
| `$0E` | `$C50E` | MEMSIZE | R | the store, in 64 KB units: 32, 64 or 128 |
| `$0F` | `$C50F` | FEATURES | R | bit 0 two SIDs, bit 1 the 6510 (tunes), bit 2 the ES5503, bit 3 raw PCM, bit 4 MP3, bit 5 EQ, bit 6 the vis page, bit 7 more in IDENTIFY |
| `$10–$11` | `$C510` | FWVER | R | the firmware's version, major then minor |
| `$7F` | `$C57F` | ID | R | `$A3` ([Q1](#the-drafts-questions-decided)) |

- **Why EVT is a level** when the video card's bits are edges: a handler drains the FIFO and an event can arrive
  between its last read and its write to ISR. A level can't be lost that way; it is the same rule a UART's receive
  flag keeps.
- **Reads are answered at once:** STATUS, EVTLEN and EVT's next byte are always ready, so a polling loop or a handler
  draining events never waits on the card.

### Records

**Command records** (host to card, through CMD):

| Byte | What |
|---|---|
| +0 | the command |
| +1 | n, the parameter bytes after +2 (0–253) |
| +2 | a tag, the host's own, given back in the reply |
| +3… | n bytes of parameters, little-endian |

**Event records** (card to host, through EVT):

| Byte | What |
|---|---|
| +0 | the kind: 0 none, 1 REPLY, `$10` up the events below |
| +1 | n, the bytes after +1 |
| +2… | for a REPLY: the tag, a status (0, or an [error](#errors)), then the command's results. For an event: its object (a stream, a voice, a music slot), then its payload |

- **Every command gets exactly one REPLY,** in the order the commands were given. The firmware keeps room in the
  event FIFO for the replies of the commands it has taken, so a reply is never lost; a command waits in the command
  FIFO until there is room for its reply.
- **Results come in the reply,** up to 250 bytes (the vis page, 98, and the SIDs' registers, 50, fit), so DATA is a
  way in only and there is no reading of card memory.
- **Asynchronous events** are coalesced where a repeat says nothing new (one NEED outstanding a stream, one
  COUNT a threshold), and counts in the status replies are exact, so an event lost to a full FIFO (EVLOST) is found
  by asking.
- **The FIFOs (Proposed):** 1 KB for commands, 2 KB for events.

### Transfers and DMA

A command that takes data (a tune, a sample, an effect table, a music block, a stream chunk, a firmware image)
names a length, and its REPLY says whether the card took it. If it did, the transfer is open: XFER is set, XLEFT is
the length, and DATA takes exactly that many bytes, by the CPU or by DMA. When the last arrives ISR's XDONE is set,
and once the card has checked what came (a `.sid`'s header, a block's layout) an event `XFERDONE` carries the result.
One transfer is open at a time; a second command that wants one is refused with `E_XFER`. `XFERABORT` closes one
early, dropping what came.

- **DMA (Proposed):** channel 2, its CARD `$0A` from reset, as today, in demand mode, memory to card. With CTRL's DRQ
  set the card pulls /DREQ only while a transfer is open and the landing buffer has room, so the controller's demand
  transfer stops by itself when the card is full and nothing asks between transfers. Revision 2's standing request
  (always asking at DRQSEL 0 and 2, so /DREQ was almost never high) is gone.
- **Words (Proposed):** DATA takes a word grant (channel 2's CARD with WIDE, `$1A`) as two bytes in order, as the
  video card's port does, halving a load's cycles. Nothing sets WIDE at first.
- **The landing buffer** takes a byte a cycle for as long as a transfer lasts (a spec the hardware meets). The card
  may still let go of /DREQ for a moment, which demand mode allows, so software never assumes a transfer's time.

### Events and the interrupt

The card pulls /IRQ while ISR AND IEN is nonzero, which reads 0 in IRQH's bit 2
([chapter 4](../../docs/oo1-technical-reference/04-interrupts.md#finding-the-source-irql-and-irqh)), as today. Under
OODOS the handler reads EVT until EVTLEN is 0, hands each REPLY to the function 82 call waiting for it, and turns each
event into the program's event area ([below](#what-oodoss-function-82-becomes)). Draining costs about 10 cycles a
byte (estimated from a read and a store); an event is 4–12 bytes.

### Errors

ERROR (at the door: a record malformed) and each REPLY's status (when it ran) use one list. **Proposed:**

| Code | Name | Meaning |
|---|---|---|
| 0 | | done |
| 1 | `E_UNKNOWN` | not a command the firmware knows |
| 2 | `E_PARAM` | a parameter out of range, or n wrong for the command |
| 3 | `E_SLOT` | no such object, or the slot is empty |
| 4 | `E_MEMORY` | not enough room in the store |
| 5 | `E_BUSY` | what it needs is in use: a chip a tune holds, every voice taken by higher priorities |
| 6 | `E_DATA` | the data isn't what it should be: not a `.sid`, a block that doesn't parse, no MP3 sync |
| 7 | `E_XFER` | a transfer is open already, or none is open |
| 8 | `E_STATE` | not in the state the command needs (resume of what isn't paused) |
| 9 | `E_NOTREADY` | the firmware isn't up |
| 10 | `E_JAM` | the 6510 met a JAM opcode; the tune is stopped |
| 11 | `E_FIRMWARE` | an update's image is bad (its CRC, its header) |

## The command set

Numbers are grouped by feature in the high nibble. **All Proposed.** Objects are named by the host: a kind and a
slot, so the host can send a load and the play after it without waiting for a reply.

### The device (`$0x`)

| Cmd | Name | Parameters | Reply's results |
|---|---|---|---|
| `$00` | NOP | none | none: a fence (every command before it has run) |
| `$01` | IDENTIFY | none | the firmware's version and build, MEMSIZE, FEATURES, the store free, the slots of each kind, the SIDs' models if the chips say (see [Q13](#the-drafts-questions-decided)) |
| `$02` | STOPALL | none | none. Every sound stops: tunes, music, effects, streams (each flushed), the beep |
| `$03` | FREEALL | none | none. STOPALL, then every object freed: the store empty |
| `$04` | EVENTS | +3 a mask of the event kinds wanted | none. The rest are not sent (their counts still kept) |
| `$05` | LOSTCLR | none | the events lost since the last, 2 bytes; EVLOST cleared |
| `$06` | TIME | none | the card's millisecond count, 4 bytes, the one events are stamped with |
| `$07` | SELFTEST | +3 what: bit 0 the store, bit 1 wave RAM, bit 2 the SIDs, bit 3 the codec's loop | a result bit each, for the BIOS's diagnostics |
| `$08` | CHECKSUM | +3 a kind, +4 a slot | the CRC-32 of what the card holds there, so a test sees that DMA delivered |
| `$0D` | FW_BEGIN | +3–6 the image's length | none; a transfer opens |
| `$0E` | FW_COMMIT | none | none; `E_FIRMWARE` if the image is bad |
| `$0F` | XFERABORT | none | the bytes that had come |

### Tunes (`$2x`)

| Cmd | Name | Parameters | Reply's results |
|---|---|---|---|
| `$20` | TUNE_LOAD | +3 a slot (0–3), +4–6 the file's length | none; a transfer opens for the `.sid` file, whole. XFERDONE then carries its header's songs, start song, clock, chips, and PSID or RSID |
| `$21` | TUNE_PLAY | +3 the slot, +4 the song (0: the file's start song), +5 flags (bit 0 PAL, bit 1 NTSC, neither: the file's), +6–9 a length in ms (0: until stopped), +10–11 a fade at the end in ms | none. The tune's chips are taken ([sharing](#how-the-firmware-shares-the-chips)); `E_BUSY` if music or a stream-locked effect holds them |
| `$22` | TUNE_STOP | none | none. The tune's chips silenced and given back |
| `$23` | TUNE_PAUSE | none | none. The 6510 held where it is, its chips at volume 0 (as `SN_HOLD` did) |
| `$24` | TUNE_RESUME | none | none |
| `$25` | TUNE_STATUS | none | the state (stopped, starting, playing, paused, stalled, jammed, ended), the slot, the song, ms played (paused time out), play calls, the 6510's cycles last frame |
| `$26` | TUNE_INFO | +3 a slot | the header: songs, start song, clock, chips and their addresses, and the name, author and released fields, 32 bytes each, as the file has them |
| `$27` | TUNE_FREE | +3 a slot | none |

- **The player is the firmware's.** It does what `sid_file.h`'s stub does today, inside the card: the free page, the
  vectors, the port, CIA 1's timer, the frame interrupt, the imitated KERNAL paths, the song in A, the mono tune's
  BOTH, the second SID's address. At revision 3's first step it is that stub, unchanged, so the corpus plays
  identically. Later firmware may imitate more of a C64 for RSIDs (the VIC-II's raster interrupt, CIA 2, NMI
  samples), each such step with a new corpus baseline.
- **Events:** `TUNE_STARTED` (init returned and the first play call came), `TUNE_STALL` (no play call for 2 s when
  one was due), `TUNE_JAM`, `TUNE_ENDED` (its length ran out, after the fade).

### The sound bank (`$3x`)

| Cmd | Name | Parameters | Reply's results |
|---|---|---|---|
| `$30` | SAMPLE_LOAD | +3 a slot (0–255), +4–6 the length, +7 flags (bit 0 8-bit signed, else unsigned as the ES5503 wants; `$00` bytes made `$01` unless bit 1 keeps them as stop markers), +8–9 the rate it was recorded at, +10–12 a loop start, +13–15 a loop end (0: none) | none; a transfer opens |
| `$31` | EFFECT_DEF | +3 an effect (0–255), +4 its kind (0 a SID effect, 1 a sample), +5… its definition (below) | none |
| `$32` | EFFECT_LOAD | +3 the first effect, +4–6 the length | none; a transfer opens for a table of definitions, many at once |
| `$33` | PLAY | +3 the effect, +4 the volume (0–255), +5 the pan (0 left, 128 centre, 255 right), +6 the priority (0–3), +7–8 a pitch offset in cents (signed), +9 the chip: 0 any, 1 a SID, 2 the ES5503 | a voice handle (1–255) |
| `$34` | VOICE_STOP | +3 a voice handle, or 0 for every effect voice | none |
| `$35` | BANK_FREE | +3 what: 0 every effect, 1 every sample, 2 both | none |

- **A SID effect** is a short table of steps: each step a length in ms, the waveform's bits, a frequency, a pulse
  width, AD and SR, a gate, and optional linear sweeps of the frequency and the pulse width over the step. It is our
  own format (OOLIB's [question 5](../oolib.md#open-questions) answered so; [Q9](#the-drafts-questions-decided)).
- **A sample effect** names a sample slot, its pitch (a rate or a note), its loop, an envelope (attack, hold, release
  in ms) and its oscillator mode.
- **Voices** are given by the voice manager ([below](#how-the-firmware-shares-the-chips)). Events: `VOICE_DONE`
  (handle, effect), `VOICE_STOLEN` (handle, by a higher priority).

### Music (`$4x`)

| Cmd | Name | Parameters | Reply's results |
|---|---|---|---|
| `$40` | MUSIC_LOAD | +3 a slot (0–7), +4–6 the length | none; a transfer opens for a block |
| `$41` | MUSIC_PLAY | +3 the slot, +4 flags: bit 0 queue it after what plays (PLAY's MB), bit 1 free the slot when done | none |
| `$42` | MUSIC_STOP | none | none: every queued block dropped, the voices released |
| `$43` | MUSIC_PAUSE, `$44` MUSIC_RESUME | none | none |
| `$45` | MUSIC_STATUS | none | the state, the notes waiting on each voice (8 × 2 bytes), the blocks queued, ms played |
| `$46` | MUSIC_THRESH | +3 n (0 off) | none. A `MUSIC_COUNT` event when the notes waiting fall below n (GW-BASIC's ON PLAY(n)) |

**A note block (Proposed):**

| Part | What |
|---|---|
| a header | a version, the voices used (up to 8 logical voices), a flag for the tone generator |
| instruments | per voice: the sound (a SID waveform, AD, SR, pulse width; or a sample slot; or the tone generator), its volume and pan |
| entries | per entry: the voice; a note (a number 0–127) or a frequency (Hz × 16, 24 bits) or a rest; a **length in µs, 32 bits**; a gate time in µs, 32 bits; or an instrument change, a volume change, a marker (0–254) |

- **One transfer, no splitting:** a whole PLAY statement, or a whole tune of a game's, is one block, however many
  notes; the store holds it.
- **32-bit lengths in µs** reach 71 minutes, so SOUND's longest (65,535 PC ticks, about 3,600 s) is one entry, and
  the 2 min 44 s limit recorded for 7f goes. A µs is fine enough for a PC tick (54,925 µs) and for any tempo.
- **Events:** `MUSIC_MARK` (voice, marker), `MUSIC_COUNT` (notes waiting), `MUSIC_DONE` (one, when the last entry of
  the last queued block has finished, not one a note).

### Streams (`$5x`)

| Cmd | Name | Parameters | Reply's results |
|---|---|---|---|
| `$50` | STREAM_OPEN | +3 a stream (0–1), +4 the codec (0 raw PCM, 1 MP3), +5–6 the rate, +7 the format (bit 0 stereo, bit 1 16-bit), +8–10 the chunk size the host will send (default 32 KB), +11–12 the prebuffer in ms (default 500) | none |
| `$51` | STREAM_DATA | +3 the stream, +4–6 the chunk's length | none; a transfer opens, into the stream's ring. `E_MEMORY` if the ring hasn't room |
| `$52` | STREAM_END | +3 the stream | none: no more data; what is in plays out |
| `$53` | STREAM_NEXT | +3 the stream, +4 the next stream | none: when the first ends, the next starts with no gap |
| `$54` | STREAM_PAUSE, `$55` STREAM_RESUME | +3 the stream | none |
| `$56` | STREAM_FLUSH | +3 the stream | none: a seek. The ring and the decoder emptied; it buffers again |
| `$57` | STREAM_STATUS | +3 the stream | the state (as revision 2's STATE: idle, buffering, playing, paused, underrun, ended, an error), the bytes buffered (4), the ms buffered (4), frames played (4), kbit/s (2), underruns (2), the rate and format |
| `$58` | STREAM_CLOSE | +3 the stream | none: stopped and its ring freed |

[Gapless streaming](#gapless-streaming) has the rules. Two streams let a player queue the next track. A stream, the
ES5503 and the SIDs all play at once (Proposed: revision 2's mutual exclusion, STATE `$82`, is gone, the hardware
built to the spec).

### The beep (`$6x`)

| Cmd | Name | Parameters | Reply's results |
|---|---|---|---|
| `$60` | BEEP | +3–4 the pitch in Hz (20–20,000), +5–6 the length in ms, +7 the volume (0: the default) | none; `BEEP_DONE` when it ends |
| `$61` | BELL | none | none: the console's bell, a sound in the card's flash |

The beep is the firmware's **tone generator** on the digital path, a band-limited square wave, so it takes no SID
voice or oscillator, plays whatever else is playing, and is exact at any pitch. A second BEEP replaces the first.

### Mixing, EQ and the vis page (`$7x`)

| Cmd | Name | Parameters | Reply's results |
|---|---|---|---|
| `$70` | VOLUME | +3 left, +4 right (0–255, 255 unity) | none: the master volumes (`SN_VOLUME`'s) |
| `$71` | MIX | +3 a source (0 tunes, 1 music, 2 effects, 3 streams, 4 the beep), +4 its level (255 unity) | none |
| `$72` | EQ | +3 a band (0 the preamp, 1–10), +4 a gain in signed half-dB (±24) | none |
| `$73` | EQ_ALL | +3–13 the preamp's gain and the ten bands' | none; each checked before any is set |
| `$74` | EQ_ON | +3 0 off, 1 on | none |
| `$75` | VIS | none | the vis page, 98 bytes, revision 2's layout, taken at this moment |
| `$76` | SIDREGS | none | both SIDs' 25 registers as last written, 50 bytes, `SID_VOICES`'s layout |
| `$77` | LEVELS | none | ten bytes: eight bands and the VU pair, `PB_LEVELS`'s layout, for every source |

The EQ's bands, the vis page's layout and the volume's meaning are revision 2's. With the SIDs in the digital path
the EQ, the vis page and the volumes cover them too.

## How the firmware shares the chips

**Decided:** a tune can play while effects run on the other SID or the ES5503. **Proposed, the rules:**

- **The resources:** SID A (three voices, a filter, a volume), SID B (the same), the ES5503 (32 oscillators, in
  pairs where a mode needs two), and the digital path (streams, the tone generator). A chip's filter and volume are
  shared by its voices, so **a tune takes whole chips:** a one-SID tune takes SID A, a two-SID tune both. The 6510 is
  the tune's alone; nothing else runs on it.
- **The 6510's map** follows the tune: with one SID, `$D400–$D7FF` is SID A and its mirrors, as on a C64, so a tune
  that writes a mirror reaches SID A; with two, the second at the address the header names. The ES5503 isn't in the
  6510's map (only the BIOS's boot player used it, and the boot sound moves into the firmware).
- **A one-SID tune** plays on SID A alone, mixed into both ears through the ADC (the decided path), so SID B is always
  free for effects and music while it plays. Today's BOTH, writing SID B as well, isn't needed.
- **Music** takes the voices its block names, given at MUSIC_PLAY: SID voices from the chips no tune holds, sample
  voices from the ES5503, the tone generator. If they aren't free it is refused with `E_BUSY`; music is never stolen.
- **Effects** take a free voice of the chip asked for (any: a SID first for a SID effect, the ES5503 for a sample),
  else the lowest-priority, oldest effect voice at or below their priority, which gets `VOICE_STOLEN`. Never a
  tune's chip, never music's voice.
- **The ES5503 is the card's:** its oscillators are voices for effects
  and music, and its interrupt is the firmware's, never the host's.
- **The register shadows** of both SIDs are kept for every write, by the 6510 or by the firmware, so SIDREGS answers
  at any time and nothing reads the chips' write-only registers.

## Gapless streaming

**Decided:** a song that doesn't fit card memory is fed in chunks without skips, the card asking for the next chunk
while enough is buffered, underruns reported. **Proposed, the rules:**

- **The ring.** A stream's chunks go into a ring in the store, as big as the store allows after the bank and the
  tunes (at least 1 MB on an 8 MB card). The decoder reads the ring as one byte stream: chunk boundaries mean nothing
  to it, so a frame split across two chunks plays (as revision 2's FIFO already does across writes).
- **Who asks.** The card. It sends `NEED` (the stream, the bytes it has room for, the ms buffered) whenever it has room
  for a whole chunk of the size STREAM_OPEN named, and no NEED is outstanding. A STREAM_DATA answers it; the next NEED
  comes once there is room again. So the host never polls, and never sends what the card can't hold.
- **When it plays.** After the prebuffer (500 ms of audio by default, by the stream's byte rate; for MP3 by the first
  frames' bitrate), or at STREAM_END, whichever comes first.
- **The low-water mark.** Below 1 s buffered, NEED carries an urgent flag, which OODOS passes on, so a player can put
  its disk reads ahead of its drawing.
- **Underrun.** The ring empty with no STREAM_END: silence, and an `UNDERRUN` event (the stream, the frame position,
  the count). Data again plays from the next frame due, with no new prebuffer, as revision 2. An `UNDERRUN_END` event
  says when sound came back, and the count is in STREAM_STATUS.
- **Between songs.** STREAM_NEXT queues a second stream behind the first: when the first's data ends, the second's
  first frame follows the first's last with no gap and no silence, at its own rate (the mix follows the rate). An
  MP3 with an encoder's gapless header has its leading delay and trailing padding taken off ([Q11](#the-drafts-questions-decided)). A
  `TRACK` event says the second has started, and the first is then closed; the player opens the third and queues it.
- **Arithmetic for the rule.** At CD quality with 32 KB chunks the card asks about five times a second; at 128 kbit/s
  MP3, once every two seconds. With 8 MB a whole CD-quality minute can be buffered, so a host that falls behind by
  seconds loses nothing.

## Boot, reset and Apple II mode

- **Power-on (Proposed):** the firmware starts from the card's flash, tests the store briefly, and sets READY and
  ISR's bit 3, within 100 ms (a spec). The BIOS waits up to 250 ms for READY.
- **The boot sound (Proposed):** the BIOS's splash sends one command, `BOOTSOUND` (`$09`, in the device group), at the
  vertical blank the splash's frame 0 begins on, as it now writes CTRL then. The sound (today's ES5503 player and its
  80 KB of samples) is in the card's flash and plays in the firmware, so the BIOS loads nothing and the 80 KB leave the
  system flash. With no READY in time, the splash runs silent.
- **The ROM's other 6510 programs (Proposed):** the splash has two players today, a SID tune for revision 1 and an
  ES5503 player for revision 2 (`splash/bootsnd.s`, `splash/docsnd.s`), with the ES5503's 80 KB wave image in flash
  blocks 24–28 (bios.cfg's WAVE). On revision 3 both players and the image leave the ROM: BOOTSOUND replaces them, and
  blocks 24–28 are free. The diagnostics' own 6510 program (`backplane/oohw/card6510.s`, embedded in `diag22`) and
  stage 10's S2–S8 (the 6510's RAM, DMA into it, the program, the mailbox, the 6510's sum, the SIDs through OSC3 and
  ENV3, wave RAM) test parts the host can no longer reach; they are replaced by IDENTIFY, SELFTEST (the firmware
  tests the store, wave RAM, the SIDs and the codec's loop and reports a bit each), CHECKSUM after a DMA transfer
  (channel 2 and DATA end to end), the event path and its interrupt, and a short stream's frames (S9's rate).
  S10's interactive tones (`SIDTONE`, `DOCTONE`, host writes to the SIDs and the ES5503) become a music block on a SID
  voice and a sample effect, played by command. The diagnostics' QUIET copy (`diag.inc`) sends STOPALL as QUIET
  does.
- **The USB card's speaker stays** (USB_SPKR): Apple II mode's speaker is it, and the diagnostics' U5 tests it. Only
  `SN_BEEP` and the bell stop using it.
- **/RESET (Proposed, as the storage card):** abandons an open transfer, empties both FIFOs, stops every sound, sets
  CTRL, IEN and ISR to 0 and the volumes to 255 and the EQ off. The firmware keeps running (READY stays) and the store
  keeps what is loaded, as the storage card keeps its mounts. OODOS's cold boot sends FREEALL, so a new session
  starts empty.
- **CTRL's RESET** does what /RESET does to the sound and the FIFOs, from software. **FWRESTART** starts the firmware
  over, READY clear until it is up, the store emptied. A watchdog in the firmware does the same if it stops answering.
- **Apple II mode (Proposed):** as today: the card is idle and its block hidden. Entering the mode pulses /RESET,
  which stops everything; Apple software never addresses the card. Nothing here plans an Apple sound card.
- **The north bridge's I²C** isn't used by the card: nothing of it is on the bus beside the DS3231.

## Bare metal

**Decided (the owner):** the card's command records are its documented interface, and the BIOS gets entry points for
BEEP and STOP-ALL. **Proposed:**

- **The Technical Reference's chapter 7** documents the registers, the records, every command, event and error, as
  chapter 10 documents the storage card's commands: a program with no OODOS (WOZMON, a ROM program, the diagnostics)
  writes records to CMD and reads EVT as OODOS does.
- **Two BIOS functions,** added to the `$C818` list as the jump-table rule allows (their numbers given in step 4):
  BEEP (A/X the pitch, Y the length in 10 ms units, or a block) and STOPALL. Each waits for READY and for its REPLY,
  with a time-out, and returns carry set with no card. QUIET calls STOPALL; the monitor's bell calls BEEP.
- **Under OODOS** a program still goes through function 82 alone; the bare-metal interface is for code that runs
  without OODOS.

## What OODOS's function 82 becomes

**Decided (the owner, under AGENTS.md's interface rule, before 1.0):** the raw operations are removed: `SN_LOAD` (3),
`SN_START` (4), `SN_SEND` (6), `SN_WAVE` (14), `SN_DOCHOOK` (15), `SN_HOLD` (17), `SN_GO` (18). `SN_NOISE` (20) is
removed by the noise branch ahead of this. **Proposed:** each removed number stays reserved and is refused, A = X =
`$FF`, as a retired BIOS function is, and is never given to another operation.

**Decided, as a consequence of "no raw mode":** a claim no longer lets a program write the part's registers. Today
a claimed part is the program's to drive itself, its registers written directly
([the Technical Reference, chapter 7, "Under OODOS"](../../docs/oo1-technical-reference/07-sound.md#under-oodos)): the
SIDs while the 6510 is stopped, the ES5503's oscillators, the EQ and the vis page. On revision 3 there are no such
registers, and a claim is only an exclusivity among programs and RSXs. AGENTS.md's rule and 06-program-formats.md's
"don't take over the sound card's 6510" become "drive the card through function 82 only".

**Proposed: also gone, as consequences** (decided: [Q6](#the-drafts-questions-decided)): the 6510's
mailbox events (`EK_SOUND` with a mailbox byte), the event area's mailbox stamp (`EV_SMBOX`, `EV_SMMS`, `EV_SMCNT`,
+108–114) and `ES_NOQUEUE` (+115), all of which only `SN_SEND`'s mailbox fed. Their bytes stay reserved, 0.

**Proposed: kept,** with their numbers, blocks and results, now done by device commands:

| Op | Name | On the device |
|---|---|---|
| 0 | `SN_INFO` | A = the revision (3), X = FEATURES (was CODECS); the block's +3 the parts free |
| 1, 2 | `SN_CLAIM`, `SN_RELEASE` | the parts become `SC_TUNE` `$01` (was `SC_6510`, the same bit: the tune player), `SC_STREAM` `$02` as before, `SC_MUSIC` `$08` new; `SC_DOC` `$04` retired, refused ([Q4](#the-drafts-questions-decided)) |
| 5 | `SN_STOP` | TUNE_STOP: the tune stopped, its chips silenced ([Q5](#the-drafts-questions-decided)) |
| 7–13 | `SN_STREAM`, `SN_BUFFER`, `SN_PAUSE`, `SN_RESUME`, `SN_SEEK`, `SN_END`, `SN_STATUS` | stream 0: STREAM_OPEN, STREAM_DATA, …, STREAM_STATUS. The two buffers taken in turn stay; OODOS sends a buffer when the card's NEED says there is room, so `EV_SBUFS` means what it did. `SN_STATUS`'s room in 256-byte units reaches 16 MB |
| 16 | `SN_BEEP` | BEEP, on the sound card. Refused with no revision 3 card: **Decided**, "no card, no sound, not even a beep" |
| 19 | `SN_VOLUME` | VOLUME |

**Proposed: new operations** (numbers 21 on; OOLIB's interface version raised once, with them):

| Op | Name | The block, after +0 | What it does |
|---|---|---|---|
| 21 | `SN_TUNE` | +1–3 a physical address, +4–6 the length, +7 a slot | loads a `.sid` file into the card (TUNE_LOAD), by channel 2 |
| 22 | `SN_PLAY` | +1 a slot, +2 the song, +3 flags, +4–7 a length in ms, +8–9 a fade in ms | TUNE_PLAY [`SC_TUNE`] |
| 23 | `SN_TPAUSE`, 24 `SN_TRESUME` | none | TUNE_PAUSE, TUNE_RESUME [`SC_TUNE`] |
| 25 | `SN_TSTATUS` | 16 bytes to fill | TUNE_STATUS |
| 26 | `SN_TINFO` | +1 a slot, then 104 bytes to fill | TUNE_INFO |
| 27 | `SN_SAMPLE` | +1 a slot, +2–4 a physical address, +5–7 the length, +8… SAMPLE_LOAD's flags, rate and loop | loads a sample into the bank |
| 28 | `SN_EFFECTS` | +1 the first effect, +2–4 a physical address, +5–7 the length | loads a table of effect definitions |
| 29 | `SN_EFFECT` | +1 the effect, +2 volume, +3 pan, +4 priority, +5–6 cents, +7 chip | PLAY; A = the voice handle |
| 30 | `SN_ESTOP` | +1 a voice handle, 0 every effect voice | VOICE_STOP |
| 31 | `SN_MUSIC` | +1 a slot, +2–4 a physical address, +5–7 the length, +8 MUSIC_PLAY's flags | loads a note block and plays or queues it [`SC_MUSIC`] |
| 32 | `SN_MCTL` | +1 0 stop, 1 pause, 2 resume, 3 threshold with +2 n | MUSIC_STOP, …, MUSIC_THRESH [`SC_MUSIC`] |
| 33 | `SN_MSTATUS` | 24 bytes to fill | MUSIC_STATUS |
| 34 | `SN_NEXT` | +1 the codec, +2–3 the rate, +4 the format | opens stream 1 and queues it behind stream 0 (STREAM_NEXT); `SN_BUFFER` then feeds the one that wants data [`SC_STREAM`] |
| 35 | `SN_EQ` | +1 a band, +2 a gain; or +1 `$FF` and +2–12 all; or +1 `$FE` and +2 on/off | EQ, EQ_ALL, EQ_ON |
| 36 | `SN_VIS` | 98 bytes to fill | VIS |
| 37 | `SN_SIDREGS` | 50 bytes to fill | SIDREGS |
| 38 | `SN_LEVELS` | 10 bytes to fill | LEVELS |
| 39 | `SN_MIX` | +1 a source, +2 a level | MIX |
| 40 | `SN_IDENTIFY` | 32 bytes to fill | IDENTIFY |
| 41 | `SN_FIRMWARE` | +1–3 a physical address, +4–6 the length, +7 0 send a piece, 1 begin (+8–11 the image's length), 2 commit | FW_BEGIN, the transfer in pieces, FW_COMMIT: the update (**Decided**), for `SNDFW.COM`; no claim, refused while any part is claimed |

- **Typed operations, not a pass-through** ([Q3](#the-drafts-questions-decided)): OODOS checks every address (as function
  51's DMA segment), owns channel 2, keeps the claims and the warm boot's clean-up, and can follow a firmware change
  without the program seeing it.
- **Each call waits for its REPLY** with a time-out of 100 ms (`SN_BUSY` past it); a load waits for XFERDONE too.
- **The event area's sound part (Proposed):** +96–107 the stream's, as now; +108–115 reserved (above); +116–119 the
  tune's state and ms played; +120–123 music's state and notes waiting; +124–127 reserved. Asynchronous events reach
  the queue as `EK_SOUND` events whose code is the event's kind and whose value is its object: `TUNE_ENDED`,
  `TUNE_STALL`, `MUSIC_MARK` (the value the marker, the voice in the code's low bits), `MUSIC_COUNT`, `MUSIC_DONE`,
  `VOICE_DONE`, `UNDERRUN`, `TRACK`, `BEEP_DONE`. This is the event channel that replaces the mailbox's: `EK_SOUND`
  keeps its kind number, and its code now says which event it is.
- **The stall rule's new source.** `PB_STATUS` judges a SID tune today by the mailbox's stamp (a byte from the stub
  in the last 2 s, 10 s before the first). The card knows better: TUNE_STATUS gives the ms since the last play call
  and the state (starting, playing, stalled), and `TUNE_STALL` and `TUNE_STARTED` come as events, with the same 2 s
  and 10 s as the firmware's rule. OODOS writes the tune's state and ms played into +116–119 once a frame while
  `SC_TUNE` is claimed, as it writes the stream's now.
- **DMA channel 2 stays the sound card's,** its CARD `$0A` as from reset, so the warm boot's DMAWARM (and `video.s`'s
  assert that module A is SD + 3) and DEAFEN's care for channel 2 while a buffer moves stay as they are; what changes is
  the register the channel serves (DATA, not RAMDATA, PCMDATA or DOCDATA) and that DRQSEL is gone.
- **The warm boot** sends STOPALL and FREEALL, VOLUME 255/255 and EQ_ON 0 with every gain 0, sets IEN to OODOS's,
  aborts channel 2 and closes a transfer: nothing a program left plays on, and nothing it loaded stays.
- **An RSX** may claim parts as a program does; the card's sharing lets an RSX's music play beside a program's
  effects.
- **OODOS drives revision 3 only** ([Q12](#the-drafts-questions-decided)): on revision 1 or 2 `SN_INFO` says the
  revision and every other operation is refused, as OODOS has no console on a revision 1 video card.

## OOLIB's sound layer

**Decided:** the music driver (`oodos/lib/mudrv/`, and with it `mu.s`'s loading of it) and `sid.s`'s player stub are
retired; `PB_*` goes onto the device's status. **Proposed:**

| Module | Today | Revision 3 |
|---|---|---|
| `mu.s`, `munote.s`, `mutone.s` | the 6510 driver, a mailbox byte at a time | the `MU_` calls kept ([Q7](#the-drafts-questions-decided)): `MU_NOTE`, `MU_TONE`, `MU_INSTR`, `MU_MARK` build a note block in the program's memory; `MU_FLUSH` (new) sends it as one `SN_MUSIC`, queued; a note sent alone is a one-entry block. `MU_INIT` claims `SC_MUSIC` and loads nothing. `MU_COUNT` and `MU_EVENT` read the card's counts and events. `MU_BLOCK` (new) sends a whole block a program built |
| `sid.s` | `SID_PARSE`, `SID_PLAN` (the stub), `SID_PLAY`, `SID_STOP` | `SID_PARSE` kept (the header, for display); `SID_PLAN` retired; `SID_PLAY` is `SN_TUNE` and `SN_PLAY`, RSIDs too; `SID_STOP` is `SN_STOP` |
| `sidv.s`, `sidnote.s`, `sidenv.s` | reads of the SIDs' registers | `SID_VOICES` from `SN_SIDREGS` (the same 50 bytes); `SID_NOTE` unchanged (arithmetic); `SID_ENV` kept, or replaced by `SN_LEVELS`, which the card measures |
| `pb.s`, `pbstat.s`, `pbvol.s`, `pblev.s` | per source: the 6510 by the mailbox stamp, the stream by `SN_STATUS`, the ES5503 by its RUN | `PB_SID` from `SN_TSTATUS`, `PB_STREAM` from `SN_STATUS`, `PB_MUSIC` (new, 3) from `SN_MSTATUS`; `PB_DOC` refused (`PE_REFUSED`). `PB_LEVELS` from `SN_LEVELS`, the same form for every source and no longer an estimate for a SID |
| `pbeq.s`, `pbvis.s` | the card's EQ and vis registers | `SN_EQ`, `SN_VIS`; the calls and their errors as now |
| `pbcard.s`, `pb.inc` | the ID read straight from `$C57F`; `pb.inc`'s own copy of the card's register addresses | `SN_INFO`; `pb.inc`'s register equates removed |

**No library module touches the card.** Today `sidv.s`, `pbeq.s`, `pbvis.s`, `pbcard.s`, `pbstat.s` (CTRL's RUN,
DOCCTL's RUN, RATE) and `pblev.s` (CTRL's clock bit) read or write the card's registers directly, with no claim,
through `pb.inc`'s private equates. On revision 3 every one of them goes through function 82, so OODOS is the card's
only driver. OO-Amp (its own repo) calls function 82 and `PB_EQ`/`PB_VIS` and drives claimed parts' registers, so it
needs the same change; that is its own step.

## GW-BASIC's SOUND, PLAY and BEEP

**Decided:** SOUND and PLAY are recorded in 7f as "on the current card, to be reworked on the sound device"; this is
that rework (step 6). **Decided (the owner): GW-BASIC's voice is a tone generator, "as long as it doesn't add
complexity".** What keeps it simple:

- **The tone generator exists anyway:** it is the beep's (BEEP, BELL), so GW-BASIC adds no part to the card. The one
  addition is that a note block's voice may name it, which the block's instrument field already allows.
- **One voice, one queue, as the PC has:** SOUND and PLAY share one speaker and one background queue on the PC. Here
  both go to the one tone-generator voice, so GW-BASIC needs no voice allocation, no instruments, no SID envelope
  settings, and no clamping: every frequency SOUND allows plays at its pitch (today's SID voice tops out at 3,848 Hz on
  PAL), and PLAY's highest octave plays too. PLAY's MN, ML and MS are a gate time within the note's length, which the
  block has already.
- **Less code than 7f's:** no driver image in block P7, no `MU_INSTR` set-up, no event counting for PLAY(n) (below).
  SID instruments for PLAY are not planned; if ever wanted, they are an OO-1 extension.
- **SOUND f, d** is one entry: the frequency × 16, and d PC ticks as d × 54,925 µs in 32 bits. SOUND 0 stops the
  queue.
- **PLAY** parses its string (as now) into one note block and sends it with `SN_MUSIC`: MF waits for `MUSIC_DONE`;
  MB queues it. The block holds any number of notes, so a long string is one transfer.
- **PLAY(n)** is MUSIC_STATUS's notes waiting, exact, not a count of events handed over. **ON PLAY(n)** sets
  MUSIC_THRESH and traps on `MUSIC_COUNT`. The 32-note background limit stays as the manual's behaviour, enforced by
  GW-BASIC, not by a queue's size.
- **BEEP** and the console's ^G: `SN_BEEP` (800 Hz for a quarter second, the PC's beep) and BELL for BDOS's BEL
  (today `crt.s` does nothing for BEL).
- **The 7f limits that go:** the 2 min 44 s SOUND cap (C9), the 1 ms shortest SOUND (C7) becomes 1 µs resolution, and
  the dependence on handing every event to `MU_EVENT` for PLAY(n).

## The emulator's model

**Proposed:**

- **`sound_card.{h,cpp}` gains revision 3** (`rev=3` on the machine file's sound line), keeping revisions 1 and 2 for
  their tests and the record. The card's parts stay as they are: the 6510 (`cpu6502`), the SIDs (`sid.h`), the
  ES5503 (`es5503.h`), the DSP (`sound_dsp.h`), minimp3.
- **The firmware's model** is a class of its own (`sound_fw.{h,cpp}`), with no knowledge of the bus: the command
  parser, the store (sized by MEMSIZE), the object slots, the voice manager, the tune player (`sid_file.h`'s planner
  and stub, moved behind it unchanged), the music sequencer, the effect players, the stream rings, the tone
  generator, the event FIFO. The card's revision 3 front end is the registers, the FIFOs and the DATA port. Whether
  this class is also the real card's code (or its reference model) is [Q2](#the-drafts-questions-decided).
- **Time:** commands run at the card's time of the record's last byte, plus a fixed modelled latency (20 µs,
  Proposed), and every event is stamped on the card's clock, so results are the same at any audio rate and however the
  emulator is ticked, as revision 2's are. The firmware's millisecond steps fall on exact SID-clock counts.
- **DMA:** DRQ while a transfer is open and the landing buffer has room; the landing buffer drains at a modelled rate
  (Proposed: never the limit, so a load runs at a byte a cycle).
- **The WebUI's Play .sid** (through the session, `sid_file.h` today) hands the file to the firmware model's tune
  player directly, outside the bus, and the tune bar reads TUNE_STATUS. The audio is the same as the corpus's. The
  WebUI's other audio (the stream, the mix) is unchanged.
  - Today `sid_file.cpp`'s `start_sid` and `stop_sid` write CTRL, RAMADDR, RAMDATA and both SIDs straight into the
    card (not over the bus), and the page's tune bar takes "ended" from CTRL's RUN going off
    (`bindings.cpp`'s `sound_running`, `sid-bar.ts`). On revision 3 the WASM API's `loadSid`, `sidSong` and `stopSid`
    call the firmware model, and `soundRunning` becomes "a tune is playing" from TUNE_STATUS. `soundCpu` and
    `soundVoice` (the 6510's registers, the SIDs' voices for the page) read the firmware model's 6510 and register
    shadows, as a debugger may; nothing on the OO-1's bus can.
  - The debugger's sound-card pane shows the device's registers, its FIFOs and the firmware model's state (objects,
    voices, streams) instead of `$C500`'s old map.
  - `apps/sidcheck` (the headless `.sid` checker) follows `sid_file.h`.
  - The USB card's speaker model stays; the session's mix keeps the speaker's audio for Apple II mode.
- **The web tests:** `e2e/sound.spec.ts` writes SID registers at WOZMON's prompt, which revision 3 doesn't allow. It
  becomes a test of the bare-metal interface ([below](#bare-metal)): a BEEP record and a short music block typed at
  WOZMON into CMD (`$C503`), the event read back from EVT (`$C505`).
- **`backplane.machine`** moves to `rev=3` with the BIOS that drives it (step 4), so main never has a BIOS and a card
  that don't match.

## Tests and acceptance

**Proposed acceptance:**

- [ ] **The corpus plays identically:** `test_sid_corpus` gains a device mode that plays each tune by TUNE_LOAD and
  TUNE_PLAY; for every one of the 61,201 tunes the 6510's SID writes (address, value, SID-clock cycle) and the card's
  audio hash are the same as today's register path's, against the commit before step 2, and the known failures are
  the same set (148 left out).
- [ ] Every existing test that doesn't drive the removed interfaces passes unchanged. Tests of the removed interfaces
  (`test_mudrv`, `test_sound_driver`, the raw parts of `test_oodos_sound`, `test_oodos_mu`, `test_oodos_playsid`,
  `test_oodos_pb`'s mailbox-stamp and `ES_NOQUEUE` cases, `test_bios`'s QUIET and splash cases, `test_bios_diag`'s
  stage 10 and its `romdiag` goldens, `test_oodos_sysinfo`'s goldens, `test_session`'s Play .sid cases, the page's
  vitest and Playwright sound tests) are retired or rewritten on the device, each listed in the branch's notes.
  `test_usb`'s speaker cases and `test_iie`'s bell stay as they are.
- [ ] **A stream bigger than card memory:** from an OODOS program, a 44.1 kHz 16-bit stereo raw PCM file larger than
  the store (60 s, 10.6 MB, on an 8 MB card), fed in chunks on NEED, plays with zero underruns, byte for byte at the
  card's tap.
- [ ] **Gapless:** two streams queued by STREAM_NEXT give, at the tap, exactly the frames of one stream of both files'
  data (raw PCM), and an MP3 pair with gapless headers gives the trimmed frames, sample for sample.
- [ ] **Underrun:** a feeder held back makes UNDERRUN events whose count and positions match STREAM_STATUS, and the
  sound comes back at the next frame due.
- [ ] **Sharing:** a one-SID tune with effects on SID B and samples on the ES5503: the tune's SID A writes are the
  same as the tune alone's.
- [ ] **Music:** a block of 10,000 notes in one transfer plays with one MUSIC_DONE; PLAY(n) and ON PLAY(n) are exact;
  a 3,600 s SOUND is one entry.
- [ ] **Beep** plays over a tune and a stream, at its pitch within 0.1%.
- [ ] **Time-independence:** each feature's output is the same at audio rate 0 and 48,000, however ticked.
- [ ] Native and WASM builds pass; ASan and UBSan clean on the release candidate (the test tiers' rule).

**New suites (Proposed):** `test_sound_device` (registers, records, FIFOs, errors, reset), `test_sound_fw` (the
firmware class alone), `test_sound_stream3` (rings, NEED, gapless, underrun), `test_sound_bank`, `test_sound_music`,
each registered with its labels and with map rules in hw/testing.md's map, as new files need.

## The impact list

| Where | What changes |
|---|---|
| Hardware | a new card revision (3), built to this spec: the card's logic (its part open), the store (2, 4 or 8 MB), the codec with an ADC, the flash. hw/design.md's "Sound" gains "Sound card revision 3"; hw/sound-rev2.md stays the record of revision 2 |
| `backplane/bios/backplane.inc` | revision 3's equates (`SOUND_ID_V3`, CTRL…ID, the commands, the event kinds, the errors); revision 1 and 2 equates kept for the record |
| Emulator | `sound_card` revision 3, `sound_fw`, `sid_file`'s host path, the session's Play .sid, the WASM bindings (`soundRunning`, `soundCpu`, `soundVoice`), the page's tune bar, catalog and debugger pane, `sidcheck`, the machine file, the tests above, `e2e/sound.spec.ts` |
| OODOS (more) | `irq.s` (the sound interrupt's handler, DEAFEN, CLKOFF), `input.s` (the warm boot's quiet, the interrupt routing), `cold.s`'s boot status (unchanged while the ID's high nibble is `$A`), the test programs `l3test.s` and `l3strm.s` (they hard-code DOCCTL and the SIDs' `$D418`) |
| BIOS | QUIET and `diag.inc`'s copy (STOPALL on revision 3), the splash's two players and the 80 KB wave image in blocks 24–28 (BOOTSOUND instead), the diagnostics' stage 10 (S1–S9 on IDENTIFY, SELFTEST, CHECKSUM, the event path; S10's tones by command) and `card6510.s` retired, `hwname.s`'s known IDs, the monitor's bell; USB_SPKR and U5 unchanged; its version raised |
| Gone already (the noise branch) | BIOS 0.18's `$C818` `$A2`, `SN_NOISE`, OOLIB interface 15's addition, GW-BASIC 7f's RANDOMIZE SOUND and its start-up noise read: removed before this work, not part of it |
| OODOS | `sys/sound.s` rewritten on the device; function 82 as above; the event area's sound part; the warm boot; `crt.s`'s BEL; the version raised and OOLIB's interface version with it |
| OOLIB | `mudrv/` retired, `mu*.s`, `sid*.s`, `pb*.s` as above |
| Utilities | PLAYSID (RSIDs now, the header's text, the tune's status), SYSINFO's SOUND section (IDENTIFY: firmware, memory, features), a new `SNDFW.COM` for firmware updates (decided) |
| Books | the Technical Reference's chapter 7 (revision 3), chapters 4 and 5's notes on module A and channel 2, chapter 15's stage 10; the Programmer's Guide's chapter 8 "Sound" and function 82; the User's Guide's PLAYSID; `docs/index/registers.json` and `functions.json`; examples in `docs/examples/` that use sound |
| GW-BASIC | SOUND, PLAY, BEEP, ON PLAY, PLAY(n) (step 6); 7g's books wait for it |
| Site | the emulator's deploy with revision 3; catalog cards whose programs use the removed calls rebuilt or marked |
| OO-Amp | its own repo: a separate step for the owner to schedule |

## The steps

**Decided order;** what each holds is Proposed. One branch per step, each gated as hw/testing.md says, main gated
after each merge.

1. **The spec** (this draft), for the owner's answers. Before it is final, measure today's driver and the ES5503 in
   the emulator (the decision's "measures today's driver and DOC"): the 6510 driver's share of the 6510 (the books'
   19% with six notes), and the ES5503's and the stream's cost to the host under OODOS, as the baseline the device's
   numbers are compared with.
2. **The emulator's card model:** revision 3, `sound_fw`, the new suites, the corpus in device mode identical.
3. **OODOS and OOLIB's device layer:** function 82 as above, the old operations removed and refused, OOLIB's modules,
   the warm boot, the event area, `SN_FIRMWARE`. Versions raised.
4. **The BIOS:** QUIET, the boot sound, the diagnostics, the bell, the BEEP and STOPALL entry points;
   `backplane.machine` to revision 3.
5. **PLAYSID, SYSINFO, SNDFW and the books.**
6. **GW-BASIC's sound rework** (SOUND, PLAY, BEEP, ON PLAY).
7. **The site.**

## Decided on this draft

The owner's answers of 2026-11-21 to the draft's first questions, in the owner's words where given:

- **No compatibility placement.** "I dont care about old code, there really isnt any, it can break." The registers
  are laid out from the block's start; nothing is placed to keep old software quiet.
- **Card memory:** 2, 4 or 8 MB, "like the video card (a size register), 8 MB max."
- **The part:** not pinned; "possibly an FPGA, like the video card and OO-AP, doing the 6510, the ES5503 and the SIDs
  all in one package. The spec says what the card does, not the part."
- **Hardware:** "treat it like the Video Card here as well, its good to know the specs, but assume the hardware can
  be built to support those specs." So a stream, the ES5503 and the SIDs play at once, and revision 2's mutual
  exclusion goes.
- **The SIDs through the codec and ADC:** "yes." So a one-SID tune plays on SID A alone, heard in both ears.
- **Firmware:** in the card's own flash. "OODOS loads nothing onto the card" only meant OODOS shouldn't have to load
  firmware at every boot when it can live on the device's own storage; updates through OODOS (`SN_FIRMWARE`,
  `SNDFW.COM`) are simply a feature.
- **GW-BASIC's voice:** "a tone generator, as long as it doesn't add complexity" ([what keeps it
  simple](#gw-basics-sound-play-and-beep)).
- **No card:** "no sound, not even a beep."
- **Bare metal:** the card's command records documented as its interface; BIOS entry points for BEEP and STOP-ALL
  ([Bare metal](#bare-metal)).
- **RSID:** "played as today at first, fuller C64 support later."

### The draft's questions, decided

The owner, 2026-11-21: "recommendations as listed for the sound spec." Each question's recommendation is the
decision. The numbers are kept so the text above can cite them.

- **Q1.** **The ID.** **Decided: `$A3`, "revision 3".** The owner's 2026-11-07 rule ("`$A3` and up keep what `$48–$63`
   mean") lapses with "it can break"; the high nibble stays `$A`, the module, which the BIOS and OODOS use to find
   the card.
- **Q2.** **One code base for the card's logic and the emulator.** **Decided:** `sound_fw` written as portable C++ with
   no emulator dependencies (as `es5503` is): if the card's part runs C++, it is the card's code; if it is an FPGA, it
   is the design's reference model, and the FPGA is tested against it. Either way its arithmetic (the EQ's precision,
   the resampling, the ADC path's level) is the card's, bit for bit.
- **Q3.** **Typed operations or a pass-through.** **Decided: typed** `SN_` operations only: OODOS checks addresses, owns
   channel 2, keeps the claims and the warm boot, and the device's command set may change behind them. A pass-through
   would make the card's commands OODOS's stable interface.
- **Q4.** **Claims.** **Decided:** keep `SN_CLAIM` for exclusivity between a program and an RSX: `SC_TUNE` (bit 0,
   `SC_6510`'s, renamed), `SC_STREAM`, `SC_MUSIC` (new, bit 3); `SC_DOC` retired. Effects and the beep need no claim.
   Renaming bit 0's meaning is an interface change for the owner to name.
- **Q5.** **`SN_STOP`.** **Decided:** keep 5 as "stop the tune" (TUNE_STOP), the nearest to stopping the 6510 and
   silencing the SIDs, rather than retiring it.
- **Q6.** **The mailbox's leftovers.** **Decided:** retire `EK_SOUND`'s mailbox meaning, `EV_SMBOX`, `EV_SMMS`,
   `EV_SMCNT` and `ES_NOQUEUE` with `SN_SEND` (their bytes reserved, 0), and give the tune and music status new bytes
   (+116–123), not the old ones.
- **Q7.** **The `MU_` calls.** **Decided:** keep them, rebuilt on note blocks (with `MU_FLUSH` and `MU_BLOCK` added),
   so programs and tests that use them carry over; the driver itself is retired as decided. The alternative is to
   retire `MU_` with `mu.s` and have programs build blocks themselves.
- **Q8.** **The note block's units.** **Decided:** lengths and gate times in µs, 32 bits; frequencies as Hz × 16 in 24
   bits, or a note number; up to 8 logical voices a block.
- **Q9.** **The SID effect format.** **Decided:** our own step table (above), with a Linux converter later; tracker
   formats wait for codec 2 (MOD), as OOLIB's question 5 said.
- **Q10.** **A tune's length.** **Decided:** TUNE_PLAY takes an optional length and fade from the program (a `.sid` has
    no end); the card reports ms played and play calls. A song-length list is the program's, not the card's.
- **Q11.** **Gapless MP3 trimming.** **Decided: yes,** the encoder delay and padding taken off when the file's gapless
    header gives them; none otherwise.
- **Q12.** **Older cards under OODOS.** **Decided:** OODOS and the BIOS drive revision 3 only (as with video revision 2);
    revisions 1 and 2 stay in the emulator for their tests and the record.
- **Q13.** **The SIDs' model.** **Decided:** the card sets the chips' model (6581 or 8580) per tune, as the `.sid`
    header asks, and IDENTIFY says it can; with an FPGA's SIDs that is a setting, with drop-in chips a configuration
    sequence where the chip takes one.
- **Q14.** **Loading from a file.** **Decided:** add, later, an operation that takes an open FCB and a length and sends
    the records to the card through OODOS's own buffer, so a program needs no memory for a 64 KB tune or a large
    sample set. Not in step 3.
- **Q15.** **What /RESET keeps.** **Decided:** the store's objects, as the storage card keeps its mounts; OODOS's cold
    boot sends FREEALL. Power-on and FWRESTART empty it.

### The interface details, decided (2026-11-23)

- The owner: "recommendations as listed, with the clean table". [The interface details](#appendix-interface-details-proposed-for-s1-2026-11-23)
  are approved as written, with function 82 laid out afresh as one table of 34 operations (`SN_OPS` 34), removed
  numbers reused and none kept refused; FWRESULT at `$11`; `SC_MUSIC` `$04`; a rest entry of 5 bytes.
- Added: `SN_REFUSED`, A = `$FD` and X = the card's error, when the card refuses a command.
- "Notes waiting" excludes the sounding note; branch C checks PLAY(n) against the IBM manual.

## Open questions

1. **The interface versions.** **Recommended:** set OODOS's version and OOLIB's interface number (15 at OODOS 0.14)
   when step 3 lands, after the noise and serial branches have merged and taken theirs, raised once for all of
   function 82's changes here.

## What this draft couldn't verify

- The OOLIB interface version this will raise: 15 at OODOS 0.14; the noise branch changes it first, so the number is
  set when step 3 lands.
- The 19% load of the 6510 driver is the emulator's count (step 1 measures the rest in the emulator).
- The software figures marked as estimates (the handler's cost a byte, a reply's time-out of 100 ms, the BIOS's
  250 ms wait for READY) are this draft's, to be set when OODOS and the BIOS are written.

## Appendix: the owner's decisions (2026-11-21) ##

The decisions as recorded in the discussion that produced this draft, kept here so the draft stands alone.


Owner: "recommendations as listed, make sure all this is written down, use as many parallel agents as you can".

### The sound card as a device (to be specified: a new sound card revision, the owner's spec)
- The card presents itself as one device with features. Data goes in by DMA; everything is played and controlled
  by commands (messages / API calls); events come back (done, markers, counts, underruns). The caller never knows
  what is inside the card. No raw mode; OODOS loads nothing onto the card.
- A controller on the card (part to be chosen, e.g. an RP2350 as on the storage card) with its firmware in the
  card's own flash. It owns the 6510, both SIDs, the ES5503 (DOC) and its wave RAM, the FIFO and codec, the EQ, the
  vis page. The 6510 stays inside, hidden, running .sid tune code (PSID and RSID, the controller managing it).
- The controller shares the chips: a tune can play while effects run on the other SID or the DOC.
- No host window into the sound card's memory (the video card keeps its window while the CPU draws; a future GPU
  video card might drop it). The interface follows the video card's conventions: registers, DMA requests, card
  memory, an event/interrupt path back.
- Features: tunes (load .sid, song n, pause, stop, progress); a sound bank (SID effects and DOC samples loaded once,
  "play effect X", voices managed across both chips); music as a block of notes in one transfer with 32-bit lengths
  (no splitting, one done signal, note counts for PLAY(n)/ON PLAY); streams (PCM, MP3) gapless: a song that doesn't
  fit card memory is fed in chunks without skips, the card asking for the next chunk while enough is buffered,
  underruns reported; volume, balance, EQ, vis as status.
- Card memory: revision 2 has 64 KB 6510 RAM, 128 KB wave RAM, a 128 KB FIFO (CD-quality PCM ~0.74 s, 128 kbit/s
  MP3 ~8 s). Recommended for the new revision: much more, e.g. 2 MB SRAM (~12 s CD PCM); the size a spec question.
- The beep is done by the sound card (a device command), not the USB card. GW-BASIC's BEEP and the console bell go
  there.
- Impacts: hardware (new revision; hw/sound-rev2.md stays the record of rev 2), the emulator's card model and the
  WebUI's Play .sid/audio, the BIOS (sound diagnostics, splash card check), OODOS function 82 (raw SN_LOAD,
  SN_START, SN_SEND, SN_HOLD/GO, SN_WAVE, SN_DOCHOOK removed: the owner's decision under AGENTS.md's rule, before
  1.0), OOLIB (music driver mu.s and sid.s's stub retired, PB_* onto device status), PLAYSID, SYSINFO, the books,
  the tests (the SID corpus must still play identically), GW-BASIC's SOUND/PLAY/BEEP. OO-Amp (its own repo) is a
  separate step for the owner to schedule.
- Steps: (1) the spec draft for the owner (asks first, measures today's driver and DOC); (2) the emulator's card
  model; (3) OODOS/OOLIB's device layer, the old calls removed; (4) the BIOS; (5) PLAYSID and the books;
  (6) GW-BASIC's sound rework; (7) the site.

### The noise seed: removed
- The SID voice-3 noise seed goes: BIOS 0.18's $C818 function $A2 (retired to a refusal per the jump-table rule),
  OODOS's SN_NOISE (function 82 operation 20, refused), OOLIB interface 15's addition; versions raised
  (OODOS 0.16, BIOS 0.19 expected). Its own small branch, after feat/serial-rx merges.
- GW-BASIC's RANDOMIZE SOUND (7f U7) and its start-up noise read are taken out of 7f before it merges; RND keeps
  the manual's generator (RANDOMIZE TIMER is the manual's clock seed).
- The random number source gets its own plan: a pseudo-random generator, or a crypto chip on the north bridge's
  I2C bus (beside the DS3231); the choice the owner's.

### GW-BASIC 7f
- Review round 1 (13 findings + C9, all new): fix C1-C4 (ON TIMER), C5 (negative SOUND frequency), C7 (the
  shortest SOUND plays the driver's 1 ms), C8 (duplicate compare), T1-T5. Skip C6 (code to be replaced). C9: the
  ~2 min 44 s SOUND cap recorded as a temporary limit. SOUND/PLAY recorded as "on the current card, to be reworked
  on the sound device". Remove U7. Then gate; merge on the owner's word.
- 7g waits (its books would describe SOUND/PLAY that change). OODOS 1.0 waits on the sound campaign and ON COM.

### Later
- ON COM (GW-BASIC) on the serial receive buffer (feat/serial-rx, built, review pending).
- The owner wants to circle back to how COM and the interrupts work after this plan.

### The owner on the spec draft (2026-11-21)
- No compatibility placement: "I dont care about old code, there really isnt any, it can break." Drop "registers
  placed where old software can't stumble into them / old code meets a quiet card".
- Card memory: 2/4/8 MB like the video card (a size register), 8 MB max.
- The controller: not pinned on an RP2350B; possibly an FPGA, like the video card and OO-AP, doing the 6510, the
  ES5503 and the SIDs all in one package. The spec says what the card does, not the part.
- Hardware: "treat it like the Video Card here as well, its good to know the specs, but assume the hardware can be
  built to support those specs."
- SIDs through the codec and ADC: yes.
- Firmware updates via OODOS (the owner's exception to "OODOS loads nothing onto the card", for updates only).
- GW-BASIC's voice: a tone generator, as long as it doesn't add complexity.
- No card, no sound, not even a beep.
- Bare metal: document the card's command records as its interface; BIOS entry points for BEEP and STOP-ALL.
- RSID: played as today at first, fuller C64 support later.
- Clarified (owner, 2026-11-21): "OODOS loads nothing onto the card" meant only that OODOS shouldn't have to load
  firmware at every boot when it can live on the device's own storage. Updates through OODOS are fine; it is not a
  rule against OODOS ever sending the card code.

### The noise seed and the random number source (owner, 2026-11-21)
- "why all the baggage around removing the noise seed, no one is using this yet but me ... a feature that never saw
  the light of day." Remove it plainly: the code, the tests, the docs and records; no withdrawn slots, no
  compatibility notes; the numbers ($A2, operation 20) simply free again. Only the BIOS version moves (the ROM
  image changes); OODOS's and OOLIB's numbers are left alone (no one used interface 15's addition).
- GW-BASIC: the manual's RANDOMIZE TIMER only.
- The random number comes from hardware via the north bridge, like the RTC: an ATECC608 crypto chip on the
  north bridge's I2C; or built into the north bridge itself as an LFSR from logic chips (two 74HC164 shift
  registers and a 74HC86 XOR: 15 bits, taps x^15 + x^14 + 1, period 32,767; a nonzero seed; clocked at least 8
  times between reads). Hardware treated as the video card's: state the spec, assume it can be built.
- The earlier software-generator plan (rng/plan.md) is superseded.
- Owner, 2026-11-21: "Lets just plan on using the ATECC608 and skip the LFSR idea." The random source is the
  ATECC608 on the north bridge's I2C, like the RTC. No LFSR.
- Owner, 2026-11-21: "recommendations as listed for the sound spec" — the 15 open questions decided as
  recommended (ID $A3; one C++ code base; typed operations; SC_TUNE/SC_STREAM/SC_MUSIC, SC_DOC retired; SN_STOP
  kept; mailbox leftovers retired; MU_ kept on note blocks + MU_FLUSH/MU_BLOCK; us/32-bit, Hz x16/24-bit, 8 voices;
  own SID step table; length/fade by the program; gapless MP3; rev 3 only; SID model per tune; load-from-file
  later; /RESET keeps, cold boot clears). The owner asked where an MCU (e.g. RP2350B, 520 KB SRAM) gets the
  2/4/8 MB store: answer to be written into the spec (QSPI PSRAM on the RP2350's second QMI chip-select).

## Appendix: interface details (proposed for S1, 2026-11-23) ##

**Decided (the owner, 2026-11-23): approved as written,** "recommendations as listed, with the clean table"
([the build plan](sound-device-build.md), unit S1, and [its decisions](sound-device-build.md#decisions)), with
FWRESULT at `$11`, `SC_MUSIC` at `$04`, a rest entry of 5 bytes, and `SN_REFUSED` added. The draft leaves these layouts open; this fills them, starting from the
build plan's section 5.1. Function 82's operations are laid out afresh as one clean table, as the owner's revised
answer 4+5 says, which replaces the numbering from 21 and the reserved, refused numbers in
[What OODOS's function 82 becomes](#what-oodoss-function-82-becomes). Every multi-byte field is little-endian.
"ms" is the card's own millisecond count (TIME's); "µs" lengths are 32 bits.

### Event kinds and their records

Every event record is the kind (+0), n (+1: the bytes after +1), the object (+2), the card's ms stamp when it
happened (+3-6, 4 bytes), then the payload (+7 on). A REPLY has no object or stamp: +2 the tag, +3 the status (0 or
an [error](#errors)), +4 on the results. The kind's high nibble is the group, as the commands' is; group 1 is the
transfers and the flash, which no one command group owns.

| Kind | Name | Object (+2) | Payload (+7 on) | n |
|---|---|---|---|---|
| `$00` | none | | (EVT reads 0 with nothing waiting) | |
| `$01` | REPLY | the tag | +3 status, +4 on the command's results | 2 + results |
| `$10` | XFERDONE | the tag of the command that opened the transfer | +7 that command, +8 the status (0, `E_DATA`, `E_MEMORY`), +9 on its results (below) | 7 + results |
| `$11` | FWRESULT | 0 | +7 the status (0: written and marked to boot; `E_FIRMWARE`), +8-9 the image's version (major, minor) | 8 |
| `$20` | TUNE_STARTED | the tune slot | +7-8 the song (1-based) | 7 |
| `$21` | TUNE_STALL | the tune slot | +7-10 ms since the last play call | 9 |
| `$22` | TUNE_JAM | the tune slot | +7-8 the 6510's address of the JAM opcode | 7 |
| `$23` | TUNE_ENDED | the tune slot | +7-10 ms played (its length and fade ran out) | 9 |
| `$30` | VOICE_DONE | the voice handle | +7 the effect | 6 |
| `$31` | VOICE_STOLEN | the voice handle | +7 the effect, +8 the priority that took it | 7 |
| `$40` | MUSIC_MARK | the logical voice (0-7) | +7 the marker (0-254), +8 the music slot | 7 |
| `$41` | MUSIC_COUNT | 0 | +7-8 the notes waiting (all voices), +9 the threshold | 8 |
| `$42` | MUSIC_DONE | the last block's slot | +7-10 ms played since MUSIC_PLAY began the queue | 9 |
| `$50` | NEED | the stream | +7-10 the room in bytes, +11-14 ms buffered, +15 bit 0 urgent (under 1 s buffered) | 14 |
| `$51` | UNDERRUN | the stream | +7-10 the frame position, +11-12 the underruns so far | 11 |
| `$52` | UNDERRUN_END | the stream | +7-10 the frame position sound came back at, +11-14 the frames missed | 13 |
| `$53` | TRACK | the stream now playing | +7 the stream that ended (now closed) | 6 |
| `$54` | STREAM_ENDED | the stream | +7-10 the frames played | 9 |
| `$60` | BEEP_DONE | 0 | none (BEEP's or BELL's sound ended, or a second BEEP replaced it) | 5 |

- **XFERDONE's results** by the command that opened the transfer: TUNE_LOAD +9-10 the songs, +11-12 the start song,
  +13 the clock (0 unknown, 1 PAL, 2 NTSC, 3 either), +14 the SIDs (1 or 2), +15 bit 0 RSID, bit 1 the start song
  CIA-timed; SAMPLE_LOAD none; EFFECT_LOAD +9 the definitions taken; MUSIC_LOAD +9-12 the entries; STREAM_DATA and
  FW_BEGIN none.
- **EVENTS' mask** (+3) is by group: bit n wants kinds `$n0-$nF` (bit 1 the transfers and the flash, 2 tunes, 3
  voices, 4 music, 5 streams, 6 the beep). REPLY is always sent. After power-on, FWRESTART, CTRL's RESET and
  /RESET the mask is `$FF`.
- **BOOTSOUND** is `$09` in the device group: no parameters, no results; the boot sound plays from the card's flash
  and its end sends nothing.

### The replies' results

| Command | Results (after the reply's +3 status), by offset from the first |
|---|---|
| IDENTIFY (32) | +0 the card type (`$A3`), +1-2 the firmware version (major, minor), +3-4 its build, +5 MEMSIZE, +6 FEATURES, +7 FEATURES2 (bit 0 the tone generator, bit 1 the SIDs' model set per tune, bit 2 the firmware update), +8-11 the store free, +12-15 the store's size (bytes), +16 tune slots (4), +17 music slots (8), +18 streams (2), +19 sample slots less 1 (255), +20 effects less 1 (255), +21 effect voices (6 SID, 32 oscillators: +21 the SID voices, +22 the oscillators), +23 SID A's model, +24 SID B's (0 not known, 1 6581, 2 8580), +25 the image running (0 A, 1 B), +26-31 reserved, 0 |
| LOSTCLR | +0-1 the events lost since the last |
| TIME | +0-3 ms |
| SELFTEST | +0 the parts tested (the request's bits that the card has), +1 those that failed |
| CHECKSUM | +0-3 the CRC-32, +4-7 the bytes it covers |
| XFERABORT | +0-2 the bytes that had come |
| TUNE_STATUS (16) | +0 the state (0 stopped, 1 starting, 2 playing, 3 paused, 4 stalled, 5 jammed, 6 ended), +1 the slot, +2-3 the song, +4-7 ms played (paused time out), +8-11 play calls, +12-13 the 6510's cycles in the last frame, +14-15 ms since the last play call (held at 65,535) |
| TUNE_INFO (104) | +0-1 the songs, +2-3 the start song, +4 the clock (as XFERDONE's), +5 bit 0 RSID, bit 1 two SIDs, bit 2 written for the 8580, bit 3 the start song CIA-timed, +6-7 the second SID's address (0 none), +8-39 the name, +40-71 the author, +72-103 the released field, 32 bytes each as the file has them |
| PLAY | +0 the voice handle (1-255) |
| MUSIC_STATUS (24) | +0 the state (0 stopped, 1 playing, 2 paused), +1 the blocks queued, +2-17 the notes waiting on each logical voice (8 × 2), +18-21 ms played, +22-23 the notes waiting on all (held at 65,535) |
| STREAM_STATUS (21) | +0 the state (revision 2's: 0 idle, 1 buffering, 2 playing, 3 paused, 4 underrun, 5 ended, `$80` up an error), +1 the codec, +2-3 the rate, +4 the format, +5-8 the bytes buffered, +9-12 ms buffered, +13-16 frames played, +17-18 kbit/s, +19-20 underruns |
| VIS (98), SIDREGS (50), LEVELS (10) | as the command set says |

- **CHECKSUM's kind** (+3) is the command that loaded the object (`$20` a tune, `$30` a sample, `$32` the effect
  table last loaded, `$40` a music block, `$51` a stream's last chunk, `$0D` the firmware image last received), +4 its
  slot (the stream, for `$51`; ignored for `$32` and `$0D`). The CRC is of the bytes as they came through DATA,
  before any conversion (a sample's sign, its `$00` bytes).
- **The CRC-32** everywhere (CHECKSUM, the firmware image) is the common one: polynomial `$04C11DB7` reflected
  (`$EDB88320`), starting at `$FFFFFFFF`, the result inverted.
- **"Notes waiting"** are a block's note, frequency and rest entries not yet begun; the one sounding isn't counted
  (decided; branch C checks GW-BASIC's PLAY(n) against the IBM manual and brings any difference back).
  MUSIC_COUNT comes when the total falls from the threshold n to n − 1.

### The note block

A block is a header, an instrument table and its entries, in one transfer (MUSIC_LOAD).

| Part | Bytes | What |
|---|---|---|
| Header | 8 | +0 the version (1), +1 the logical voices used (bit n voice n), +2-3 reserved (0), +4-7 the entries that follow the instruments |
| Instruments | 8 each | one for each voice in +1's mask, in voice order: the voice's sound until an instrument entry changes it |
| Entries | 2-12 each | in time order per voice; the voices' entries may be interleaved, and each voice keeps its own time |

**An instrument** (8 bytes): +0 the source (0 a SID voice, 1 an ES5503 sample, 2 the tone generator); for a SID voice
+1 the control register's waveform, ring and sync bits (the gate is the entries'), +2 AD, +3 SR, +4-5 the pulse width
(12 bits); for a sample +1 the sample slot, +2 the note it plays at its recorded rate, +3 0 one-shot or 1 looped at
the sample's loop points; +6 the volume (0-255, 255 unity: the sample's or the tone generator's; for a SID voice its
chip's volume while music holds the chip), +7 the pan (0 left, 128 centre, 255 right; a SID voice takes its chip's
place in the mix, so it is ignored). Unused bytes are 0.

**An entry:** +0 the kind in the high nibble, the logical voice (0-7) in the low.

| Kind | Bytes | After +0 |
|---|---|---|
| 0 note | 10 | +1 the note (0-127, 69 the A at 440 Hz), +2-5 the length in µs, +6-9 the gate in µs (at most the length; equal is legato) |
| 1 frequency | 12 | +1-3 Hz × 16 (24 bits), +4-7 the length in µs, +8-11 the gate in µs |
| 2 rest | 5 | +1-4 the length in µs |
| 3 instrument | 9 | +1-8 an instrument, as the table's, for this voice's notes from here |
| 4 volume | 2 | +1 the voice's volume (0-255) from here |
| 5 marker | 2 | +1 the marker (0-254): MUSIC_MARK when the voice reaches it |

A block whose header, counts or entries don't parse is refused at XFERDONE with `E_DATA` (a voice outside the mask,
an entry past the transfer's end, a gate longer than its length, a kind above 5).

### The sound bank's definitions

**A SID effect** (EFFECT_DEF kind 0): +0 the steps (1-16), +1 the step to loop back to after the last (`$FF` none:
the effect ends), then 13 bytes a step:

| Step | What |
|---|---|
| +0-1 | its length in ms (1-65,535) |
| +2 | the control register: waveform, ring, sync and gate bits as the chip's (the test bit kept 0) |
| +3-5 | the frequency, Hz × 16 (24 bits) |
| +6-7 | the pulse width (12 bits) |
| +8 | AD |
| +9 | SR |
| +10-11 | a frequency sweep, signed Hz × 16 a ms, over the step |
| +12 | a pulse-width sweep, signed, a ms, over the step |

**A sample effect** (EFFECT_DEF kind 1), 11 bytes: +0 the sample slot, +1-2 the rate to play it at in Hz (0: the rate
it was recorded at), +3 0 one-shot or 1 looped at the sample's loop points, +4-5 the attack in ms, +6-7 the hold in ms
(0: to the sample's end, or to VOICE_STOP when looped), +8-9 the release in ms, +10 reserved (0). PLAY's pitch offset
in cents applies to both kinds.

**EFFECT_LOAD's table** is a run of definitions, each +0 its length (the bytes after it), +1 the kind, +2 on the
definition, numbered from the command's first effect on.

### The firmware image

A 32-byte header, then the body:

| Bytes | What |
|---|---|
| +0-3 | the magic, `OOSD` in ASCII |
| +4 | the card type, `$A3` |
| +5 | the header's version, 1 |
| +6-7 | the image's version (major, minor), as FWVER will read |
| +8-9 | its build |
| +10-11 | reserved, 0 |
| +12-15 | the body's length |
| +16-19 | the body's CRC-32 |
| +20-27 | reserved, 0 |
| +28-31 | the CRC-32 of +0-27 |

FW_BEGIN's length is the header's and the body's. FW_COMMIT's reply comes at once (the image is taken for checking);
FWRESULT says, once the image is checked and written into the slot not running, whether it will boot. A wrong magic,
card type, length or either CRC is `E_FIRMWARE`, and the old image stays the one that boots. In the emulator the slots
live in the card object (a FWRESTART or /RESET keeps them; a machine built afresh starts from the built-in image), and
behaviour stays the model's: what is tested is the update's path ([the build plan](sound-device-build.md), 4.7).

### The firmware's port

The firmware talks to the chips through one interface (`SoundFwPort`), so the same code can drive the emulator's
chips, a microcontroller's pins or an FPGA's cores: the 6510 (write its RAM, reset, run, hold, stop; its play-call
signal, the stub's `$DE00` write, which reaches the firmware and not the host; its JAM; its cycle count); the SIDs (a
register write by chip, register and value; the model; the ADC's samples); the ES5503 (a register write, wave RAM, its
output and interrupt); the codec's DAC; and the card's clock, in SID clocks and 48 kHz samples. The 6510's map at
the first step is today's decode (a second SID at `$D420`, or `$D500` when the header names it; every other
`$D400-$D7FF` address SID A), so the corpus can play identically; the map above, with mirrors and a header-named
second SID, comes as a later firmware step with its own corpus baseline (the owner's answer 2).

**Times in the model:** a command runs at the first 48 kHz sample at least 20 µs after its record's last byte came
(the byte's card time, its place within a sample included); the start-up after power-on or FWRESTART takes 20 ms
(inside the spec's 100 ms). Both are constants with test hooks (`set_latency_us`, `set_startup_ms`).

**XFERABORT runs at once.** A command naming an object whose transfer is still open waits for it, in order, with
everything behind it; XFERABORT alone doesn't wait behind such a command: it runs as soon as it is due, ends the
transfer, and its reply comes before the waiting command's (which then finds the object gone: `E_SLOT`). So a host
can always end a load it can't finish.

### Function 82: one clean table

The owner's answer 4+5 (2026-11-23): function 82's operations are numbered afresh, grouped by job, with no gaps and
nothing kept refused; removed numbers are reused, since nothing has shipped before 1.0. The guard is OOLIB's interface
18: every rebuilt caller (GW-BASIC's loader, `ui_load`, PLAYSID, SYSINFO, OO-Amp's port) checks it and refuses an
older OODOS. As before: X = 0 and A the operation, or A/X a block whose +0 is the operation; A = X = `$FF` for an
operation that isn't, a block needed and not given, a part not claimed, or no revision 3 card; A = `$FE` (`SN_BUSY`),
X = `$FF` when the card doesn't answer in 100 ms or channel 2 is busy with the stream. **Decided (new, the owner,
2026-11-23):** A = `$FD` (`SN_REFUSED`), X = the card's error (`E_MEMORY`, `E_BUSY` ...) when the card refused the command, so a program can
tell a full store from a chip in use. [part] is the claim an operation needs.

| Op | Name | Block (after +0) | Results; on the card |
|---|---|---|---|
| | **The device** | | |
| 0 | `SN_INFO` | none, or +1-3 to fill | A the revision (3; 0 no card), X FEATURES; a block +1, +2 those, +3 the parts no one has claimed |
| 1 | `SN_CLAIM` | +1 the parts | all or none |
| 2 | `SN_RELEASE` | +1 the parts | each stopped and given back |
| 3 | `SN_IDENTIFY` | +1-32 to fill | IDENTIFY's 32 bytes |
| 4 | `SN_FIRMWARE` | +1-3 a physical address, +4-6 the length, +7 0 a piece, 1 begin (+8-11 the image's length), 2 commit | FW_BEGIN, the pieces into DATA, FW_COMMIT then FWRESULT awaited; no claim; refused while any part is claimed |
| | **Tunes** | | |
| 5 | `SN_TUNE` | +1-3 a physical address, +4-6 the length, +7 a slot (0-3) | TUNE_LOAD, the file by channel 2, XFERDONE awaited [`SC_TUNE`] |
| 6 | `SN_PLAY` | +1 the slot, +2 the song (0 the file's), +3 flags (bit 0 PAL, bit 1 NTSC), +4-7 a length in ms (0 until stopped), +8-9 a fade in ms | TUNE_PLAY [`SC_TUNE`] |
| 7 | `SN_STOP` | none | TUNE_STOP [`SC_TUNE`] |
| 8 | `SN_TPAUSE` | none | TUNE_PAUSE [`SC_TUNE`] |
| 9 | `SN_TRESUME` | none | TUNE_RESUME [`SC_TUNE`] |
| 10 | `SN_TSTATUS` | +1-16 to fill | TUNE_STATUS's 16 bytes |
| 11 | `SN_TINFO` | +1 a slot, +2-105 to fill | TUNE_INFO's 104 bytes |
| | **Streams** (stream 0, and 1 for SN_NEXT) | | |
| 12 | `SN_STREAM` | +1 the codec, +2-3 the rate, +4 the format | STREAM_OPEN: a stream started afresh, both buffers free [`SC_STREAM`] |
| 13 | `SN_BUFFER` | +1-3 a physical address, +4-5 the length (0: 65,536) | A the buffer taken (0 or 1); sent into DATA on NEED [`SC_STREAM`] |
| 14 | `SN_PAUSE` | none | STREAM_PAUSE [`SC_STREAM`] |
| 15 | `SN_RESUME` | none | STREAM_RESUME [`SC_STREAM`] |
| 16 | `SN_SEEK` | none | STREAM_FLUSH, both buffers dropped [`SC_STREAM`] |
| 17 | `SN_END` | none | STREAM_END [`SC_STREAM`] |
| 18 | `SN_STATUS` | +1-12 to fill | A the state, X the buffers free; the block as today's, +3-4 the ring's room in 256-byte units |
| 19 | `SN_NEXT` | +1 the codec, +2-3 the rate, +4 the format | stream 1 opened and queued behind stream 0 (STREAM_NEXT); SN_BUFFER feeds the one that wants data [`SC_STREAM`] |
| | **The sound bank** | | |
| 20 | `SN_SAMPLE` | +1 a slot, +2-4 a physical address, +5-7 the length, +8 the flags, +9-10 the rate, +11-13 the loop start, +14-16 the loop end | SAMPLE_LOAD, XFERDONE awaited |
| 21 | `SN_EFFECTS` | +1 the first effect, +2-4 a physical address, +5-7 the length | EFFECT_LOAD, XFERDONE awaited |
| 22 | `SN_EFFECT` | +1 the effect, +2 the volume, +3 the pan, +4 the priority, +5-6 cents, +7 the chip | PLAY; A the voice handle |
| 23 | `SN_ESTOP` | +1 a voice handle, 0 every effect voice | VOICE_STOP |
| | **Music** | | |
| 24 | `SN_MUSIC` | +1 a slot, +2-4 a physical address, +5-7 the length, +8 MUSIC_PLAY's flags | MUSIC_LOAD, XFERDONE awaited, MUSIC_PLAY [`SC_MUSIC`] |
| 25 | `SN_MCTL` | +1 0 stop, 1 pause, 2 resume, 3 the threshold (+2 n) | MUSIC_STOP, MUSIC_PAUSE, MUSIC_RESUME, MUSIC_THRESH [`SC_MUSIC`] |
| 26 | `SN_MSTATUS` | +1-24 to fill | MUSIC_STATUS's 24 bytes |
| | **The beep** | | |
| 27 | `SN_BEEP` | +1-2 the pitch in Hz (20-20,000), +3-4 the length in ms | BEEP; returns when BEEP_DONE comes (the owner's answer 6) |
| | **The mix and what it shows** | | |
| 28 | `SN_VOLUME` | +1 left, +2 right | VOLUME |
| 29 | `SN_MIX` | +1 a source, +2 a level | MIX |
| 30 | `SN_EQ` | +1 a band and +2 a gain; or +1 `$FF` and +2-12 all; or +1 `$FE` and +2 off or on | EQ, EQ_ALL, EQ_ON |
| 31 | `SN_VIS` | +1-98 to fill | VIS |
| 32 | `SN_SIDREGS` | +1-50 to fill | SIDREGS |
| 33 | `SN_LEVELS` | +1-10 to fill | LEVELS |
| | `SN_OPS` = 34 | | |

- **The parts:** `SC_TUNE` `$01`, `SC_STREAM` `$02`, `SC_MUSIC` `$04` (the bit `SC_DOC` had, as the clean table
  reuses numbers; the draft had `$08`). Effects and the beep need no claim.
- **What goes** (no number kept): `SN_LOAD`, `SN_START`, `SN_SEND`, `SN_WAVE`, `SN_DOCHOOK`, `SN_HOLD`, `SN_GO`,
  `SC_6510` and `SC_DOC`. `SN_STOP` keeps its name (the draft's Q5) at its new number.
- **The event area:** +96-107 the stream's, as now; +108-115 reserved, 0; +116 the tune's state (TUNE_STATUS's),
  +117-119 its ms played (24 bits); +120 music's state, +121 the blocks queued, +122-123 the notes waiting; +124-127
  reserved, 0. OODOS writes them once a frame while the part is claimed, from the frame's status requests (the build
  plan's flag 12).
