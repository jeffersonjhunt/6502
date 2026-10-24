#pragma once

/**
 * @file video_card.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The backplane's video card (module ID 9): VGA 640x480 from snooped memory.
 * @version 0.1.0
 * @date 2026-10-24
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md ("Video", "Video registers") is the spec. Registers at $C480:
 *
 *   $00 CTRL  $01 BORDER  $02 PALBASE  $03 SPRBASE  $04 IEN  $05 ISR
 *   $06-$07 LINE  $08 STATUS  $7F ID
 *   layer n at $10 + n x $10:  +0 CONFIG  +1 MAPBASE  +2 TILEBASE
 *                              +3-4 HSCROLL  +5-6 VSCROLL  +7 PALOFS
 *
 * Memory. The card draws from banks 8-B ($80000-$BFFFF), which on the
 * board it keeps a copy of by snooping every write to them. A copy that
 * sees every write is the same as the RAM, so the emulator reads the RAM
 * (attach()), with no bus cycles: CPU, DMA and front-panel writes all show.
 * Bases are in 1 KB units of that 256 KB, and addresses wrap within it.
 *
 * Time. VGA 640x480 at 60 Hz: 800 pixel clocks a line at 25.175 MHz, 525
 * lines, 480 of them shown. The card is timed: tick() turns CPU cycles
 * into pixel clocks at the CPU clock it is told (set_cpu_hz(); 7.16 MHz
 * by default), so the picture runs at 60 frames a second whatever the CPU
 * does. Each line is drawn when the beam finishes its 640 visible pixels,
 * from memory and registers as they are then, so a change mid-frame shows
 * from the next line, as it would on the card. Vertical blank starts at
 * line 480: base and scroll registers written since take effect then (one
 * write flips buffers without tearing), the finished frame becomes
 * frame(), and the vertical blank interrupt fires. The line interrupt
 * fires as the beam starts line LINE.
 *
 * Interrupts: an event sets its ISR bit whether or not IEN enables it;
 * irq() is ISR & IEN, which pulls bit 1 of IRQH (ID 9). Write 1s to ISR to
 * clear.
 *
 * Pixels. Layer 1 is drawn over layer 0. A pixel whose value is 0 (before
 * PALOFS) is transparent. Where both layers are transparent the screen
 * shows palette entry 0; outside the area a layer covers (the bitmap modes
 * do not fill 640x480) it shows BORDER. Display off (CTRL bit 0) is black.
 * Palette entries are 12-bit, `GGGGBBBB` then `0000RRRR`.
 *
 * Layer types so far: text 80x30 and 40x30 (40 columns are doubled
 * across), and mono 640x480. Text cells are a character byte and an
 * attribute byte (foreground low nibble, background high), the font is 256
 * characters of 16 bytes at TILEBASE, the leftmost pixel in bit 7; CONFIG
 * bit 7 makes attribute bit 7 blink the character (16 frames on, 16 off)
 * and leaves 8 backgrounds. Mono is 80 bytes a row, bit 7 leftmost: 1 is
 * colour 1. Scroll wraps within the layer. Tile, bitmap 256 and bitmap
 * 512 layers, and sprites, come later: until then they draw nothing.
 */

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "device.h"

namespace ooe::emu
{
    class VideoCard : public Device
    {
    public:
        static constexpr uint8_t MODULE = 0x9;
        static constexpr uint8_t ID = MODULE << 4 | 1; ///< +$7F: type (module ID) and revision

        static constexpr unsigned WIDTH = 640, HEIGHT = 480;
        static constexpr unsigned LINE_CLOCKS = 800, LINES = 525;
        static constexpr uint32_t PIXEL_HZ = 25'175'000;
        static constexpr uint32_t NATIVE_HZ = 7'159'090; ///< 14.31818 MHz / 2
        static constexpr uint32_t VRAM_BASE = 0x80000, VRAM_SIZE = 0x40000; ///< banks 8-B

