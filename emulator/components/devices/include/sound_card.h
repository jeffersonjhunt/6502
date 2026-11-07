#pragma once

/**
 * @file sound_card.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's sound card (module ID A): two SIDs, and a 6510 of its own.
 * @version 0.1.0
 * @date 2026-10-25
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Sound") is the spec. Registers at $C500:
 *
 *   $00-$1C SID A   $20-$3C SID B   $40 CTRL   $41 STATUS   $42 IEN
 *   $43-$44 RAMADDR   $45 RAMDATA   $46 MBOUT   $47 MBIN   $7F ID
 *
 * Revision 2 ($A2; hw/sound-rev2.md, proposed) adds a PCM stream and an
 * ES5503 (es5503.h). So far (that spec's steps 1 to 5) its registers hold
 * what is written and read it back, with their reset values, and the PCM
 * stream runs, raw or MP3, through the EQ, the vis tap and volume, mixed
 * with the SIDs; no ES5503 sound yet:
 *
 *   $48 PCMCTL: bits 0 RUN, 1 PAUSE, 3 EOF kept; bit 2 FLUSH a strobe (reads 0)
 *   $49 CODEC (writes ignored while RUN)   $4A-$4B RATE   $4C PCMFMT (bits 0-1)
 *   $4D-$4E FREE: the FIFO's room in 256-byte units (512, empty); reading
 *           $4D latches $4E   $4F PCMDATA: into the 128 KB FIFO; dropped full
 *   $50 STATE (below)   $51-$54 POS: reading $51 latches $52-$54, a write
 *           to $51 zeroes it   $55-$56 KBPS: 0   $57/$58 VOLL/VOLR
 *   $59 EQIDX: 0 the preamp, 1-10 the bands, bit 7 the enable instead; an
 *           EQVAL access moves bits 0-6 on   $5A EQVAL: a gain, -24 to 24
 *           (clamped), or the enable's bit 0; an index past 10 reads 0
 *   $5B VISIDX: writing 0 takes a snapshot (sound_dsp.h)   $5C VISDATA:
 *           the vis page's byte at VISIDX, which moves on; past 97, 0
 *   $5D CODECS: $83   $5E DOCCTL (bits 0, 2, 5, 6)   $5F DOCDATA: wave RAM
 *           (DOCCTL bit 6) or the DOC's register at DOCADDR, which moves on
 *           with bit 5, wrapping at 128 KB   $60-$61 DOCADDR   $62 DOCBANK
 *           (bit 0: address bit 16)   $63 DRQSEL: 0 RAMDATA, 1 PCMDATA
 *           (DRQ while the FIFO has room; a fly-by read $FF), 2 DOCDATA,
 *           3 none (no DRQ)
 *
 * The stream. RUN takes RATE and PCMFMT (a RATE outside 8,000-48,000, or a
 * CODEC of 2 or more, is STATE $80) and buffers: playing starts
 * at 4 KB in the FIFO, or at EOF. Each frame then falls due on the host's
 * clock (tick()'s cycles at set_cpu_hz()), whether sound is made or not, and
 * takes 1, 2 or 4 bytes, whole frames only, so one split across writes
 * waits for its last byte. A frame due with none in the FIFO is underrun
 * (silence, counted once by underruns()) without EOF, and the end with it;
 * a byte arriving plays from the next frame; at the end a partial frame is
 * dropped. PAUSE holds the place (its release goes back to the state it
 * paused), FLUSH empties the FIFO (with RUN set, a seek: buffering again),
 * clearing RUN keeps it. POS counts the frames played.
 * The test tap (set_tap()) records the bus before any resampling, signed
 * 16-bit left then right, 8-bit made (b - 128) x 256, mono on both sides.
 *
 * The host's side: the SIDs' registers, the card's 64 KB through RAMADDR
 * and RAMDATA, and the mailbox. The card's side: its own 6510, an NMOS
 * 6502 with the undocumented opcodes (cpu6502.h), which runs while CTRL
 * RUN is set, one cycle per SID clock: PAL 985,248 Hz, or NTSC 1,022,727 Hz
 * with CTRL bit 0. Its map, modelled on the C64's:
 *
 *   $0000-$FFFF  the card's 64 KB
 *   $0000/$0001  the 6510's port: DDR and data, as on the C64. Bits 0-2
 *                bank $D000-$DFFF: I/O while bit 2 and bit 0 or 1 are high
 *                (inputs read high), RAM otherwise. An input bit reads 1,
 *                but 6 and 7, unconnected, hold what was last written, so
 *                DDR $2F with $37 reads $37 as on a C64. Writes to $00/$01
 *                go to the RAM under them too.
 *   $D011/$D012  with FRAMEEN bit 1 (below), the VIC-II's raster line, read
 *                only, from the card's frame: $D012 its low 8 bits, $D011
 *                bit 7 its ninth (the rest of $D011 reads 1); $FF without
 *   $D400        SID A, repeating every 32 bytes to $D7FF, except where
 *                SID B is: $D420-$D43F, or $D500-$D51F with CTRL bit 3
 *   $DC00        CIA 1, imitated: timer A ($DC04/$DC05), its interrupt
 *                ($DC0D) and control ($DC0E), repeating every 16 bytes
 *   $DE03/$DE04  FRAME and FRAMEEN: the card's frame interrupt, for the
 *                host's stub, and the raster line's switch (below)
 *   $DE00/$DE01  the mailbox and its status
 *   $DE02        STOP: any write clears CTRL RUN, so a one-shot (the boot
 *                splash's tune) hands the SIDs back to the host when done
 *
 * The rest of $D000-$DFFF (the rest of the VIC-II, colour RAM, CIA 2, the
 * rest of $DE00-$DFFF) reads $FF
 * and ignores writes while I/O is in. The timer counts down one each SID
 * clock and underflows after latch + 1 of them: it reloads from the latch,
 * sets ICR bit 0, and pulls the 6510's /IRQ while ICR's mask has bit 0.
 * CRA bit 0 starts it, bit 3 makes it one-shot, bit 4 loads the latch now.
 * Writing the latch's high byte while the timer is stopped loads it too.
 *
 * The frame: a C64's, 19,656 SID clocks PAL (312 lines of 63) or 17,095
 * NTSC (263 of 65), counted while RUN is set from the 6510's reset, whether
 * it uses it or not (it stops with the 6510); the raster line above is
 * where in it the clock is. Its interrupt is
 * the card's own, for the host's player stub (sid_file.h), at addresses the
 * C64 leaves free so a tune can't turn it on by accident: $DE03 FRAME bit 0
 * latches at each frame's start and a 1 written there clears it, bit 7 reads
 * 1 while it pulls /IRQ (the other bits read 0); $DE04 FRAMEEN bit 0 lets it,
 * and bit 1 shows the raster line at $D011/$D012. The stub sets bit 1 for
 * a PSID only: an RSID expects a whole C64, and one that gets past a raster
 * wait it can't otherwise pass may go on to what the card lacks and jam.
 *
 * CTRL RESET resets the 6510 (its vector at $FFFC in the card's RAM), the
 * CIA and the frame (FRAMEEN clear, the frame starting over). The usual start: RUN = 0, load the code through RAMDATA, write the
 * reset vector, then CTRL = RUN | RESET. Time: the card is a timed device,
 * ticked in host CPU cycles; set_cpu_hz() says how fast those are (0: 7.16
 * MHz, also for a host run flat out), so the card keeps its own clock's
 * time against the host's.
 *
 * SIDs. A host write to $00-$1C goes to SID A, unless CTRL RUN is set: the
 * card's 6510 owns the chips then, and the write is dropped. With CTRL BOTH,
 * a write to SID A also goes to SID B, from either side. Reads always work:
 * the card answers the 25 write-only registers from what was last written to
 * each, and $19-$1C (POTX, POTY, OSC3, ENV3) from the chip (sid.h): no
 * paddles are connected, so POTX and POTY read $FF; OSC3 and ENV3 are voice
 * 3's waveform and envelope, as of that clock. The chips run from the SID
 * clock whether the 6510 runs or not, and are brought up to date whenever
 * they are written or read (by either side, or the debugger), so a quiet
 * card costs nothing. The RP2350 forwards a write at once in the
 * emulator, so STATUS BUSY never sets.
 *
 * RAM. RAMADDR is the next address; a RAMDATA access, read or write, moves
 * it on, wrapping at 64 KB. The card is always ready, so DRQ is always
 * asserted: a DMA channel wired to the card (`drq=` in the machine file)
 * moves bytes through RAMDATA, fly-by, as fast as it is programmed to.
 *
 * Mailbox. Two one-byte boxes. MBOUT puts a byte in the card's box (STATUS
 * bit 1 until the card takes it; a second write replaces the first). The
 * card's byte waits in MBIN (STATUS bit 2), and reading MBIN empties it.
 * With IEN bit 2, a byte waiting in MBIN interrupts (STATUS bit 7), on bit 2
 * of IRQH. The card sees the same two boxes from its side: $DE00 reads the
 * host's byte and writes its own; $DE01 bit 0 is "a byte from the host
 * waiting", bit 1 "my byte not yet taken".
 *
 * Audio. With set_audio_rate(), the card also makes the SIDs' sound
 * (sid.h's output stage) at that rate, stereo: SID A left, SID B right (a
 * mono tune plays on both with CTRL BOTH). Each sample averages the chips'
 * output over the SID clocks it spans, then a high-pass at 16 Hz takes out
 * the DC, as the C64's output capacitor does, and the result is scaled by
 * 1/8 (a voice at full envelope peaks at about -18 dBFS: room for three,
 * resonance and the 6581's DC steps) and clipped to -1..1. Samples wait, at most half a
 * second of them, oldest dropped first, until take_audio() collects them;
 * that brings the chips up to now first. Of a backlog over two seconds
 * (nobody collecting), only the last second is made: the rest, whose
 * samples would be dropped, is clocked in bulk. Off (rate 0, the default)
 * the chips are clocked in bulk and nothing is made. The model, 6581 or 8580,
 * is the replacement chips' jumper (set_model()), not a register.
 *
 * MP3 (CODEC 1): the FIFO decoded by minimp3 (third_party/minimp3, CC0),
 * layer III, a frame at a time as its first sample falls due. In step, a
 * frame is decoded with all of it and the next header there; out of step,
 * a frame is taken by minimp3's own sync rule (ten in step after it, or the
 * data's end, or a tag), the card waiting until that decides, so a stream
 * fed as it plays decodes as the whole data would; until its first frame
 * decodes, an MP3 is buffering. The rate and channels follow each
 * frame. Free format, layers I and II and frames that decode to nothing are
 * junk; 64 KB of it is STATE $81. ID3v2 tags are skipped by their size
 * wherever met (8 MB of them, $81). A frame with nothing frame-like and no
 * tag after it, to EOF or for 32 KB, plays by its own length. A fruitless
 * search costs a few header checks at most.
 *
 * The mix (revision 2). While the PCM stream plays or runs dry, its bus
 * frames are resampled to the audio rate by linear interpolation into a
 * queue, and each SID sample made takes one frame from it, adds it scaled by
 * MIX_DIGITAL and clips the sum; with the queue empty (the bus stopped) the
 * SIDs' sample is made exactly as before. Each run of the bus (a start, a
 * pause's release, a seek, a new RUN, within one write too, and a bulk
 * catch-up) starts its queue lined up with the SIDs' samples made up to that
 * moment, behind a lead of silence ((audio rate / stream rate, rounded up) +
 * 1 frames), so the bursts a bus frame's outputs come in never leave it dry
 * while the bus runs. The same backlog rules: of more than two seconds of
 * bus frames in one tick only the last second is resampled (the samples for
 * the rest an offset to pass first); at a second and a half queued the SIDs
 * are caught up, which takes it; when the SIDs skip a backlog, the queue's
 * frames for the samples skipped go with them. sid.cpp and this file build
 * with -ffp-contract=off, so every target makes the same samples, given the
 * same libm (std::pow, std::sin, std::exp): glibc and Emscripten's agree
 * today.
 *
 * The bus (revision 2, step 5). Each frame goes through the EQ (sound_dsp.h),
 * then the vis tap, then VOLL and VOLR (255 unity, else x vol / 255, rounded
 * half away from zero), then the test tap and the mix. Every frame does,
 * those the mix has no use for (no audio rate, a bulk catch-up's) too, so the
 * EQ's state and the vis page are the same whatever the audio rate and the
 * ticks. While RUN is set and the stream doesn't play (paused, buffering,
 * ended, an error), the vis tap has silence at the stream's rate (an MP3
 * sought, the bus's last), on the stream's frame times from the one that
 * found the stop; the EQ keeps its state. Clearing RUN empties the vis tap's
 * frames, so the next snapshot's spectrum and samples are silence (its VU,
 * as after a pause, the peaks since the last); the page already taken stays
 * until VISIDX is written 0. The EQ's filters start from silence at RUN and at FLUSH; a gain or
 * enable written, or an MP3's rate changing, gives it new coefficients from
 * the next frame. VOLL and VOLR scale SID A's and SID B's $D418 too: the chip
 * has round(nibble x vol / 255) in the low nibble, the card's registers (and
 * sid()) what was written; a volume written reaches the chips at once.
 *
 * /RESET (reset()) stops the card's 6510 and silences the SIDs (their
 * registers cleared, as the SID's /RES pin does), resets the CIA and the
 * 6510's port, and empties the mailbox; the card's RAM keeps its contents. In Apple II mode the card sits idle:
 * entering it is a reset, and Apple software never addresses the block.
 */

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "bus.h"
#include "cpu6502.h"
#include "device.h"
#include "es5503.h"
#include "sid.h"
#include "sound_dsp.h"

