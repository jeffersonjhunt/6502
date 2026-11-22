#pragma once

/**
 * @file backplane_bridge.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's north bridge: windows, VEC, ROM space, I/O hole.
 * @version 0.6.0
 * @date 2026-11-17
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
 * On the new bus (hw/design.md, "The new bus", step B1) addresses are 24
 * bits and memory answers by address: RAM block b is physical b x $4000,
 * ROM block b is $F00000 + b x $4000. Every memory cycle carries the
 * bridge's own ID, D, on PS0-PS3 (memory_select()), and the DMA
 * controller's memory to memory cycles its 8.
 *
 * Three revisions (step B7, proposed; the constructor's `revision`, a machine
 * file's `revision=1|2|3`; REVISION and ID are the default's):
 *
 * - Revision 3, ID $D3 (hw/future/north-bridge-rev3.md, random-source.md):
 *   revision 2's registers, the timebase ($1C-$1D, $28-$2E, below) and in
 *   the random source ($19-$1B, below). RTCCTL's TIE reads 0 and ignores
 *   writes: the 1 Hz TICK can't interrupt.
 *
 * - Revision 2, ID $D2 (hw/design.md, "The video aperture moved out of
 *   RAM"): WIN0H-WIN3H ($20-$23) and VECH ($24) give A20-A23 for a window's
 *   block, or VEC's on a vector pull, while its ROM bit is clear (bits 3-0;
 *   7-4 read 0), so a window reaches any 16 KB of the 16 MB. MAP ($25) holds
 *   AP8 in bit 0 (bits 7-1 read 0): set, the CPU card sends every memory
 *   cycle for bank 8, $080000-$08FFFF, to the video card's aperture at
 *   $E00000-$E0FFFF instead (redirect(): a window with ROM clear, WINnH 0 and
 *   blocks 32-35, and the DMA controller's memory side), as bank 8 was the
 *   card's before; clear, bank 8 is the RAM's. It follows MAP live. Any
 *   reset sets WINnH and VECH to 0 and MAP to $01, whatever is fitted: a
 *   machine with no revision 2 video card reads $FF in bank 8 until its
 *   BIOS clears AP8.
 * - Revision 1, ID $D1, B1's model: $20-$25 read $FF and drop writes, and
 *   the alias is fixed when the machine is built, from the card it has
 *   (set_aperture()): bank 8 goes to the aperture while a revision 2 card is
 *   fitted, and is the RAM's without one.
 *
 * The Console card, driving the bus itself, sees bank 8 as the RAM's either
 * way.
 *
 * Revision 3's common hole, HOLE ($26: bit 7 EN, bit 6 RO, bits 5-0 the
 * block): with EN set, every native CPU cycle to $F800-$FFFF goes to RAM at
 * the block's top 2 KB (block x $4000 + $3800), ahead of VEC and W3, whose
 * ROM, RO and WINnH bits don't reach it; HOLE's RO drops writes. $00 at every
 * reset and the /RESET pulse; the Apple II map comes first, and DMA (physical
 * addresses) is untouched. Before revision 3 $26 reads $FF and drops writes.
 *
 * While VPB is low, $C000-$FFFF goes through VEC instead of W3. The I/O hole,
 * $C000-$C7FF, is decoded ahead of the windows; module n's registers are at
 * $C000 + n x $80. This bridge is both the router (NorthBridge) and the
 * device holding its own registers at $C680 (module ID 0xD):
 *
 *   $00-$03 WIN0-WIN3   $04 VEC   $05 MODE   $06 STATUS   $07 RTCCTL
 *   $08/$09 IRQL/IRQH   $0A-$0D WAIT0-WAIT3   $10-$16 SEC-YEAR
 *   $17 RTCREG   $18 RTCDATA   $19-$1B RNGST RNGCNT RNGDATA
 *   $1C/$1D MSSL/MSSH   $20-$23 WIN0H-WIN3H
 *   $24 VECH   $25 MAP   $26 HOLE   $28 TBCTL   $29-$2C MS0-MS3   $2D/$2E TBSUBL/H
 *   $7F ID (WIN0H-MAP on revision 2 or more)
 *
 * At reset W0-W2 map RAM blocks 0-2 and W3 and VEC map the top block of
 * ROM A read-only ($DF); with /BOOTSEL held they map RAM block 63 ($3F).
 *
 * IRQL/IRQH are the /IACK poll: bit n (of module n, or n + 8) reads 0 while
 * a card with that module ID and a wired IRQ asserts it. Unused offsets read
 * $FF, the data lines' weak pull-ups.
 *
 * WAIT0-WAIT3 are read/write storage, reset 0, reserved for a turbo: they
 * slow nothing (step B2). Every wait state comes from a card pulling /WAIT
 * (a mapping's `wait`, decoder.h), which the bridge turns into RDY for the
 * CPU. The diagnostics keep their options in WAIT3.
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
 * The timebase (revision 3): the bridge counts OSC, the 14.31818 MHz master
 * clock, which is native cycles x 2 and, in Apple II mode, 912 clocks in 65
 * cycles (any other CPU clock by its ratio to NATIVE_HZ). The k-th tick comes
 * floor(157,500 k / 11) OSC clocks after a hard reset's release, and tick()
 * steps to its exact cycle, as it steps to the DS3231's edges, so DMA's cycles
 * count. At each tick MS (32 bits) gains 1 and MSF is set, OVR too if MSF
 * already was; MSF && MSIE is the interrupt, IRQH bit 5. TBCTL: a read clears
 * nothing, a 1 written to MSF or OVR clears it, MSIE takes the value written.
 * Reading MS0 latches MS1-MS3 and TBSUB (OSC clocks since the last tick,
 * halved); reading SEC latches MSS too. MSS is min(999, the whole
 * milliseconds since R), counted by its own 2-in-11 divider from R, the
 * SQW edge whose Refresh last landed (SET's end, after a SET; the host's
 * clock's fraction after set_rtc_time()). SYNC: set when an edge's read lands,
 * clear from power-on until then and 1.1 s after the last edge. A hard reset
 * clears MS and the divider (they start at the release); a /RESET pulse
 * leaves them; any reset clears MSF, OVR and MSIE. peek() takes no latch.
 *
 * The random source (revision 3; hw/future/random-source.md): an ATECC608B
 * (atecc608.h) on the same I²C bus, driven by the bridge alone, which keeps
 * a pool of 32 bytes. A draw is a run of short transactions with the bus free
 * between them, queued with the clock's one at a time and timed by their bits:
 * the wake byte ($00 at 100 kHz, SDA low 80 µs), 1.5 ms, the wake check (a
 * 4-byte read: $04 $11 $33 $43), the Random command (a constant group), a poll
 * 1 ms after it and every 1 ms (a NAK while the chip works) up to 100 ms, the
 * response (count 35: 32 bytes and a CRC, checked), then sleep. A failed try
 * ends with sleep and the I²C software reset, and is tried again from the
 * wake: three tries, then FAULT and an empty pool. RNGST: READY (a byte in the
 * pool), CHIP, LOCKED, FAULT, BUSY (a draw under way); DRAW (a 1 written)
 * clears FAULT and, unless a draw is under way, empties the pool and draws.
 * Taking the pool's last byte from RNGDATA starts a draw, unless FAULT is set;
 * RNGDATA reads $00 with the pool empty. RTCCTL's BUSY is the clock's
 * transactions alone. At power-on the set-up (wake, the lock byte's Read,
 * and on a locked chip the first draw) is done at once, as the clock's, so the
 * first instruction finds 32 bytes; a chip that doesn't answer is CHIP clear
 * and never addressed again, and an unlocked one is never drawn from. A later
 * reset keeps the pool. peek() takes no byte.
 *
 * At power-on (the first reset) the bridge sets the square wave to 1 Hz
 * (INTCN and RS cleared, the rest of the control register kept) and reads
 * the time, all while /RESET is held, so the first instruction sees a
 * valid time with BUSY clear. Any reset clears TICK and TIE; the DS3231,
 * its time, and a transaction under way carry on. The chip itself is
 * shared, so a session can keep it through a power cycle (rtc()).
 *
 * The native registers, the windows and VEC are out of the way until a hard
 * reset (reset()), the only way back; the IIe's map never reads WINnH or MAP.
 * A /RESET pulse (reset_pulse(): entering, and later Ctrl-Reset) resets the
 * windows, WINnH, VECH, MAP and the IIe's switches but keeps APPLE. clock_hz() is the Apple clock while in the mode: the run loop paces
 * by it, as the clock card follows the mode register.
 */

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

