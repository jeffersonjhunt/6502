#pragma once

/**
 * @file usb_card.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's USB card (module ID B): keyboard, mouse, game port and speaker.
 * @version 0.1.0
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("USB card registers") is the spec. Registers at $C580:
 *
 *   $00 KEY  $01 KEYCLR  $02 MODS  $03 EVCOUNT  $04 EVENT  $05 LEDS  $06 REPEAT  $07 KBCFG
 *   $10 MCTRL  $11 MBTN  $12-$13 MX  $14-$15 MY  $16 MWHEEL  $17-$18 MAXX  $19-$1A MAXY
 *   $20 GAME  $21 PTRIG  $22-$25 PADDLE0-3  $26 SPKR
 *   $30 IEN  $31 ISR  $32 DEVICES  $7F ID
 *
 * The host side is what the RP2350 hears over USB: key() takes HID usage
 * codes (modifiers $E0-$E7 included), the mouse takes movement, a position,
 * buttons and the wheel, and the game port takes paddle settings and buttons.
 * queue_key() is key() at the keyboard's pace: one change per 8 ms polling
 * interval of CPU time, as reports arrive over USB.
 *
 * Keyboard. Every press and release goes into the raw event FIFO (128 bytes:
 * usage, then flags, bit 7 = release; a full FIFO drops the event whole),
 * which asserts DRQ while not empty. Presses that mean a character also go to
 * KEY, cooked in the US layout with the Apple's conventions: the strobe in
 * bit 7, arrows $08 $15 $0A $0B, Delete $7F, Ctrl with $40-$5F giving $00-$1F,
 * and Caps Lock on letters only (on at power-up, as a IIe is usually left).
 * KEY holds one character, as on the Apple: a new one replaces it. The last
 * key pressed repeats while held: after (REPEAT bits 4-5 + 1) x 250 ms, every
 * (bits 0-3 + 1) x 33 ms; REPEAT resets to $10. With KBCFG bit 7 set,
 * Pause/Break pulls /NMI (take_nmi()).
 *
 * Mouse. Relative mode adds movement up; absolute mode (MCTRL bit 0) keeps a
 * position clamped to MAXX, MAXY. Reading MX's low byte latches MX, MY and
 * MWHEEL together, and in relative mode starts the next sums at 0.
 *
 * Game port. PTRIG starts the 558: each paddle's GAME bit is 1 for about
 * 11 us per count of its setting, in real time (set_cpu_hz()). PADDLE0-3 are
 * the settings themselves, as the RP2350 measures them. SPKR toggles the
 * speaker on any access (speaker_toggles() counts them, for sound later).
 *
 * Interrupts: a key event, mouse change, game button change or device change
 * sets its ISR bit whether IEN enables it or not; irq() is ISR & IEN, which
 * pulls bit 3 of IRQH. Write 1s to ISR to clear. A keyboard and a mouse are
 * connected from power on (DEVICES).
 *
 * In Apple II mode the IIe's addresses reach the card through the north
 * bridge (apple_io.h): KEY, KEYCLR, SPKR, the game port, and Open and Closed
 * Apple (apple_keys()) on buttons 0 and 1. Ctrl-Delete is the IIe's
 * Ctrl-Reset: the card pulses /RESET (take_reset()), in either mode.
 */

#include <array>
#include <cstdint>
#include <deque>
#include <utility>

#include "device.h"

namespace ooe::emu
{
    class UsbCard : public Device
    {
    public:
        static constexpr uint8_t MODULE = 0xB;
        static constexpr uint8_t ID = MODULE << 4 | 1; ///< +$7F: type (module ID) and revision
        static constexpr uint32_t NATIVE_HZ = 7'159'090;

        enum Reg : uint8_t
        {
            KEY = 0x00,
            KEYCLR = 0x01,
            MODS = 0x02,
            EVCOUNT = 0x03,
            EVENT = 0x04,
            LEDS = 0x05,
            REPEAT = 0x06,
            KBCFG = 0x07,
            MCTRL = 0x10,
            MBTN = 0x11,
            MX = 0x12,
            MY = 0x14,
            MWHEEL = 0x16,
            MAXX = 0x17,
            MAXY = 0x19,
            GAME = 0x20,
            PTRIG = 0x21,
            PADDLE0 = 0x22,
            SPKR = 0x26,
            IEN = 0x30,
            ISR = 0x31,
            DEVICES = 0x32,
            ID_REG = 0x7F,
        };
        static constexpr uint8_t STROBE = 0x80;
        static constexpr uint8_t IRQ_KEY = 0x01, IRQ_MOUSE = 0x02, IRQ_GAME = 0x04, IRQ_DEVICE = 0x08;
        static constexpr uint8_t RELEASE = 0x80; ///< EVENT flags
        static constexpr uint8_t NMI_ON_PAUSE = 0x80, ABSOLUTE = 0x01;
        static constexpr size_t FIFO_BYTES = 128;