namespace ooe::emu
{
    class SoundCard : public Device
    {
    public:
        static constexpr uint8_t MODULE = 0xA;
        static constexpr uint8_t ID = MODULE << 4 | 2; ///< +$7F: type (module ID) and revision (2: hw/sound-rev2.md), unless set_revision(1)
        static constexpr unsigned SID_REGS = 29;       ///< $00-$1C
        static constexpr size_t RAM_BYTES = 0x10000;

        enum Reg : uint8_t
        {
            SID_A = 0x00,
            SID_B = 0x20,
            CTRL = 0x40,
            STATUS = 0x41,
            IEN = 0x42,
            RAMADDR = 0x43, ///< low byte; $44 high
            RAMDATA = 0x45,
            MBOUT = 0x46,
            MBIN = 0x47,
            PCMCTL = 0x48,
            CODEC = 0x49,
            RATE = 0x4A, ///< low byte; $4B high
            PCMFMT = 0x4C,
            FREE = 0x4D, ///< low byte, latching; $4E high
            PCMDATA = 0x4F,
            STATE = 0x50,
            POS = 0x51, ///< low byte, latching; $52-$54 the rest
            KBPS = 0x55, ///< low byte; $56 high
            VOLL = 0x57,
            VOLR = 0x58,
            EQIDX = 0x59,
            EQVAL = 0x5A,
            VISIDX = 0x5B,
            VISDATA = 0x5C,
            CODECS = 0x5D,
            DOCCTL = 0x5E,
            DOCDATA = 0x5F,
            DOCADDR = 0x60, ///< low byte (the register number); $61 high
            DOCBANK = 0x62,
            DRQSEL = 0x63,
            ID_REG = 0x7F,
        };
        /// The SID's read-only registers.
        static constexpr uint8_t POTX = 0x19, POTY = 0x1A, OSC3 = 0x1B, ENV3 = 0x1C;
        static constexpr uint8_t SID_MODE_VOL = 0x18; ///< filter mode and volume: VOLL / VOLR scale its low nibble
        static constexpr uint8_t CTRL_NTSC = 0x01, CTRL_RUN = 0x02, CTRL_RESET = 0x04, CTRL_SID_B_D500 = 0x08,
                                 CTRL_BOTH = 0x10;
        static constexpr uint8_t STATUS_BUSY = 0x01, STATUS_TO_CARD = 0x02, STATUS_TO_HOST = 0x04,
                                 STATUS_IRQ = 0x80;
        static constexpr uint8_t IEN_MAILBOX = 0x04;
        static constexpr uint8_t PCMCTL_RUN = 0x01, PCMCTL_PAUSE = 0x02, PCMCTL_FLUSH = 0x04, PCMCTL_EOF = 0x08;
        static constexpr uint8_t PCMFMT_STEREO = 0x01, PCMFMT_16BIT = 0x02;
        /// CODECS: raw PCM (bit 0), MP3 (bit 1), an ES5503 (bit 7).
        static constexpr uint8_t CODECS_PRESENT = 0x83;
        enum PcmState : uint8_t
        {
            STATE_IDLE = 0,
            STATE_BUFFERING = 1,
            STATE_PLAYING = 2,
            STATE_PAUSED = 3,
            STATE_UNDERRUN = 4,
            STATE_ENDED = 5,
            STATE_BAD_FORMAT = 0x80, ///< at RUN, a RATE or CODEC that doesn't play; an MP3 frame above 48 kHz
            STATE_NO_SYNC = 0x81,    ///< MP3: 64 KB of data with no frame found in it
        };
        static constexpr uint32_t RATE_MIN = 8'000, RATE_MAX = 48'000;
        static constexpr size_t START_BYTES = 4096; ///< buffering ends at this much in the FIFO, or at EOF
        static constexpr unsigned EQ_BANDS = 10; ///< EQIDX 1-10; 0 is the preamp
        static constexpr uint8_t EQIDX_ENABLE = 0x80;
        static constexpr int EQ_LIMIT = 24; ///< EQVAL's half-dB, either way
        static constexpr unsigned VIS_BYTES = 98;
        static constexpr uint8_t DOCCTL_RUN = 0x01, DOCCTL_IRQ_CARD = 0x04, DOCCTL_AUTOINC = 0x20,
                                 DOCCTL_RAM = 0x40;
        enum DrqSource : uint8_t
        {
            DRQ_RAMDATA = 0,
            DRQ_PCMDATA = 1,
            DRQ_DOCDATA = 2,
        };
        static constexpr size_t FIFO_BYTES = 0x20000; ///< 128 KB
        /// $DE01, the card's view of the mailbox.
        static constexpr uint8_t CARD_FROM_HOST = 0x01, CARD_UNTAKEN = 0x02;
        static constexpr uint32_t PAL_HZ = 985'248, NTSC_HZ = 1'022'727, NATIVE_HZ = 7'159'090;
        /// The digital bus's share of the mix, against the SIDs' output: the real card's analog stage will set it.
        static constexpr float MIX_DIGITAL = 0.5f;
        /// CIA 1's registers the card imitates (offsets from $DC00).
        static constexpr uint8_t CIA_TA_LO = 0x04, CIA_TA_HI = 0x05, CIA_ICR = 0x0D, CIA_CRA = 0x0E;
        /// The frame interrupt's registers ($DE03 FRAME, $DE04 FRAMEEN) and the frame in SID clocks and lines.
        static constexpr uint16_t CARD_FRAME = 0xDE03, CARD_FRAME_EN = 0xDE04;
        static constexpr uint8_t FRAMEEN_IRQ = 0x01, FRAMEEN_RASTER = 0x02;
        static constexpr uint32_t FRAME_PAL = 19'656, FRAME_NTSC = 17'095, LINES_PAL = 312, LINES_NTSC = 263;
        static constexpr uint8_t CRA_START = 0x01, CRA_ONE_SHOT = 0x08, CRA_LOAD = 0x10;