        enum Reg : uint8_t
        {
            CTRL = 0x00,
            BORDER = 0x01,
            PALBASE = 0x02,
            SPRBASE = 0x03,
            IEN = 0x04,
            ISR = 0x05,
            LINE = 0x06,
            STATUS = 0x08,
            LAYER0 = 0x10,
            LAYER1 = 0x20,
            ID_REG = 0x7F,
        };
        enum LayerReg : uint8_t
        {
            CONFIG = 0x0,
            MAPBASE = 0x1,
            TILEBASE = 0x2,
            HSCROLL = 0x3,
            VSCROLL = 0x5,
            PALOFS = 0x7,
        };
        static constexpr uint8_t DISPLAY = 0x01, LAYER0_ON = 0x02, LAYER1_ON = 0x04, SPRITES_ON = 0x08;
        static constexpr uint8_t VBLANK = 0x01, LINE_MATCH = 0x02, COLLISION = 0x04;
        static constexpr uint8_t IN_VBLANK = 0x80;
        static constexpr uint8_t TEXT80 = 0, TEXT40 = 1, TILE = 2, BITMAP256 = 3, BITMAP512 = 4, MONO = 5;
        static constexpr uint8_t BLINK = 0x80; ///< CONFIG: attribute bit 7 blinks

        VideoCard();

        /// Banks 8-B: VRAM_SIZE bytes, as the RAM holds them. Not owned; null draws $FF.
        void attach(const uint8_t *vram) { vram_ = vram; }
        /// The CPU clock that tick()'s cycles run at; 0 = NATIVE_HZ.
        void set_cpu_hz(uint32_t hz);
        void reset();

        // Device: the registers at $C480
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override { return peek(offset); }
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return (isr_ & ien_ & 0x07) != 0; }
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;

        /// The last finished frame: WIDTH x HEIGHT pixels, row by row, each R, G, B, A bytes in memory.
        /// The pointer changes at every vertical blank (the buffers swap): ask again each time.
        const uint32_t *frame() const { return front_.data(); }
        /// Frames finished since reset.
        uint64_t frames() const { return frames_; }
        /// The line the beam is on, 0-524 (480 on: vertical blank).
        unsigned beam_line() const { return static_cast<unsigned>(pos_ / LINE_CLOCKS); }

        /// 12-bit palette colour (0x0RGB) as a frame() pixel.
        static uint32_t rgba(uint16_t rgb12);

    private:
        struct Layer
        {
            uint8_t config{0};
            uint8_t mapbase{0}, tilebase{0}; ///< as written
            uint16_t hscroll{0}, vscroll{0};  ///< as written, 10 bits
            uint8_t palofs{0};
            uint8_t live_mapbase{0}, live_tilebase{0}; ///< in effect since the last vertical blank
            uint16_t live_hscroll{0}, live_vscroll{0};
        };

        uint8_t vram(uint32_t offset) const { return vram_ ? vram_[offset & (VRAM_SIZE - 1)] : 0xFF; }
        /// Pixel clocks: draw each line as it ends, start each one.
        void advance(uint64_t clocks);
        void start_line(unsigned y);
        void draw_line(unsigned y);
        /// Layer pixels into `line`: palette indices; -1 where nothing covers (border), 0 transparent.
        void draw_layer(const Layer &l, unsigned y, int16_t *line) const;
        void draw_text(const Layer &l, unsigned y, int16_t *line, bool wide) const;
        void draw_mono(const Layer &l, unsigned y, int16_t *line) const;

        const uint8_t *vram_{nullptr};
        uint32_t cpu_hz_{NATIVE_HZ};
        uint64_t remainder_{0}; ///< pixel clocks x cpu_hz_ not yet a whole clock
        uint64_t pos_{0};       ///< pixel clock in the frame, 0 to LINES x LINE_CLOCKS - 1
        uint64_t frames_{0};

        uint8_t ctrl_{0}, border_{0};
        uint8_t palbase_{0}, live_palbase_{0};
        uint8_t sprbase_{0}, live_sprbase_{0};
        uint8_t ien_{0}, isr_{0};
        uint16_t match_line_{0};
        std::array<Layer, 2> layers_{};

        std::vector<uint32_t> front_, back_;
    };

    /// Write the card's last frame as a binary PPM (P6). @return false if the file cannot be written
    bool write_ppm(const VideoCard &card, const std::string &path);
} // namespace ooe::emu
