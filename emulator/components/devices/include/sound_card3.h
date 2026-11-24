#pragma once

/**
 * @file sound_card3.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The sound card, revision 3: the device's registers over its firmware (hw/future/sound-device.md).
 * @version 0.1.0
 * @date 2026-11-23
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The front end: the card's block at $C500-$C57F (module A), the command and event FIFOs and DATA as the
 * firmware (sound_fw.h) holds them, DMA channel 2's request, the interrupt (ISR and IEN), CTRL's strobes,
 * /RESET and READY. It implements the firmware's port (SoundFwPort) over the card's parts.
 *
 * Time: ticked in host CPU cycles (set_cpu_hz() says how fast); the firmware's 48 kHz samples fall at exact
 * host cycles (sample k at the first cycle at or past k x host Hz / 48,000), so how the card is ticked
 * changes nothing.
 *
 * Registers (offset, name): $00 CTRL (bit 0 DRQ; bit 6 FWRESTART and bit 7 RESET, strobes), $01 STATUS
 * (READY, CMDRDY, EVT, XFER, DATA, EVLOST, ERR, the card interrupting), $02 ERROR, $03 CMD (W), $04 CMDFREE,
 * $05 EVT (a read takes the byte), $06 EVTLEN, $07 DATA (W), $08-$0A XLEFT (reading $08 latches all three),
 * $0B XTAG, $0C IEN, $0D ISR (bit 0 EVT, a level; bits 1-3 XDONE, CMDROOM, READY, cleared by writing 1),
 * $0E MEMSIZE, $0F FEATURES, $10-$11 FWVER, $7F ID ($A3). Every other offset reads 0 and ignores writes.
 *
 * The card's chips are revision 2's card (sound_card.h) used as a part: its 6510 with the C64-like map, CIA 1
 * and the frame interrupt, and both SIDs, driven through its own registers by the port, never by the bus; so a
 * tune's 6510 runs the same code as revision 2's, cycle for cycle (the stub, the decode: a second SID at $D420
 * or $D500), except that a one-SID tune plays on SID A alone (no BOTH).
 *
 * /RESET (bus_reset) and CTRL's RESET do the same: an open transfer abandoned, both FIFOs emptied, every
 * sound stopped, CTRL, IEN and ISR 0; the firmware runs on and the store keeps its objects. FWRESTART starts
 * the firmware over: READY clear for its start-up, the store emptied.
 */

#include <array>
#include <cstdint>
#include <memory>

#include "bus_cycle.h"
#include "device.h"
#include "es5503.h"
#include "sound_card.h"
#include "sound_fw.h"

namespace ooe::emu
{
    class SoundCard3 : public Device, private SoundFwPort
    {
    public:
        static constexpr uint8_t ID = 0xA3;
        static constexpr uint32_t NATIVE_HZ = 7'159'090;
        enum Reg : uint8_t
        {
            CTRL = 0x00,
            STATUS = 0x01,
            ERROR = 0x02,
            CMD = 0x03,
            CMDFREE = 0x04,
            EVT = 0x05,
            EVTLEN = 0x06,
            DATA = 0x07,
            XLEFT = 0x08, ///< $08-$0A
            XTAG = 0x0B,
            IEN = 0x0C,
            ISR = 0x0D,
            MEMSIZE = 0x0E,
            FEATURES = 0x0F,
            FWVER = 0x10, ///< major; $11 minor
            ID_REG = 0x7F,
        };
        static constexpr uint8_t CTRL_DRQ = 0x01, CTRL_FWRESTART = 0x40, CTRL_RESET = 0x80;
        static constexpr uint8_t STATUS_READY = 0x01, STATUS_CMDRDY = 0x02, STATUS_EVT = 0x04, STATUS_XFER = 0x08,
                                 STATUS_DATA = 0x10, STATUS_EVLOST = 0x20, STATUS_ERR = 0x40, STATUS_IRQ = 0x80;
        static constexpr uint8_t INT_EVT = 0x01, INT_XDONE = 0x02, INT_CMDROOM = 0x04, INT_READY = 0x08;