        SoundCard();
        ~SoundCard() override;
        SoundCard(const SoundCard &) = delete;
        SoundCard &operator=(const SoundCard &) = delete;
        /// The card's revision, for tests of software that must take the card at any (ID's low nibble):
        /// 2, the default, or 1, the card before hw/sound-rev2.md, whose $48-$63 read $FF and ignore writes
        /// (so DRQ and fly-by stay RAMDATA's). Not a register: kept through resets, as set_model() is.
        void set_revision(unsigned rev);
        unsigned revision() const { return rev_; }
        /// The host CPU's clock, which tick()'s cycles run at (0: 7.16 MHz).
        void set_cpu_hz(uint32_t hz)
        {
            const uint32_t to = hz ? hz : NATIVE_HZ;
            // The stream has been ticked at the old clock; the part of a frame owed carries over, rescaled
            // (below host Hz, so the product fits), and so does the vis tap's while the bus is stopped.
            pcm_phase_ = pcm_phase_ * to / host_hz_;
            vis_phase_ = vis_phase_ * to / host_hz_;
            phase_ = phase_ * to / host_hz_; // the SIDs' clock too: the part of a SID clock owed
            host_hz_ = to;
        }
        /// /RESET: the 6510 stopped, the SIDs silent, the mailbox empty. RAM stays.
        void reset();
        void bus_reset() override { reset(); }
        /// Make sound at `rate` samples a second (0: none, the default).
        void set_audio_rate(uint32_t rate);
        uint32_t audio_rate() const { return audio_rate_; }
        /// The SIDs' model, both chips (a jumper on the card). Kept through resets.
        void set_model(Sid::Model m);
        /// Up to `max_frames` stereo frames (left, right) made since the last call, oldest first,
        /// into `out`; the chips are brought up to now first. @return frames copied
        size_t take_audio(float *out, size_t max_frames);
        /// Leave voices of SID `chip` out of the sound (Sid::set_muted()).
        void set_muted(unsigned chip, uint8_t voices)
        {
            catch_up();
            chips_[chip & 1].set_muted(voices);
        }
        /// Frames waiting, the chips brought up to now.
        size_t audio_frames() const
        {
            catch_up();
            return audio_.size() / 2;
        }

        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return (ien_ & IEN_MAILBOX) && to_host_full_; }
        bool drq() const override
        {
            return drqsel_ == DRQ_PCMDATA ? fifo_count_ < FIFO_BYTES : drqsel_ <= DRQ_DOCDATA;
        }
        uint8_t dack_read(bool tc) override;
        void dack_write(uint8_t data, bool tc) override;
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;

