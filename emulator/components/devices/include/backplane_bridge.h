#pragma once

/**
 * @file backplane_bridge.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's north bridge: windows, VEC, ROM space, I/O hole.
 * @version 0.3.0
 * @date 2026-10-26
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Native mode", "North bridge registers") is the spec. The
 * CPU's 64 KB is four 16 KB windows, each a register:
 *
 *   bit 7 ROM    1 = flash: blocks 0-31 ROM A, 32-63 ROM B (the cartridge)
 *   bit 6 RO     1 = writes through the window are ignored
 *   bits 5-0     the 16 KB block (A14-A19, or the offset into the flash)
 *
 * While VPB is low, $C000-$FFFF goes through VEC instead of W3. The I/O hole,
 * $C000-$C7FF, is decoded ahead of the windows; module n's registers are at
 * $C000 + n x $80. This bridge is both the router (NorthBridge) and the
 * device holding its own registers at $C680 (module ID 0xD):
 *
 *   $00-$03 WIN0-WIN3   $04 VEC   $05 MODE   $06 STATUS   $07 RTCCTL
 *   $08/$09 IRQL/IRQH   $0A-$0D WAIT0-WAIT3   $10-$16 SEC-YEAR
 *   $17 RTCREG   $18 RTCDATA   $7F ID
 *
 * At reset W0-W2 map RAM blocks 0-2 and W3 and VEC map the top block of
 * ROM A read-only ($DF); with /BOOTSEL held they map RAM block 63 ($3F).
 *
 * IRQL/IRQH are the /IACK poll: bit n (of module n, or n + 8) reads 0 while
 * a card with that module ID and a wired IRQ asserts it. Unused offsets read
 * $FF, the data lines' weak pull-ups.
 *
 * WAIT0-WAIT3 give each module ID 2 bits of wait states: WAITn holds IDs
 * 4n to 4n+3, the lowest ID in bits 1-0. Every access to that module is held
 * that many extra cycles with RDY.
 *
 * Apple II mode (MODE bit 0; hw/design.md, "Apple II mode"): writing it
 * asks for a /RESET pulse (take_reset()) and switches to the IIe's map. A0-
 * A15 pass through and the bridge supplies A16-A19 from the IIe's switches
 * (apple_io.h holds them, and $C000-$CFFF, at APPLE_IO):
 *
 *   $0000-$01FF  bank 0, or 1 with ALTZP
 *   $0200-$BFFF  bank 0 or 1: RAMRD for reads, RAMWRT for writes; with
 *                80STORE, PAGE2 picks for $0400-$07FF, and with HIRES too for
 *                $2000-$3FFF
 *   $C000-$CFFF  the soft switches, I/O and ROM space (AppleIo)
 *   $D000-$FFFF  the language card: RAM in bank 2 (main) or 3 (ALTZP),
 *                with the other $D000 bank at bank 4 or 5; else the ROM image
 *                in bank E. Writes with RAM write-protected are dropped
 *
 * The real-time clock (hw/design.md, "Real-time clock"): the bridge owns
 * the DS3231 (ds3231.h) and talks I²C to it at 400 kHz, so software sees
 * registers only. Each transaction keeps BUSY set for its bits' time at the
 * CPU clock (9 a byte, one for each START and STOP), and its effect lands
 * when it ends; they queue, one at a time:
 *
 * - On each falling edge of SQW (1 Hz as the bridge sets it up) it reads
 *   the seven time registers and the status into SEC-YEAR, LOST (OSF) and
 *   ALARM (A1F or A2F), and sets TICK. An edge while that read is still
 *   queued is missed.
 * - Reading SEC copies SEC-YEAR into a latch, which reads of SEC+1 to
 *   YEAR return: a time read in order never straddles a second.
 * - Writes to SEC-YEAR go to a holding copy (reads still give the time);
 *   SET sends it, then clears OSF, keeping EN32kHz and the alarm flags.
 *   SET is a strobe and reads 0: BUSY shows it pending; SEC-YEAR then show
 *   what it sent.
 * - Writing RTCREG reads that DS3231 register into RTCDATA; writing RTCDATA
 *   writes it and reads it back. A status read updates LOST and ALARM. A
 *   control write goes with INTCN and RS cleared: SQW stays the bridge's.
 * - TICK && TIE is the bridge's interrupt: IRQH bit 5, ID 0xD.
 *
 * At power-on (the first reset) the bridge sets the square wave to 1 Hz
 * (INTCN and RS cleared, the rest of the control register kept) and reads
 * the time, all while /RESET is held, so the first instruction sees a
 * valid time with BUSY clear. Any reset clears TICK and TIE; the DS3231,
 * its time, and a transaction under way carry on. The chip itself is
 * shared, so a session can keep it through a power cycle (rtc()).
 *
 * The native registers, the windows and VEC are out of the way until a hard
 * reset (reset()), the only way back. A /RESET pulse (reset_pulse(): entering,
 * and later Ctrl-Reset) resets the windows and the IIe's switches but keeps
 * APPLE. clock_hz() is the Apple clock while in the mode: the run loop paces
 * by it, as the clock card follows the mode register.
 */

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "apple_io.h"
#include "decoder.h"
#include "device.h"
#include "ds3231.h"