        /// `memsize_units`: the store in 64 KB units, 32, 64 or 128 (2, 4 or 8 MB).
        explicit SoundCard3(unsigned memsize_units = 128);
        ~SoundCard3() override;
        SoundCard3(const SoundCard3 &) = delete;
        SoundCard3 &operator=(const SoundCard3 &) = delete;

        void set_cpu_hz(uint32_t hz);
        uint32_t cpu_hz() const { return host_hz_; }

        // Device
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        void bus_reset() override;
        bool irq() const override
        {
#ifdef EMU_FAULT_HOOKS
            if (irq_dead_)
                return false;
#endif
            return (isr() & ien_) != 0;
        }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;
        bool drq() const override { return (ctrl_ & CTRL_DRQ) && fw_.xfer_open(); }
        void dack_write(uint8_t data, bool tc) override;
        /// A word grant (channel 2 with WIDE): DATA takes its two bytes, the low first.
        void dack_write_word(uint16_t data, bool tc) override;

        /// The card's chips (the debugger's and the tests' view: nothing on the bus reaches them).
        SoundCard &chips() { return chips_; }
        const SoundCard &chips() const { return chips_; }
        /// Another observer of the 6510's bus cycles, after the card's own; null for none.
        void set_cpu_observer(CycleObserver *observer) { watch_.next = observer; }
        /// The rate the SIDs' sound is made at (their samples, each chip's alone, as the ADC takes them); 0 none.
        /// A tune's start begins its samples afresh, so they fall alike relative to the 6510's reset.
        void set_adc_rate(uint32_t rate);
        uint32_t adc_rate() const { return adc_rate_; }
        /// The card's sound for the host, resampled to `rate` (0: none made for the host; the card plays on).
        void set_audio_rate(uint32_t rate);
        uint32_t audio_rate() const { return audio_rate_; }
        /// The card's sound at the audio rate, left then right, -1 to 1: up to `max_frames`. @return frames
        size_t take_audio(float *out, size_t max_frames);
        /// Tests: the firmware's tap (SoundFw::set_tap): the output, or one source alone, at 48 kHz.
        void set_tap(bool on, uint8_t source = SoundFw::SOURCES) { fw_.set_tap(on, source); }
        std::vector<int16_t> take_tap() { return fw_.take_tap(); }
        /// Tests: a copy of the ADC's samples as the firmware takes them (SID A, SID B), kept until taken.
        void set_adc_copy(bool on)
        {
            adc_copy_on_ = on;
            adc_copy_.clear();
        }
        std::vector<float> take_adc_copy()
        {
            std::vector<float> out;
            out.swap(adc_copy_);
            return out;
        }
        Es5503 &doc() { return doc_; }
        const Es5503 &doc() const { return doc_; }
        /// Tunes started since power-on (tests: the moment a TUNE_PLAY ran).
        uint64_t tune_starts() const { return starts_; }

        SoundFw &firmware() { return fw_; }
        const SoundFw &firmware() const { return fw_; }
        /// Bytes DATA dropped (none open), for tests.
        uint64_t data_dropped() const { return dropped_; }
#ifdef EMU_FAULT_HOOKS
        /// Test-only (fault_devices.h; the diagnostics' stage 10): the card's interrupt line dead, irq() false
        /// whatever ISR and IEN say; and the `n`th byte DMA brings to DATA from now (1-based; 0 none) lost on
        /// its way in, as a landing buffer that drops one would. Compiled only into the native test build
        /// (devices/CMakeLists.txt: EMU_FAULT_HOOKS), never the WebUI's.
        void set_irq_dead(bool dead) { irq_dead_ = dead; }
        void set_dma_drop(uint64_t n) { dma_drop_ = n; }
        /// Test-only: the `n`th byte DMA brings to DATA from now arrives with every bit flipped.
        void set_dma_corrupt(uint64_t n) { dma_corrupt_ = n; }
#endif

