# OO-1 Sound Card Rev 2 — Emulator Changes (DAC and ES5503)

Nov 5, 2026 · @Jefferson

## Scope

Add sound card revision 2 to the emulator: a stereo PCM output with a stream FIFO, MP3 decode, EQ and a vis page, plus an emulated ES5503. On the real card these run on the RP2350's second core and an I2S DAC. All new behaviour stays off until software turns it on, so every existing SID test must pass unchanged, apart from the ID byte.

**In scope:** `emulator/components/devices/sound_card.{h,cpp}`, a new `es5503.{h,cpp}`, vendored minimp3, `backplane.inc` equates, BIOS QUIET, tests, and the matching updates to `hw/design.md` and the register index.

**Out of scope:** RP2350 firmware, the OO-Amp program, a MOD player, and any change to SID emulation beyond the volume scaling below. Keep this design generic: it serves OO-Amp, games and GW-BASIC alike.

**Read first:** `AGENTS.md`, `hw/design.md` ("Sound" and "DMA"), and the header comment in `sound_card.h`. The DAC and ES5503 are decided (see Decisions). The register details here are **proposed**, so mark them that way in `design.md`, as earlier emulator additions were.

## PCM output and stream FIFO

The host streams bytes into a 128 KB FIFO, and the card plays them on its own clock whether or not anyone is collecting audio. The ID byte becomes `$A2`, so software can tell revisions apart.

### New registers (offsets from `$C500`)

| Offset | Name | Access | Behaviour |
| --- | --- | --- | --- |
| `$48` | PCMCTL | read/write | Bit 0 RUN. Bit 1 PAUSE. Bit 2 FLUSH, a strobe that empties the FIFO and decoder and reads 0. Bit 3 EOF, no more data is coming |
| `$49` | CODEC | read/write | 0 raw PCM, 1 MP3, 2 reserved for MOD. Ignored while RUN is set |
| `$4A–$4B` | RATE | read/write | Raw PCM rate in Hz, 8,000 to 48,000. Reads back the actual rate, including an MP3's |
| `$4C` | PCMFMT | read/write | Bit 0 stereo, bit 1 16-bit. Reads back the actual format. 8-bit is unsigned, 16-bit is signed little-endian, stereo is left then right |
| `$4D–$4E` | FREE | read | FIFO space in 256-byte units, rounded down. Reading `$4D` latches both |
| `$4F` | PCMDATA | write | The stream. A write with the FIFO full is dropped |
| `$50` | STATE | read | 0 idle, 1 buffering, 2 playing, 3 paused, 4 underrun, 5 ended, or an error below |
| `$51–$54` | POS | read/write | Frames played, 32 bits. Reading `$51` latches all four. Any write to `$51` zeroes it |
| `$55–$56` | KBPS | read | Current MP3 bitrate, 0 for raw PCM |
| `$57–$58` | VOLL, VOLR | read/write | Master volume per side, 0 to 255, reset to 255 |
| `$59` | EQIDX | read/write | 0 preamp, 1 to 10 bands. Bit 7 set selects the enable, read and written through EQVAL. Auto-increments after each EQVAL access |
| `$5A` | EQVAL | read/write | Gain in signed half-dB, clamped to ±24 (±12 dB). For the enable, bit 0 |
| `$5B` | VISIDX | read/write | Read position in the vis page. Writing 0 takes a new snapshot |
| `$5C` | VISDATA | read | Vis page byte at VISIDX, auto-incrementing |
| `$5D` | CODECS | read | Bit *n* set means codec *n* exists. Bit 7 means an ES5503 exists. `$83` on this revision |
| `$63` | DRQSEL | read/write | The source of the card's DRQ: 0 RAMDATA (reset value), 1 PCMDATA, 2 DOCDATA |

**Errors in STATE:** `$80` bad RATE or PCMFMT at RUN, `$81` no MP3 sync in 64 KB of data, `$82` PCM and ES5503 both asked to run.

**Note:** DRQSEL replaces the "PCMCTL bit 7" DRQ route in the OO-Amp spec, because there are now three sources. PCMCTL bit 7 is reserved.

### Behaviour