        // The card's side: what its 6510 reaches ($DE00, $DE01, $D400)
        /// $DE00 read: the host's byte, taking it; -1 if none is waiting.
        int card_receive();
        /// $DE00 write: a byte for the host, in MBIN (replacing one not yet taken).
        void card_send(uint8_t byte);
        /// $DE01.
        uint8_t card_status() const;
        /// A write to SID `chip` (0 = A, 1 = B) register `reg`, from the card's
        /// side; with CTRL BOTH, a write to A goes to B too.
        void sid_write(unsigned chip, unsigned reg, uint8_t data);
        /// A read of SID `chip` register `reg`, as the chip answers it: the
        /// read-only four live, the rest as last written.
        uint8_t sid_read(unsigned chip, unsigned reg) const;

        uint8_t ctrl() const { return ctrl_; }
        /// SID `chip`'s registers as written ($D418 too, before VOLL or VOLR scales its volume): kept up to date.
        const std::array<uint8_t, SID_REGS> &sid(unsigned chip) const { return written_[chip & 1]; }
        /// SID `n` as of now: its voices' oscillators and envelopes.
        const Sid &chip(unsigned n) const
        {
            catch_up();
            return chips_[n & 1];
        }
        /// SID clocks since power-up, whether the 6510 ran or not.
        uint64_t sid_clocks() const { return now(); }
        std::array<uint8_t, RAM_BYTES> &ram() { return ram_; }
        const std::array<uint8_t, RAM_BYTES> &ram() const { return ram_; }
        // Tests: the PCM stream as the card's digital bus carries it, before any resampling.
        /// Record the bus's frames from now on (true), or stop and drop them (false). For tests only: it keeps
        /// every frame until take_tap(), uncapped, so nothing else turns it on (it starts off, and /RESET leaves it).
        /// With `silence` false, the frames of silence while the stream is dry aren't recorded: only what played.
        void set_tap(bool on, bool silence = true)
        {
            tap_on_ = on;
            tap_silence_ = silence;
            tap_.clear();
        }
        /// The frames recorded since the last call, left then right, signed 16-bit; emptied.
        std::vector<int16_t> take_tap()
        {
            std::vector<int16_t> out;
            out.swap(tap_);
            return out;
        }
        /// Frames the bus has put into the mix, resampled to the audio rate, since /RESET or set_audio_rate().
        uint64_t mix_frames() const { return mix_frames_; }
        /// Of them, those not yet added to a SID sample.
        size_t mix_backlog() const { return pcm_mix_.size() / 2; }
        /// SID samples made while the bus ran with nothing queued for them (a gap in the stream): 0 if the lead
        /// held. Since /RESET or set_audio_rate().
        uint64_t mix_dry() const { return mix_dry_; }
        /// Times the stream ran dry with no EOF, since /RESET.
        unsigned underruns() const { return underruns_; }
        /// Bytes waiting in the FIFO, a partial frame's among them.
        size_t fifo_bytes() const { return fifo_count_; }
        /// The card's ES5503, its registers and wave RAM.
        Es5503 &doc() { return doc_; }
        const Es5503 &doc() const { return doc_; }
        /// The DOC's address, 17 bits: DOCBANK bit 0, then DOCADDR.
        uint32_t doc_address() const { return doc_addr_; }
        /// Times CTRL RESET was written.
        unsigned cpu_resets() const { return cpu_resets_; }
        /// The card's 6510, for the debugger.
        const Cpu6502 &cpu() const { return cpu_; }
        /// What the card's 6510 sees, for disassembly.
        const Bus &cpu_bus() const { return bus_; }
        /// Watch the card's 6510's bus cycles (not owned; nullptr to stop).
        void set_cpu_observer(CycleObserver *observer) { cpu_.set_observer(observer); }
        /// $D000-$DFFF is I/O to the card's 6510 now, by its port.
        bool io_visible() const { return io_in(); }
        /// The SID clock CTRL picks.
        uint32_t sid_hz() const { return ctrl_ & CTRL_NTSC ? NTSC_HZ : PAL_HZ; }
        /// The 6510's port: DDR ($00) and data ($01), as written.
        uint8_t port_ddr() const { return ddr_; }
        uint8_t port_data() const { return port_; }
        /// CIA 1's timer A: its counter and latch, and whether it is running.
        uint16_t timer() const { return cia_counter_; }
        uint16_t timer_latch() const { return cia_latch_; }
        bool timer_running() const { return cia_cra_ & CRA_START; }
        /// ICR's flags as they stand (bit 0: timer A underflowed), without acknowledging them.
        uint8_t timer_flags() const { return cia_flags_; }
        /// The frame interrupt: FRAME's latch (bit 0) and FRAMEEN's enable (bit 0).
        uint8_t frame_flags() const { return vic_flags_; }
        bool frame_enabled() const { return vic_mask_ & FRAMEEN_IRQ; }
        /// $D011/$D012 read the raster line (FRAMEEN bit 1).
        bool raster_shown() const { return vic_mask_ & FRAMEEN_RASTER; }
        /// The raster line the frame is on (what $D012/$D011 read with FRAMEEN bit 1).
        unsigned raster_line() const;

