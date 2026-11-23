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
 * /RESET (bus_reset) and CTRL's RESET do the same: an open transfer abandoned, both FIFOs emptied, every
 * sound stopped, CTRL, IEN and ISR 0; the firmware runs on and the store keeps its objects. FWRESTART starts
 * the firmware over: READY clear for its start-up, the store emptied.
 */

#include <cstdint>
#include <memory>

#include "device.h"
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
        bool irq() const override { return (isr() & ien_) != 0; }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;
        bool drq() const override { return (ctrl_ & CTRL_DRQ) && fw_.xfer_open(); }
        void dack_write(uint8_t data, bool tc) override;

        SoundFw &firmware() { return fw_; }
        const SoundFw &firmware() const { return fw_; }
        /// Bytes DATA dropped (none open), for tests.
        uint64_t data_dropped() const { return dropped_; }

    private:
        // SoundFwPort
        void silence() override;

        void catch_up();       ///< the firmware to the host's cycle
        void take_edges();     ///< ISR's latched bits from the firmware
        uint8_t isr() const { return static_cast<uint8_t>(isr_ | (fw_.evt_len() ? INT_EVT : 0)); }
        uint8_t status() const;
        void strobe_reset();

        SoundFw fw_;
        uint32_t host_hz_{NATIVE_HZ};
        uint64_t phase_{0};    ///< host cycles x 48,000 owed below one sample
        uint8_t ctrl_{0}, ien_{0}, isr_{0};
        uint32_t xlatch_{0};
        uint64_t dropped_{0};
    };
} // namespace ooe::emu
