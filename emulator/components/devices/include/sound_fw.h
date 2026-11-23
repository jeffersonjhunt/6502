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
            bool has_play{false}, first{false};
            uint8_t held_state{0};      ///< the state a pause left
            uint32_t played_ms{0};      ///< paused time out
            uint32_t length_ms{0}, fade_ms{0};
            uint32_t quiet_ms{0};       ///< since the last play call (or the start, before the first)
            uint64_t calls{0};
        };
        Tune tune_;
        void tune_ms();
        void stop_tune(uint8_t state);
        uint8_t play_tune(const uint8_t *p);
        static constexpr uint32_t STALL_MS = 2000, FIRST_CALL_MS = 10000;
    };
} // namespace ooe::emu