    private:
        /// What the card's 6510 sees.
        class CardBus : public Bus
        {
        public:
            explicit CardBus(SoundCard &card) : card_(card) {}
            uint8_t read(uint16_t addr) override { return card_.card_read(addr); }
            void write(uint16_t addr, uint8_t data) override { card_.card_write(addr, data); }
            uint8_t peek(uint16_t addr) const override { return card_.card_peek(addr); }

        private:
            SoundCard &card_;
        };

        uint8_t ram_byte() { return ram_[ram_addr_++]; }
        /// The FIFO's room, in 256-byte units.
        uint16_t free_units() const { return static_cast<uint16_t>((FIFO_BYTES - fifo_count_) / 256); }
        /// The playing format's frame: 1, 2 or 4 bytes.
        size_t frame_bytes() const
        {
            return size_t{play_fmt_ & PCMFMT_STEREO ? 2u : 1u} * (play_fmt_ & PCMFMT_16BIT ? 2u : 1u);
        }
        /// CODEC 1: the decoder's state and the frame it last made (sound_card.cpp; minimp3 underneath).
        struct Mp3;
        bool mp3() const { return play_codec_ == 1; }
        /// The next MP3 frame's samples ready (decoding from the FIFO as it must), or false: none can be yet.
        bool mp3_next();
        /// Stereo frames ready from the last MP3 frame decoded.
        size_t mp3_ready() const;
        /// A new MP3 frame's rate: the bus follows it.
        void mp3_rate(uint32_t hz);
        /// `n` bytes off the FIFO's front.
        void fifo_drop(size_t n)
        {
            fifo_head_ = (fifo_head_ + n) % FIFO_BYTES;
            fifo_count_ -= n;
        }
        /// A byte into the FIFO (dropped if it's full), then the state as that leaves it.
        void pcm_write(uint8_t data);
        /// PCMCTL written: FLUSH, RUN's edges, PAUSE and EOF.
        void pcm_control(uint8_t data);
        /// The state as the FIFO and PCMCTL now leave it, between frames; a start or stop unanchors the mix.
        void pcm_update();
        /// pcm_update()'s state flow. The mix is re-anchored in one place, pcm_update(), around this: any start
        /// (from buffering, paused, or a RUN or seek that stopped the stream first, even within one write) or
        /// stop; and by pcm_advance() after a bulk catch-up, and tick() when time passes with the bus stopped.
        void pcm_step();
        /// The bus runs (frames fall due) in these states.
        static bool bus_runs(uint8_t state) { return state == STATE_PLAYING || state == STATE_UNDERRUN; }
        /// `cycles` host cycles of playback: the frames due, from the FIFO or, run dry, silence.
        void pcm_advance(uint64_t cycles);
        /// `n` frames out of the FIFO, onto the bus.
        void pcm_play(size_t n);
        /// `n` frames of silence onto the bus (the stream dry).
        void pcm_silence(uint64_t n);
        /// One frame of the bus: the EQ, the vis tap and volume (bus_dsp), then to the tap and resampled into the mix.
        void bus_frame(int16_t l, int16_t r);
        /// The EQ, the vis tap and volume, in place: every frame of the bus, the mix's or not, so the vis page and
        /// the EQ's state are the same whatever the audio rate or the ticks.
        void bus_dsp(int16_t &l, int16_t &r);
        /// `n` frames of silence through bus_dsp, none to the tap or the mix.
        void bus_dsp_silence(uint64_t n);
        /// `cycles` with the bus stopped and RUN set: silence to the vis tap at the stream's rate.
        void vis_quiet(uint64_t cycles);
        /// VOLL or VOLR on each SID's volume: its $D418 as written, the low nibble scaled.
        void sid_volume();
        uint8_t sid_mode_vol(unsigned chip) const
        {
            const unsigned v = vol_[chip & 1], mv = written_[chip & 1][SID_MODE_VOL];
            return static_cast<uint8_t>((mv & 0xF0) | ((mv & 0x0F) * v + 127) / 255);
        }
        /// The mix takes the bus from now: the SIDs' samples made up to now, so the bus's next lines up with theirs.
        void mix_anchor();
        /// An audio rate is set, so the bus's frames (while the stream plays or runs dry) are resampled into the mix.
        bool mixing() const { return audio_rate_ != 0; }
        /// DOCDATA: wave RAM or a register at the DOC's address, which moves on with DOCCTL bit 5.
        uint8_t doc_data_read();
        void doc_data_write(uint8_t data);
        void doc_step()
        {
            if (docctl_ & DOCCTL_AUTOINC)
                doc_addr_ = (doc_addr_ + 1) & (Es5503::RAM_BYTES - 1);
        }
        /// EQVAL at EQIDX, then EQIDX moves on (bits 0-6; bit 7 stays).
        uint8_t eq_value() const;
        void eq_step() { eqidx_ = static_cast<uint8_t>((eqidx_ & EQIDX_ENABLE) | ((eqidx_ + 1) & 0x7F)); }
        /// A new vis page, from the bus as the vis tap has had it.
        void vis_snapshot();
        /// Revision 2's registers to their /RESET values.
        void reset_rev2();
        /// $48-$63 on a revision 1 card: nothing there.
        bool hidden(unsigned o) const { return rev_ < 2 && o >= PCMCTL && o <= DRQSEL; }
        bool io_in() const;
        uint8_t card_read(uint16_t addr);
        uint8_t card_peek(uint16_t addr) const;
        void card_write(uint16_t addr, uint8_t data);
        /// Which SID (0 or 1) answers $D400-$D7FF address `addr`.
        unsigned sid_at(uint16_t addr) const;
        void cia_reset();
        void cia_tick(unsigned cycles);
        uint8_t cia_read(unsigned reg);
        void cia_write(unsigned reg, uint8_t data);
        bool cia_irq() const { return cia_flags_ & cia_mask_ & 0x01; }
        bool vic_irq() const { return vic_flags_ & vic_mask_ & 0x01; }
        /// The frame's clock: a frame each FRAME_PAL or FRAME_NTSC SID clocks.
        void vic_tick(unsigned cycles);
        void reset_cpu();
        uint64_t now() const;
        /// Clock the SIDs up to now(). Const: the debugger looks through peek().
        void catch_up() const;
        /// `clocks` SID clocks through the output stage, into audio_.
        void render(uint64_t clocks) const;
        void tune_clocks();

