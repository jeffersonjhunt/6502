#pragma once

/**
 * @file es5503.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The Ensoniq ES5503 (the DOC), as sound card revision 2 emulates it.
 * @version 0.1.0
 * @date 2026-11-05
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/sound-rev2.md ("ES5503") is the spec, proposed. The chip alone, with no
 * knowledge of the card: its 32 oscillators and its 128 KB of wave RAM.
 *
 *   $00-$1F frequency, low byte    $20-$3F frequency, high byte
 *   $40-$5F volume                 $60-$7F data: the last sample, read only
 *   $80-$9F wave table pointer     $A0-$BF control: bit 0 halt, bits 1-2 mode,
 *       bit 3 IE, bits 4-7 channel
 *   $C0-$DF table: bit 6 bank, bits 3-5 size, bits 0-2 resolution
 *   $E0 interrupt: reading it takes the lowest pending oscillator,
 *       its number in bits 1-5 (bit 7 clear), and acknowledges it; with
 *       none pending, the last one taken with bit 7 set ($FF at reset).
 *       Bits 0 and 6 read 1.
 *   $E1 enable: (oscillators - 1) x 2    $E2 A/D: no input, $80
 *
 * The registers read back as MAME's ES5503 keeps them (read for its meaning,
 * no code taken): the table register from its bank, size and resolution, so
 * bit 7 of a write to it reads 0; writes to the data registers, $E0 and $E2
 * are ignored; $E3-$FF read 0 and ignore writes. The bank is a latch of its
 * own: a pointer write leaves it (MAME packs the two into one value, so its
 * pointer write clears the bank; not copied).
 *
 * Sound (step 6). step() makes one output sample, every
 * clocks_per_sample() of the chip's 7,159,090 Hz (8 x (enabled + 2)): each
 * enabled oscillator not halted, in number order, steps once. Its 24-bit
 * accumulator's index (accumulator >> (9 + resolution - size), the table's
 * size masked) picks a byte at its table (pointer x 256, the size's low bits
 * dropped, in its bank); then the frequency is added. A byte of $00 halts it
 * (and plays nothing). Otherwise (byte - 128) x volume goes to its channel,
 * and an index at the table's last byte or past is the end of the table:
 * free run loops (the phase kept: the table's size less one taken off);
 * one-shot halts; swap halts and starts its partner (number XOR 1) from 0; an
 * even one whose partner swaps, itself not swapping, starts again; sync/AM
 * loops, and an even one restarts the odd one below it (oscillator 0 has none).
 * In sync/AM an odd oscillator plays nothing, and its byte becomes the volume
 * of the one above it if that one runs (31 has none). The accumulator is 24
 * bits and a carry: a frequency that carries it past the top leaves an index
 * past the table, which the next step judges the end (as MAME's sum does);
 * that takes the carry off again, or halts. A loop never takes the
 * accumulator below 0: only where the size taken off is more than the
 * accumulator holds (MAME's would wrap: an even one's second loop at one end,
 * its partner swapping, or its loop at a $00 before the table's last byte) it
 * goes to 0, one end a pass; otherwise a loop is as ever, after a $00 too. A
 * halt with IE set makes the oscillator pending and raises irq(); a $E0 read
 * drops it, and raises it
 * again while an enabled oscillator is still pending. Halting one-shot or swap
 * from a control write does what the end of the table would (an interrupt
 * only if both the old and the new value have IE); clearing the halt bit
 * starts it from 0.
 * out() is each of the 16 channels' sum, stereo() the even channels' and the
 * odd's: integers, the card scales them (step 7). Unlike MAME, the highest
 * enabled oscillator is mixed once, not three times.
 *
 * reset() is /RESET: every oscillator's halt bit set and its accumulator 0,
 * no interrupt pending and irq() low, $E1 0 (one oscillator), the data
 * registers $80. The other registers and the wave RAM keep what they hold.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace ooe::emu
{
    class Es5503
    {
    public:
        static constexpr unsigned OSCILLATORS = 32;
        static constexpr size_t RAM_BYTES = 0x20000; ///< 128 KB: the table register's bank bit is address bit 16
        static constexpr uint32_t CLOCK_HZ = 7'159'090;

        enum Reg : uint8_t
        {
            FREQ_LO = 0x00,
            FREQ_HI = 0x20,
            VOLUME = 0x40,
            DATA = 0x60,
            POINTER = 0x80,
            CONTROL = 0xA0,
            TABLE = 0xC0,
            INTERRUPT = 0xE0,
            ENABLE = 0xE1,
            ADC = 0xE2,
        };
        static constexpr uint8_t CONTROL_HALT = 0x01;
        static constexpr uint8_t TABLE_BANK = 0x40;
        static constexpr uint8_t ADC_NONE = 0x80;

        Es5503() { reset(); }
        /// /RESET: every oscillator halted, nothing pending, one oscillator enabled. Wave RAM stays.
        void reset();
        /// A read of register `reg`: $E0 acknowledges the interrupt it reports.
        uint8_t read(uint8_t reg);
        /// The same without acknowledging, for the debugger.
        uint8_t peek(uint8_t reg) const;
        void write(uint8_t reg, uint8_t data);

        /// The oscillators $E1 enables, 1 to 32.
        unsigned enabled() const { return enabled_; }
        static constexpr unsigned CHANNELS = 16;
        static constexpr uint8_t CONTROL_IE = 0x08;
        enum Mode : uint8_t
        {
            MODE_FREE = 0,
            MODE_ONE_SHOT = 1,
            MODE_SYNC_AM = 2,
            MODE_SWAP = 3,
        };
        /// The chip's clocks an output sample takes: 8 x (enabled + 2); 272 with all 32, about 26,320 Hz.
        uint32_t clocks_per_sample() const { return 8u * (enabled_ + 2u); }
        /// The output rate, CLOCK_HZ / clocks_per_sample(), rounded down.
        uint32_t output_hz() const { return CLOCK_HZ / clocks_per_sample(); }
        /// One output sample: every enabled oscillator not halted steps once, lowest first.
        void step();
        /// The last sample's sum on each channel: (byte - 128) x volume over its oscillators.
        const std::array<int32_t, CHANNELS> &out() const { return out_; }
        /// The even channels' sum (left), the odd's (right).
        std::array<int32_t, 2> stereo() const;
        /// The interrupt line: an oscillator has become pending, and no $E0 read has taken them all.
        bool irq() const { return irq_; }
        /// Oscillator `n`'s accumulator, 24 bits (for tests and the debugger).
        uint32_t accumulator(unsigned n) const { return osc_[n % OSCILLATORS].acc & 0xFFFFFFu; }
        /// The wave RAM, RAM_BYTES of it.
        std::array<uint8_t, RAM_BYTES> &ram() { return *ram_; }
        const std::array<uint8_t, RAM_BYTES> &ram() const { return *ram_; }

    private:
        struct Oscillator
        {
            uint16_t freq{0};
            uint8_t volume{0};
            uint8_t data{0x80};
            uint8_t pointer{0};
            uint8_t control{CONTROL_HALT};
            uint8_t table{0}; ///< bank, size and resolution, as the table register reads
            bool pending{false};
            uint32_t acc{0}; ///< 24 bits, and a carry until the next step takes it
        };
        /// The end of oscillator `n`'s table, or a $00 byte (`zero`), or a halt written with `ctrl` (end of table
        /// rules, IE from `ctrl`): its accumulator `acc` as the step has it.
        void halt(unsigned n, bool zero, uint32_t &acc, uint8_t ctrl);
        static unsigned mode(const Oscillator &o) { return (o.control >> 1) & 3u; }
        static unsigned size(const Oscillator &o) { return (o.table >> 3) & 7u; }
        static unsigned shift(const Oscillator &o) { return 9u + (o.table & 7u) - size(o); }

        std::array<Oscillator, OSCILLATORS> osc_{};
        unsigned enabled_{1};
        uint8_t last_irq_{0xFF}; ///< what $E0 reads with nothing pending
        bool irq_{false};
        std::array<int32_t, CHANNELS> out_{};
        /// On the heap, 128 KB: a fixed size, which the card's 17-bit address can't pass.
        std::unique_ptr<std::array<uint8_t, RAM_BYTES>> ram_ = std::make_unique<std::array<uint8_t, RAM_BYTES>>();
    };
} // namespace ooe::emu
