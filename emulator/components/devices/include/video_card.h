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
 *   $06-$07 LINE  $08 STATUS  $09 SPLASH  $7F ID
 *   layer n at $10 + n x $10:  +0 CONFIG  +1 MAPBASE  +2 TILEBASE
 *                              +3-4 HSCROLL  +5-6 VSCROLL  +7 PALOFS
 *
 * Memory, revision 1 (the default for now; set_revision()). The card draws
 * from banks 8-B ($80000-$BFFFF), which on the board it keeps a copy of by
 * snooping every write to them. A copy that sees every write is the same as
 * the RAM, so the emulator reads the RAM (attach()), with no bus cycles:
 * CPU, DMA and front-panel writes all show. Bases are in 1 KB units of that
 * 256 KB, and addresses wrap within it.
 *
 * Revision 2 (hw/video-rev2.md, proposed; set_revision(2), or `rev=2` on the
 * machine line): the card owns 2 MB of card memory and snoops nothing in
 * native mode. Registers ($C480 + offset; Rev2 names the moved ones):
 *
 *   $00 CTRL  $01 BORDER  $04 IEN  $05 ISR  $06-$07 LINE  $08 STATUS  $09 SPLASH
 *   $0A-$0B PALBASE  $0C-$0D SPRBASE      card address / 256, low byte first
 *   layer n at $10 + n x $10:  +0 CONFIG  +1-2 MAPBASE  +3-4 TILEBASE
 *                              +5-6 HSCROLL  +7-8 VSCROLL  +9 PALOFS
 *   $30-$37 SLOT0-3   aperture pages (16 KB), low byte first; /RESET: SLOTn = n
 *   $38-$3A PADDR     the port's 24-bit address    $3B PDATA  the port's data
 *   $3C-$3D PSTEP     the step after each PDATA access (0 holds); /RESET: 1
 *   $3E PCTL          bit 0 DRQ enable, bit 1 from the card, bit 2 step down
 *   $3F MEMSIZE       64 KB units fitted: 32     $7F ID  $92
 *
 * Every other byte reads 0. Bases are 24-bit card addresses (unit x 256);
 * the sprite image address is 15 bits of 256-byte units (byte 0, then byte 1
 * bits 0-6; bit 7 is still the depth). Card memory past MEMSIZE reads $FF
 * and ignores writes, by scanout, the aperture and the port alike. The
 * aperture (VideoAperture, a memory device on bank 8) shows four 16 KB slots
 * of card memory, slot n from SLOTn x $4000, and ignores cycles while the
 * north bridge is in Apple II mode. Each PDATA read or write moves PADDR by
 * PSTEP, up or (PCTL bit 2) down, wrapping at the fitted memory's end. While
 * PCTL bit 0 is set the card asserts DRQ (its machine line's `drq=`, channel
 * 1), and /DACK cycles move bytes through the port in PCTL bit 1's
 * direction; a /DACK cycle the other way is ignored. /RESET resets the
 * registers and never touches card memory.
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
 * APPLE_CHARS (on revision 2, card $1E0000: APPLE_CHARS_V2; 2 KB: 8 bytes
 * a code, bit 0 the leftmost of 7 dots, lit where the bit is 0): ALTCHARSET off makes $40-$7F flash between inverse and
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
 *
 * Boot splash (SPLASH, hw/design.md "Boot splash"): the One Off Endeavors
 * logo and the VINTAGE FAST stamp, drawn by the card itself from its own ROM (splash_rom.h, the FPGA's
 * block RAM: splashgen.py makes both), not from video memory, so the BIOS
 * can start it before anything is in the video banks. Writing SPLASH bit 0
 * starts it at the next vertical blank; it covers the whole screen, whatever
 * CTRL says, for the ROM's 300 frames (5 s), and then the card goes back to
 * its layers by itself and bit 0 clears. Writing 0 stops it at the next
 * vertical blank. /RESET stops it. Each frame, each of the logo's parts sits
 * at its home plus an offset from its curve in the ROM, shaken in y by the
 * shake curve: moving parts through a box filter as wide as the curve says
 * (motion blur: on the card, a running sum over a delay line), the stamp
 * scaled about its centre (two accumulators) in its ink. The brightest part
 * wins; then the glint, and the fade over the last frames. splashgen.py's Rom.render_frame() is the same
 * rule in Python, and the reference.
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
        static constexpr uint8_t ID = MODULE << 4 | 1; ///< +$7F, revision 1: type (module ID) and revision
        static constexpr uint8_t ID_V2 = MODULE << 4 | 2; ///< +$7F, revision 2 (hw/video-rev2.md)
        static constexpr uint32_t MEMORY_SIZE = 0x200000; ///< revision 2: 2 MB of card memory
        static constexpr uint32_t APERTURE_SIZE = 0x10000, SLOT_SIZE = 0x4000; ///< four 16 KB slots

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
            SPLASH = 0x09,
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
        /// Revision 2's register offsets where they differ from revision 1's (the rest are the same).
        struct Rev2
        {
            static constexpr uint8_t PALBASE = 0x0A, SPRBASE = 0x0C; ///< two bytes each
            static constexpr uint8_t MAPBASE = 0x1, TILEBASE = 0x3;  ///< layer +, two bytes each
            static constexpr uint8_t HSCROLL = 0x5, VSCROLL = 0x7, PALOFS = 0x9;
            static constexpr uint8_t SLOT0 = 0x30; ///< SLOTn at SLOT0 + 2n, two bytes
            static constexpr uint8_t PADDR = 0x38, PDATA = 0x3B, PSTEP = 0x3C, PCTL = 0x3E, MEMSIZE = 0x3F;
            static constexpr uint8_t PCTL_DRQ = 0x01, PCTL_FROM_CARD = 0x02, PCTL_DOWN = 0x04;
        };
        static constexpr uint8_t DISPLAY = 0x01, LAYER0_ON = 0x02, LAYER1_ON = 0x04, SPRITES_ON = 0x08;
        static constexpr uint8_t VBLANK = 0x01, LINE_MATCH = 0x02, COLLISION = 0x04;
        static constexpr uint8_t IN_VBLANK = 0x80;
        static constexpr uint8_t SPLASH_ON = 0x01; ///< SPLASH: playing (or about to, at the next vertical blank)
        static constexpr uint8_t TEXT80 = 0, TEXT40 = 1, TILE = 2, BITMAP256 = 3, BITMAP512 = 4, MONO = 5;
        static constexpr uint8_t BLINK = 0x80; ///< CONFIG: attribute bit 7 blinks
        static constexpr uint8_t TILE16 = 0x08, TILE8BPP = 0x10; ///< CONFIG, tile layers: 16x16 tiles, 8 bits
        static constexpr uint8_t MAP_W64 = 0x20, MAP_H64 = 0x40; ///< CONFIG, tile layers: map 64 wide, 64 high

        VideoCard();

        /// Revision 1: banks 8-B, VRAM_SIZE bytes, as the RAM holds them. Not owned; null draws $FF.
        void attach(const uint8_t *vram) { vram_ = vram; }
        /// 1 (snooped banks 8-B, ID $91) or 2 (card memory, ID $92). Resets the registers, not memory.
        /// Build time only: once lock_revision() has been called (a Machine does, as it wires the bus
        /// for the revision its description chose) it changes nothing and returns false.
        bool set_revision(unsigned rev);
        void lock_revision() { revision_locked_ = true; }
        unsigned revision() const { return rev_; }
        /// Revision 2's card memory, MEMORY_SIZE bytes; nullptr on revision 1, which has none.
        /// card_peek() and card_poke() are the safe way in.
        uint8_t *card_memory() { return rev_ == 2 ? memory_.data() : nullptr; }
        const uint8_t *card_memory() const { return rev_ == 2 ? memory_.data() : nullptr; }
        /// Card memory at a 24-bit card address: $FF past what is fitted (revision 2).
        uint8_t card_peek(uint32_t addr) const { return addr < memory_.size() ? memory_[addr] : 0xFF; }
        void card_poke(uint32_t addr, uint8_t data)
        {
            if (addr < memory_.size())
                memory_[addr] = data;
        }
        /// The aperture's byte at `offset` (0-$FFFF): slot offset / 16 KB. Revision 2; ignored in Apple II mode.
        uint8_t aperture_peek(uint32_t offset) const;
        void aperture_write(uint32_t offset, uint8_t data);
        /// Apple II mode: banks 0 and 1 (main and auxiliary, 128 KB as the RAM holds
        /// them), and the north bridge whose mode and switches say what to draw. Not owned.
        void attach_apple(const uint8_t *banks01, const BackplaneBridge *bridge)
        {
            apple_ram_ = banks01;
            bridge_ = bridge;
        }
        /// The character ROM's 2 KB in video memory (the BIOS's C806R puts it there): $BE000.
        static constexpr uint32_t APPLE_CHARS = 0x3E000;
        /// Revision 2: the character ROM in card memory (hw/video-rev2.md's layout).
        static constexpr uint32_t APPLE_CHARS_V2 = 0x1E0000;
        /// The CPU clock that tick()'s cycles run at; 0 = NATIVE_HZ.
        void set_cpu_hz(uint32_t hz);
        /// Power-on: the registers, the beam at line 0 and a black picture.
        void reset();

        // Device: the registers at $C480
        size_t size() const override { return 0x80; }
        uint8_t read(uint32_t offset) override; ///< revision 2's PDATA steps PADDR
        uint8_t peek(uint32_t offset) const override;
        void write(uint32_t offset, uint8_t data) override;
        bool irq() const override { return (isr_ & ien_ & 0x07) != 0; }
        /// Revision 2: the port asks for DMA while PCTL bit 0 is set.
        bool drq() const override { return rev_ == 2 && (pctl_ & Rev2::PCTL_DRQ); }
        uint8_t dack_read(bool tc) override;
        void dack_write(uint8_t data, bool tc) override;
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
        /// The splash's frame on the screen now, or -1 when the layers are.
        int splash_frame() const { return splash_live_ ? static_cast<int>(splash_frame_) : -1; }

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
            uint16_t mapbase{0}, tilebase{0}; ///< as written (revision 1: the low byte only)
            uint16_t hscroll{0}, vscroll{0};  ///< as written, 10 bits
            uint8_t palofs{0};
            uint16_t live_mapbase{0}, live_tilebase{0}; ///< in effect since the last vertical blank
            uint16_t live_hscroll{0}, live_vscroll{0};
        };

        /// What scanout reads: revision 1, banks 8-B, wrapping; revision 2, card memory.
        uint8_t vram(uint32_t offset) const
        {
            if (rev_ == 2)
                return card_peek(offset);
            return vram_ ? vram_[offset & (VRAM_SIZE - 1)] : 0xFF;
        }
        /// A base register's card address: 1 KB units on revision 1, 256 bytes on revision 2.
        uint32_t base(uint16_t reg) const { return rev_ == 2 ? uint32_t{reg} << 8 : uint32_t{reg} << 10; }
        uint8_t peek_rev2(uint32_t offset) const;
        void write_rev2(uint32_t offset, uint8_t data);
        /// The port: the byte at PADDR, or a byte stored there; then PADDR moves by PSTEP.
        uint8_t port_read();
        void port_write(uint8_t data);
        void port_step();
        bool apple_mode() const;
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
        /// The boot splash's line y, from the card's ROM.
        void draw_splash(unsigned y, uint32_t *out) const;
        /// Vertical blank: start, advance or end the splash.
        void splash_vblank();

        unsigned rev_{1};
        bool revision_locked_{false};
        const uint8_t *vram_{nullptr};
        std::vector<uint8_t> memory_; ///< revision 2: card memory (empty on revision 1)
        std::array<uint16_t, 4> slot_{};
        uint32_t paddr_{0}; ///< 24 bits
        uint16_t pstep_{1};
        uint8_t pctl_{0};
        const uint8_t *apple_ram_{nullptr};
        const BackplaneBridge *bridge_{nullptr};
        /// Pixel clocks per CPU cycle, 32.32 fixed point: tick() is a multiply, and
        /// the sum of the parts is the whole however the cycles are split.
        uint64_t step_{0};
        uint64_t fraction_{0}; ///< a pixel clock's 2^-32 parts not yet whole
        unsigned x_{0}, y_{0}; ///< the beam: pixel clock in the line, and the line
        uint64_t frames_{0};

        uint8_t ctrl_{0}, border_{0};
        uint16_t palbase_{0}, live_palbase_{0}; ///< revision 1: the low byte only
        uint16_t sprbase_{0}, live_sprbase_{0};
        uint8_t ien_{0}, isr_{0};
        uint8_t coll_{0};       ///< STATUS COLL: the groups in the last frame with a collision
        uint8_t frame_coll_{0}; ///< groups that collided in the frame being drawn
        uint16_t match_line_{0};
        std::array<Layer, 2> layers_{};
        uint8_t splash_{0};          ///< SPLASH as written (bit 0); cleared when it ends
        bool splash_live_{false};    ///< on the screen, since a vertical blank
        unsigned splash_frame_{0};

        std::vector<uint32_t> front_, back_;
    };

    /**
     * @brief Revision 2's aperture: 64 KB of physical memory (bank 8 on the backplane) that the
     * card answers, four 16 KB slots of card memory (VideoCard::aperture_peek()). The machine's
     * `aperture` line; without a revision 2 card it shows nothing (reads $FF).
     */
    class VideoAperture : public Device
    {
    public:
        void attach(VideoCard *card) { card_ = card; }
        size_t size() const override { return VideoCard::APERTURE_SIZE; }
        uint8_t read(uint32_t offset) override { return peek(offset); }
        uint8_t peek(uint32_t offset) const override { return card_ ? card_->aperture_peek(offset) : 0xFF; }
        void write(uint32_t offset, uint8_t data) override
        {
            if (card_)
                card_->aperture_write(offset, data);
        }

    private:
        VideoCard *card_{nullptr};
    };

    /// Write the card's last frame as a binary PPM (P6). @return false if the file cannot be written
    bool write_ppm(const VideoCard &card, const std::string &path);
} // namespace ooe::emu
