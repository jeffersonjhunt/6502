/**
 * @file hd44780.cpp
 * @brief HD44780 LCD controller and its 6522 wiring.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "hd44780.h"

namespace ooe::emu
{
    void Hd44780::pins(bool rs, bool rw, bool e, uint8_t data)
    {
        const bool rising = e && !e_;
        const bool falling = !e && e_;
        e_ = e;

        if (rising && rw)
        {
            if (eight_bit_)
                data_out_ = read_value(rs);
            else
            {
                if (!low_nibble_)
                {
                    read_latch_ = read_value(rs);
                    data_out_ = static_cast<uint8_t>(read_latch_ & 0xF0);
                }
                else
                    data_out_ = static_cast<uint8_t>(read_latch_ << 4);
                low_nibble_ = !low_nibble_;
            }
        }
        driving_ = e && rw;

        if (falling && !rw)
        {
            if (eight_bit_)
                rs ? write_data(data) : command(data);
            else if (!low_nibble_)
            {
                high_ = static_cast<uint8_t>(data & 0xF0);
                low_nibble_ = true;
            }
            else
            {
                const auto v = static_cast<uint8_t>(high_ | (data >> 4));
                low_nibble_ = false;
                rs ? write_data(v) : command(v);
            }
        }
    }

    unsigned Hd44780::cell(uint8_t addr) const
    {
        if (!two_line_)
            return addr % 80u;
        return (addr & 0x40) ? 40u + (addr & 0x3Fu) % 40u : (addr & 0x3Fu) % 80u;
    }

    uint8_t Hd44780::read_value(bool rs)
    {
        if (!rs)
            return ac_; // busy flag (bit 7) always clear
        const uint8_t v = cgram_mode_ ? cgram_[ac_ & 0x3F] : ddram_[cell(ac_)];
        step_ac();
        return v;
    }

    void Hd44780::step_ac()
    {
        if (cgram_mode_)
        {
            ac_ = static_cast<uint8_t>((ac_ + (increment_ ? 1 : -1)) & 0x3F);
            return;
        }
        if (two_line_)
        {
            if (increment_)
                ac_ = ac_ == 0x27 ? 0x40 : ac_ == 0x67 ? 0x00 : static_cast<uint8_t>(ac_ + 1);
            else
                ac_ = ac_ == 0x40 ? 0x27 : ac_ == 0x00 ? 0x67 : static_cast<uint8_t>(ac_ - 1);
        }
        else if (increment_)
            ac_ = ac_ >= 0x4F ? 0x00 : static_cast<uint8_t>(ac_ + 1);
        else
            ac_ = ac_ == 0x00 ? 0x4F : static_cast<uint8_t>(ac_ - 1);
    }

    void Hd44780::write_data(uint8_t d)
    {
        ++version_;
        if (cgram_mode_)
            cgram_[ac_ & 0x3F] = d;
        else
            ddram_[cell(ac_)] = d;
        step_ac();
        if (shift_on_entry_ && !cgram_mode_)
            shift_ += increment_ ? 1 : -1;
    }

    void Hd44780::command(uint8_t c)
    {
        ++version_;
        if (c & 0x80)
        {
            ac_ = static_cast<uint8_t>(c & 0x7F);
            cgram_mode_ = false;
        }
        else if (c & 0x40)
        {
            ac_ = static_cast<uint8_t>(c & 0x3F);
            cgram_mode_ = true;
        }
        else if (c & 0x20) // function set: DL, N, F
        {
            eight_bit_ = c & 0x10;
            two_line_ = c & 0x08;
            low_nibble_ = false;
        }
        else if (c & 0x10) // cursor or display shift
        {
            const bool right = c & 0x04;
            if (c & 0x08)
                shift_ += right ? -1 : 1; // the text moves right: the window moves left
            else
            {
                const bool saved = increment_;
                increment_ = right;
                const bool cg = cgram_mode_;
                cgram_mode_ = false;
                step_ac();
                cgram_mode_ = cg;
                increment_ = saved;
            }
        }
        else if (c & 0x08)
            display_on_ = c & 0x04; // cursor and blink are not drawn
        else if (c & 0x04)
        {
            increment_ = c & 0x02;
            shift_on_entry_ = c & 0x01;
        }
        else if (c & 0x02) // home
        {
            ac_ = 0;
            shift_ = 0;
            cgram_mode_ = false;
        }
        else if (c & 0x01) // clear
        {
            ddram_.fill(' ');
            ac_ = 0;
            shift_ = 0;
            increment_ = true;
            cgram_mode_ = false;
        }
    }

    std::string Hd44780::line(unsigned row) const
    {
        std::string out(columns_, ' ');
        if (!display_on_ || row >= rows_ || (!two_line_ && row > 0))
            return out;
        const int length = two_line_ ? 40 : 80;
        const unsigned start = (row % 2) ? 40 : 0;
        const int first = row >= 2 ? static_cast<int>(columns_) : 0;
        for (unsigned col = 0; col < columns_; ++col)
        {
            const int pos = ((first + static_cast<int>(col) + shift_) % length + length) % length;
            const uint8_t c = ddram_[start + static_cast<unsigned>(pos)];
            out[col] = (c >= 0x20 && c <= 0x7D) ? static_cast<char>(c) : '?';
        }
        return out;
    }

    // --- on a 6522 -------------------------------------------------------------

    ViaLcd::ViaLcd(Via6522 &via, ViaPins data, ViaPin rs, ViaPin rw, ViaPin e, unsigned columns, unsigned rows)
        : via_(via), data_(data), rs_(rs), rw_(rw), e_(e), lcd_(columns, rows)
    {
        via_.listen([this](Via6522::Port, uint8_t, uint8_t) { update(); });
    }

    void ViaLcd::update()
    {
        // A pin is high only if the VIA drives it high; undriven pins read low.
        auto driven = [this](Via6522::Port p) { return static_cast<uint8_t>(via_.output(p) & via_.ddr(p)); };
        auto pin = [&](const ViaPin &p) { return (driven(p.port) & p.mask()) != 0; };

        const auto bits = static_cast<uint8_t>((driven(data_.port) & data_.mask()) >> data_.first);
        const auto data = static_cast<uint8_t>(data_.count == 4 ? bits << 4 : bits); // 4 wires: D7-D4
        lcd_.pins(pin(rs_), pin(rw_), pin(e_), data);

        uint8_t out = 0;
        if (lcd_.driving())
            out = static_cast<uint8_t>(data_.count == 4 ? lcd_.data_out() >> 4 : lcd_.data_out());
        via_.drive(data_.port, data_.mask(), static_cast<uint8_t>(out << data_.first));
    }
} // namespace ooe::emu