#include "apple_io.h"
#include "atecc608.h"
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
        static constexpr uint8_t REVISION = 3;                    ///< the default revision (revision()): 3 since N5
        static constexpr uint8_t ID = MODULE << 4 | REVISION;     ///< the default's +$7F: type and revision (id())
        static constexpr uint8_t WIDE = 2;                        ///< the revision with WINnH, VECH and MAP
        static constexpr uint8_t TIMEBASE = 3;                    ///< the revision with the timebase

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
            WIN0H = 0x20, ///< WIN0H-WIN3H: $20-$23 (revision 2)
            VECH = 0x24,  ///< revision 2
            MAP = 0x25,   ///< revision 2
            HOLE = 0x26,  ///< revision 3: the common hole
            RNGST = 0x19, ///< revision 3: the random source
            RNGCNT = 0x1A,
            RNGDATA = 0x1B,
            MSSL = 0x1C,  ///< revision 3: MSS from SEC's latch
            MSSH = 0x1D,
            TBCTL = 0x28, ///< revision 3: the timebase
            MS0 = 0x29,   ///< MS0-MS3: $29-$2C; reading MS0 latches the rest and TBSUB
            TBSUBL = 0x2D,
            TBSUBH = 0x2E,
            ID_REG = 0x7F,
        };
        static constexpr uint8_t HIGH = 0x0F;    ///< WINnH and VECH: A20-A23
        static constexpr uint8_t MAP_AP8 = 0x01; ///< bank 8 is the aperture
        static constexpr uint8_t STATUS_BOOTSEL = 0x01;  ///< booted from RAM
        static constexpr uint8_t STATUS_POWER_ON = 0x02; ///< the last reset was power-on
        static constexpr uint8_t MODE_APPLE = 0x01;
        static constexpr uint8_t MODE_TURBO = 0x02; ///< reserved for /1: kept, not modelled
        // RTCCTL
        static constexpr uint8_t RTC_TICK = 0x80, RTC_TIE = 0x40, RTC_ALARM = 0x20, RTC_LOST = 0x04,
                                 RTC_SET = 0x02, RTC_BUSY = 0x01;
        static constexpr unsigned TIME_REGS = 7;
        // TBCTL (revision 3)
        static constexpr uint8_t TB_MSF = 0x80, TB_OVR = 0x40, TB_MSIE = 0x20, TB_SYNC = 0x10;
        static constexpr uint64_t OSC_11MS = 157'500;         ///< OSC clocks in 11 ms: 9 ticks of 14,318, 2 of 14,319
        static constexpr uint64_t SYNC_OSC = 1100 * OSC_11MS / 11; ///< SYNC's timeout, 1.1 s of OSC
        static constexpr unsigned MSS_MAX = 999;
        // RNGST (revision 3)
        static constexpr uint8_t RNG_READY = 0x80, RNG_CHIP = 0x40, RNG_LOCKED = 0x20, RNG_FAULT = 0x10,
                                 RNG_BUSY = 0x01, RNG_DRAW = 0x01;
        static constexpr unsigned POOL = 32, TRIES = 3;
        static constexpr uint32_t WAKE_HZ = 100'000;   ///< the wake byte's rate
        static constexpr uint32_t WAKE_LOW_US = 80;    ///< SDA low for the $00 byte at 100 kHz
        static constexpr uint32_t WAKE_DELAY_US = 1'500, POLL_US = 1'000, GIVE_UP_US = 100'000;
        // The chip's transactions, in bits (9 a byte, 1 for each START and STOP)
        static constexpr unsigned WAKE_BITS = 11;    ///< at WAKE_HZ
        static constexpr unsigned CHECK_BITS = 47;   ///< START, address, 4 bytes, STOP
        static constexpr unsigned COMMAND_BITS = 83; ///< START, address, word address, 7 bytes, STOP
        static constexpr unsigned POLL_BITS = 11;    ///< START, address, and its ACK or NAK
        static constexpr unsigned SLEEP_BITS = 20;   ///< START, address, word address, STOP
        static constexpr unsigned RESET_BITS = 12;   ///< START, nine SCL pulses, START, STOP
        static constexpr uint32_t NATIVE_HZ = 7'159'090; ///< 14.31818 MHz / 2
        static constexpr uint32_t I2C_HZ = 400'000;
        // I²C bits a transaction takes: 9 a byte, one for each START, repeated START and STOP.
        static constexpr unsigned READ_BITS = 9 * 3 + 3;  ///< address, register, address; + data
        static constexpr unsigned WRITE_BITS = 9 * 2 + 2; ///< address, register; + data
        static constexpr unsigned REFRESH_BITS = READ_BITS + 9 * TIME_REGS + READ_BITS + 9;
        static constexpr unsigned SET_BITS = WRITE_BITS + 9 * TIME_REGS + WRITE_BITS + 9;

        /// `bootsel`: the bus spy holds /BOOTSEL, so reset maps RAM, not flash, at the top.
        /// `rtc`: the DS3231 on the CPU card; nullptr, a new one at 2000-01-01 00:00:00.
        /// `revision`: 1, WIDE (2) or 3; anything else is taken as 1.
        explicit BackplaneBridge(bool bootsel = false, std::shared_ptr<Ds3231> rtc = nullptr,
                                 uint8_t revision = REVISION);

        uint8_t revision() const { return revision_; }
        uint8_t id() const { return static_cast<uint8_t>(MODULE << 4 | revision_); }
        /// Revision 2 or more: WINnH, VECH and MAP.
        bool wide() const { return revision_ >= WIDE; }
        /// Revision 3: the timebase (TBCTL, MS, TBSUB, MSS), and TIE gone.
        bool timebase() const { return revision_ >= TIMEBASE; }

        /// The decoder whose module IDs the interrupt poll reads. Not owned.
        void attach(Decoder *decoder)
        {
            decoder_ = decoder;
            apple_io_.attach(decoder);
        }

        static constexpr uint8_t DMA_MODULE = 0x8;                ///< the DMA controller's ID, on its memory cycles
        static constexpr uint32_t BANK8 = 0x080000;               ///< what AP8 sends to the aperture: 64 KB
        static constexpr uint32_t APERTURE = 0xE00000;            ///< the video card's aperture

        // NorthBridge
        Route route(uint16_t cpu_addr, bool vector_pull, bool write) const override;
        uint8_t memory_select(Master master) const override { return master == Master::Dma ? DMA_MODULE : MODULE; }
        uint32_t redirect(uint32_t physical) const override
        {
            if ((physical & ~0xFFFFu) != BANK8) // A20-A23 0 and bank 8 only
                return physical;
            if (wide())
                return map_ & MAP_AP8 ? APERTURE + (physical & 0xFFFF) : physical; // AP8, live
            return aperture_ != NO_APERTURE ? aperture_ + (physical & 0xFFFF) : physical;
        }
        /// Revision 1: send bank 8 to the aperture at `base` (64 KB; a revision 2 video card is
        /// fitted), or NO_APERTURE: bank 8 is the RAM's. Fixed when the machine is built. Revision 2
        /// ignores it: MAP's AP8 decides.
        void set_aperture(uint32_t base) { aperture_ = base; }
        static constexpr uint32_t NO_APERTURE = 0xFFFFFFFF;
        uint32_t aperture() const { return aperture_; }
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

        // Device: the registers at $C680
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;

        bool irq() const override { return timebase() ? msf_ && msie_ : tick_ && tie_; }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;

        uint8_t window(unsigned n) const { return win_[n & 3]; }
        uint8_t vec() const { return vec_; }
        /// Revision 2's WIN0H-WIN3H, VECH and MAP (0, 0 and $01 held on revision 1, which has none).
        uint8_t winh(unsigned n) const { return winh_[n & 3]; }
        uint8_t vech() const { return vech_; }
        uint8_t map() const { return map_; }
        uint8_t hole() const { return hole_; } ///< HOLE (revision 3; 0 before)
        static constexpr uint8_t HOLE_EN = 0x80, HOLE_RO = 0x40, HOLE_BLOCK = 0x3F;
        static constexpr uint16_t HOLE_BASE = 0xF800; ///< the hole: $F800-$FFFF

        // The real-time clock
        /// The CPU clock tick()'s cycles run at in native mode (0: NATIVE_HZ); Apple II mode's is APPLE_HZ.
        void set_cpu_hz(uint32_t hz);
        const std::shared_ptr<Ds3231> &rtc() const { return rtc_; }
        /// Set the DS3231 from outside (the host's clock, `ms` into the second), and read it into SEC-YEAR at once.
        void set_rtc_time(const Ds3231::Time &t, unsigned ms = 0);
        /// SEC-YEAR as the bridge last read them (not the latch).
        const std::array<uint8_t, TIME_REGS> &rtc_time() const { return time_; }
        uint8_t rtcctl() const;
        /// A SET queued, under way or behind another transaction: what BUSY can't say, as SET reads 0.
        bool set_pending() const;
        /// The clock in words, for the debugger: the time, RTCCTL, the chip's registers (and on revision 3 the
        /// timebase: describe_timebase()).
        std::string describe_clock() const;

        // The timebase (revision 3)
        /// The millisecond counter, live, taking no latch.
        uint32_t ms() const { return ms_; }
        /// Test hook: put the counter at `value` (just before a carry, say); the divider runs on.
        void set_ms(uint32_t value) { ms_ = value; }
        uint8_t tbctl() const;
        /// MSS live: the whole milliseconds since R, at most 999 (not SEC's latch).
        unsigned mss() const;
        /// OSC clocks since power-on.
        uint64_t osc() const { return osc_; }
        /// The counter, TBCTL, MSS and SYNC in words, for the debugger.
        std::string describe_timebase() const;

        // The random source (revision 3)
        /// The chip fitted (a locked one, the fixed stream, by default; nullptr: none). Before the first
        /// reset: the power-on set-up is the chip's.
        void set_rng_chip(std::unique_ptr<Atecc608> chip) { chip_ = std::move(chip); }
        /// The chip, for tests' fault injection (nullptr: none).
        Atecc608 *rng_chip() { return chip_.get(); }
        uint8_t rngst() const;
        size_t pool_size() const { return pool_.size(); }
        /// What the engine did: draws started, tries failed, polls made, and for the last draw the cycles from
        /// the command's end to its first poll.
        struct RngCounts
        {
            unsigned draws{0}, failures{0}, polls{0};
            uint64_t first_poll{0};
        };
        const RngCounts &rng_counts() const { return counts_; }
        /// RNGST, the pool and the counts in words, for the debugger.
        std::string describe_random() const;

    private:
        /// The DS3231's control bits that pick SQW: the bridge's, kept 0.
        static constexpr uint8_t SQW_BITS = Ds3231::INTCN | Ds3231::RS2 | Ds3231::RS1;

        enum class Op : uint8_t
        {
            Refresh, ///< read the time and status (SQW fell)
            Set,     ///< write the time, clear OSF
            Read,    ///< RTCREG into RTCDATA
            Write,   ///< RTCDATA to RTCREG, read back
            // The random source's: never RTCCTL's BUSY
            Wake,     ///< the $00 byte at 100 kHz
            Check,    ///< the wake answer read
            Command,  ///< Random, or the lock byte's Read (reg 1)
            Poll,     ///< the address, for a read: ACK or NAK
            Response, ///< the rest of the group (reg: its count)
            Sleep,    ///< word address $01
            SwReset,  ///< the I²C software reset
        };
        /// What follows a draw's sleep.
        enum class After : uint8_t
        {
            Done,  ///< the pool filled, or the set-up over
            Retry, ///< the software reset, then the wake again
            Fault, ///< three tries failed
        };
        struct Transaction
        {
            Op op;
            uint8_t reg{0}, data{0};
            std::array<uint8_t, TIME_REGS> time{};
            uint64_t osc{0}; ///< a Refresh: the OSC clock its edge fell on
        };

        void apply_reset();
        void rtc_reset();
        void retime();
        uint64_t cost(const Transaction &t) const;
        static bool clock_op(Op op) { return op <= Op::Write; }
        void rng_start();
        void rng_fail();
        void rng_after(Op op, uint32_t us); ///< `op` queued `us` after now
        void rng_now();                     ///< the power-on set-up, run at once
        void queue(const Transaction &t);
        void finish(const Transaction &t);
        void read_time();
        void read_status();
        /// The timebase: OSC clocks per CPU cycle at hz_, as num_ / den_.
        void osc_rate();
        /// The OSC clock of tick k since the hard reset's release.
        uint64_t tick_at(uint64_t k) const { return tb_base_ + k * OSC_11MS / 11; }
        /// CPU cycles until the next tick (at least 1).
        uint64_t until_tick() const;
        /// `cycles` of OSC counted, and any tick they reach.
        void count_osc(uint64_t cycles);
        /// IRQL (high = false) or IRQH: 0 bits for interrupting modules.
        uint8_t poll(bool high) const;

        Decoder *decoder_{nullptr};
        uint32_t aperture_{NO_APERTURE};
        uint8_t revision_;
        bool bootsel_;
        bool power_on_{true};
        bool reset_seen_{false};
        std::array<uint8_t, 4> win_{};
        uint8_t vec_{0};
        std::array<uint8_t, 4> winh_{};
        uint8_t vech_{0};
        uint8_t map_{MAP_AP8};
        uint8_t hole_{0}; ///< HOLE: 0 at every reset
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

        // The timebase
        uint64_t num_{2}, den_{1};       ///< OSC clocks a CPU cycle, num_ / den_
        uint64_t osc_{0}, frac_{0};      ///< OSC clocks since power-on, and den_ths of one
        uint64_t tb_base_{0};            ///< osc_ at the hard reset's release: tick 0
        uint64_t ticks_{0};              ///< ticks since then
        uint32_t ms_{0}, ms_latch_{0};   ///< MS, and MS0's latch of it
        uint16_t tbsub_latch_{0}, mss_latch_{0};
        bool msf_{false}, ovr_{false}, msie_{false};
        bool synced_{false};             ///< an edge's read has landed since power-on
        int64_t r_osc_{0};               ///< R: where MSS counts from
        uint64_t edge_osc_{0};           ///< the last SQW edge

        // The random source
        std::unique_ptr<Atecc608> chip_;
        std::deque<uint8_t> pool_;
        bool rng_chip_{false}, rng_locked_{false}, rng_fault_{false}, drawing_{false};
        bool setup_{false};               ///< the power-on set-up: the lock byte's Read first
        bool reading_lock_{false};        ///< the command under way is the lock byte's Read
        unsigned try_{0};
        After after_{After::Done};
        uint64_t rng_wait_{0};            ///< cycles until rng_next_ is queued (0: nothing waits)
        Op rng_next_{Op::Wake};
        uint64_t since_command_{0};       ///< cycles since the command's end, for the give-up
        bool first_poll_{false};
        std::vector<uint8_t> resp_;
        RngCounts counts_;
    };
} // namespace ooe::emu