        /// HID usages the card treats specially.
        static constexpr uint8_t CAPS_LOCK = 0x39, PAUSE = 0x48, DELETE = 0x4C, LEFT_CTRL = 0xE0;

        UsbCard() { reset(); }
        void reset();
        /// The CPU clock tick()'s cycles run at (0: 7.16 MHz), for key repeat and the paddle timer.
        void set_cpu_hz(uint32_t hz) { cpu_hz_ = hz ? hz : NATIVE_HZ; }

        // The host side: what arrives over USB
        /// A key report, now.
        void key(uint8_t usage, bool down);
        /// A key change from the host, as the keyboard would report it: at most
        /// one every POLL_MS (the HID polling interval), queued until then, so
        /// a burst of keys does not overwrite KEY faster than USB could.
        void queue_key(uint8_t usage, bool down);
        static constexpr unsigned POLL_MS = 8;
        void mouse_move(int dx, int dy);
        /// Absolute mode: where the pointer is now (clamped to MAXX, MAXY).
        void mouse_to(int x, int y);
        void mouse_buttons(uint8_t buttons);
        void mouse_wheel(int steps);
        void set_paddle(unsigned n, uint8_t value);
        void set_game_buttons(uint8_t buttons); ///< bits 0-2
        /// Pause/Break with KBCFG bit 7: /NMI, taken once.
        bool take_nmi();
        /// Ctrl-Delete: the card pulses the bus's /RESET (Ctrl-Reset, which Apple II
        /// mode keeps). Taken once.
        bool take_reset();
        /// Open Apple (left GUI or Alt) and Closed Apple (right GUI or Alt), bits 0 and
        /// 1, as a IIe wires them to buttons 0 and 1.
        uint8_t apple_keys() const;
        uint64_t speaker_toggles() const { return speaker_toggles_; }
        bool caps_lock() const { return caps_; }
        uint8_t leds() const { return leds_; }

        // Device: the registers at $C580
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override;
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return (isr_ & ien_ & 0x0F) != 0; }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;
        bool drq() const override { return fifo_count_ != 0; }
        uint8_t dack_read(bool tc) override;

        /// The US layout with the Apple's conventions: the character a usage
        /// gives with these modifiers, or 0 for none (function keys, modifiers).
        static uint8_t cooked(uint8_t usage, uint8_t mods, bool caps);

    private:
        void push_event(uint8_t usage, uint8_t flags);
        uint8_t pop_event();
        void strobe(uint8_t ascii);
        uint64_t ms(unsigned n) const { return uint64_t{cpu_hz_} * n / 1000; }
        int clamp(int v, int max) const { return v < 0 ? 0 : v > max ? max : v; }

        uint32_t cpu_hz_{NATIVE_HZ};
        uint64_t now_{0}; ///< cycles since reset

        // keyboard
        uint8_t key_{0}, mods_{0}, leds_{0}, repeat_{0x10}, kbcfg_{0};
        bool caps_{true}, nmi_{false}, reset_{false};
        std::array<uint8_t, FIFO_BYTES> fifo_{};
        size_t fifo_head_{0}, fifo_count_{0};
        uint8_t held_{0};         ///< the usage that repeats, 0 for none
        uint64_t next_repeat_{0}; ///< cycle of its next repeat
        std::array<bool, 256> down_{};
        std::deque<std::pair<uint8_t, bool>> pending_; ///< queue_key()'s, not yet reported
        uint64_t next_report_{0};

        // mouse
        uint8_t mctrl_{0}, mbtn_{0};
        int sum_x_{0}, sum_y_{0}, sum_wheel_{0}; ///< relative: since the last latch
        int pos_x_{0}, pos_y_{0};
        uint16_t max_x_{639}, max_y_{479};
        uint16_t latch_x_{0}, latch_y_{0};
        uint8_t latch_wheel_{0};

        // game port and speaker
        std::array<uint8_t, 4> paddles_{};
        uint8_t game_buttons_{0};
        uint64_t trigger_{0};
        bool triggered_{false};
        uint64_t speaker_toggles_{0};

        uint8_t ien_{0}, isr_{0};
    };
} // namespace ooe::emu
