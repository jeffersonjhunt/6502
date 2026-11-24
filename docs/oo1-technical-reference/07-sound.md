# 7 Sound

The sound card (module A, `$C500`), revision 3: the card as a device. Two SIDs, a 6510 of its own for `.sid` tunes,
an ES5503 for samples, a stream decoder (raw PCM and MP3), a tone generator, and a store of 2, 4 or 8 MB, all behind
a controller and its firmware. The host never touches the chips: it writes command records to the card and reads
event records back, and sends data through one port by DMA.

The design record, for the reasons: [hw/design.md, "Sound"](../../hw/design.md#sound), and the card's spec,
[hw/future/sound-device.md](../../hw/future/sound-device.md).

## The card as a device

Status: **emulated**

The card's registers fill `$C500–$C511`, with its ID at `$C57F` ([Registers](#registers)); the rest of the block
reads `$FF`. The emulator models the card on the machine file's sound line (`rev=3`, the only revision, may be said;
`memsize=2`, `4` or `8`, 8 the default).

- **A command** is a record written to [CMD](#reg-c503) (`$C503`), a byte at a time: +0 the command, +1 n (the
  parameter bytes after +2), +2 a tag of the host's own choosing, +3 on the parameters, low byte first. The card
  runs it once its last byte is in. Write a record whole, with interrupts off if a handler of yours writes records
  too: two records interleaved are both refused. [CMDFREE](#reg-c504) and STATUS's CMDRDY say there is room.
- **An event** is a record read from [EVT](#reg-c505) (`$C505`): +0 the kind, +1 n (the bytes after +1), then its
  body. Every command gets exactly one REPLY (kind 1), in the order the commands came: +2 its tag, +3 a status (0, or
  an error), +4 on its results. Every other event says something that happened since: +2 the object it is about (a
  slot, a voice, a stream), +3–6 the card's millisecond count, +7 on a payload. [EVTLEN](#reg-c506) says how many
  bytes wait; STATUS's EVT that one does.
- **Data** (a tune, samples, note blocks, a stream's bytes, a firmware image) goes in through [DATA](#reg-c507)
  (`$C507`) once a command has opened a transfer for it: by DMA channel 2 in demand mode (CTRL's DRQ set; the card
  asks only while the transfer is open and has room), or by the CPU a byte at a time. [XLEFT](#reg-c508) counts what
  is still wanted, and the transfer's end is an XFERDONE event and ISR's XDONE.
- **The interrupt.** The card pulls /IRQ (IRQH's bit 2, module A) while [ISR](#reg-c50d) AND [IEN](#reg-c50c) isn't
  0: an event waiting, a transfer done, CMD's room back, the firmware up.

The commands, the event kinds and the errors are backplane.inc's `SOUND_*` equates; the spec has every record's
layout.

### The commands

Status: **emulated**

| Group | Commands |
|---|---|
| The device (`$00–$0F`) | NOP, IDENTIFY (the firmware's version, the store, the features), STOPALL (every sound stopped), FREEALL (the store emptied), EVENTS (which events are sent), LOSTCLR (EVLOST cleared), TIME (the card's ms), SELFTEST (each part checked, a bit each), CHECKSUM (a CRC-32 of a loaded object), BOOTSOUND, FW_BEGIN and FW_COMMIT (a firmware update), XFERABORT |
| Tunes (`$20–$27`) | TUNE_LOAD (a `.sid` file into one of four slots), TUNE_PLAY (a song, PAL or NTSC, a length, a fade), TUNE_STOP, TUNE_PAUSE, TUNE_RESUME, TUNE_STATUS, TUNE_INFO (the file's header), TUNE_FREE |
| The sound bank (`$30–$35`) | SAMPLE_LOAD (a sample into one of 256 slots), EFFECT_DEF and EFFECT_LOAD (effects: a sample, a pitch, an envelope), PLAY (an effect on a voice, its volume, pan and priority), VOICE_STOP, BANK_FREE |
| Music (`$40–$46`) | MUSIC_LOAD (a note block into one of eight slots), MUSIC_PLAY (now or queued), MUSIC_STOP, MUSIC_PAUSE, MUSIC_RESUME, MUSIC_STATUS, MUSIC_THRESH (a MUSIC_COUNT event when the notes waiting fall to n) |
| Streams (`$50–$58`) | STREAM_OPEN (a codec, a rate, a format, a chunk and a prebuffer), STREAM_DATA (a transfer into the stream's ring), STREAM_END, STREAM_NEXT (the next stream queued behind it, gapless), STREAM_PAUSE, STREAM_RESUME, STREAM_FLUSH, STREAM_STATUS, STREAM_CLOSE |
| The beep (`$60–$61`) | BEEP (a pitch, a length and a volume on the tone generator), BELL (the console's bell) |
| The mix (`$70–$77`) | VOLUME (the master volumes), MIX (a source's level: tunes, music, effects, streams, the beep), EQ, EQ_ALL and EQ_ON (the ten-band EQ), VIS (the vis page), SIDREGS (both SIDs' registers as last written), LEVELS (eight bands and a VU pair a source) |

### The events and the errors

Status: **emulated**

| Kind | Event |
|---|---|
| `$01` | REPLY: a command's status and results |
| `$10`, `$11` | XFERDONE (a transfer took its last byte), FWRESULT (a firmware update's outcome) |
| `$20–$23` | TUNE_STARTED, TUNE_STALL (the tune's play routine stopped being called), TUNE_JAM (its 6510 stopped on a jam opcode), TUNE_ENDED |
| `$30`, `$31` | VOICE_DONE (an effect ended), VOICE_STOLEN (a voice taken for a higher priority) |
| `$40–$42` | MUSIC_MARK (a marker in a block reached), MUSIC_COUNT (the threshold met), MUSIC_DONE (the last block ended) |
| `$50–$54` | NEED (a stream's ring has room for a chunk), UNDERRUN and UNDERRUN_END, TRACK (the next stream began), STREAM_ENDED |
| `$60` | BEEP_DONE |

A record refused before it runs (a bad length, an unknown command) sets STATUS's ERR and [ERROR](#reg-c502); one
refused when it runs gets its error as the REPLY's status. The errors, the same in both: 1 UNKNOWN, 2 PARAM, 3 SLOT,
4 MEMORY, 5 BUSY, 6 DATA, 7 XFER, 8 STATE, 9 NOTREADY, 10 JAM, 11 FIRMWARE (`SOUND_E_*`).

### Silencing it

Status: **emulated**

One write stops everything: CTRL's RESET ([`$C500`](#reg-c500)) stops every sound on the card, abandons an open
transfer, empties both FIFOs and clears IEN; the store keeps what it holds. The BIOS's QUIET does it on Break, BRK
and the demo's end:

<!-- listing: ../../backplane/bios/bios.s:606-610 -->
```
@sound:         LDA     SOUND_ID
                CMP     #SOUND_ID_V3
                BNE     @panel
                LDA     #SOUND_CTRL_RESET
                STA     SOUND_CTRL
```

A program that wants the card's state kept (its loaded tunes and samples, its volumes) sends STOPALL instead: every
sound stopped, nothing else changed. The ROM's `$C818` with X = `$A3` sends it ([below](#the-beep-and-the-bell)).

### The boot sound

Status: **emulated**

The boot splash's sound ([hw/design.md, "The splash's sound"](../../hw/design.md#the-splashs-sound)) plays when both
a video card and a sound card answer. The card's firmware holds it, samples and sequence, in its own flash; the ROM
only asks for it. `splash.s` writes CTRL's RESET, waits for READY (250 ms at most, else the splash runs silent),
starts the splash, and on the vertical blank its frame 0 begins on sends one record, BOOTSOUND (tag `$FE`, its reply
not waited for):

<!-- listing: ../../backplane/bios/splash.s:50-63 -->
```
@video:         LDA     #VIDEO_SPLASH_ON
                STA     VIDEO_SPLASH   ; latched: it starts at the next vertical blank
                LDA     PLAYS
                BEQ     @done
@out:           BIT     VIDEO_STATUS   ; a vertical blank under way: wait it out
                BMI     @out
@in:            BIT     VIDEO_STATUS   ; the next one: the splash's frame 0 begins
                BPL     @in
                LDA     #SOUND_BOOTSOUND ; the boot sound's frame 0, now: n 0
                STA     SOUND_CMD
                STZ     SOUND_CMD
                LDA     #STAG
                STA     SOUND_CMD
@done:          RTS
```

The sound ends by itself; Break stops it early.

### The beep and the bell

Status: **emulated**

The tone generator plays a band-limited square, one note at a time, beside everything else. BEEP gives it a pitch
(20–20,000 Hz), a length in ms and a volume (0 the default), and BEEP_DONE says it ended; BELL plays the console's
bell, the firmware's own short tone.

On the bare machine the ROM has both through its dispatching entry `$C818` ([chapter 3](03-boot.md)), Y = `$5A`:
X = `$A2` is BEEP, 800 Hz for A × 10 ms (A = 0: 250 ms), and X = `$A3` is STOPALL. Each waits for the card's READY
(250 ms) and for room in CMD (100 ms), writes the record whole with interrupts off (tag `$FF`), and waits for its
REPLY (100 ms), reading and dropping any other record meanwhile: carry clear once the reply is in, carry set at once
with no card, or when a wait runs out. A, X, Y and the windows are kept. Under OODOS the beep is function 82's
`SN_BEEP` and the console's BEL character rings BELL ([below](#under-oodos)).

### Playing a .sid file

Status: **emulated**

A C64 `.sid` file is the tune's own 6502 code, with a header saying where it loads, its init and play routines, its
songs and its clock. The card plays it on its 6510: TUNE_LOAD takes the file into a slot (its header checked), and
TUNE_PLAY starts a song. The firmware loads the code into the 6510's RAM, sets up what a C64 would have left for the
tune, calls init with the song, then play from its timer or once a frame. TUNE_STATUS says how it goes; TUNE_STALL,
TUNE_JAM and TUNE_ENDED say when it stops. Under OODOS, PLAYSID does this through function 82.

The WebUI's Play .sid hands the file to the same tune player directly, outside the bus: no program runs on the OO-1
for it. hw/design.md has the details and their reasons ("`.sid` playback", under
["Sound"](../../hw/design.md#sound)).

### In Apple II mode

Status: **emulated**

The card is idle in Apple II mode, and its block is hidden. Entering the mode pulses /RESET, which does to the card
what CTRL's RESET does ([chapter 13](13-apple-ii-mode.md#the-reset-pulse)). Apple software never addresses the card.

## Under OODOS

Status: **built**

OODOS drives the card itself, through function 82 ([the Programmer's Guide,
chapter 8](../oodos-programmers-guide/08-game-services.md#sound)); a program never writes the card's registers. Its
three parts, the tune player (`SC_TUNE`), the stream (`SC_STREAM`) and music (`SC_MUSIC`), are claimed one program at
a time; the sound bank, the beep, the mix and the EQ need no claim. The operations ask the card in records and take
its replies, and the event area's sound part (+96–127) shows the stream's, the tune's and music's state once a frame
while a part is claimed. The card's events that a program cares for (a tune ended, a marker, an underrun) come as
`EK_SOUND` events, the card's kind in their code.

- **Records.** OODOS writes every record whole with interrupts off, so a program's and the interrupt handler's never
  mix. A function 82 call waits for its own reply (100 ms; a beep's its length more), A = `SN_BUSY` if it doesn't
  come; a command the card refuses gives A = `SN_REFUSED` and X the card's error. The handler's own records (the
  stream's data, the frame's status requests, the bell) carry tags from `$80` up.
- **Loads.** A tune, samples, effects, note blocks and a firmware image go in by DMA channel 2 into DATA, memory to
  the card in demand mode, 4,096 bytes a piece, each piece under a tick of the millisecond counter. Each address is
  checked as function 51 checks a segment.
- **The stream.** A program gives OODOS two buffers in turn (`SN_BUFFER`); when the card sends NEED, OODOS opens a
  transfer of the next buffer's length and channel 2 sends it in pieces, one each terminal count. A buffer sent is
  free again (`EV_SBUFS`). `SN_NEXT` queues another stream behind the one playing; `SN_END` says no more data comes.
- **IEN:** EVT, whenever OODOS has the card up. OODOS's turning off of strays leaves it, and channel 2 while a stream
  buffer moves on it, alone.
- **The cold boot** writes CTRL's RESET (a record the ROM left half written is dropped), waits for READY (250 ms),
  sets CTRL's DRQ and empties the store (FREEALL).
- **The warm boot**, at every program's end, whether a program claimed the card or not: CTRL's RESET (every sound
  stopped, an open transfer abandoned, the volumes and every source's MIX 255, the EQ off), FREEALL, channel 2
  aborted, every claim released. So a program leaves nothing playing and nothing loaded.

## Registers

Status: **emulated**

<a id="reg-c500"></a>
### $C500 SOUND_CTRL: the card's control

#### Equates

`SOUND`, `SOUND_CTRL`

#### Module

A, the sound card

#### Access

R/W. Bits 6 and 7 are strobes: they act on the write and read 0.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | RESET | as /RESET: an open transfer abandoned, both FIFOs emptied, every sound stopped, CTRL, IEN and ISR 0; the store keeps what it holds |
| 6 | FWRESTART | the firmware starts again: READY clear until it is up, the store emptied |
| 5–1 | | read 0 |
| 0 | DRQ | DATA asks for DMA while a transfer is open and has room |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

`SOUND` is the block's base. The bits are `SOUND_CTRL_DRQ`, `SOUND_CTRL_FWRESTART` and `SOUND_CTRL_RESET`. The BIOS's
QUIET and OODOS's boots write RESET ([Silencing it](#silencing-it)).


<a id="reg-c501"></a>
### $C501 SOUND_STATUS: the card's status

#### Equates

`SOUND_STATUS`

#### Module

A, the sound card

#### Access

R. Reads are answered at once.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | IRQ | the card is interrupting: ISR AND IEN |
| 6 | ERR | the last record was refused before it ran: ERROR says why |
| 5 | EVLOST | events were lost since the last LOSTCLR command |
| 4 | DATA | DATA can take a byte now |
| 3 | XFER | a transfer is open |
| 2 | EVT | an event record waits |
| 1 | CMDRDY | CMD has room for a whole record (256 bytes) |
| 0 | READY | the firmware is up |

#### Reset

`$03` once the firmware is up (READY, CMDRDY), within 100 ms of power-on.

#### Status

**emulated**

#### Notes

The bits are `SOUND_ST_READY` to `SOUND_ST_IRQ`.

<a id="reg-c502"></a>
### $C502 SOUND_ERROR: why a record was refused

#### Equates

`SOUND_ERROR`

#### Module

A, the sound card

#### Access

R.

#### Bits

All: 0 none, or the error that refused the last record at the door (`SOUND_E_UNKNOWN` 1 to `SOUND_E_FIRMWARE` 11): a command the firmware doesn't know, or n wrong for it.

#### Reset

`$00`.

#### Status

**emulated**


<a id="reg-c503"></a>
### $C503 SOUND_CMD: the command FIFO

#### Equates

`SOUND_CMD`

#### Module

A, the sound card

#### Access

W. A command record's bytes, in order; the record runs once its last byte is in. Reads 0.

#### Bits

All: the next byte of a record.

#### Reset

Empty: /RESET and CTRL's RESET drop a half-written record.

#### Status

**emulated**

#### Notes

Write a whole record at a time, with interrupts off if a handler of yours writes records too: two records interleaved are both refused. The FIFO holds 1 KB.

<a id="reg-c504"></a>
### $C504 SOUND_CMDFREE: the command FIFO's room

#### Equates

`SOUND_CMDFREE`

#### Module

A, the sound card

#### Access

R.

#### Bits

All: the room in bytes, 255 meaning 255 or more.

#### Reset

`$FF`.

#### Status

**emulated**


<a id="reg-c505"></a>
### $C505 SOUND_EVT: the event FIFO

#### Equates

`SOUND_EVT`

#### Module

A, the sound card

#### Access

R. A read takes the next byte; with none waiting it reads 0 (kind 0, none).

#### Bits

All: the next byte of an event record.

#### Reset

Empty.

#### Status

**emulated**

#### Notes

The FIFO holds 2 KB; the firmware keeps room in it for the replies of the commands it has taken.

<a id="reg-c506"></a>
### $C506 SOUND_EVTLEN: the bytes waiting in EVT

#### Equates

`SOUND_EVTLEN`

#### Module

A, the sound card

#### Access

R.

#### Bits

All: the bytes waiting, 255 meaning 255 or more.

#### Reset

`$00`.

#### Status

**emulated**


<a id="reg-c507"></a>
### $C507 SOUND_DATA: the open transfer's bytes

#### Equates

`SOUND_DATA`

#### Module

A, the sound card

#### Access

W. A byte with no transfer open, or no room, is dropped. DMA channel 2 serves it (CARD `$0A`).

#### Bits

All: the next byte of the transfer.

#### Reset

No transfer open.

#### Status

**emulated**

#### Notes

With CTRL's DRQ set the card asks for DMA only while a transfer is open and its landing buffer has room.

<a id="reg-c508"></a>
### $C508 SOUND_XLEFT: the bytes the transfer still wants

#### Equates

`SOUND_XLEFT`

#### Module

A, the sound card

#### Access

R, 3 bytes, `$C508–$C50A`, low byte first. Reading `$C508` latches all three.

#### Bits

All: a 24-bit count.

#### Reset

`$000000`.

#### Status

**emulated**


<a id="reg-c50b"></a>
### $C50B SOUND_XTAG: the transfer's tag

#### Equates

`SOUND_XTAG`

#### Module

A, the sound card

#### Access

R.

#### Bits

All: the tag of the command that opened the transfer.

#### Reset

`$00`.

#### Status

**emulated**


<a id="reg-c50c"></a>
### $C50C SOUND_IEN: the card's interrupt enable

#### Equates

`SOUND_IEN`

#### Module

A, the sound card

#### Access

R/W.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | read 0 |
| 3 | READY | the firmware came up |
| 2 | CMDROOM | CMD had been full and has room again |
| 1 | XDONE | a transfer took its last byte |
| 0 | EVT | an event waits |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

The card pulls /IRQ while ISR AND IEN isn't 0 (IRQH's bit 2, as before). The bits are `SOUND_INT_EVT` to `SOUND_INT_READY`.

<a id="reg-c50d"></a>
### $C50D SOUND_ISR: the card's interrupt status

#### Equates

`SOUND_ISR`

#### Module

A, the sound card

#### Access

R; write 1 to a bit to clear it (bits 1–3).

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | read 0 |
| 3 | READY | the firmware came up |
| 2 | CMDROOM | CMD had been full and has room again |
| 1 | XDONE | a transfer took its last byte |
| 0 | EVT | a level: set while an event waits; writing it does nothing |

#### Reset

`$00`.

#### Status

**emulated**

#### Notes

EVT is a level so that an event that comes between a handler's last read of EVT and its write here is never lost. Bits 1–3 are set whether IEN has them or not.

<a id="reg-c50e"></a>
### $C50E SOUND_MEMSIZE: the store's size

#### Equates

`SOUND_MEMSIZE`

#### Module

A, the sound card

#### Access

R.

#### Bits

All: the store in 64 KB units: 32, 64 or 128 (2, 4 or 8 MB).

#### Reset

The card's size.

#### Status

**emulated**


<a id="reg-c50f"></a>
### $C50F SOUND_FEATURES: what the card has

#### Equates

`SOUND_FEATURES`

#### Module

A, the sound card

#### Access

R.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | MORE | more in IDENTIFY's reply |
| 6 | VIS | the vis page |
| 5 | EQ | the EQ |
| 4 | MP3 | MP3 streams |
| 3 | PCM | raw PCM streams |
| 2 | DOC | the ES5503 |
| 1 | TUNES | the 6510: `.sid` tunes |
| 0 | TWOSID | two SIDs |

#### Reset

The card's features: `$FF` on the emulator's.

#### Status

**emulated**

#### Notes

The bits are `SOUND_FT_TWOSID` to `SOUND_FT_MORE`.

<a id="reg-c510"></a>
### $C510 SOUND_FWVER: the firmware's version

#### Equates

`SOUND_FWVER`

#### Module

A, the sound card

#### Access

R, 2 bytes, `$C510–$C511`: the major, then the minor.

#### Bits

All: the running image's version.

#### Reset

The version running (the emulator's model: 1.0).

#### Status

**emulated**


<a id="reg-c57f"></a>
### $C57F SOUND_ID: the card's ID

#### Equates

`SOUND_ID`

#### Module

A, the sound card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | TYPE | the module ID: `$A` |
| 3–0 | REV | the revision: 3 |

#### Reset

`$A3`.

#### Status

**emulated**

#### Notes

`SOUND_ID_V3` (`$A3`) is the value, and `SOUND_TYPE` (`$A0`) its high nibble. The BIOS and OODOS check for `$A3`
before they touch the card. The offsets with no register, `$12–$7E`, read `$FF` and ignore writes.
