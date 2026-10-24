#pragma once

/**
 * @file apple_io.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Apple II mode's $C000-$CFFF: the IIe's soft switches, I/O and slot ROM space.
 * @version 0.1.0
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Apple II mode", "Apple II Mode") is the spec. In Apple mode
 * the north bridge (backplane_bridge.h) sends $C000-$CFFF here, at APPLE_IO
 * in Io space. read() and write() are bus cycles, with the IIe's side effects
 * (many switches change on a read as well as a write); peek() changes
 * nothing, for the debugger.
 *
 *   $C000-$C00F  read: the keyboard (the USB card's KEY). Write: 80STORE,
 *                RAMRD, RAMWRT, INTCXROM, ALTZP, SLOTC3ROM, 80COL, ALTCHARSET,
 *                off then on
 *   $C010        clear the keyboard strobe; bit 7 = a key is down
 *   $C011-$C01F  bit 7: BSRBANK2, BSRREADRAM, RAMRD, RAMWRT, INTCXROM, ALTZP,
 *                SLOTC3ROM, 80STORE, VBL (0 in vertical blank), TEXT, MIXED,
 *                PAGE2, HIRES, ALTCHARSET, 80COL. Bits 0-6: the keyboard latch
 *   $C030        the speaker      $C050-$C05F  TEXT, MIXED, PAGE2, HIRES,
 *                                              AN0-AN3 (AN3 off = DHIRES on)
 *   $C061-$C063  buttons 0-2      $C064-$C067  paddle timers (bit 7)
 *   $C070        trigger the 558  $C07E/$C07F  read: IOUDIS, DHIRES
 *   $C080-$C08F  the language card: bit 3 bank 1 (else 2), bits 0-1 00 read
 *                RAM, 01 read ROM and write RAM, 10 read ROM, 11 read and write
 *                RAM. Writing needs two reads of an odd address in a row;
 *                an even address, or a write, clears that
 *   $C090-$C0FF  slot I/O; $C100-$CFFF slot ROMs or the internal ROM, as
 *                INTCXROM, SLOTC3ROM and INTC8ROM say (a $C3xx access with
 *                the internal C3 ROM sets INTC8ROM, $CFFF clears it)
 *
 * The internal ROM is the IIe ROM image in bank E ($EC000-$EFFFF). No slot
 * has a card yet (the Disk II comes for slot 6), so empty slots and the
 * cassette read $FF, the floating bus as the emulator models it.
 *
 * VBL follows the Apple's timing in CPU cycles: 65 a line, 262 lines, 17,030
 * a frame, the last 70 lines blank. /RESET puts the switches back as a IIe's
 * MMU and IOU do: memory main, ROMs internal-slot as the IIe ROM expects,
 * the language card reading ROM and writing RAM bank 2, text mode.
 */

#include <cstdint>

#include "device.h"

namespace ooe::emu
{
    class Decoder;
    class UsbCard;

    struct AppleSwitches
    {
        bool store80{false}, ramrd{false}, ramwrt{false}, intcxrom{false}, altzp{false}, slotc3rom{false};
        bool col80{false}, altchar{false};
        bool text{true}, mixed{false}, page2{false}, hires{false}, dhires{false}, ioudis{true};
        uint8_t an{0}; ///< annunciators AN0-AN3, bit n
        bool lc_bank2{true}, lc_read{false}, lc_write{true}, lc_prewrite{false};
        bool intc8rom{false};
    };

    class AppleIo : public Device
    {
    public:
        static constexpr unsigned LINE_CYCLES = 65, FRAME_LINES = 262, SHOWN_LINES = 192;
        static constexpr uint64_t FRAME_CYCLES = uint64_t{LINE_CYCLES} * FRAME_LINES; ///< 17,030
        static constexpr uint32_t ROM_BASE = 0xE0000; ///< bank E: the IIe ROM image at $EC000-$EFFFF
        static constexpr uint8_t FLOATING = 0xFF;

        void attach(Decoder *decoder) { decoder_ = decoder; }
        void attach(UsbCard *usb) { usb_ = usb; }
        void reset() { s_ = AppleSwitches{}; }
        const AppleSwitches &switches() const { return s_; }
        /// The Apple's vertical blank (lines 192-261), from the cycle count.
        bool vbl() const;

        size_t size() const override { return 0x1000; }
        uint8_t read(uint32_t offset) override { return access(offset, false, 0, true); }
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override { access(offset, true, data, true); }

    private:
        /// One access to $C000 + offset; `effects` false for a peek.
        uint8_t access(uint32_t offset, bool write, uint8_t data, bool effects);
        uint8_t status(unsigned n) const;
        uint8_t rom(uint32_t offset) const;
        void language_card(unsigned a, bool write);

        Decoder *decoder_{nullptr};
        UsbCard *usb_{nullptr};
        AppleSwitches s_{};
    };
} // namespace ooe::emu
