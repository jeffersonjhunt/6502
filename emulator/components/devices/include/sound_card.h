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
 * The host's side so far (step 7b): the SIDs' registers, the card's 64 KB
 * through RAMADDR and RAMDATA, and the mailbox. The card's own 6510, which
 * runs from the SID clock with the SIDs at $D400 in its map, is step 7c;
 * until then the card's side is card_receive(), card_send() and
 * sid_write(), which are what its $DE00, $DE01 and $D400 will call.
 *
 * SIDs. A host write to $00-$1C goes to SID A, unless CTRL RUN is set: the
 * card's 6510 owns the chips then, and the write is dropped. With CTRL BOTH,
 * a write to SID A also goes to SID B, from either side. Reads always work:
 * the card answers the 25 write-only registers from what was last written to
 * each, and $19-$1C (POTX, POTY, OSC3, ENV3) from the chip. No paddles are
 * connected (POTX, POTY read $FF); OSC3 and ENV3 come with the SID model,
 * step 7d, and read 0 until then. The RP2350 forwards a write at once in the
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
 * /RESET (reset()) stops the card's 6510 and silences the SIDs (their
 * registers cleared, as the SID's /RES pin does), and empties the mailbox;
 * the card's RAM keeps its contents. In Apple II mode the card sits idle:
 * entering it is a reset, and Apple software never addresses the block.
 */

#include <array>
#include <cstdint>

#include "device.h"

namespace ooe::emu
{
    class SoundCard : public Device
    {
    public:
        static constexpr uint8_t MODULE = 0xA;
        static constexpr uint8_t ID = MODULE << 4 | 1; ///< +$7F: type (module ID) and revision
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
            ID_REG = 0x7F,
        };
        /// The SID's read-only registers.
        static constexpr uint8_t POTX = 0x19, POTY = 0x1A, OSC3 = 0x1B, ENV3 = 0x1C;
        static constexpr uint8_t CTRL_NTSC = 0x01, CTRL_RUN = 0x02, CTRL_RESET = 0x04, CTRL_SID_B_D500 = 0x08,
                                 CTRL_BOTH = 0x10;
        static constexpr uint8_t STATUS_BUSY = 0x01, STATUS_TO_CARD = 0x02, STATUS_TO_HOST = 0x04,
                                 STATUS_IRQ = 0x80;
        static constexpr uint8_t IEN_MAILBOX = 0x04;
        /// $DE01, the card's view of the mailbox.
        static constexpr uint8_t CARD_FROM_HOST = 0x01, CARD_UNTAKEN = 0x02;

        SoundCard() { reset(); }
        /// /RESET: the 6510 stopped, the SIDs silent, the mailbox empty. RAM stays.
        void reset();
        void bus_reset() override { reset(); }

        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return (ien_ & IEN_MAILBOX) && to_host_full_; }
        bool drq() const override { return true; }
        uint8_t dack_read(bool tc) override;
        void dack_write(uint8_t data, bool tc) override;

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
        const std::array<uint8_t, SID_REGS> &sid(unsigned chip) const { return sid_[chip & 1]; }
        std::array<uint8_t, RAM_BYTES> &ram() { return ram_; }
        const std::array<uint8_t, RAM_BYTES> &ram() const { return ram_; }
        /// Times CTRL RESET was written (the card's 6510 resets, from step 7c).
        unsigned cpu_resets() const { return cpu_resets_; }

    private:
        uint8_t ram_byte() { return ram_[ram_addr_++]; }

        std::array<std::array<uint8_t, SID_REGS>, 2> sid_{};
        std::array<uint8_t, RAM_BYTES> ram_{};
        uint8_t ctrl_{0};
        uint8_t ien_{0};
        uint16_t ram_addr_{0};
        uint8_t to_card_{0}, to_host_{0};
        bool to_card_full_{false}, to_host_full_{false};
        unsigned cpu_resets_{0};
    };
} // namespace ooe::emu
