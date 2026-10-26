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
 * across), mono 640x480, bitmap 256 and 512, and tiles. Text cells are a character byte and an
 * attribute byte (foreground low nibble, background high), the font is 256
 * characters of 16 bytes at TILEBASE, the leftmost pixel in bit 7; CONFIG
 * bit 7 makes attribute bit 7 blink the character (16 frames on, 16 off)
 * and leaves 8 backgrounds. Mono is 80 bytes a row, bit 7 leftmost: 1 is
 * colour 1. Bitmaps are 256 bytes a row and centred, with BORDER either
 * side: bitmap 256 is 256x240 at 8 bits, doubled both ways to 512x480
 * (PALOFS does not apply); bitmap 512 is 512x480 at 4 bits, the left pixel
 * in the high 4 bits. Tile layers are 320x240, doubled: 8x8 or 16x16 tiles
 * (CONFIG bit 3) at 4 or 8 bits (bit 4), from a map 32 or 64 entries wide
 * (bit 5) and high (bit 6) of VERA's 2-byte entries (tile number 0-1023,
 * H and V flip, palette offset); tile n is at TILEBASE + n x its size, row
 * by row, left pixel high. A 4-bit tile's colour is (entry offset + PALOFS)
 * x 16 + pixel; an 8-bit tile's is the pixel. Scroll wraps within the
 * layer.
 *
 * Apple II mode (the north bridge's APPLE, hw/design.md "Apple II mode"):
 * the card draws the IIe's display from banks 0 and 1 instead, as the north
 * bridge's switches say, its own registers out of reach. Each Apple dot is
 * 2x2 pixels, the 560x384 picture centred in 640x480 on black. Text, 40 or
 * 80 columns (80: aux, main, aux...), with the IIe's character ROM at
 * APPLE_CHARS (2 KB: 8 bytes a code, bit 0 the leftmost of 7 dots, lit where
 * the bit is 0): ALTCHARSET off makes $40-$7F flash between inverse and
 * normal, on shows them as they are (MouseText, inverse lowercase). Lo-res:
 * 40x48 blocks of 16 colours. Hi-res: 280 dots a line, colour from NTSC
 * artifacts (a lone dot is violet or green, blue or orange when its byte's
 * bit 7 shifts it half a dot right; two together are white). MIXED: the last
 * four text rows over graphics. PAGE2 picks page 2 unless 80STORE. With
 * 80COL and AN3 off, the double modes: lo-res as 80 blocks of 7 dots (aux,
 * main, ...; aux nibbles shown rotated a bit, as their dots fall out of
 * phase), hi-res as 560 dots of aux and main bytes in turn, each 4 dots one
 * of the 16 colours by the pattern they make. Every colour follows one rule:
 * a 560-dot line's dot x counts as bit x mod 4 of a lo-res colour, so
 * hi-res's violet is dots 0-1 (3), green 2-3 (12), and so on. A hi-res
 * gap between two dots of one colour takes that colour, as NTSC smears it.
 * Colours are the IIe's, in the card's 12 bits.
 *
 * Sprites (CTRL bit 3) share the tiles' 320x240 space: 64 entries of 8
 * bytes at SPRBASE in VERA's layout (image in 64-byte units, 8-bit flag, X
 * and Y wrapping at 1024, flips, depth, collision groups, palette offset,
 * 8-64 pixels each way), images row by row like tiles. On each line the
 * lowest-numbered 32 that touch it are drawn, a lower number in front.
 * Depth places a sprite behind both layers (1), between them (2) or in
 * front (3); 0 is off. Opaque pixels of two sprites on one spot whose
 * groups share a bit are a collision: at the vertical blank after it,
 * STATUS COLL holds the shared groups and ISR's collision bit is set.
 */

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "device.h"

namespace ooe::emu
{
    class BackplaneBridge;
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
        static constexpr uint8_t TILE16 = 0x08, TILE8BPP = 0x10; ///< CONFIG, tile layers: 16x16 tiles, 8 bits
        static constexpr uint8_t MAP_W64 = 0x20, MAP_H64 = 0x40; ///< CONFIG, tile layers: map 64 wide, 64 high

