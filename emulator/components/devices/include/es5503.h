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
 * knowledge of the card: its 32 oscillators' registers and its 128 KB of
 * wave RAM. So far (step 1 of that spec) it holds its registers only, and
 * makes no sound:
 *
 *   $00-$1F frequency, low byte    $20-$3F frequency, high byte
 *   $40-$5F volume                 $60-$7F data: the last sample, read only
 *   $80-$9F wave table pointer     $A0-$BF control: bit 0 halt, mode, IE, channel
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
 * are ignored; $E3-$FF read 0 and ignore writes.
 *
 * reset() is /RESET: every oscillator's halt bit set, no interrupt pending,
 * $E1 0 (one oscillator), the data registers $80. The other registers and the
 * wave RAM keep what they hold.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

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
        /// The wave RAM, RAM_BYTES of it.
        std::vector<uint8_t> &ram() { return ram_; }
        const std::vector<uint8_t> &ram() const { return ram_; }

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
        };

        std::array<Oscillator, OSCILLATORS> osc_{};
        unsigned enabled_{1};
        uint8_t last_irq_{0xFF}; ///< what $E0 reads with nothing pending
        std::vector<uint8_t> ram_ = std::vector<uint8_t>(RAM_BYTES); ///< on the heap: 128 KB
    };
} // namespace ooe::emu
