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
 *   $D400        SID A, repeating every 32 bytes to $D7FF, except where
 *                SID B is: $D420-$D43F, or $D500-$D51F with CTRL bit 3
 *   $DC00        CIA 1, imitated: timer A ($DC04/$DC05), its interrupt
 *                ($DC0D) and control ($DC0E), repeating every 16 bytes
 *   $DE00/$DE01  the mailbox and its status
 *
 * The rest of $D000-$DFFF (the C64's VIC-II, colour RAM, CIA 2) reads $FF
 * and ignores writes while I/O is in. The timer counts down one each SID
 * clock and underflows after latch + 1 of them: it reloads from the latch,
 * sets ICR bit 0, and pulls the 6510's /IRQ while ICR's mask has bit 0.
 * CRA bit 0 starts it, bit 3 makes it one-shot, bit 4 loads the latch now.
 * Writing the latch's high byte while the timer is stopped loads it too.
 *
 * CTRL RESET resets the 6510 (its vector at $FFFC in the card's RAM) and the
 * CIA. The usual start: RUN = 0, load the code through RAMDATA, write the
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
 * /RESET (reset()) stops the card's 6510 and silences the SIDs (their
 * registers cleared, as the SID's /RES pin does), resets the CIA and the
 * 6510's port, and empties the mailbox; the card's RAM keeps its contents. In Apple II mode the card sits idle:
 * entering it is a reset, and Apple software never addresses the block.
 */

#include <array>
#include <cstdint>

#include "bus.h"
#include "cpu6502.h"
#include "device.h"
#include "sid.h"

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
        static constexpr uint32_t PAL_HZ = 985'248, NTSC_HZ = 1'022'727, NATIVE_HZ = 7'159'090;
        /// CIA 1's registers the card imitates (offsets from $DC00).
        static constexpr uint8_t CIA_TA_LO = 0x04, CIA_TA_HI = 0x05, CIA_ICR = 0x0D, CIA_CRA = 0x0E;
        static constexpr uint8_t CRA_START = 0x01, CRA_ONE_SHOT = 0x08, CRA_LOAD = 0x10;

        SoundCard() : cpu_(bus_, Cpu6502::Model::Nmos6502) { reset(); }
        SoundCard(const SoundCard &) = delete;
        SoundCard &operator=(const SoundCard &) = delete;
        /// The host CPU's clock, which tick()'s cycles run at (0: 7.16 MHz).
        void set_cpu_hz(uint32_t hz) { host_hz_ = hz ? hz : NATIVE_HZ; }
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
        const std::array<uint8_t, SID_REGS> &sid(unsigned chip) const { return chips_[chip & 1].regs(); }
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
        /// Times CTRL RESET was written.
        unsigned cpu_resets() const { return cpu_resets_; }
        /// The card's 6510, for the debugger.
        const Cpu6502 &cpu() const { return cpu_; }
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
        void reset_cpu();
        uint64_t now() const;
        /// Clock the SIDs up to now(). Const: the debugger looks through peek().
        void catch_up() const;

        mutable std::array<Sid, 2> chips_{};
        mutable uint64_t sids_at_{0}; ///< the SID clock the chips have been clocked to
        std::array<uint8_t, RAM_BYTES> ram_{};
        uint8_t ctrl_{0};
        uint8_t ien_{0};
        uint16_t ram_addr_{0};
        uint8_t to_card_{0}, to_host_{0};
        bool to_card_full_{false}, to_host_full_{false};
        unsigned cpu_resets_{0};

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
    };
} // namespace ooe::emu