        VideoCard();

        /// Banks 8-B: VRAM_SIZE bytes, as the RAM holds them. Not owned; null draws $FF.
        void attach(const uint8_t *vram) { vram_ = vram; }
        /// Apple II mode: banks 0 and 1 (main and auxiliary, 128 KB as the RAM holds
        /// them), and the north bridge whose mode and switches say what to draw. Not owned.
        void attach_apple(const uint8_t *banks01, const BackplaneBridge *bridge)
        {
            apple_ram_ = banks01;
            bridge_ = bridge;
        }
        /// The character ROM's 2 KB in video memory (the BIOS's C806R puts it there): $BE000.
        static constexpr uint32_t APPLE_CHARS = 0x3E000;
        /// The CPU clock that tick()'s cycles run at; 0 = NATIVE_HZ.
        void set_cpu_hz(uint32_t hz);
        /// Power-on: the registers, the beam at line 0 and a black picture.
        void reset();

        // Device: the registers at $C480
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override { return peek(offset); }
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return (isr_ & ien_ & 0x07) != 0; }
        /// /RESET (START, Ctrl-Reset, entering Apple II mode): the registers back to their
        /// reset values, interrupts off. The beam and the picture run on: the monitor keeps its sync.
        void bus_reset() override;
        bool timed() const override { return true; }
        void tick(uint64_t cycles) override;

        /// The last finished frame: WIDTH x HEIGHT pixels, row by row, each R, G, B, A bytes in memory.
        /// The pointer changes at every vertical blank (the buffers swap): ask again each time.
        const uint32_t *frame() const { return front_.data(); }
        /// Frames finished since reset.
        uint64_t frames() const { return frames_; }
        /// The line the beam is on, 0-524 (480 on: vertical blank).
        unsigned beam_line() const { return y_; }

        /// 12-bit palette colour (0x0RGB) as a frame() pixel.
        static uint32_t rgba(uint16_t rgb12);

    private:
        static constexpr unsigned SPRITES = 64, SPRITES_PER_LINE = 32, SPRITE_WIDTH = WIDTH / 2;
        struct SpritePixel
        {
            uint8_t colour{0};
            uint8_t depth{0}; ///< 0: no sprite here
        };

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
        void draw_bitmap(const Layer &l, unsigned y, int16_t *line, bool bpp8) const;
        void draw_tiles(const Layer &l, unsigned y, int16_t *line) const;
        /// Apple II mode: line y of the IIe's display, 560x384 centred.
        void draw_apple(unsigned y, uint32_t *out) const;
        uint8_t apple(uint32_t bank, uint16_t addr) const { return apple_ram_ ? apple_ram_[bank << 16 | addr] : 0; }
        /// Line `ly` of the 320x240 sprite space; sprite collisions are noted for the frame.
        void draw_sprites(unsigned ly, SpritePixel *line);

        const uint8_t *vram_{nullptr};
        const uint8_t *apple_ram_{nullptr};
        const BackplaneBridge *bridge_{nullptr};
        /// Pixel clocks per CPU cycle, 32.32 fixed point: tick() is a multiply, and
        /// the sum of the parts is the whole however the cycles are split.
        uint64_t step_{0};
        uint64_t fraction_{0}; ///< a pixel clock's 2^-32 parts not yet whole
        unsigned x_{0}, y_{0}; ///< the beam: pixel clock in the line, and the line
        uint64_t frames_{0};

        uint8_t ctrl_{0}, border_{0};
        uint8_t palbase_{0}, live_palbase_{0};
        uint8_t sprbase_{0}, live_sprbase_{0};
        uint8_t ien_{0}, isr_{0};
        uint8_t coll_{0};       ///< STATUS COLL: the groups in the last frame with a collision
        uint8_t frame_coll_{0}; ///< groups that collided in the frame being drawn
        uint16_t match_line_{0};
        std::array<Layer, 2> layers_{};

        std::vector<uint32_t> front_, back_;
    };

    /// Write the card's last frame as a binary PPM (P6). @return false if the file cannot be written
    bool write_ppm(const VideoCard &card, const std::string &path);
} // namespace ooe::emu
