#pragma once

/**
 * @file hd44780.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief HD44780 character LCD, at the pin level, and its 6522 wiring.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Hd44780 is the controller seen from its pins: RS, R/W, E and D7..D0.
 * A write is latched on E's falling edge, a read is driven from E's rising
 * edge. In 4-bit mode only D7..D4 are used, high nibble first, for reads
 * and writes alike. Wired to four data lines, D3..D0 read as 0, which is
 * how the one-nibble "function set: 4-bit" at power-up works.
 *
 * Modelled: the instruction set (clear, home, entry mode, display
 * control, cursor/display shift, function set, CGRAM/DDRAM address), the
 * address counter with 1- and 2-line DDRAM layout, reads of AC and data,
 * and display shift for what is visible.
 *
 * DDRAM is 80 cells. In 2-line mode, line 2 is cells 40-79 and the
 * datasheet addresses them as $40-$67; an address in the unlisted
 * $28-$3F lands on cell 40 + (addr - $28) as well. That is inferred from
 * Eater's keyboard.s, whose Enter sets address $28 ("position 40") and
 * reaches line 2 on his board, not from the datasheet.
 *
 * Not modelled: execution time. The busy flag always reads clear, so a
 * program that polls it (all of Eater's) runs at full speed, and one that
 * forgets to never shows it. CGRAM is stored but custom characters render
 * as '?'; the A00 ROM's characters above $7D and its yen at $5C render as
 * ASCII or '?'.
 *
 * ViaLcd connects one to a Via6522: data on consecutive pins of one port
 * (4 or 8 of them), RS, R/W and E on any pins, as a machine description
 * wires it (hd44780 line, machine.h).
 */

#include <array>
#include <cstdint>
#include <string>

#include "peripheral.h"
#include "via6522.h"

namespace ooe::emu
{
    class Hd44780
    {
    public:
        Hd44780(unsigned columns = 16, unsigned rows = 2) : columns_(columns), rows_(rows) { ddram_.fill(' '); }

        /// The pins as they are now; edges on E do the work.
        void pins(bool rs, bool rw, bool e, uint8_t data);
        /// Whether the LCD is driving D7..D0 (a read with E high), and what.
        bool driving() const { return driving_; }
        uint8_t data_out() const { return data_out_; }

        /// Visible text of one row: `columns` characters.
        std::string line(unsigned row) const;
        unsigned columns() const { return columns_; }
        unsigned rows() const { return rows_; }
        /// Bumped by anything that can change what is shown.
        uint64_t version() const { return version_; }
        /// True once anything has been sent to it.
        bool used() const { return version_ != 0; }

        uint8_t address_counter() const { return ac_; }
        bool four_bit() const { return !eight_bit_; }

    private:
        void command(uint8_t c);
        void write_data(uint8_t d);
        uint8_t read_value(bool rs);
        void step_ac();
        /// The DDRAM cell (0-79) an address selects.
        unsigned cell(uint8_t addr) const;

        unsigned columns_, rows_;
        std::array<uint8_t, 80> ddram_{};
        std::array<uint8_t, 0x40> cgram_{};
        uint8_t ac_{0};
        bool cgram_mode_{false};
        bool eight_bit_{true};
        bool two_line_{false};
        bool display_on_{false};
        bool increment_{true};
        bool shift_on_entry_{false};
        int shift_{0};
        bool e_{false};
        bool low_nibble_{false}; ///< 4-bit: the next transfer is the low half
        uint8_t high_{0};        ///< 4-bit: the high half of a write
        uint8_t read_latch_{0};  ///< 4-bit: the byte being read
        bool driving_{false};
        uint8_t data_out_{0};
        uint64_t version_{0};
    };

    /// An Hd44780 on a 6522's pins.
    class ViaLcd : public Peripheral
    {
    public:
        ViaLcd(Via6522 &via, ViaPins data, ViaPin rs, ViaPin rw, ViaPin e, unsigned columns, unsigned rows);

        const Hd44780 &lcd() const { return lcd_; }

    private:
        void update();

        Via6522 &via_;
        ViaPins data_;
        ViaPin rs_, rw_, e_;
        Hd44780 lcd_;
    };
} // namespace ooe::emu