    private:
        // SoundFwPort
        void silence() override;
        void tune_start(const SidPlan &plan, bool ntsc, bool sid_b_d500) override;
        void tune_stop() override;
        void tune_hold(bool held) override;
        bool tune_jammed(uint16_t &pc) override;
        uint64_t play_calls() override { return watch_.calls; }
        uint32_t play_cycles() override { return watch_.last_play; }
        void sid_regs(unsigned chip, uint8_t out[25]) override;
        void sid_write(unsigned chip, unsigned reg, uint8_t data) override;
        uint32_t adc_hz() override { return adc_rate_; }
        void adc_take(std::vector<float> &out) override;
        void doc_write(uint8_t reg, uint8_t data) override { doc_.write(reg, data); }
        uint8_t doc_read(uint8_t reg) override { return doc_.read(reg); }
        void doc_ram(uint32_t addr, const uint8_t *bytes, size_t n) override;
        void doc_frame(int16_t out[4]) override;

        /// The 6510's bus: play calls (the stub's $DE00 writes) and their cost from the frame's interrupt.
        struct PlayWatch : CycleObserver
        {
            const SoundCard *card{nullptr};
            CycleObserver *next{nullptr};
            uint64_t calls{0}, irq_at{0};
            uint32_t last_play{0};
            void on_cycle(const BusCycle &c) override
            {
                if (!c.write && c.vector_pull && c.addr == 0xFFFE)
                    irq_at = c.cycle;
                else if (c.write && c.addr == 0xDE00 && card->io_visible())
                {
                    ++calls;
                    last_play = static_cast<uint32_t>(c.cycle - irq_at);
                }
                if (next)
                    next->on_cycle(c);
            }
        };
        void run_chips(uint64_t cycles); ///< the chips, and the mailbox the stub writes taken
        /// Before a tune's start: the ADC's samples so far into the firmware's queue (and the tests' copy), so
        /// none made before the start is taken as after it.
        void fw_adc_flush() { fw_.pull_adc(); }

        void catch_up();       ///< the firmware to the host's cycle
        void take_edges();     ///< ISR's latched bits from the firmware
        uint8_t isr() const { return static_cast<uint8_t>(isr_ | (fw_.evt_len() ? INT_EVT : 0)); }
        uint8_t status() const;
        void strobe_reset();

        SoundCard chips_;
        PlayWatch watch_;
        uint32_t adc_rate_{SoundFw::RATE};
        bool adc_copy_on_{false};
        std::vector<float> adc_copy_;
        Es5503 doc_;
        uint64_t doc_acc_{0};       ///< the ES5503's clocks owed, x 48,000
        int64_t doc_sum_[4]{0, 0, 0, 0};
        unsigned doc_steps_{0};
        int16_t doc_last_[4]{0, 0, 0, 0};
        uint32_t audio_rate_{0};
        std::vector<int16_t> out48_; ///< the firmware's output waiting for the host
        size_t out_at_{0};           ///< the frame next
        uint64_t out_phase_{0};      ///< in 1/audio-rate frames, x 48,000
        uint8_t chips_ctrl_{0};          ///< the chips' CTRL while a tune runs (RESET not kept)
        bool tune_two_{false};           ///< the tune's chips: both (true), or SID A alone
        /// The tune's chips silenced (their 25 registers 0, as stop_sid() does both): SID A, and SID B for a two-SID tune.
        void stop_tune_chips();
        std::array<uint8_t, 2> held_vol_{}; ///< each SID's $18 as a pause found it
        uint64_t starts_{0};
        SoundFw fw_;
        uint32_t host_hz_{NATIVE_HZ};
        uint64_t phase_{0};    ///< host cycles x 48,000 owed below one sample
        uint8_t ctrl_{0}, ien_{0}, isr_{0};
        uint32_t xlatch_{0};
        uint64_t dropped_{0};
#ifdef EMU_FAULT_HOOKS
        bool irq_dead_{false}; ///< set_irq_dead()'s, test-only
        uint64_t dma_drop_{0}; ///< set_dma_drop()'s: DMA's bytes to go before the lost one
        uint64_t dma_corrupt_{0}; ///< set_dma_corrupt()'s: DMA's bytes to go before the flipped one
#endif
    };
} // namespace ooe::emu
