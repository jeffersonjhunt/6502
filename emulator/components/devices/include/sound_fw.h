#pragma once

/**
 * @file sound_fw.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The sound card's firmware, revision 3: the card as a device (hw/future/sound-device.md).
 * @version 0.1.0
 * @date 2026-11-23
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Portable C++ with no emulator dependency (the spec's Q2): it is the card's
 * code on a part that runs C++, or the reference model an FPGA is tested
 * against. It talks to the chips only through SoundFwPort, which the
 * emulator's front end (sound_card3.h) implements over its 6510, SIDs and
 * ES5503, and a test implements as a fake.
 *
 * Time is the card's own: a count of 48 kHz samples (the digital path's
 * rate), advanced by advance(). A millisecond is 48 samples, so the
 * firmware's millisecond steps fall on exact sample counts. A command record
 * runs at the first sample at least LATENCY_US after its last byte came,
 * once the commands before it have run and the event FIFO has room for its
 * reply; the start-up after power-on or FWRESTART takes STARTUP_MS. Results
 * depend on the samples advanced and the bytes given at each, never on how
 * the caller splits its calls.
 *
 * The host's side is the card's registers' (the front end maps them): the
 * command FIFO (CMD, CMDFREE), the event FIFO (EVT, EVTLEN), DATA for the
 * open transfer (XLEFT, XTAG), ERROR and the status bits. The records,
 * commands, events and their layouts are the spec's, with the interface
 * details of its S1 appendix.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "sid_plan.h"
#include "sound_dsp.h"

namespace ooe::emu
{
    /// CRC-32 (polynomial $04C11DB7 reflected, from $FFFFFFFF, inverted): CHECKSUM's and the firmware image's.
    class Crc32
    {
    public:
        void add(uint8_t b);
        void add(const uint8_t *p, size_t n)
        {
            for (size_t i = 0; i < n; ++i)
                add(p[i]);
        }
        uint32_t value() const { return ~state_; }
        static uint32_t of(const uint8_t *p, size_t n)
        {
            Crc32 c;
            c.add(p, n);
            return c.value();
        }

    private:
        uint32_t state_{0xFFFFFFFFu};
    };

    /// What the firmware needs of the chips. The emulator's front end implements it over its parts; a real
    /// card's would be pins or an FPGA's cores. Every call is at the firmware's current sample.
    class SoundFwPort
    {
    public:
        virtual ~SoundFwPort() = default;
        /// Every chip quiet: the 6510 stopped, both SIDs' registers 0, the ES5503's oscillators halted.
        virtual void silence() = 0;

        // The 6510 and the tune's SIDs.
        /// Reset the chips (the 6510, CIA 1 and both SIDs as at power-on), set their model, put the plan's blocks
        /// into the 6510's RAM and reset it running, on the NTSC clock or PAL's, its second SID at $D500 or $D420 (today's decode).
        virtual void tune_start(const SidPlan &plan, bool ntsc, bool sid_b_d500) = 0;
        /// Stop the 6510 and silence both SIDs.
        virtual void tune_stop() = 0;
        /// Hold the 6510 where it is, each SID's volume nibble 0 (true); or put them back and run on (false).
        virtual void tune_hold(bool held) = 0;
        /// The 6510 met a JAM opcode: true, and its address.
        virtual bool tune_jammed(uint16_t &pc) = 0;
        /// The play calls since tune_start(): the stub's writes to $DE00.
        virtual uint64_t play_calls() = 0;
        /// The 6510's cycles from the last frame's interrupt to its play call's end.
        virtual uint32_t play_cycles() = 0;
        /// SID `chip`'s 25 registers as last written, by the 6510 or the firmware.
        virtual void sid_regs(unsigned chip, uint8_t out[25]) = 0;
        /// A SID register written by the firmware (its own voices: effects, music).
        virtual void sid_write(unsigned chip, unsigned reg, uint8_t data) = 0;

        // The ADC: the SIDs' sound.
        /// The ADC's rate (48,000 on the card; 0: the SIDs aren't heard, as a front end may save their cost).
        virtual uint32_t adc_hz() = 0;
        /// The ADC's samples since the last call, appended: SID A's then SID B's, each chip's own output.
        virtual void adc_take(std::vector<float> &out) = 0;

        // The ES5503.
        virtual void doc_write(uint8_t reg, uint8_t data) = 0;
        virtual uint8_t doc_read(uint8_t reg) = 0;
        /// Wave RAM from `addr`.
        virtual void doc_ram(uint32_t addr, const uint8_t *bytes, size_t n) = 0;
        /// One frame of the digital path's (1/48,000 s): the chip stepped at its own rate, its output then,
        /// left and right as revision 2's bus has it (signed 16-bit).
        virtual void doc_frame(int16_t out[2]) = 0;
    };

    class SoundFw
    {
    public:
        // --- the card's constants ------------------------------------------------------------------------
        static constexpr uint32_t RATE = 48'000;                ///< the digital path, and the firmware's clock
        static constexpr uint32_t SAMPLES_PER_MS = RATE / 1000; ///< 48
        static constexpr uint32_t LATENCY_US = 20;              ///< a command runs this long after its last byte
        static constexpr uint32_t STARTUP_MS = 20;              ///< power-on or FWRESTART to READY
        static constexpr size_t CMD_BYTES = 1024, EVT_BYTES = 2048;
        static constexpr size_t RECORD_MAX = 256;               ///< a command record: 3 + n, n at most 253
        static constexpr uint8_t CARD_TYPE = 0xA3;
        static constexpr uint8_t VERSION_MAJOR = 1, VERSION_MINOR = 0;
        static constexpr uint16_t BUILD = 1;
        static constexpr unsigned TUNE_SLOTS = 4, SAMPLE_SLOTS = 256, EFFECTS = 256, MUSIC_SLOTS = 8, STREAMS = 2;

        enum Command : uint8_t
        {
            NOP = 0x00,
            IDENTIFY = 0x01,
            STOPALL = 0x02,
            FREEALL = 0x03,
            EVENTS = 0x04,
            LOSTCLR = 0x05,
            TIME = 0x06,
            SELFTEST = 0x07,
            CHECKSUM = 0x08,
            BOOTSOUND = 0x09,
            FW_BEGIN = 0x0D,
            FW_COMMIT = 0x0E,
            XFERABORT = 0x0F,
            TUNE_LOAD = 0x20,
            TUNE_PLAY = 0x21,
            TUNE_STOP = 0x22,
            TUNE_PAUSE = 0x23,
            TUNE_RESUME = 0x24,
            TUNE_STATUS = 0x25,
            TUNE_INFO = 0x26,
            TUNE_FREE = 0x27,
            SAMPLE_LOAD = 0x30,
            EFFECT_DEF = 0x31,
            EFFECT_LOAD = 0x32,
            PLAY = 0x33,
            VOICE_STOP = 0x34,
            BANK_FREE = 0x35,
            MUSIC_LOAD = 0x40,
            MUSIC_PLAY = 0x41,
            MUSIC_STOP = 0x42,
            MUSIC_PAUSE = 0x43,
            MUSIC_RESUME = 0x44,
            MUSIC_STATUS = 0x45,
            MUSIC_THRESH = 0x46,
            STREAM_OPEN = 0x50,
            STREAM_DATA = 0x51,
            STREAM_END = 0x52,
            STREAM_NEXT = 0x53,
            STREAM_PAUSE = 0x54,
            STREAM_RESUME = 0x55,
            STREAM_FLUSH = 0x56,
            STREAM_STATUS = 0x57,
            STREAM_CLOSE = 0x58,
            BEEP = 0x60,
            BELL = 0x61,
            VOLUME = 0x70,
            MIX = 0x71,
            EQ = 0x72,
            EQ_ALL = 0x73,
            EQ_ON = 0x74,
            VIS = 0x75,
            SIDREGS = 0x76,
            LEVELS = 0x77,
        };
        enum Kind : uint8_t
        {
            EV_NONE = 0x00,
            EV_REPLY = 0x01,
            EV_XFERDONE = 0x10,
            EV_FWRESULT = 0x11,
            EV_TUNE_STARTED = 0x20,
            EV_TUNE_STALL = 0x21,
            EV_TUNE_JAM = 0x22,
            EV_TUNE_ENDED = 0x23,
            EV_VOICE_DONE = 0x30,
            EV_VOICE_STOLEN = 0x31,
            EV_MUSIC_MARK = 0x40,
            EV_MUSIC_COUNT = 0x41,
            EV_MUSIC_DONE = 0x42,
            EV_NEED = 0x50,
            EV_UNDERRUN = 0x51,
            EV_UNDERRUN_END = 0x52,
            EV_TRACK = 0x53,
            EV_STREAM_ENDED = 0x54,
            EV_BEEP_DONE = 0x60,
        };
        enum Error : uint8_t
        {
            OK = 0,
            E_UNKNOWN = 1,
            E_PARAM = 2,
            E_SLOT = 3,
            E_MEMORY = 4,
            E_BUSY = 5,
            E_DATA = 6,
            E_XFER = 7,
            E_STATE = 8,
            E_NOTREADY = 9,
            E_JAM = 10,
            E_FIRMWARE = 11,
        };
        /// FEATURES ($0F) and FEATURES2 (IDENTIFY's +7).
        static constexpr uint8_t FEAT_TWO_SIDS = 0x01, FEAT_TUNES = 0x02, FEAT_ES5503 = 0x04, FEAT_PCM = 0x08,
                                 FEAT_MP3 = 0x10, FEAT_EQ = 0x20, FEAT_VIS = 0x40, FEAT_MORE = 0x80;
        static constexpr uint8_t FEAT2_TONE = 0x01, FEAT2_SID_MODEL = 0x02, FEAT2_UPDATE = 0x04;

        /// `memsize_units`: the store in 64 KB units (32, 64 or 128: 2, 4 or 8 MB). Powered on.
        explicit SoundFw(SoundFwPort &port, unsigned memsize_units = 128);
        ~SoundFw();
        SoundFw(const SoundFw &) = delete;
        SoundFw &operator=(const SoundFw &) = delete;

        // --- time ----------------------------------------------------------------------------------------
        /// `samples` of the card's clock: due commands run, millisecond steps, the digital path.
        void advance(uint64_t samples);
        uint64_t now() const { return now_; }
        /// Samples until the firmware next acts on its own (a millisecond step, a record falling due, a
        /// transfer's check, READY): the samples between are the digital path's alone, so a front end may
        /// clock its chips to that sample in one step. At least 1.
        uint64_t next_action() const;
        uint32_t ms() const { return static_cast<uint32_t>(now_ / SAMPLES_PER_MS); }

        // --- resets ----------------------------------------------------------------------------------------
        /// Power-on: the store emptied, READY after STARTUP_MS.
        void power_on();
        /// CTRL's RESET or the backplane's /RESET: an open transfer abandoned, both FIFOs emptied, every sound
        /// stopped, the volumes to 255 and the EQ off. The firmware runs on (READY stays); the store keeps its
        /// objects.
        void reset();
        /// FWRESTART: the firmware starts over, READY clear until it is up, the store emptied.
        void restart() { power_on(); }

        // --- the host's side ----------------------------------------------------------------------------
        bool ready() const { return ready_; }
        /// CMD: a record's next byte. False (dropped) with the FIFO full.
        bool cmd_write(uint8_t b);
        size_t cmd_free() const { return CMD_BYTES - cmd_used_; }
        /// CMDRDY: room for a whole record.
        bool cmd_ready() const { return cmd_free() >= RECORD_MAX; }
        /// EVT: the next byte, taken; 0 with none waiting.
        uint8_t evt_read();
        size_t evt_len() const { return evt_.size(); }
        /// DATA: a byte of the open transfer; false (dropped) with none open.
        bool data_write(uint8_t b);
        bool xfer_open() const { return xfer_.open; }
        uint32_t xleft() const { return xfer_.open ? xfer_.left : 0; }
        uint8_t xtag() const { return xfer_.tag; }
        uint8_t error() const { return error_; }
        bool err() const { return error_ != OK; }
        bool evlost() const { return lost_ != 0; }
        /// ISR's edges, taken by the front end: a transfer took its last byte; the command FIFO had been full
        /// and has room again; the firmware came up.
        bool take_xdone() { return take(xdone_); }
        bool take_cmdroom() { return take(cmdroom_); }
        bool take_ready() { return take(came_up_); }
        uint8_t memsize() const { return static_cast<uint8_t>(memsize_units_); }
        uint8_t features() const;
        uint8_t version_major() const { return VERSION_MAJOR; }
        uint8_t version_minor() const { return VERSION_MINOR; }

        // --- for tests and the debugger -----------------------------------------------------------------
        size_t store_bytes() const { return store_.size(); }
        /// The SIDs a tune holds: bit 0 SID A, bit 1 SID B.
        uint8_t tune_chips() const { return tune_.chips; }
        enum TuneState : uint8_t
        {
            T_STOPPED = 0,
            T_STARTING = 1,
            T_PLAYING = 2,
            T_PAUSED = 3,
            T_STALLED = 4,
            T_JAMMED = 5,
            T_ENDED = 6,
        };
        uint8_t tune_state() const { return tune_.state; }
        /// The digital path's mix sources (MIX): tunes, music, effects, streams, the beep.
        enum Source : uint8_t
        {
            SRC_TUNES = 0,
            SRC_MUSIC = 1,
            SRC_EFFECTS = 2,
            SRC_STREAMS = 3,
            SRC_BEEP = 4,
            SOURCES = 5,
        };
        /// Tests: the card's output (after the EQ and the volumes), signed 16-bit frames at 48 kHz, kept until
        /// taken; with `source` below SOURCES, that source alone before the mix instead.
        void set_tap(bool on, uint8_t source = SOURCES)
        {
            tap_on_ = on;
            tap_source_ = source;
            tap_.clear();
        }
        std::vector<int16_t> take_tap()
        {
            std::vector<int16_t> out;
            out.swap(tap_);
            return out;
        }
        /// The output for the host: signed 16-bit frames at 48 kHz since the last call, appended (at most a
        /// second is kept).
        void take_output(std::vector<int16_t> &out);
        /// The ADC's samples so far into the firmware's queue (a front end's, before it restarts them).
        void pull_adc() { port_.adc_take(adc_); }
        /// Tests: each stream's frames as they play, at the stream's own rate before any resampling (a TRACK's
        /// join continuing the same list), signed 16-bit, left then right; underruns' silence not in it.
        void set_stream_tap(bool on)
        {
            stream_tap_on_ = on;
            stream_tap_.clear();
        }
        std::vector<int16_t> take_stream_tap()
        {
            std::vector<int16_t> out;
            out.swap(stream_tap_);
            return out;
        }
        enum StreamState : uint8_t
        {
            S_IDLE = 0,
            S_BUFFERING = 1,
            S_PLAYING = 2,
            S_PAUSED = 3,
            S_UNDERRUN = 4,
            S_ENDED = 5,
            S_BAD_FORMAT = 0x80,
            S_NO_SYNC = 0x81,
        };
        uint8_t stream_state(unsigned s) const { return streams_[s % STREAMS].state; }
        /// A stream's ring in the store, bytes.
        size_t stream_ring(unsigned s) const { return rings_[s % STREAMS].len; }
        /// The voice manager's voices: the SIDs' six (A's 0-2, B's 3-5) and the ES5503's sixteen (each two
        /// oscillators, a pair for the pan), and who has each.
        enum VoiceUse : uint8_t
        {
            V_FREE = 0,
            V_EFFECT = 1,
            V_MUSIC = 2,
        };
        static constexpr unsigned SID_VOICES = 6, DOC_VOICES = 16, VOICES = SID_VOICES + DOC_VOICES;
        uint8_t voice_use(unsigned v) const { return voices_[v % VOICES].use; }
        uint8_t voice_handle(unsigned v) const { return voices_[v % VOICES].handle; }
        /// Wave RAM's use as the store's cache: the sample slots there now.
        unsigned cached_samples() const;
        enum MusicState : uint8_t
        {
            M_STOPPED = 0,
            M_PLAYING = 1,
            M_PAUSED = 2,
        };
        uint8_t music_state() const { return seq_.state; }
        /// The notes waiting (not yet begun), on every voice, in the block playing and those queued.
        uint32_t notes_waiting() const;
        bool music_tone_on() const { return mtone_.on; }
        bool beeping() const { return tone_.left > 0; }
        bool boot_sound_playing() const { return boot_.active; }
        size_t store_free() const;
        uint32_t events_lost() const { return lost_; }

        // --- an object in the store ---------------------------------------------------------------------
        struct Object
        {
            bool used{false};
            size_t at{0}, len{0}; ///< its run in the store
            uint32_t crc{0};      ///< of the bytes as they came
            bool loading{false};  ///< its transfer is open
        };

    private:
        struct Transfer
        {
            bool open{false};
            uint8_t cmd{0}, tag{0}, slot{0};
            uint32_t len{0}, left{0};
            Object *obj{nullptr};
            Crc32 crc;
            bool done{false}; ///< the last byte came; XFERDONE at the next sample
        };
        struct Pending
        {
            std::vector<uint8_t> rec;
            uint64_t due{0};
            uint8_t refused{OK}; ///< refused at the door (E_UNKNOWN, E_PARAM): answered so, in its turn
        };
        static bool take(bool &f)
        {
            const bool was = f;
            f = false;
            return was;
        }

        void step();        ///< one sample
        void run_commands();
        void ms_step();
        /// The record at the FIFO's head: whole, due, and its reply has room. Runs it and @return true.
        bool run_one();
        /// The command's parameter count, or -1 for any (checked by the command), -2 for not a command.
        static int params_for(uint8_t cmd);
        /// Bytes a reply to `cmd` may take (its results and the reply's own 4).
        static size_t reply_room(uint8_t cmd);
        /// The object a command waits on while its transfer is open, or null.
        Object *waits_on(const std::vector<uint8_t> &rec);
        void execute(const std::vector<uint8_t> &rec);

        // events
        void reply(uint8_t tag, uint8_t status, const std::vector<uint8_t> &results = {});
        /// An event of `kind` for `object` with `payload`, stamped; lost (counted) without room or when masked.
        void event(uint8_t kind, uint8_t object, const std::vector<uint8_t> &payload = {});
        size_t evt_room() const { return EVT_BYTES - evt_.size(); }

        // the store
        std::optional<size_t> allocate(size_t len);
        void free_object(Object &o);
        Object *object(uint8_t kind, uint8_t slot);
        void stop_all();
        void free_all();

        // transfers
        uint8_t open_transfer(uint8_t cmd, uint8_t tag, uint8_t slot, uint32_t len, Object &o);
        void finish_transfer();
        void abort_transfer();

        SoundFwPort &port_;
        unsigned memsize_units_;
        std::vector<uint8_t> store_;
        uint64_t now_{0};
        bool ready_{false};
        uint64_t ready_at_{0};
        bool came_up_{false}, xdone_{false}, cmdroom_{false}, cmd_was_full_{false};
        size_t cmd_used_{0};          ///< the command FIFO's bytes: the partial record and the whole ones waiting
        std::deque<Pending> records_; ///< whole records, waiting their turn
        std::vector<uint8_t> partial_;
        std::deque<uint8_t> evt_;
        uint8_t error_{OK};
        uint32_t lost_{0};
        uint8_t mask_{0xFF};
        Transfer xfer_;
        std::array<Object, TUNE_SLOTS> tunes_{};
        std::array<Object, SAMPLE_SLOTS> samples_{};
        std::array<Object, MUSIC_SLOTS> music_{};
        Object effects_{};
        std::array<Object, STREAMS> chunks_{}; ///< each stream's last chunk, for CHECKSUM
        Object image_{};                       ///< a firmware image received
        std::array<std::optional<SidFile>, TUNE_SLOTS> files_{}; ///< each tune slot's header, once checked

        struct Tune
        {
            uint8_t state{0}; ///< TuneState
            uint8_t slot{0}, chips{0};
            uint16_t song{0};
            bool has_play{false}, first{false}, ntsc{false};
            uint8_t held_state{0};      ///< the state a pause left
            uint32_t played_ms{0};      ///< paused time out
            uint32_t length_ms{0}, fade_ms{0};
            uint32_t quiet_ms{0};       ///< since the last play call (or the start, before the first)
            uint64_t calls{0};
        };
        Tune tune_;

        // --- streams ------------------------------------------------------------------------------------------
        static constexpr uint32_t CHUNK_DEFAULT = 32 * 1024, PREBUFFER_DEFAULT = 500, URGENT_MS = 1000;
        static constexpr size_t RING_MOST = 4u << 20; ///< a ring at most 4 MB, at most half the store free
        struct Mp3;
        struct Stream
        {
            uint8_t state{0};
            bool open{false}, paused{false}, ended{false}, need_out{false}, queued{false}, underrun{false};
            uint8_t codec{0}, fmt{0};
            uint32_t rate{0}, chunk{CHUNK_DEFAULT}, prebuffer_ms{PREBUFFER_DEFAULT};
            int next{-1};          ///< the stream that follows it (STREAM_NEXT)
            uint64_t rd{0}, wr{0}; ///< the ring's bytes read and written, from the start
            uint64_t frames{0}, missed{0};
            uint16_t underruns{0}, kbps{0};
            uint64_t phase{0};     ///< x 48,000: where the output is between prev and cur
            int16_t prev[2]{0, 0}, cur[2]{0, 0};
            std::shared_ptr<Mp3> mp3;
        };
        std::array<Stream, STREAMS> streams_{};
        std::array<Object, STREAMS> rings_{};
        bool stream_tap_on_{false};
        std::vector<int16_t> stream_tap_;
        std::vector<uint8_t> mp3_scratch_;
        uint8_t open_stream(const uint8_t *p);
        void close_stream(unsigned s);
        void reset_decoder(Stream &st);
        size_t frame_bytes(const Stream &st) const;
        uint32_t buffered_ms(const Stream &st) const;
        uint8_t ring_byte(unsigned s, uint64_t at) const;
        /// The next frame of stream `s` (or the stream that follows it) into `out`: false if none has come yet.
        bool next_frame(unsigned &s, int16_t out[2]);
        bool decode_mp3(unsigned s);
        void stream_frame(unsigned s, float out[2]);
        void streams_ms();

        // --- the digital path ---------------------------------------------------------------------------
        void mix_frame();
        void tone_start(uint32_t hz, uint32_t ms, uint8_t volume);
        struct Tone
        {
            double phase{0}, step{0};
            float amp{0};
            uint64_t left{0}; ///< samples to go
        };
        Tone tone_;
        std::vector<float> adc_;      ///< the ADC's samples waiting (A, B)
        size_t adc_at_{0};            ///< the pair next
        uint64_t adc_phase_{0};       ///< in 1/48,000ths of an ADC sample
        float adc_now_[2]{0, 0}, adc_prev_[2]{0, 0};
        float hp_in_[2]{0, 0}, hp_out_[2]{0, 0};
        std::array<uint8_t, SOURCES> mix_{255, 255, 255, 255, 255};
        uint8_t vol_l_{255}, vol_r_{255};
        std::array<int8_t, 11> eq_gain_{};
        bool eq_on_{false};
        BusEq eq_;
        BusVis vis_;
        std::array<uint8_t, BusVis::PAGE> vis_page_{};
        bool tap_on_{false};
        uint8_t tap_source_{SOURCES};
        std::vector<int16_t> tap_;
        std::deque<int16_t> out_;
        void reset_path(); ///< volumes 255, the EQ off and flat, the tone stopped

        // --- the sound bank --------------------------------------------------------------------------------
        struct Sample
        {
            uint32_t rate{0}, loop_start{0}, loop_end{0};
            uint8_t flags{0};
            bool ready{false};          ///< XFERDONE checked and converted
            // wave RAM: a cached table of 256 << size bytes at page (0-511), or none
            int page{-1};
            uint8_t size{0};
            uint64_t used{0};           ///< for the LRU
        };
        struct EffectDef
        {
            uint8_t kind{0xFF};         ///< 0 a SID effect, 1 a sample; 0xFF none
            std::vector<uint8_t> def;   ///< as EFFECT_DEF had it, after the kind
        };
        struct Voice
        {
            uint8_t use{V_FREE};
            uint8_t handle{0}, effect{0}, prio{0};
            uint64_t order{0};          ///< when it was given: the oldest is stolen first
            // a SID effect's player
            unsigned step{0};
            uint32_t step_ms{0};        ///< left in the step
            int32_t freq{0};            ///< Hz x 16
            int32_t pw{0};
            // a sample's player
            int sample{-1};
            uint8_t vol{0}, pan{128};
            uint32_t env_ms{0};         ///< since the start
            uint32_t attack{0}, hold{0}, release{0};
            bool releasing{false};
            uint32_t release_ms{0};
            bool looped{false};
        };
        std::array<Sample, SAMPLE_SLOTS> sample_info_{};
        std::array<EffectDef, EFFECTS> effects_def_{};
        std::array<Voice, VOICES> voices_{};
        uint64_t voice_order_{0}, cache_clock_{0};
        uint8_t next_handle_{1};
        std::array<bool, 512> wave_pages_{}; ///< wave RAM's 256-byte pages in use by the cache
        bool doc_all_{false};                ///< the ES5503 set for all 32 oscillators (the bank's)
        uint32_t sid_clock() const { return tune_.ntsc ? 1'022'727u : 985'248u; }
        bool sid_voice_free_for(unsigned v) const; ///< not a tune's chip, not music's
        int give_voice(bool sid, uint8_t prio, uint8_t &stolen_handle, uint8_t &stolen_effect);
        void free_voice(unsigned v, bool done_event);
        void silence_voice(unsigned v);
        uint8_t play_effect(const uint8_t *p, uint8_t &handle);
        bool cache_sample(unsigned slot);
        void sid_step(unsigned v);
        void voices_ms();
        void write_sid_freq(unsigned v, int32_t hz16);
        void doc_voice_start(unsigned v, unsigned slot, uint32_t rate);
        void doc_voice_volume(unsigned v, uint32_t level); ///< level 0-255 of the voice's volume, by its pan
        void stop_voices_on_chips(uint8_t chips);
        bool parse_effect(uint8_t effect, uint8_t kind, const uint8_t *def, size_t n);

        // --- music --------------------------------------------------------------------------------------------
        struct Instrument
        {
            uint8_t b[8]{};             ///< as the block has it: +0 the source (0 SID, 1 sample, 2 tone), ...
        };
        struct Entry
        {
            uint8_t kind{0}, voice{0};
            uint32_t freq16{0};         ///< Hz x 16 (a note's, worked out)
            uint32_t len{0}, gate{0};   ///< µs
            uint8_t value{0};           ///< a volume's, a marker's
            Instrument inst;
        };
        struct Block
        {
            uint8_t mask{0};
            std::array<Instrument, 8> inst{};
            std::vector<Entry> entries;
            std::array<uint32_t, 8> notes{}; ///< note, frequency and rest entries a voice
        };
        std::array<std::optional<Block>, MUSIC_SLOTS> blocks_{};
        struct MusicVoice
        {
            int phys{-1};               ///< its voice in voices_ (a SID's or the ES5503's), or -1 the tone voice
            bool tone{false};
            Instrument inst;
            uint8_t vol{255};
            std::vector<uint32_t> order; ///< its entries' places in the block, in order
            size_t pos{0};
            uint64_t next_us{0};        ///< when its next entry begins
            uint64_t gate_off_us{0};
            bool gated{false}, done{false};
            uint32_t waiting{0};        ///< its notes not yet begun
        };
        struct Music
        {
            uint8_t state{M_STOPPED};
            int slot{-1};
            bool free_when_done{false};
            std::deque<std::pair<uint8_t, bool>> queue; ///< blocks waiting: slot, free when done
            std::array<MusicVoice, 8> v{};
            uint64_t paused_at{0};
            uint64_t played_us{0}, started_us{0};
            uint32_t thresh{0};
        };
        Music seq_;
        struct MusicTone
        {
            bool on{false};
            double phase{0}, step{0};
            float amp{0};
        };
        MusicTone mtone_;
        uint64_t now_us() const { return now_ * 125 / 6; } ///< the card's clock in µs (48 kHz samples: 20.83 µs)
        uint8_t parse_block(unsigned slot);
        uint8_t start_block(uint8_t slot, bool free_when_done);
        void stop_music();
        void music_tick();
        void music_entry(unsigned lv, const Entry &e, uint64_t at);
        void music_gate(unsigned lv, bool on);
        void music_instrument(unsigned lv, const Instrument &in);
        uint32_t queued_notes(unsigned lv) const;

        // --- the boot sound (sound_flash.h's, as splash/docsnd.s played it on revision 2) -------------
        struct BootSound
        {
            bool active{false};
            uint32_t started_ms{0}, ticks{0};
            uint8_t tick{0}, mode{0}, run{0};
            uint16_t frame{0};
            std::array<uint16_t, 6> fc{}, fd{}, vl{}, dl{}, vr{}, dr{};
        };
        BootSound boot_;
        void boot_start();
        void boot_tick();
        void boot_frame();
        void boot_voice(unsigned x, uint8_t idx);
        void boot_write(unsigned x);
        void boot_ctl(unsigned x, uint8_t a);
        void tune_ms();
        void stop_tune(uint8_t state);
        uint8_t play_tune(const uint8_t *p);
        static constexpr uint32_t STALL_MS = 2000, FIRST_CALL_MS = 10000;
    };
} // namespace ooe::emu