- **State flow:** RUN goes to buffering. Playback starts at 4 KB in the FIFO, or as soon as EOF is set. An empty FIFO without EOF is underrun, with silence until data comes. An empty FIFO with EOF is ended. Clearing RUN returns to idle and keeps the FIFO unless FLUSH is also written.
- **DRQ:** `drq()` follows DRQSEL. PCMDATA holds DRQ only while the FIFO has room, so demand-mode DMA stops by itself (`dma_controller.cpp` already handles this). RAMDATA and DOCDATA stay always ready.
- **Fly-by:** `dack_write()` and `dack_read()` also follow DRQSEL. A read through DRQSEL 1 returns `$FF`.
- **Time:** playback advances with the card's time in `catch_up()`, in host cycles scaled by `set_cpu_hz()`. **FIFO drain, POS, STATE and interrupts must behave the same with `set_audio_rate(0)`.** Only making float samples may be skipped.
- **Partial frames:** a frame split across two writes is carried over, never played as a click.
- **MP3:** vendor [minimp3](https://github.com/lieff/minimp3) (CC0) with its licence in `emulator/licenses/`. It must build for both the native and WASM targets. Decode happens as output needs it. Bytes count against the FIFO until the decoder consumes them. Skip junk, resyncing on padding and tags. Rate and channels follow each frame. Output is 16-bit, with mono copied to both sides.
- **MPEG rates:** RATE reads back the stream's rate, and output runs at that rate. An MP3 above 48 kHz is not expected in MPEG-1 and -2. Treat one as error `$80`.
- **Reset:** `/RESET` clears RUN, empties the FIFO, zeroes POS, restores volumes, EQ off with all gains 0, DRQSEL 0. `CTRL RESET` leaves all of this alone.

### Signal path

The digital sources share one bus, whose rate is the stream's rate while PCM runs, and 48 kHz while the ES5503 runs.

1. **EQ:** when enabled, the preamp, then ten RBJ peaking biquads with Q 1.41 per channel, centred at 60, 170, 310, 600 Hz and 1, 3, 6, 12, 14, 16 kHz. Bypass any band at or above 0.45 of the bus rate. Disabled means bit-exact pass-through.
2. **Vis tap**, after EQ.
3. **Volume:** VOLL and VOLR, with 255 meaning unity.
4. **Mix:** resampled by linear interpolation to the host's `audio_rate`, then added to the SIDs. Scale the digital bus by 0.5 against the existing SID output, and clip the sum to -1..1. Make `MIX_DIGITAL` a named constant, because the real card's analog stage will set the true balance.

**Test tap:** expose the bus before resampling through a test-only accessor, as signed 16-bit frames, along with an `underruns()` counter. Bit-exact tests use this, not `take_audio()`.

### Vis page

| Index | Contents |
| --- | --- |
| 0–31 | Spectrum bands from a 64-point FFT of the left and right mix with a Hann window, 0 to 255 for -60 to 0 dBFS |
| 32–33 | VU left and right, peak since the last snapshot, same scale |
| 34–97 | The last 64 samples of the mix, unsigned, `$80` is zero |

Compute the page at the snapshot, not continuously. Reads past 97 return 0. SID sound is not in this page, because it never passes through the RP2350.

### SID volume

VOLL and VOLR also scale SID A and SID B, as the firmware will: a `$D418` write reaches the chip with its low nibble replaced by `round(nibble × vol / 255)`. Register reads still return what was written. Changing a volume re-applies it to the chip at once. At the reset value of 255 nothing changes, which keeps the corpus tests exact.

## ES5503

The card gains a software ES5503 (Ensoniq DOC): 32 oscillators, 8-bit samples, and 128 KB of its own wave RAM, clocked at 7,159,090 Hz as in the Apple IIgs. Registers are the real chip's. Only the access port is ours, modelled on the IIgs Sound GLU.

Implement it as a class of its own in `es5503.{h,cpp}`, with no knowledge of the card, so it can be tested alone. Use the [MAME ES5503 source](https://git.redump.net/mame/plain/src/devices/sound/es5503.cpp) (BSD-3-Clause) as the reference for edge cases, and the [IIgs DOC register notes](https://git.applefritter.com/Apple-2-SW/ksherlock-iigs/wiki/Ensoniq-DOC). If you copy code rather than reimplement, add its licence to `emulator/licenses/`.

### Host port

| Offset | Name | Access | Behaviour |
| --- | --- | --- | --- |
| `$5E` | DOCCTL | read/write | Bit 0 RUN, bit 2 IRQ to the 6510 instead of the host, bit 5 auto-increment, bit 6 DOCDATA reaches RAM rather than registers |
| `$5F` | DOCDATA | read/write | The register or RAM byte at DOCADDR. Moves DOCADDR on if bit 5 is set |
| `$60–$61` | DOCADDR | read/write | Low byte is the register number. Both bytes form the RAM address |
| `$62` | DOCBANK | read/write | Bit 0 is RAM address bit 16. Carries with auto-increment and wraps at 128 KB |

- **No dummy read.** The real GLU needs one because the DOC is slow. Here a read returns its value at once, and code ported from the IIgs must drop that read. Record this in `design.md`.
- **Wave RAM and registers stay reachable** whether RUN is set or not.
- **128 KB rather than the IIgs's 64 KB,** because the chip's bank bit allows it and samples benefit. The RP2350 has room: 64 KB for the 6510, 128 KB FIFO, and 128 KB wave RAM leave about 200 KB of 520 KB.

### The 6510's view

While DOCCTL RUN is set and I/O is banked in, `$DF00–$DFFF` is the DOC's registers, so a player on the card reaches them with plain loads and stores. With RUN clear it stays `$FF` as today, so SID tunes that touch that page behave exactly as they do now. Wave RAM is reached from the host only. A player has it loaded before it starts.

### Registers

| Register | Name | Contents |
| --- | --- | --- |
| `$00–$1F` | Frequency low | Per oscillator |
| `$20–$3F` | Frequency high | Per oscillator |
| `$40–$5F` | Volume | Per oscillator, 0 to 255 |
| `$60–$7F` | Data | Last sample read, read-only |
| `$80–$9F` | Pointer | Wave table address bits 8 to 15. Bits below the table's size are ignored |
| `$A0–$BF` | Control | Bit 0 halt. Bits 1–2 mode: 0 free run, 1 one-shot, 2 sync/AM, 3 swap. Bit 3 interrupt enable. Bits 4–7 channel |
| `$C0–$DF` | Table | Bit 6 bank. Bits 3–5 size, 256 << *n* bytes. Bits 0–2 resolution |
| `$E0` | Interrupt | Read: the pending oscillator's number in bits 1–5, and bit 7 for its status. That read acknowledges it. Take the exact status bits and scan order from MAME's read |
| `$E1` | Enable | Enabled oscillators = ((value >> 1) & 31) + 1 |
| `$E2` | A/D | No input is connected, so it reads `$80` |

### Synthesis

- **Output rate:** (7,159,090 / 8) / (enabled + 2), which is about 26,320 Hz with all 32 enabled. Each output sample, every enabled oscillator that is not halted takes one step.
- **Step:** add the 16-bit frequency to its 24-bit accumulator. The table index is (accumulator >> (9 + resolution - size)), masked to the table size. Read the byte at (pointer << 8, masked by table size) + index, in bank 0 or 1.
- **Zero byte:** a sample of `$00` is the stop marker, not sound. It halts the oscillator and takes the interrupt path. Follow MAME's `halt_osc` for how each mode treats it.
- **Output:** (sample - 128) × volume, added to the oscillator's channel.
- **End of table:** free run keeps phase and loops. One-shot halts. Swap halts this oscillator and starts its partner (oscillator number XOR 1) from zero.
- **Sync/AM:** an even oscillator plays and restarts the odd oscillator below it. An odd one plays nothing, and its sample sets the volume of the oscillator above it.
- **Do not copy** MAME's mixing of the highest oscillator three times into the last channel. Its source calls that a bug on real hardware, so check whether this applies to your reimplementation and leave it out.

### Stereo and level

Channels 0 to 15 fold to stereo by their low bit: even is left, odd is right. Each voice's float level is ((sample - 128) / 128) × (volume / 255) × 1/8, matching the SID path's headroom. This goes onto the digital bus and through EQ, vis and volume. The DOC's output is resampled to the 48 kHz bus by averaging the steps each bus sample spans, or holding the last when none does.

### Interrupts

A halting oscillator with its enable bit set becomes pending. If DOCCTL bit 2 is clear, it raises the card's host interrupt through a new IEN bit 3 and STATUS bit 7, on IRQH bit 2 alongside the mailbox. If bit 2 is set, it pulls the 6510's `/IRQ` instead. Reading `$E0` from either side acknowledges it. Update `irq()` to include this.

### Mutual exclusion

For now, PCM and the ES5503 cannot run together, because the load on a real RP2350 is unmeasured. Setting either RUN while the other is set leaves the first running, refuses the second, and shows `$82` in STATE until the next write to either RUN bit. The SIDs may always run alongside either.

### Reset

`/RESET` clears DOCCTL, sets every oscillator's halt bit, clears pending interrupts, and sets `$E1` to 0. Wave RAM keeps its contents, like the card's other RAM. `CTRL RESET` leaves the ES5503 alone.

## Build order and tests

Build in this order, one branch per step, with the full suite and the ASan/UBSan build passing before the next. New tests go in `test_sound.cpp` or a new `test_es5503.cpp`, registered in `emulator/test/host/CMakeLists.txt`.

1. **Revision and registers.** ID `$A2`, every new register reading and writing as specified, reset values, CODECS `$83`. Add `SOUND_ID_V2` and the new equates to `backplane.inc`. Update tests that check the ID byte, and no others.
2. **Raw PCM.** FIFO, DRQSEL, DMA streaming, states, POS, FREE. Test: 8 and 16-bit, mono and stereo streams come out bit-exact at the tap, by CPU writes and by demand DMA. Test underrun and recovery, ended, pause, flush, and a frame split across writes. Run the timing tests at audio rates 0 and 48,000 and require identical POS and STATE.
3. **Mix.** Resampling and `take_audio()`. Test: a sine stream arrives at the expected level and frequency. PCM plus a SID tune sum without either being lost.
4. **MP3.** Test: CBR and VBR at low and high bitrates, mono and stereo, match minimp3 run directly on the same file. Test junk before the first frame and trailing CP/M padding. Error `$81` on non-MP3 data. Keep the fixtures small and made by us.
5. **EQ, volume, vis.** Test: disabled EQ is bit-exact, each band's gain at its centre is within 1 dB, and bypass works near the bus limit. A sine lands in the expected spectrum band. Volume 0 is silent. SID `$D418` scaling applies and reads back unscaled, and the SID corpus test is unchanged at 255.
6. **ES5503 core.** The class alone. Test: output rate against enabled count, pitch from frequency, size and resolution, all four modes, swap handover, sync restart, AM, `$00` halt, bank bit, register reads, and `$E0` acknowledgement order.
7. **ES5503 on the card.** Port, auto-increment across the bank, DMA to wave RAM through DRQSEL 2, `$DF00` window on and off, both interrupt routes, mutual exclusion, and reset. Include a hand-assembled 6510 program that plays a looping waveform, as `test_sound.cpp` does for the SIDs.
8. **BIOS and docs.** QUIET also clears PCM RUN and DOC RUN when the ID is at least `$A2`, so Break and the demo's end silence everything. Update the header comment in `sound_card.h`, `hw/design.md` "Sound", and the register index with `make -C docs check`.

### Acceptance

- [ ] Every existing test passes, with only ID-byte expectations changed
- [ ] `test_sid_corpus` output is identical to before this work
- [ ] Native and WASM builds both build and pass, with minimp3 included
- [ ] A real 44.1 kHz stereo stream runs through emulated DMA from an OODOS program with zero underruns
- [ ] Results do not depend on the host's speed or audio rate
- [ ] ASan and UBSan are clean

### Decisions

Jefferson approved these on 2026-11-05, so record them as decided in `design.md`:

- The I2S DAC and the emulated ES5503 go on sound card revision 2.
- Channels fold to stereo by their low bit: even is left, odd is right.