        mutable std::array<Sid, 2> chips_{};
        mutable uint64_t sids_at_{0}; ///< the SID clock the chips have been clocked to
        uint32_t audio_rate_{0};
        mutable std::deque<float> audio_;          ///< left, right, ...: trimmed at the front
        mutable double sum_l_{0}, sum_r_{0};       ///< this sample's output so far
        mutable uint64_t sample_clocks_{0};        ///< and its clocks so far
        mutable uint64_t sample_length_{0};        ///< the clocks this sample spans
        mutable uint64_t sample_phase_{0};         ///< SID Hz owed to the next sample's length
        mutable std::array<float, 2> hp_in_{}, hp_out_{};
        float hp_pole_{0};
        std::array<uint8_t, RAM_BYTES> ram_{};
        uint8_t ctrl_{0};
        uint8_t ien_{0};
        uint16_t ram_addr_{0};
        uint8_t to_card_{0}, to_host_{0};
        bool to_card_full_{false}, to_host_full_{false};
        unsigned cpu_resets_{0};

        // Revision 2 (hw/sound-rev2.md)
        unsigned rev_{2};
        uint8_t pcmctl_{0}, codec_{0}, pcmfmt_{0};
        uint16_t rate_{0};
        // The stream: the FIFO a ring, on the heap; RATE and PCMFMT as RUN took them.
        std::unique_ptr<std::array<uint8_t, FIFO_BYTES>> fifo_ = std::make_unique<std::array<uint8_t, FIFO_BYTES>>();
        size_t fifo_head_{0}, fifo_count_{0};
        uint8_t state_{STATE_IDLE};
        uint8_t resume_{STATE_BUFFERING}; ///< the state PAUSE's release goes back to
        uint32_t play_rate_{0};
        uint8_t play_fmt_{0};
        uint8_t play_codec_{0};            ///< CODEC as RUN took it
        std::unique_ptr<Mp3> mp3_;
        uint64_t pcm_phase_{0};           ///< host cycles x rate owed, below one frame
        unsigned underruns_{0};
        bool tap_on_{false};
        bool tap_silence_{true};
        std::vector<int16_t> tap_;
        // The mix: the bus resampled to the audio rate by linear interpolation, waiting for the SIDs' samples it is
        // added to (render() takes one frame a sample). Mutable: render() is the SIDs' catch-up, const.
        mutable std::deque<float> pcm_mix_;
        float mix_prev_[2]{0, 0};          ///< the bus frame before the next
        uint64_t mix_acc_{0};              ///< the next output's place past mix_prev_, in 1/audio-rate frames
        bool bus_on_{false};               ///< the mix has its anchor for this run of the bus
        uint64_t mix_skip_{0};             ///< bus frames of a bulk catch-up that make nothing
        mutable uint64_t mix_offset_{0};   ///< SID samples to make before the queue's front is theirs
        uint64_t mix_frames_{0};
        mutable uint64_t mix_dry_{0};
        uint8_t free_hi_{0};               ///< FREE's high byte, as the last $4D read latched it
        uint32_t pos_{0}, pos_latch_{0};   ///< frames played; $52-$54 read the latch
        std::array<uint8_t, 2> vol_{};     ///< VOLL, VOLR
        uint8_t eqidx_{0};
        std::array<int8_t, EQ_BANDS + 1> eq_gain_{}; ///< the preamp, then the bands
        bool eq_on_{false};
        BusEq eq_;
        bool eq_dirty_{true};  ///< the gains or enable changed: the coefficients anew
        /// The bus's last rate: the one the EQ's coefficients are for, kept on every frame, the EQ on or off; and
        /// the rate of vis_quiet()'s silence when the stream has none (an MP3 sought, until its next first frame).
        uint32_t eq_rate_{0};
        BusVis vis_bus_;
        uint64_t vis_phase_{0}; ///< host cycles x rate owed to the vis tap's silence while the bus is stopped
        /// Each SID's registers as written ($00-$18; the read-only four 0): the chip has its $D418 scaled by VOLL
        /// or VOLR.
        std::array<std::array<uint8_t, SID_REGS>, 2> written_{};
        uint8_t visidx_{0};
        std::array<uint8_t, VIS_BYTES> vis_{};
        uint8_t docctl_{0};
        uint32_t doc_addr_{0};
        uint8_t drqsel_{DRQ_RAMDATA};
        Es5503 doc_;

        CardBus bus_{*this};
        Cpu6502 cpu_;
        uint32_t host_hz_{NATIVE_HZ};
        uint64_t phase_{0}; ///< SID clocks x host Hz owed, below one SID clock
        int64_t owed_{0};   ///< SID clocks the 6510 is behind (negative: ahead)
        uint64_t clock_{0}; ///< SID clocks passed, to the end of the 6510's last instruction
        uint64_t step_start_{0};
        bool in_step_{false};
        uint8_t ddr_{0}, port_{0};
        uint16_t cia_latch_{0xFFFF}, cia_counter_{0xFFFF};
        uint8_t cia_cra_{0}, cia_flags_{0}, cia_mask_{0};
        uint32_t vic_count_{0}; ///< SID clocks to the next frame (0: a frame from now)
        uint8_t vic_flags_{0}, vic_mask_{0};
    };
} // namespace ooe::emu