namespace ooe::emu
{
    class BackplaneBridge : public NorthBridge, public Device
    {
    public:
        static constexpr uint8_t MODULE = 0xD;                     ///< the ID its registers use
        static constexpr uint32_t REGISTERS = MODULE * 0x80;      ///< offset in the I/O hole: $C680
        static constexpr uint8_t REVISION = 1;
        static constexpr uint8_t ID = MODULE << 4 | REVISION;     ///< +$7F: type (module ID) and revision

        static constexpr uint8_t ROM = 0x80;
        static constexpr uint8_t RO = 0x40;
        static constexpr uint8_t BLOCK = 0x3F;
        static constexpr uint8_t RESET_TOP = ROM | RO | 31;       ///< $DF: top 16 KB of ROM A
        static constexpr uint8_t BOOTSEL_TOP = 63;                ///< $3F: RAM block 63

        enum Reg : uint8_t
        {
            WIN0 = 0x00,
            VEC = 0x04,
            MODE = 0x05,
            STATUS = 0x06,
            RTCCTL = 0x07,
            IRQL = 0x08,
            IRQH = 0x09,
            WAIT0 = 0x0A,
            SEC = 0x10, ///< SEC-YEAR: $10-$16
            RTCREG = 0x17,
            RTCDATA = 0x18,
            ID_REG = 0x7F,
        };
        static constexpr uint8_t STATUS_BOOTSEL = 0x01;  ///< booted from RAM
        static constexpr uint8_t STATUS_POWER_ON = 0x02; ///< the last reset was power-on
        static constexpr uint8_t MODE_APPLE = 0x01;
        static constexpr uint8_t MODE_TURBO = 0x02; ///< reserved for /1: kept, not modelled
        // RTCCTL
        static constexpr uint8_t RTC_TICK = 0x80, RTC_TIE = 0x40, RTC_ALARM = 0x20, RTC_LOST = 0x04,
                                 RTC_SET = 0x02, RTC_BUSY = 0x01;
        static constexpr unsigned TIME_REGS = 7;
        static constexpr uint32_t NATIVE_HZ = 7'159'090; ///< 14.31818 MHz / 2
        static constexpr uint32_t I2C_HZ = 400'000;
        // I²C bits a transaction takes: 9 a byte, one for each START, repeated START and STOP.
        static constexpr unsigned READ_BITS = 9 * 3 + 3;  ///< address, register, address; + data
        static constexpr unsigned WRITE_BITS = 9 * 2 + 2; ///< address, register; + data
        static constexpr unsigned REFRESH_BITS = READ_BITS + 9 * TIME_REGS + READ_BITS + 9;
        static constexpr unsigned SET_BITS = WRITE_BITS + 9 * TIME_REGS + WRITE_BITS + 9;

        /// `bootsel`: the bus spy holds /BOOTSEL, so reset maps RAM, not flash, at the top.
        /// `rtc`: the DS3231 on the CPU card; nullptr, a new one at 2000-01-01 00:00:00.
        explicit BackplaneBridge(bool bootsel = false, std::shared_ptr<Ds3231> rtc = nullptr);

