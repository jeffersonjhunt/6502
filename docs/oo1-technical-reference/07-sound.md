# 7 Sound

The sound card (module A, `$C500`): two SIDs and a 6510 of its own. Revision 2 adds a PCM stream and an ES5503
([below](#revision-2)); the emulator plays both, as far as the spec's steps so far go.

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

<!-- listing: ../../backplane/bios/bios.s:201-220 -->
```
@sound:         LDA     SOUND_ID
                AND     #$F0           ; any revision: each has CTRL, IEN and the SIDs
                CMP     #SOUND_TYPE
                BNE     @panel
                LDA     SOUND_CTRL
                AND     #<~SOUND_RUN   ; the 6510 stopped, so the SIDs take writes
                STA     SOUND_CTRL
                STZ     SOUND_IEN
                LDX     #SOUND_SIDS - 1
@sid:           STZ     SOUND,X        ; every SID register: gates and volume off
                DEX
                BPL     @sid
                LDA     SOUND_ID
                CMP     #SOUND_ID_V2   ; revision 2 and on: the PCM stream and the ES5503 too
                BCC     @panel
                LDA     #SOUND_PCM_FLUSH
                STA     SOUND_PCMCTL   ; RUN, PAUSE, EOF off and the FIFO emptied: the stream idle
                LDA     SOUND_DOCCTL
                AND     #<~SOUND_DOC_RUN ; the ES5503 frozen where it was; its route, AUTOINC, RAM kept
                STA     SOUND_DOCCTL
```

`SOUND_SIDS` is `$3D`, so the loop covers `$C500–$C53C`: SID A, the gap between and SID B. The gap's writes are
ignored. QUIET keeps the rest of CTRL, NTSC among it, and leaves the card's RAM, RAMADDR and the mailbox alone.
It takes the card at any revision, by the ID's high nibble, as `splash.s` does. Since BIOS 0.12, when the ID is
`$A2` or more it also writes FLUSH alone to [PCMCTL](#reg-c548), which stops the PCM stream and empties its FIFO,
and clears RUN in [DOCCTL](#reg-c55e), keeping its other bits, which freezes the ES5503: nothing on the card plays
on behind the monitor. Revision 1 has nothing at `$48–$63`, and QUIET writes
nothing there. BIOS 0.11 left revision 2's registers alone.

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
a one-shot program hands the SIDs back when it is done. On revision 2, a write with bit 0 set clears DOCCTL RUN too,
as the host's write does, so the program hands back the ES5503 as well; with bit 0 clear the ES5503 is left as it is. A /RESET stops it too. The card's RAM keeps its contents
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
| `$DE02` | STOP: any write clears CTRL's RUN. On revision 2, bit 0 set clears DOCCTL RUN too (the ES5503 frozen, as the host's write does); bit 0 clear leaves it |
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
card answer. `splash.s` clears CTRL and loads a program, under 2 KB, at `$1000` through RAMDATA: on revision 2 or
later, a player for the ES5503, after the player's samples, 80 KB from flash, into wave RAM through DOCDATA (about
0.13 s), and DOCCTL RUN set, so the 6510 has the chip at `$DF00`; on revision 1, a tune for the SIDs. It points the
card's reset vector at the program's entry, `$1006`. Then it starts the splash, waits for the vertical blank the
splash begins on, and writes CTRL:

<!-- listing: ../../backplane/bios/splash.s:139-160 -->
```
@vector:        LDA     #$FC           ; the card's reset vector: the player's boot entry
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
                AND     #$F0
                CMP     #SOUND_TYPE
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
by about 3 s on the ES5503 (about 2.4 s on the SIDs), but the program runs to the end. Then it silences what it played, the SIDs' registers zeroed or the
ES5503's oscillators halted, and writes `$DE02`; on revision 2 it writes 1 there, which clears DOCCTL RUN too. It
sends nothing through the mailbox. So under the monitor CTRL reads `$03` for the first 5 s and `$01` after, and
RAMADDR reads `$FFFE`. A program that takes the card in the first 5 s stops the 6510 and silences the SIDs first,
and on revision 2 clears DOCCTL RUN, as QUIET does. Break stops the boot sound early.

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

### OOLIB's music driver

Status: **emulated**

OOLIB's music library ([the Programmer's Guide, chapter 8](../oodos-programmers-guide/08-game-services.md#music))
plays notes through a driver of ours on the card's 6510, `oodos/lib/mudrv/mudrv.s`. The library loads it at
`$1000` of the card's RAM, with CIA 1's latch for a millisecond at `$1003` (984 on the PAL clock, 1,022 on NTSC), and
starts it there. `$1005` holds its version, 1. `$1006` counts the events it has dropped (held at 255), and
`$1007–$1009` count the milliseconds it has stepped.

Each millisecond, timer A's interrupt steps the six voices: SID A's at `$D400`, `$D407` and `$D40E`, then SID B's at
`$D420`, `$D427` and `$D42E`. Each voice has a queue of 32 entries. Between interrupts the main loop takes the host's
bytes from the mailbox and sends its own. A command is a byte, its high nibble the command and its low a voice
(0–5), then its operands:

| Bytes | Command |
|---|---|
| `$0v`, frequency (2), length ms (2), gate ms (2) | a note queued (frequency 0 a rest; gate 0 silent) |
| `$1v`, control bits, AD, SR, pulse width (2) | the voice's instrument, from its next note |
| `$2v` | the voice stopped: its queue emptied, its gate off (`$27`: all six) |
| `$3v`, m | marker m (0–15) queued |
| `$40`, `$50` | pause (both volumes 0), resume |
| `$6s`, n | SID s's volume (0 A, 1 B, 7 both) |
| `$70` | hello |

Its answers, one byte each, sent only once the host has taken the one before: `$01` hello's, `$1v` an entry finished,
`$2v` the voice's queue empty, `$3v` an entry refused (32 waiting), `$37` a command refused, and `$80` + v × 16 + m a
marker reached. Up to 64 wait on the card.

With six notes held the driver takes about 19% of the 6510's time. Six notes starting in the same millisecond take
about 1,140 clocks, so that step runs past its millisecond and the next starts late, but no step is lost.

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

## Revision 2

Status: **proposed**

Revision 2 ([hw/design.md, "Sound card revision 2"](../../hw/design.md#sound-card-revision-2), and the spec,
[hw/sound-rev2.md](../../hw/sound-rev2.md)) keeps everything above and adds, on the RP2350's second core and an
I2S DAC:

- **A PCM stream.** The host writes bytes into a 128 KB FIFO through PCMDATA, by the CPU or by DMA, and the card
  plays them on its own clock: raw PCM at 8,000 to 48,000 Hz, 8 or 16 bits, mono or stereo, or MP3. PCMCTL starts
  and stops it, STATE says how it is going, POS counts the frames played, and FREE says how much room is left.
- **A master volume, an EQ and a vis page.** VOLL and VOLR scale the PCM path and the SIDs' volume; a preamp and
  ten bands of EQ; a page of spectrum, VU and recent samples to draw.
- **An ES5503,** the Apple IIgs's DOC: 32 oscillators playing 8-bit samples from 128 KB of wave RAM of its own. The
  host reaches its registers and RAM through DOCCTL, DOCDATA, DOCADDR and DOCBANK; while its RUN is set, the card's
  6510 sees its registers at `$DF00–$DFFF`. Its channels fold to stereo by their low bit, even left and odd right.
  Unlike the IIgs's Sound GLU, the port needs no dummy read before a DOCDATA read: the value is there at once.
- **DRQSEL** picks the register the card's DRQ and fly-by DMA serve: RAMDATA, PCMDATA or DOCDATA.

PCM and the ES5503 don't run at once; the SIDs may always run beside either.

**Taking the card at any revision.** The ID's high nibble, `$A` (`SOUND_TYPE` is `$A0`), says it is the sound card,
and the low nibble its revision. Software that uses only the SIDs, the 6510 and the mailbox tests the high nibble,
as the BIOS and OODOS do. Software that uses revision 2's registers tests the low nibble for 2 or more first, or
CODECS: on revision 1, `$48–$63` read `$FF` and ignore writes.

### In the emulator so far

Status: **emulated**

The registers are in: each holds what is written and reads it back, with the reset values below, and /RESET and
CTRL RESET treat them as the design says. The raw PCM stream runs: the FIFO, PCMDATA by the CPU or by demand-mode
DMA through DRQSEL 1, STATE's flow, POS and FREE. It keeps the host's clock, whether or not the emulator is making
sound, so a program sees the same STATE and POS at any audio rate. The stream is mixed with the SIDs (the mix,
below), and plays MP3 too (below), through the EQ, the vis page and the volumes (below). The ES5503 plays
while DOCCTL's RUN is set (the spec's steps 6 and 7): the card's 6510 reaches its registers at `$DF00–$DFFF`,
its interrupt goes to the host or the 6510, and its sound goes onto the bus at 48 kHz, through the same EQ, vis
page and volumes, and into the mix. PCM and the ES5503 exclude each other (STATE `$82`). The BIOS's QUIET stops
both on Break, BRK and the demo's end, since BIOS 0.12 ([Silencing them](#silencing-them)).

**The mix.** While the stream plays or runs dry, its frames, at the stream's rate, are resampled to the
emulator's audio rate by linear interpolation, scaled by 0.5 against the SIDs' output (the emulator's
`MIX_DIGITAL`: the real card's analog stage will set the true balance), added to the SIDs' samples and clipped to
-1..1. Buffering, paused, ended or idle, the stream adds nothing, and the SIDs' samples are exactly what they were
without it.

Each run of the stream lines up with the SIDs' samples made at its start, so it lands on time however long
since the sound was last collected. A run starts when the stream starts playing, after a pause, at a FLUSH seek
or a new RUN (even when it is playing again within the same write), and after a tick of over two seconds, and
each starts behind a lead of silence: (audio rate / stream rate, rounded up) + 1 samples. A stream frame's
samples come in a burst while each SID sample takes one; with the lead, within a run the stream never runs
short, however the sound is collected, so no sample goes without the stream and none takes a later part of it.
Each new run costs one lead of silence at its start.

The stream's delay is constant within a run. A frame's level comes out about the lead after the frame falls due: 2
samples for equal rates, 7 for 8 kHz into 48 kHz, 13 for 8 kHz into 96 kHz. Counted from the start, the SIDs'
samples made at RUN to the first full-level sample, which takes in the first frame's own period, it is 3
samples for equal rates (62.5 µs at 48 kHz) and 13 for 8 kHz into 48 kHz.

Interpolating makes the stream at most sinc²(f / rate) quieter at frequency f: 0.7% at 1 kHz from 22,050 Hz,
none when the rates are equal. With nobody collecting, once a second and a half of the stream waits, the SIDs
are brought up to date, which takes it, as a SID access would. Of a gap over two seconds only the last second
is made, and the stream's samples for the part skipped go with it, so a stream over before the gap doesn't play
after it. At audio rate 0 nothing is made.

### The MP3 codec

Status: **emulated**

With CODEC 1 at RUN, the FIFO holds an MP3 stream, layer III of MPEG-1, -2 or -2.5, and the emulator decodes it
with minimp3 (CC0, vendored in `emulator/third_party/minimp3`). RATE's range isn't checked: the rate and the
channels come with each frame, and the stream follows them, at 8,000 to 48,000 Hz. Output is 16-bit, a mono
frame on both sides.

- **As output needs it.** From the second frame on, a frame is decoded when its first sample falls due; the first
  decodes when buffering ends (see The start). A frame's bytes count against the FIFO (and FREE) until the
  decoder takes them.
- **Whole frames, and minimp3's own sync.** In step with the stream, a frame is decoded once all of it, and the
  next frame's header, are in the FIFO. Out of step (at the start, or after junk), a frame is taken by minimp3's
  own rule: ten frames in step after it, or the data's end after at least one; until the run decides, the card
  waits, dry. An ID3v2 tag ends a run as the data's end does, so a short clip before a tag plays. So a stream fed
  while it plays, in any pieces, with EOF written early or late, plays exactly what the same bytes written at once
  play, and that is minimp3's decoding of the whole data, but for three cases where the card does better: tags
  (below), a last frame with something after it (below), and a lone last frame with junk before it, which the card
  plays and minimp3 drops.
- **The start.** An MP3 buffers on past 4 KB, if it must, until its first frame has decoded: its run of ten has
  decided, and it gives the rate. That is buffering, not an underrun. A program that feeds in real time starts
  later than the 4 KB start by the time that run takes to come in: fed at 1.02 times the stream's rate, about
  135 ms at 320 kbps (235 ms from RUN), 5 ms at 128 kbps, and 97 ms for VBR -V0. It's the cost of deciding the sync
  as minimp3 does. Without EOF, a stream of fewer than eleven frames (or the first ten after
  junk) plays only once more frames come or EOF is written: set EOF after a short clip.
- **Layer III, sized.** Only frames whose length their header gives are taken. Free format (bitrate index 0,
  its length nowhere in its header) is refused, and so are layer I and II frames (MP1, MP2): their bytes are
  junk, and a stream of them ends in `$81`. So does a stream of layer III frames that decode to nothing.
- **Junk.** Other bytes are skipped, and the decoder finds its place again at the next frame, as minimp3 does.
  64 KB looked through since the last frame that made samples is `$81`.
- **ID3v2 tags** are skipped by the size their header declares (its footer too, if flagged), wherever they are
  met: before the first frame, straight after a frame, or after junk, whatever their body holds (cover art's
  frame-like bytes included). They don't count toward the 64 KB, so a tag with cover art is no error; but 8 MB
  of tags with no frame between is `$81`. A frame just before a tag plays by its own length, and the decoder
  stays in step across the tag. A header must be a real one: version 2.2 to 2.4, only the flags its version
  defines, a size under 8 MB in syncsafe bytes; anything else is junk.
- **The end.** A frame with nothing frame-like and no tag after it, to EOF or for 32 KB, plays by the length its
  own header gives, whatever follows ignored: CP/M's `$1A` padding to the end of a 128-byte record, an ID3v1 or
  APE trailer, or anything else. (Trailing `$1A`s aren't stripped: a frame's own data can end in them.) Without
  EOF and with less than 32 KB after it, it waits for more, so when EOF is written doesn't change what plays.
  Where another stream's header follows (a change of rate or channels), the decoder finds its place again there,
  as minimp3 does.
- **Waiting costs little.** A search that finds nothing costs a few header checks at most: the look past a frame
  resumes where it stopped, junk is dropped as it's found, and a run is at most ten headers.
- **FLUSH** empties the decoder too, so a seek starts it afresh.
- **KBPS** reads the frame playing's bitrate (never 0 while an MP3 plays: free format is refused).

**The stream's timing,** where the design leaves it open:

- Playing starts at 4 KB in the FIFO, or as soon as EOF is set. The first frame is due a whole frame's time
  later, at RATE on the host's clock, and every frame after it at the same pace.
- A frame takes 1, 2 or 4 bytes from the FIFO, and only whole frames are taken. A frame split across two writes
  waits for its last byte. With EOF set, a partial frame left at the end is dropped when the stream ends, never
  played, so the FIFO is empty again and the next stream starts on a frame.
- The FIFO runs dry when a frame falls due and none is there. Without EOF that is underrun: silence, until a whole
  frame arrives, which plays at the next frame due with no new buffering. With EOF it is ended. A FIFO emptied and
  filled again before the next frame is due hasn't underrun.
- Releasing PAUSE goes back to the state it paused: buffering, playing or underrun. An underrun paused is the
  same dry spell, not counted again. Ended lasts until RUN is cleared.
- FLUSH with RUN set is a seek: the FIFO empties and the stream buffers again, to 4 KB or to EOF, with no underrun
  counted; POS runs on from where it was. Paused, the seek takes effect when PAUSE is released.

## Under OODOS

Status: **built**

Since OOLIB's L3 ([hw/oolib.md, "The plan"](../../hw/oolib.md#the-plan)) OODOS's function 82 hands the card's parts
out, one program at a time: the 6510 and the SIDs, the stream, the ES5503
([the Programmer's Guide, chapter 8](../oodos-programmers-guide/08-game-services.md#sound)). A program writes a part's
registers itself once it holds it; what OODOS keeps is DMA channel 2, the card's DRQSEL and IEN, the master
volumes (VOLL and VOLR, set through function 82's `SN_VOLUME`, since OODOS 0.6), and the warm boot.

- **Loads.** The card's RAM (DRQSEL 0) and the ES5503's wave RAM (DRQSEL 2) are loaded by channel 2, memory to the
  card in demand mode, 4,096 bytes a piece: RAMDATA and DOCDATA hold DRQ always, so each piece holds the CPU 4,096
  cycles, under a tick of the millisecond counter, and the tick is taken between pieces. For a wave load OODOS sets
  DOCCTL's RAM and AUTOINC (RUN and the route kept), DOCADDR and DOCBANK, holds IEN bit 3 off (in OODOS's own copy
  of IEN too, so turning a stray off doesn't turn it back on), and puts DOCCTL and IEN back after; DRQSEL goes back
  to 0. A load that would run past the RAM's end is refused rather than wrapped.
- **The stream.** DRQSEL 1, and channel 2 in demand mode with MODE's IRQ set, from two buffers of the program's, 4 KB
  a piece: at each terminal count OODOS's handler starts the next piece, or the next buffer, and marks a buffer
  sent. With the FIFO full the card drops DRQ and the piece waits; with room it runs at a byte a cycle, the CPU held
  4,096 cycles at most. While a stream buffer is going to the card the channel is the stream's, and a load is
  refused, busy.
- **IEN:** bit 2 while the 6510 is claimed and the program has an event area of layout 2 open (each byte from MBIN
  becomes an event in its queue); bit 3 while an ES5503 hook is set. The handler for bit 3 saves DOCCTL, DOCADDR and
  DOCBANK, reads the ES5503's `$E0` with DOCCTL on the registers and DOCADDR at `$E0` (bit 7 set: nothing pending,
  so not the chip's), calls the hook with the port so, and puts all three back as it found them after the hook
  returns. Writing DOCCTL back clears a STATE of `$82` (PCM and the ES5503 both asked to run) that was waiting: a
  program that wants to see a refused RUN reads STATE straight after its own DOCCTL or PCMCTL write.
  `SN_SEND` writes MBOUT only while STATUS bit 1 is clear, so OODOS never replaces a byte the 6510 hasn't taken.
  OODOS's turning off of strays leaves these bits, and channel 2 while a stream buffer moves on it, alone.
- **Hold and go** (`SN_HOLD`, `SN_GO`, since OODOS 0.6). A hold writes CTRL with RUN clear and RESET clear, so the
  6510 stops between instructions with its registers and the card's RAM as they are. RUN goes off first, since the
  card drops the host's SID writes while the 6510 runs. Then OODOS reads both SIDs' `$18` (as written, whoever wrote
  it), keeps the two bytes, and writes each back with bits 0–3 (the volume) clear and bits 4–7 kept (4–6 the filter's outputs, 7 voice 3 off); both are read before either is
  written, as with CTRL's BOTH a write to SID A reaches SID B. VOLL and VOLR are left alone: they also scale the
  stream and the ES5503, which go on playing. A go writes CTRL with RUN off (in case the program set it while held), then SID
  A's kept byte, then SID B's, then CTRL with RUN set and RESET clear, so the 6510 resumes at the instruction it stopped before.
- **The warm boot** does to the card what QUIET does (CTRL's RUN off, the SIDs' registers 0, on revision 2 PCMCTL
  FLUSH alone and DOCCTL's RUN off), and aborts channel 2 with its MODE 0 and its terminal count cleared, puts
  DRQSEL to 0 and IEN to 0: at every program's end, whether a program claimed the card or not. So a program that
  used the card behind OODOS's back leaves nothing playing either. On revision 2 it also writes VOLL and VOLR
  `$FF` (since OODOS 0.6), and it forgets a hold.

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
| 7 | IRQ | 1: the card is interrupting: IEN's bit 2 and a byte in MBIN, or (revision 2) IEN's bit 3 and the ES5503's interrupt, routed to the host |
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

R/W. Bit 2 is kept, and on revision 2 bit 3 too.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–4 | | 0 |
| 3 | | 1 (revision 2): the ES5503's interrupt, when DOCCTL's bit 2 is clear, interrupts on IRQH bit 2 |
| 2 | | 1: a byte waiting in MBIN interrupts, on IRQH bit 2 |
| 1–0 | | 0 |

#### Reset

`$00`. The BIOS never sets it, and QUIET writes `$00`. Under OODOS ([above](#under-oodos)) bits 2 and 3 are OODOS's,
set while a program has claimed what they serve, and the warm boot writes `$00`.

#### Status

**emulated**

#### Notes

The mailbox's interrupt lasts until MBIN is read; the ES5503's until its `$E0` is read (through DOCDATA, or by
the 6510 at `$DFE0`) and no other oscillator is pending. See [IRQH](11-north-bridge-and-rtc.md#reg-c689) and
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
the 6510 runs or not. With DRQSEL 0, its reset value ([`$C563`](#reg-c563)), the card asserts DRQ2 always and
fly-by DMA comes here.

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

<a id="reg-c548"></a>
### $C548 SOUND_PCMCTL: the PCM stream's control

#### Equates

`SOUND_PCMCTL`

#### Module

A, the sound card

#### Access

R/W. FLUSH is a strobe: writing it 1 empties the FIFO and the decoder, and it reads 0.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | | reserved: write 0 |
| 6–4 | | read 0 |
| 3 | EOF | 1: no more data is coming, so play what is left and end |
| 2 | FLUSH | write 1: empty the FIFO and the decoder. A strobe: reads 0 |
| 1 | PAUSE | 1: hold where it is |
| 0 | RUN | 1: play; 0: stop, keeping the FIFO unless FLUSH is written too |

#### Reset

`$00`. CTRL RESET leaves it. On revision 2 QUIET (Break, BRK and the demo's end) writes `$04`, FLUSH alone, since BIOS 0.12: it reads `$00` after, with the FIFO empty.

#### Status

**emulated**

#### Notes

`SOUND_PCM_RUN` (`$01`), `SOUND_PCM_PAUSE` (`$02`), `SOUND_PCM_FLUSH` (`$04`) and `SOUND_PCM_EOF` (`$08`) are the bits. RUN takes RATE and PCMFMT and starts buffering; playing begins at 4 KB in the FIFO or at EOF ([the stream's timing](#in-the-emulator-so-far)). FLUSH empties the FIFO whether RUN is set or not; with RUN set it is a seek, and the stream buffers again. Clearing RUN keeps the FIFO. The BIOS's QUIET writes `$04` here on revision 2 (Break, BRK and the demo's end, since BIOS 0.12): RUN, PAUSE and EOF clear and the FIFO empties, so a stream stops, STATE reads 0, FREE reads the whole FIFO, and a new stream after Break starts clean.

<a id="reg-c549"></a>
### $C549 SOUND_CODEC: the stream's codec

#### Equates

`SOUND_CODEC`

#### Module

A, the sound card

#### Access

R/W. A write while PCMCTL's RUN is set is ignored.

#### Bits

All: 0 raw PCM, 1 MP3, 2 reserved for MOD. The byte is kept as written.

#### Reset

`$00`, raw PCM. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

[CODECS](#reg-c55d) says which codecs the card has. RUN takes CODEC; a CODEC of 2 or more is `$80` in STATE
then. MP3 is decoded by [minimp3](https://github.com/lieff/minimp3) in the emulator ([the MP3
codec](#the-mp3-codec), above).

<a id="reg-c54a"></a>
### $C54A SOUND_RATE: raw PCM's rate

#### Equates

`SOUND_RATE`

#### Module

A, the sound card

#### Access

R/W, 2 bytes, low byte first.

#### Bits

All: the rate in Hz, 8,000 to 48,000. While the stream runs it reads back the rate playing: an MP3's, from its
frames (0 until the first decodes).

#### Reset

`$00 $00`. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

A RATE outside the range when RUN is set is error `$80` in STATE. RUN takes RATE, and while it runs RATE reads back the rate it took, whatever is written after.

<a id="reg-c54c"></a>
### $C54C SOUND_PCMFMT: raw PCM's format

#### Equates

`SOUND_PCMFMT`

#### Module

A, the sound card

#### Access

R/W. Bits 7–2 read 0.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–2 | | read 0 |
| 1 | 16BIT | 1: 16-bit signed, low byte first; 0: 8-bit unsigned |
| 0 | STEREO | 1: stereo, left then right; 0: mono |

#### Reset

`$00`: 8-bit mono. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

RUN takes PCMFMT, and while it runs PCMFMT reads back the format it took, whatever is written after. An MP3
reads 16-bit, and stereo as its frame is (PCMFMT as written doesn't matter to it).

<a id="reg-c54d"></a>
### $C54D SOUND_FREE: the FIFO's room

#### Equates

`SOUND_FREE`

#### Module

A, the sound card

#### Access

R, 2 bytes, low byte first. Reading `$C54D` latches `$C54E`, so the two bytes are of one moment. Writes are ignored.

#### Bits

All: the FIFO's free space in 256-byte units, rounded down. 512 (`$0200`) is all 128 KB.

#### Reset

`$00 $02`: the FIFO empty.

#### Status

**emulated**

#### Notes

Read the low byte first: until the next `$C54D` read, `$C54E` keeps the high byte of that moment while the card plays on. Bytes count against the FIFO until a frame takes them, a partial frame's too.

<a id="reg-c54f"></a>
### $C54F SOUND_PCMDATA: the stream

#### Equates

`SOUND_PCMDATA`

#### Module

A, the sound card

#### Access

W. A byte written with the FIFO full is dropped. Reads give `$FF`.

#### Bits

All: the stream's next byte.

#### Reset

Nothing: the FIFO is empty.

#### Status

**emulated**

#### Notes

With DRQSEL 1 the card's DRQ serves PCMDATA, holding DRQ only while the FIFO has room, so demand-mode DMA stops by itself when the FIFO is full and goes on as the card plays. A stream longer than the FIFO can so be fed by one channel, loaded again at each terminal count.

<a id="reg-c550"></a>
### $C550 SOUND_STATE: the stream's state

#### Equates

`SOUND_STATE`

#### Module

A, the sound card

#### Access

R. Writes are ignored.

#### Bits

All:

| Value | State |
|---|---|
| `$00` | idle |
| `$01` | buffering |
| `$02` | playing |
| `$03` | paused |
| `$04` | underrun: the FIFO ran dry before EOF; silence until data comes |
| `$05` | ended: the FIFO ran dry after EOF |
| `$80` | bad RATE or PCMFMT when RUN was set |
| `$81` | no MP3 frame found in 64 KB of data |
| `$82` | PCM and the ES5503 both asked to run |

#### Reset

`$00`, idle. On revision 2 it reads `$00` after Break, BRK and the demo's end too, since BIOS 0.12: QUIET's PCMCTL and DOCCTL writes stop both sources and clear a `$82`.

#### Status

**emulated**

#### Notes

`$80` also stands for a CODEC of 2 or more, and for an MP3 frame above 48 kHz, which MPEG-1, -2 and -2.5 can't
give. `$81` counts the bytes looked through since the last frame found. An error lasts until RUN is cleared.
`$82` is different: PCMCTL's RUN written while the ES5503 runs, or DOCCTL's while PCM runs, is refused (the RUN
bit isn't kept, so the register reads it 0) and STATE reads `$82` until PCMCTL or DOCCTL is next written. That
write clears it, and sets it again only if it is refused too. The source already running plays on underneath.

<a id="reg-c551"></a>
### $C551 SOUND_POS: the frames played

#### Equates

`SOUND_POS`

#### Module

A, the sound card

#### Access

R/W, 4 bytes, low byte first. Reading `$C551` latches `$C552–$C554`, so the four bytes are of one moment. Any write to `$C551` zeroes it; writes to the other three are ignored.

#### Bits

All: the frames played since POS was last zeroed, 32 bits.

#### Reset

`$00 $00 $00 $00`. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

Read the low byte first: until the next `$C551` read, `$C552–$C554` keep that moment's bytes while POS counts on. Silence while underrun isn't counted, nor time paused.

<a id="reg-c555"></a>
### $C555 SOUND_KBPS: the MP3's bitrate

#### Equates

`SOUND_KBPS`

#### Module

A, the sound card

#### Access

R, 2 bytes, low byte first. Writes are ignored.

#### Bits

All: the MP3 frame's bitrate in kbit/s; 0 for raw PCM.

#### Reset

`$00 $00`.

#### Status

**emulated**

#### Notes

The frame playing's, so a VBR stream's changes as it plays.

<a id="reg-c557"></a>
### $C557 SOUND_VOLL: the left master volume

#### Equates

`SOUND_VOLL`

#### Module

A, the sound card

#### Access

R/W.

#### Bits

All: 0 silent to 255 unity.

#### Reset

`$FF`. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

VOLL scales the stream's left side after the EQ and the vis page: 255 is unity, and below it each sample is multiplied by VOLL / 255, rounded to the nearest (half away from zero), so 0 is silence. It scales SID A too: a write to SID A's `$18` reaches the chip with its volume nibble replaced by round(nibble × VOLL / 255), while reading it gives what was written. A write to VOLL reaches the chip at once. At 255 nothing changes.

<a id="reg-c558"></a>
### $C558 SOUND_VOLR: the right master volume

#### Equates

`SOUND_VOLR`

#### Module

A, the sound card

#### Access

R/W.

#### Bits

All: 0 silent to 255 unity.

#### Reset

`$FF`. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

As [VOLL](#reg-c557), for the right side and SID B.

<a id="reg-c559"></a>
### $C559 SOUND_EQIDX: the EQ's index

#### Equates

`SOUND_EQIDX`

#### Module

A, the sound card

#### Access

R/W. Each access to EQVAL, read or write, moves it on by 1 in bits 0–6; bit 7 stays.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | ENABLE | 1: EQVAL is the EQ's enable, whatever bits 6–0 say |
| 6–0 | | 0: the preamp; 1–10: the bands, 60 Hz to 16 kHz. Past 10, nothing |

#### Reset

`$00`, the preamp. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

So a program writes the preamp and the ten bands with EQIDX 0 and eleven EQVAL writes. The bands are centred at 60, 170, 310 and 600 Hz and 1, 3, 6, 12, 14 and 16 kHz ([hw/sound-rev2.md](../../hw/sound-rev2.md#signal-path)).

<a id="reg-c55a"></a>
### $C55A SOUND_EQVAL: the EQ's gain or enable

#### Equates

`SOUND_EQVAL`

#### Module

A, the sound card

#### Access

R/W, at EQIDX. A read or a write moves EQIDX on.

#### Bits

All: with EQIDX's bit 7 clear, the gain in signed half-dB, -24 to 24 (-12 to +12 dB): a write outside that is clamped. With bit 7 set, the EQ's enable: bit 0, 1 on. With EQIDX past 10, reads 0 and takes no write.

#### Reset

`$00`: every gain 0 dB, and the EQ off. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

With the EQ off the path is bit for bit as it came in, and so it is on with every gain 0. On, the preamp scales both sides by its gain, then each band is a peaking filter at its centre (the cookbook's, Q 1.41), on each side. A band at or above 0.45 of the stream's rate is bypassed: at 8 kHz, the bands from 6 kHz up. A gain or the enable written takes effect from the next frame; the filters start from silence at RUN and at FLUSH. The result is rounded to the nearest sample and clipped at full scale, so a big boost can clip: lower the preamp for it. The EQ is the same on every host, bit for bit.

<a id="reg-c55b"></a>
### $C55B SOUND_VISIDX: the vis page's index

#### Equates

`SOUND_VISIDX`

#### Module

A, the sound card

#### Access

R/W. Writing 0 takes a new snapshot of the vis page. Each VISDATA read moves it on, wrapping at 256.

#### Bits

All: the next vis page byte VISDATA reads.

#### Reset

`$00`. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

The page has 98 bytes: 0–31 the spectrum's bands, 32–33 the VU, left and right, 34–97 the last 64 samples, unsigned with `$80` zero. Bands and VU are 0 to 255 for -60 to 0 dBFS. The SIDs aren't in it.

<a id="reg-c55c"></a>
### $C55C SOUND_VISDATA: the vis page's byte

#### Equates

`SOUND_VISDATA`

#### Module

A, the sound card

#### Access

R. A read moves VISIDX on. Writes are ignored.

#### Bits

All: the vis page's byte at VISIDX; past 97, 0.

#### Reset

The page of silence: `$00` for 0–33, `$80` for 34–97.

#### Status

**emulated**

#### Notes

The page is the stream after the EQ and before the volumes, as it was when VISIDX was last written 0: the last 64 frames' times, at the stream's rate. While the stream doesn't play (paused, buffering, ended, or an error) those times are silence, as a dry spell's are, so the page goes quiet as the sound does; after a seek in an MP3, before its next frame gives the rate, they go at the rate it last played at. Clearing RUN drops the frames the page is made from, so the next snapshot's spectrum and samples are silence; its VU still has the peaks since the snapshot before, as after a pause or the end. The page already taken stays as it is until VISIDX is written 0, whatever else is written to PCMCTL. It is the same whatever the emulator's audio rate and however its time is ticked.

| Index | Contents |
| --- | --- |
| 0–31 | The spectrum: byte *k* is bin *k* + 1 of a 64-point FFT of the last 64 frames' mono mix ((L + R) / 2) with a Hann window, so it is centred at (*k* + 1) × rate / 64: at 48 kHz, 750 Hz to 24 kHz. A full-scale sine on a bin reads 0 dBFS. Byte 31 is the Nyquist bin, which has no phase: a full-scale cosine there reads 0 dBFS and a sine 0, and byte 30 takes as much from it |
| 32–33 | The VU: the left and right peaks since the snapshot before |
| 34–97 | The last 64 frames' mono mix, oldest first, the high byte unsigned: `$80` is zero |

The spectrum and the VU read 0 to 255 for -60 to 0 dBFS, 4.25 to a dB, rounded; -60 dB and below is 0. A half-scale sine reads 229.

<a id="reg-c55d"></a>
### $C55D SOUND_CODECS: the codecs the card has

#### Equates

`SOUND_CODECS`

#### Module

A, the sound card

#### Access

R. Writes are ignored.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7 | DOC | an ES5503 |
| 6–3 | | 0 |
| 2 | MOD | 0: reserved |
| 1 | MP3 | MP3 |
| 0 | PCM | raw PCM |

#### Reset

`$83`.

#### Status

**emulated**

#### Notes

Bit *n* set means [CODEC](#reg-c549) value *n* exists.

<a id="reg-c55e"></a>
### $C55E SOUND_DOCCTL: the ES5503's port control

#### Equates

`SOUND_DOCCTL`

#### Module

A, the sound card

#### Access

R/W. Bits 7, 4, 3 and 1 read 0.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 6 | RAM | 1: DOCDATA reaches wave RAM; 0: the ES5503's registers |
| 5 | AUTOINC | 1: each DOCDATA access moves DOCADDR on |
| 2 | IRQ_CARD | 1: the ES5503's interrupt goes to the card's 6510, not the host |
| 0 | RUN | 1: the ES5503 plays, and the 6510 sees its registers at `$DF00–$DFFF` |

#### Reset

`$00`. CTRL RESET leaves it. On revision 2 QUIET (Break, BRK and the demo's end) clears RUN and keeps bits 2, 5 and 6, since BIOS 0.12.

#### Status

**emulated**

#### Notes

`SOUND_DOC_RUN` (`$01`), `SOUND_DOC_IRQ_CARD` (`$04`), `SOUND_DOC_AUTOINC` (`$20`) and `SOUND_DOC_RAM` (`$40`) are the bits. Wave RAM and the registers are reachable whether RUN is set or not, and a DOCDATA read returns its value at once: no dummy read as on the IIgs. RUN plays the chip: its oscillators step on its own 7,159,090 Hz clock, the card's 6510 sees its registers at `$DF00–$DFFF` (with I/O banked in; `$FF` with RUN clear, as before), and its sound goes onto the bus at 48 kHz. Clearing RUN freezes the chip where it is; setting it again plays on from there. RUN is refused while PCM runs (see [STATE](#reg-c550)). Bit 2 sends its interrupt to the 6510's /IRQ instead of the host's IEN bit 3 route, and only while RUN is set: paused, the chip keeps the interrupt pending, and pulls /IRQ again when RUN is set. The BIOS's QUIET clears RUN here on revision 2 (Break, BRK and the demo's end, since BIOS 0.12), reading DOCCTL and writing it back: the chip freezes with its registers and wave RAM kept, and bits 2, 5 and 6 stay as they were. An interrupt pending in the chip stays pending on its route: with bit 2 the 6510's, which pulls nothing while RUN is clear; without it the host's, which IEN, cleared by QUIET, holds off. With bit 6 clear and DOCADDR `$E0`, a program reads DOCDATA to take it before it sets IEN bit 3 again: QUIET keeps bit 6, and with it set DOCDATA reads wave RAM instead.

<a id="reg-c55f"></a>
### $C55F SOUND_DOCDATA: the ES5503's register or wave RAM

#### Equates

`SOUND_DOCDATA`

#### Module

A, the sound card

#### Access

R/W, at DOCADDR. With DOCCTL's AUTOINC, a read or a write moves DOCADDR on. A read of the ES5503's interrupt register `$E0` acknowledges the interrupt it reports.

#### Bits

All: with DOCCTL's RAM, the wave RAM byte at DOCBANK and DOCADDR; without, the ES5503's register DOCADDR's low byte names.

#### Reset

The ES5503's register 0, frequency low: `$00` at power-on.

#### Status

**emulated**

#### Notes

The value is there at once: drop the IIgs's dummy read. The registers are the chip's: `$00–$1F` and `$20–$3F` frequency, `$40–$5F` volume, `$60–$7F` the last sample (read-only), `$80–$9F` the table pointer, `$A0–$BF` control, `$C0–$DF` the table's bank, size and resolution, `$E0` interrupt, `$E1` the oscillators enabled, `$E2` A/D, which reads `$80`. A /RESET halts every oscillator, clears pending interrupts and writes 0 to `$E1`; wave RAM keeps its contents. With DRQSEL 2, fly-by DMA comes here.

<a id="reg-c560"></a>
### $C560 SOUND_DOCADDR: the ES5503's address

#### Equates

`SOUND_DOCADDR`

#### Module

A, the sound card

#### Access

R/W, 2 bytes, low byte first. DOCCTL's AUTOINC moves it on after each DOCDATA access, carrying into DOCBANK.

#### Bits

All: the low byte names the ES5503's register; both bytes, with DOCBANK's bit 0, the wave RAM address.

#### Reset

`$00 $00`. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

Auto-increment moves the whole 17-bit address, registers or RAM, and wraps from `$1FFFF` to `$00000`.

<a id="reg-c562"></a>
### $C562 SOUND_DOCBANK: the ES5503's RAM bank

#### Equates

`SOUND_DOCBANK`

#### Module

A, the sound card

#### Access

R/W. Bits 7–1 read 0.

#### Bits

| Bit | Name | Meaning |
|---|---|---|
| 7–1 | | read 0 |
| 0 | A16 | the wave RAM address's bit 16 |

#### Reset

`$00`. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

128 KB, twice the IIgs's, since the chip's bank bit allows it.

<a id="reg-c563"></a>
### $C563 SOUND_DRQSEL: the DRQ's source

#### Equates

`SOUND_DRQSEL`

#### Module

A, the sound card

#### Access

R/W. Bits 7–2 are dropped.

#### Bits

All: 0 RAMDATA, 1 PCMDATA, 2 DOCDATA: the register the card's DRQ and fly-by DMA serve. 3: none, with no DRQ, a fly-by read `$FF` and a write dropped (ours: the design leaves 3 unsaid).

#### Reset

`$00`, RAMDATA. CTRL RESET leaves it.

#### Status

**emulated**

#### Notes

RAMDATA and DOCDATA hold DRQ all the time. A fly-by read with DRQSEL 1 gives `$FF`.

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
| 3–0 | REV | the revision: 2 (1 before [revision 2](#revision-2)) |

#### Reset

`$A2`.

#### Status

**emulated**

#### Notes

`SOUND_ID_V2` (`$A2`) is the value, and `SOUND_ID_V1` (`$A1`) revision 1's. The BIOS and OODOS check its high
nibble against `SOUND_TYPE` (`$A0`) before they touch the card, so they take either revision
([Taking the card at any revision](#revision-2)). The offsets with no register, `$64–$7E`, read `$FF` and ignore
writes; on revision 1, `$48–$7E` do.
