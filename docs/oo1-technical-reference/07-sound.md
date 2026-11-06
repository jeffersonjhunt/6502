# 7 Sound

The sound card (module A, `$C500`): two SIDs and a 6510 of its own.

The design record, for the reasons: [hw/design.md, "Sound"](../../hw/design.md#sound).

## The SIDs

Status: **emulated**

The card holds two SIDs. SID A's 29 registers are at `$C500–$C51C` ([SOUND_SIDA](#reg-c500)) and SID B's at
`$C520–$C53C` ([SOUND_SIDB](#reg-c520)). Each chip has three voices, a filter and a master volume. In the emulator
SID A is the left channel and SID B the right.

- **Who has them.** The host CPU writes the SIDs only while the card's 6510 is stopped (CTRL's RUN is 0). While it
  runs, the chips are the 6510's and a host write is dropped. Reads always work, from either side.
- **Reading back.** The card answers each write-only register with the last value written to it, so every register
  reads back. The four read-only ones, `$19–$1C`, come from the chip: POTX and POTY read `$FF`, as no paddles are
  connected, and OSC3 and ENV3 give voice 3's waveform and envelope as they are at that clock.
- **Both at once.** With CTRL's BOTH, a write to SID A goes to SID B as well, from either side. A mono tune then
  plays in both ears.
- **The clock.** CTRL's bit 0 picks the SID clock: 985,248 Hz (PAL) or 1,022,727 Hz (NTSC). The chips run from it
  whether or not the 6510 runs, so an envelope goes on moving with the card stopped.
- **The model.** The chips can be 6581s or 8580s. That is a jumper on the replacement chips, not a register. The
  registers are the same on both; the sound differs. The emulator starts with 6581s. Play .sid sets the model the
  tune asks for ([below](#playing-a-sid-file)), and the chips keep it through resets.

### A voice

Status: **emulated**

A voice has seven registers: a frequency, a pulse width, a control byte and two envelope bytes.

- **Pitch.** The 16-bit frequency is added to a 24-bit phase counter on every SID clock. The tone is the frequency
  times the SID clock, divided by 16,777,216.
- **Waveforms.** CONTROL's bits 4–7 choose triangle, sawtooth, pulse and noise. The pulse is high while the phase
  counter's top 12 bits are at or above the 12-bit pulse width, so `$800` is a square wave. Noise comes from a 23-bit
  shift register, stepped as the phase counter's bit 19 rises.
- **Sync and ring.** Each voice takes voice n−1 as its partner, and voice 1 takes voice 3. SYNC restarts the phase
  counter whenever the partner's top bit rises. RING folds the partner's top bit into the triangle.
- **TEST** holds the phase counter at 0 and the pulse high, and resets the noise register.
- **The envelope** is an 8-bit level. GATE going to 1 starts the attack, which climbs from wherever the level is to
  `$FF`. The decay then falls to the sustain level, which is the sustain nibble times `$11`. GATE going to 0 starts
  the release, down to 0. Each rate nibble picks one of 16 step periods, from about 2 ms to about 8 s for a whole
  attack at 1 MHz. Decay and release slow down as the level gets lower, as the chip's do.
- **The rate counter** is reset only when it matches its period. A period written lower than the count runs on
  through `$7FFF` first. That delay is the chip's, and tunes depend on it.

The filter is a two-pole filter run on every clock. FC_LO and FC_HI hold an 11-bit cutoff, RES_FILT the resonance
and which voices go through it, and MODE_VOL its low-, band- and high-pass outputs and the master volume. On a
6581 the volume also moves a DC level, so writing MODE_VOL alone plays a 4-bit sample, as many tunes do. On an
8580 that effect is faint.

**What the emulator leaves out:** two or more waveforms at once are ANDed together. The real chip's mixes are
lower and uneven, and their real values are in the emulator's backlog, so OSC3 then reads approximately. The DACs'
nonlinearity, the 6581 filter's distortion and the external input (RES_FILT's bit 3) aren't modelled either.

### Silencing them

Status: **emulated**

To take the SIDs, stop the 6510 (clear RUN), then write the registers. Writing every register 0 turns off each
voice's GATE and sets the volume to 0. The BIOS's QUIET does that on Break, BRK and the demo's end:

<!-- listing: ../../backplane/bios/bios.s:197-207 -->
```
@sound:         LDA     SOUND_ID
                CMP     #SOUND_ID_V1
                BNE     @panel
                LDA     SOUND_CTRL
                AND     #<~SOUND_RUN   ; the 6510 stopped, so the SIDs take writes
                STA     SOUND_CTRL
                STZ     SOUND_IEN
                LDX     #SOUND_SIDS - 1
@sid:           STZ     SOUND,X        ; every SID register: gates and volume off
                DEX
                BPL     @sid
```

`SOUND_SIDS` is `$3D`, so the loop covers `$C500–$C53C`: SID A, the gap between and SID B. The gap's writes are
ignored. QUIET keeps the rest of CTRL, NTSC among it, and leaves the card's RAM, RAMADDR and the mailbox alone.

## The card's 6510

Status: **emulated**

The card runs its own 6510, so a tune or a sound driver plays with no time from the main CPU. In the emulator it is
an NMOS 6502 with the undocumented opcodes, which many C64 players use. It runs one cycle per SID clock while CTRL's
RUN is set, with 64 KB of RAM of its own. The host reaches that RAM through [RAMADDR](#reg-c543) and
[RAMDATA](#reg-c545), and talks to the running program through [the mailbox](#the-mailbox).

**Starting it:**

1. Clear RUN. The SIDs are the host's again.
2. Load the program through RAMADDR and RAMDATA, by the CPU or by DMA channel 2.
3. Write the program's entry at `$FFFC–$FFFD` of the card's RAM, its reset vector.
4. Write CTRL with RUN and RESET, and NTSC if the program wants that clock.

RESET restarts the 6510 from its vector. It also resets the 6510's port, the imitated CIA and the frame, below.
Setting RUN without RESET lets the 6510 go on from wherever it stopped, so start a new program with RESET.

**Stopping it.** Clear RUN from the host. The 6510 can also stop itself by writing to `$DE02`, which clears RUN, so
a one-shot program hands the SIDs back when it is done. A /RESET stops it too. The card's RAM keeps its contents
through all three.

### Its memory map

Status: **emulated**

The map follows the C64's, so a C64 player runs as it is.

| Address | What the 6510 sees |
|---|---|
| `$0000` | its port's direction register |
| `$0001` | its port. Bits 0–2 bank `$D000–$DFFF`: I/O while bit 2 and bit 0 or 1 are high, RAM otherwise. Bits 0–5 read 1 while set as inputs |
| `$0002–$FFFF` | RAM. Writes to `$0000` and `$0001` land in the RAM under them too |
| `$D011`, `$D012` | with FRAMEEN's bit 1, the raster line of the card's frame, read only: `$D012` its low 8 bits, `$D011` bit 7 its ninth. `$FF` otherwise |
| `$D400–$D7FF` | SID A, every 32 bytes, except where SID B is: `$D420–$D43F`, or `$D500–$D51F` with CTRL's bit 3 |
| `$DC00–$DCFF` | CIA 1, imitated, every 16 bytes: timer A at `$DC04–$DC05`, its interrupt control at `$DC0D`, its control at `$DC0E` |
| `$DE00` | the mailbox: a read takes the host's byte, a write sends one to the host |
| `$DE01` | the mailbox's status: bit 0, a byte from the host is waiting; bit 1, the card's last byte hasn't been taken |
| `$DE02` | STOP: any write clears CTRL's RUN |
| `$DE03` | FRAME: bit 0 is set at each frame's start, and writing 1 clears it; bit 7 reads 1 while it interrupts |
| `$DE04` | FRAMEEN: bit 0 lets FRAME interrupt; bit 1 shows the raster line at `$D011`/`$D012` |

The `$D000–$DFFF` rows apply only while `$0001` has I/O in. With it out, the 6510 sees RAM there. The rest of
`$D000–$DFFF` reads `$FF` and ignores writes while I/O is in: the rest of the VIC-II, colour RAM, CIA 2, and the
rest of `$DE00–$DFFF`. There are no ROMs.

- **The timer** counts down once a SID clock while the CIA's control has bit 0 set. It runs out after its latch
  plus 1 clocks, reloads from the latch, sets bit 0 of the interrupt control register, and pulls the 6510's /IRQ
  while that register's mask has bit 0. Control's bit 3 makes it stop after one run, and bit 4 loads the latch at
  once. Writing the latch's high byte while the timer is stopped loads it too. Reading the interrupt control
  register clears it.
- **The interrupt control register's mask** is set and cleared by bit 7 of a write: with bit 7 set, the write's
  1s set mask bits; with it clear, they clear them. So `$81` to `$DC0D` enables timer A's interrupt and `$01`
  disables it. A read gives the flags, with bit 7 set while the timer interrupts.
- **The frame** is a C64's: 19,656 SID clocks with PAL (312 lines of 63) or 17,095 with NTSC (263 of 65), counted
  from the 6510's reset while RUN is set. It is for the emulator's player stub ([below](#playing-a-sid-file)).
  FRAME and FRAMEEN sit at addresses a C64 leaves free, so a tune can't turn them on by accident.

### The mailbox

Status: **emulated**

One byte each way. The host writes [MBOUT](#reg-c546) and the 6510 reads it at `$DE00`. The 6510 writes `$DE00` and
the host reads [MBIN](#reg-c547). Either side's second byte, written before the other side has taken the first,
replaces it. [STATUS](#reg-c541) says which box is full on the host's side, and `$DE01` on the card's. With IEN's
bit 2, a byte waiting in MBIN interrupts the host, on bit 2 of IRQH
([chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh)).

What the bytes mean is the program's own: "play song 3" one way, "tune finished" the other.

### The boot sound

Status: **emulated**

The BIOS plays the boot splash's sound on the card
([hw/design.md, "The splash's sound"](../../hw/design.md#the-splashs-sound)) when both a video card and a sound
card answer. `splash.s` clears CTRL, loads the program, under 2 KB, at `$1000` through RAMDATA, and points the
card's reset vector at its entry, `$1006`. Then it starts the splash, waits for the vertical blank the splash
begins on, and writes CTRL:

<!-- listing: ../../backplane/bios/splash.s:60-80 -->
```
@vector:        LDA     #$FC           ; the card's reset vector: the tune's boot entry
                STA     SOUND_RAMADDR
                LDA     #$FF
                STA     SOUND_RAMADDR + 1
                LDA     #<TUNE_BOOT
                STA     SOUND_RAMDATA
                LDA     #>TUNE_BOOT
                STA     SOUND_RAMDATA

@video:         LDA     #VIDEO_SPLASH_ON
                STA     VIDEO_SPLASH   ; latched: it starts at the next vertical blank
                LDA     SOUND_ID
                CMP     #SOUND_ID_V1
                BNE     @done
@out:           BIT     VIDEO_STATUS   ; a vertical blank under way: wait it out
                BMI     @out
@in:            BIT     VIDEO_STATUS   ; the next one: the splash's frame 0 begins
                BPL     @in
                LDA     #SOUND_NTSC | SOUND_RUN | SOUND_RESET
                STA     SOUND_CTRL     ; the tune's frame 0, now
@done:          RTS
```

The program plays from the CIA's timer at 240 Hz, four times a video frame, for 300 frames: 5 s. The sound is over
by about 2.4 s, but the program runs to the end. Then it zeroes the SIDs' registers and writes `$DE02`. It sends
nothing through the mailbox. So under the monitor CTRL reads `$03` for the first 5 s and `$01` after, and RAMADDR
reads `$FFFE`. A program that takes the card in the first 5 s stops the 6510 and silences the SIDs first, as QUIET
does. Break stops the boot sound early.

### Playing a .sid file

Status: **emulated**

A C64 `.sid` file is the tune's own 6502 code, with a header saying where it loads, its init and play routines, its
songs and its clock. The WebUI's Play .sid plays one through the card's host registers, as a program on the host
would. No program runs on the OO-1 for it: the emulator writes the registers itself (`sid_file.h`).

1. It stops the card (CTRL `$00`) and writes 0 to each SID's registers `$00–$18`.
2. It loads the tune at its load address through RAMDATA, then a stub on a free page, and points the card's
   vectors at the stub.
3. It writes CTRL with RUN and RESET; NTSC for an NTSC tune; bit 3 for a tune whose second SID is at `$D500`; and
   BOTH for a tune with one SID.

The stub stands in for what the C64's KERNAL leaves for a tune. It banks `$01` for the routine, starts the CIA's
timer, and calls init with the song number less one in A. Then it calls play from the timer, or once a frame from
FRAME for a tune timed to the vertical blank. After each play it writes the call's number to the mailbox, so the
host can count frames. A tune that needs the VIC-II's raster interrupt, CIA 2, the ROMs or a third SID runs but
stays silent or stops. hw/design.md has the details and their reasons ("`.sid` playback", under
["Sound"](../../hw/design.md#sound)).

### In Apple II mode

Status: **emulated**

The card is idle in Apple II mode, and its block is hidden. Entering the mode pulses /RESET, which stops the 6510,
clears the SIDs and turns IEN off ([chapter 13](13-apple-ii-mode.md#the-reset-pulse)). Apple software never
addresses the card.

### Bus timing

Status: **proposed**

The design's card is an RP2350 that answers the bus through its PIO. A SID write is latched and forwarded to the
chip, and STATUS's BUSY says one is still pending; a read, or a write straight after another, holds the CPU with RDY
until the card is ready. The emulator forwards every write at once and never holds the CPU, so BUSY is always 0.

## Registers

Status: **emulated**

<a id="reg-c500"></a>
### $C500 SOUND_SIDA: SID A's registers

#### Equates

`SOUND`, `SOUND_SIDA`

#### Module

A, the sound card

#### Access

R/W, 29 bytes, `$C500–$C51C`. A write is dropped while CTRL's RUN is set; with CTRL's BOTH it goes to SID B too.
Reads always work: the write-only registers give the last value written, `$C519–$C51C` the chip's. Writes to
`$C519–$C51C` are ignored. `$C51D–$C51F` read `$FF` and ignore writes.

#### Bits

| Offset | Name | What it holds |
|---|---|---|
| `$00` | FREQ_LO1 | voice 1's frequency, low byte |
| `$01` | FREQ_HI1 | voice 1's frequency, high byte |
| `$02` | PW_LO1 | voice 1's pulse width, low byte |
| `$03` | PW_HI1 | voice 1's pulse width, high 4 bits in bits 0–3 |
| `$04` | CONTROL1 | voice 1's waveforms and switches (below) |
| `$05` | AD1 | voice 1's attack rate in bits 4–7, decay rate in bits 0–3 |
| `$06` | SR1 | voice 1's sustain level in bits 4–7, release rate in bits 0–3 |
| `$07` | FREQ_LO2 | voice 2: as `$00` |
| `$08` | FREQ_HI2 | as `$01` |
| `$09` | PW_LO2 | as `$02` |
| `$0A` | PW_HI2 | as `$03` |
| `$0B` | CONTROL2 | as `$04` |
| `$0C` | AD2 | as `$05` |
| `$0D` | SR2 | as `$06` |
| `$0E` | FREQ_LO3 | voice 3: as `$00` |
| `$0F` | FREQ_HI3 | as `$01` |
| `$10` | PW_LO3 | as `$02` |
| `$11` | PW_HI3 | as `$03` |
| `$12` | CONTROL3 | as `$04` |
| `$13` | AD3 | as `$05` |
| `$14` | SR3 | as `$06` |
| `$15` | FC_LO | the filter's cutoff, its low 3 bits in bits 0–2 |
| `$16` | FC_HI | the filter's cutoff, its high 8 bits |
| `$17` | RES_FILT | resonance in bits 4–7; bit 3 the external input (not modelled); bits 0–2 send voices 1–3 through the filter |
| `$18` | MODE_VOL | bit 7 turns voice 3 off unless it is filtered; bits 4–6 the low-, band- and high-pass outputs; bits 0–3 the master volume |
| `$19` | POTX | read only: `$FF`, no paddle connected |
| `$1A` | POTY | read only: `$FF`, no paddle connected |
| `$1B` | OSC3 | read only: voice 3's waveform now, its top 8 bits |
| `$1C` | ENV3 | read only: voice 3's envelope level now |

A voice's CONTROL:

| Bit | Name | Meaning |
|---|---|---|
| 7 | NOISE | the noise waveform |
| 6 | PULSE | the pulse waveform |
| 5 | SAW | the sawtooth |
| 4 | TRIANGLE | the triangle |
| 3 | TEST | 1: the phase counter held at 0, the pulse high, the noise reset |
| 2 | RING | 1: the triangle ring-modulated by the partner voice |
| 1 | SYNC | 1: the phase counter restarted by the partner voice |
| 0 | GATE | 0 to 1 starts the attack; 1 to 0 starts the release |

#### Reset

Every register `$00`: a /RESET pulls the SIDs' /RES. POTX and POTY read `$FF`, OSC3 and ENV3 `$00`. After a boot
with the splash, the boot sound has them for 5 s, then leaves `$00–$18` at `$00`. After Break, QUIET leaves them
at `$00`.

#### Status

**emulated**

#### Notes

The card's 6510 sees SID A at `$D400`. [The SIDs](#the-sids) and [A voice](#a-voice) say what the registers do. On
a C64 the write-only registers don't read back, so code written for one doesn't expect them to.

<a id="reg-c520"></a>
### $C520 SOUND_SIDB: SID B's registers

#### Equates

`SOUND_SIDB`

#### Module

A, the sound card

#### Access

R/W, 29 bytes, `$C520–$C53C`, as [SID A](#reg-c500). A write is dropped while CTRL's RUN is set. `$C53D–$C53F`
read `$FF` and ignore writes.

#### Bits

As [SID A's](#reg-c500), at `$C520` plus the offset.

#### Reset

As SID A's.

#### Status

**emulated**

#### Notes

The card's 6510 sees SID B at `$D420`, or at `$D500` with CTRL's bit 3. With CTRL's BOTH, every write to SID A
lands here too, from either side; a write here goes to SID B only.

<a id="reg-c540"></a>
### $C540 SOUND_CTRL: the card's control

#### Equates

`SOUND_CTRL`

#### Module

A, the sound card

#### Access

R/W. Writing RESET as 1 resets the card's 6510. Changing NTSC changes the SID clock at once.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–5 | | kept as written; do nothing |
| 4 | BOTH | 1: a write to SID A goes to SID B too, from either side |
| 3 | | 1: SID B at `$D500` in the 6510's map; 0: at `$D420` |
| 2 | RESET | write 1: reset the 6510 from its vector at `$FFFC`, with its port, the CIA and the frame. A strobe: reads 0 |
| 1 | RUN | 1: the 6510 runs, and owns the SIDs |
| 0 | NTSC | 1: the SID clock is 1,022,727 Hz; 0: 985,248 Hz |

#### Reset

`$00`: the 6510 stopped, the PAL clock. After a boot with the splash, `$03` while the boot sound plays, and `$01`
once it has stopped itself, 5 s in. QUIET clears RUN and keeps the rest, so after Break it is `$01`. With no video
card there is no boot sound, and it stays `$00`.

#### Status

**emulated**

#### Notes

`SOUND_NTSC` (`$01`), `SOUND_RUN` (`$02`) and `SOUND_RESET` (`$04`) are the bits; bits 3 and 4 have no equates.
The 6510 clears RUN itself by writing `$DE02`. Start a program with RUN and RESET together
([above](#the-cards-6510)).

<a id="reg-c541"></a>
### $C541 SOUND_STATUS: the mailbox's flags

#### Equates

`SOUND_STATUS`

#### Module

A, the sound card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | IRQ | 1: the card is interrupting: IEN's bit 2, and a byte in MBIN |
| 6–3 | | 0 |
| 2 | | 1: a byte from the card waits in MBIN |
| 1 | | 1: the byte written to MBOUT hasn't been taken by the card yet |
| 0 | BUSY | 0 in the emulator, which forwards SID writes at once |

#### Reset

`$00`: both boxes empty.

#### Status

**emulated**

#### Notes

hw/design.md has BUSY set while a SID write is pending; the emulator never sets it ([Bus timing](#bus-timing)).

<a id="reg-c542"></a>
### $C542 SOUND_IEN: the card's interrupt enable

#### Equates

`SOUND_IEN`

#### Module

A, the sound card

#### Access

R/W. Only bit 2 is kept.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–3 | | 0 |
| 2 | | 1: a byte waiting in MBIN interrupts, on IRQH bit 2 |
| 1–0 | | 0 |

#### Reset

`$00`. The BIOS never sets it, and QUIET writes `$00`.

#### Status

**emulated**

#### Notes

The interrupt lasts until MBIN is read. See [IRQH](11-north-bridge-and-rtc.md#reg-c689) and
[chapter 4](04-interrupts.md#finding-the-source-irql-and-irqh).

<a id="reg-c543"></a>
### $C543 SOUND_RAMADDR: the card's RAM address

#### Equates

`SOUND_RAMADDR`

#### Module

A, the sound card

#### Access

R/W, 2 bytes, low byte first. Each RAMDATA access, read or write, by the CPU or by DMA, moves it on by 1.

#### Bits

All: the next address in the card's 64 KB. It wraps from `$FFFF` to `$0000`.

#### Reset

`$00 $00`. After a boot with the splash, `$FFFE`, just past the reset vector `splash.s` wrote. Break leaves it.

#### Status

**emulated**

<a id="reg-c545"></a>
### $C545 SOUND_RAMDATA: the card's RAM

#### Equates

`SOUND_RAMDATA`

#### Module

A, the sound card

#### Access

R/W. A read gives the byte at RAMADDR and a write stores one there; either moves RAMADDR on. It works whether
the 6510 runs or not. The card asserts DRQ2 always.

#### Bits

All: a byte of the card's RAM.

#### Reset

Undefined: the card's RAM keeps its contents through a /RESET. The emulator's is all `$00` at power-on.

#### Status

**emulated**

#### Notes

`backplane.machine` wires the card to DMA channel 2, fly-by, one cycle a byte
([chapter 5](05-dma.md#transfers-for-a-card), [channel 2's address](05-dma.md#reg-c420)). Since DRQ2 never drops,
channel 2 runs as soon as it is enabled, at the pace its XFER gives. With the 6510 running, a write here can change
the code under it.

<a id="reg-c546"></a>
### $C546 SOUND_MBOUT: the mailbox to the card

#### Equates

`SOUND_MBOUT`

#### Module

A, the sound card

#### Access

W. Sets STATUS's bit 1 until the 6510 reads `$DE00`. A second write before then replaces the byte. Reads give
`$FF`.

#### Bits

All: a byte for the program on the card.

#### Reset

Empty: STATUS's bit 1 clear, and `$00` the byte the 6510 would read.

#### Status

**emulated**

#### Notes

The 6510 sees the box's state as `$DE01`'s bit 0. Reading `$DE00` with the box empty gives the last byte again.

<a id="reg-c547"></a>
### $C547 SOUND_MBIN: the mailbox from the card

#### Equates

`SOUND_MBIN`

#### Module

A, the sound card

#### Access

R. A read empties the box: it clears STATUS's bit 2, and with it the interrupt. Writes are ignored.

#### Bits

All: the byte the 6510 last wrote to `$DE00`. With the box empty, the same byte again.

#### Reset

`$00`, empty.

#### Status

**emulated**

#### Notes

A second byte from the 6510 before the host reads replaces the first; `$DE01`'s bit 1 tells the 6510 its byte
hasn't been taken. The boot sound sends nothing. The emulator's `.sid` stub sends the play call's number after each
call ([above](#playing-a-sid-file)).

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
| 3–0 | REV | the revision: 1 |

#### Reset

`$A1`.

#### Status

**emulated**

#### Notes

`SOUND_ID_V1` (`$A1`) is the value. The BIOS checks it before it touches the card. The offsets with no register,
`$48–$7E`, read `$FF` and ignore writes.