        /// The decoder whose module IDs the interrupt poll reads. Not owned.
        void attach(Decoder *decoder)
        {
            decoder_ = decoder;
            apple_io_.attach(decoder);
        }

        // NorthBridge
        Route route(uint16_t cpu_addr, bool vector_pull, bool write) const override;
        void reset() override;
        void reset_pulse() override;
        bool take_reset() override;
        /// Apple II mode's clock: 14.31818 MHz / 14, with the long cycle every 65th: 65 cycles in 912 master clocks.
        static constexpr uint32_t APPLE_HZ = 1'020'484;
        uint32_t clock_hz() const override { return apple() ? APPLE_HZ : 0; }

        /// In Apple II mode: the IIe's map, and the run loop's Apple clock.
        bool apple() const { return (mode_ & MODE_APPLE) != 0; }
        /// Apple mode's $C000-$CFFF, which the machine maps at APPLE_IO.
        AppleIo &apple_io() { return apple_io_; }
        const AppleSwitches &apple_switches() const { return apple_io_.switches(); }
        unsigned wait_states(uint8_t module) const override;

        // Device: the registers at $C680
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;

        bool irq() const override { return tick_ && tie_; }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;

        uint8_t window(unsigned n) const { return win_[n & 3]; }
        uint8_t vec() const { return vec_; }

        // The real-time clock
        /// The CPU clock tick()'s cycles run at in native mode (0: NATIVE_HZ); Apple II mode's is APPLE_HZ.
        void set_cpu_hz(uint32_t hz);
        const std::shared_ptr<Ds3231> &rtc() const { return rtc_; }
        /// Set the DS3231 from outside (the host's clock, `ms` into the second), and read it into SEC-YEAR at once.
        void set_rtc_time(const Ds3231::Time &t, unsigned ms = 0);
        /// SEC-YEAR as the bridge last read them (not the latch).
        const std::array<uint8_t, TIME_REGS> &rtc_time() const { return time_; }
        uint8_t rtcctl() const;
        /// The clock in words, for the debugger: the time, RTCCTL, the chip's registers.
        std::string describe_clock() const;

    private:
        /// The DS3231's control bits that pick SQW: the bridge's, kept 0.
        static constexpr uint8_t SQW_BITS = Ds3231::INTCN | Ds3231::RS2 | Ds3231::RS1;

        enum class Op : uint8_t
        {
            Refresh, ///< read the time and status (SQW fell)
            Set,     ///< write the time, clear OSF
            Read,    ///< RTCREG into RTCDATA
            Write,   ///< RTCDATA to RTCREG, read back
        };
        struct Transaction
        {
            Op op;
            uint8_t reg{0}, data{0};
            std::array<uint8_t, TIME_REGS> time{};
        };

        void apply_reset();
        void rtc_reset();
        void retime();
        uint64_t cost(Op op) const;
        void queue(const Transaction &t);
        void finish(const Transaction &t);
        void read_time();
        void read_status();
        /// IRQL (high = false) or IRQH: 0 bits for interrupting modules.
        uint8_t poll(bool high) const;

        Decoder *decoder_{nullptr};
        bool bootsel_;
        bool power_on_{true};
        bool reset_seen_{false};
        std::array<uint8_t, 4> win_{};
        uint8_t vec_{0};
        uint8_t mode_{0};
        std::array<uint8_t, 4> wait_{};
        bool reset_wanted_{false};
        AppleIo apple_io_;

        std::shared_ptr<Ds3231> rtc_;
        uint32_t cpu_hz_{NATIVE_HZ};
        uint32_t hz_{NATIVE_HZ}; ///< the clock left_ counts: cpu_hz_, or APPLE_HZ
        std::array<uint8_t, TIME_REGS> time_{};   ///< SEC-YEAR as last read
        std::array<uint8_t, TIME_REGS> latch_{};  ///< what reading SEC caught
        std::array<uint8_t, TIME_REGS> staged_{}; ///< what SET will send
        uint8_t status_{0};                       ///< the DS3231's status as last read
        bool tick_{false}, tie_{false};
        uint8_t rtc_reg_{0}, rtc_data_{0};
        std::deque<Transaction> queue_; ///< the front is under way
        uint64_t left_{0};              ///< its cycles still to go
    };
} // namespace ooe::emu
