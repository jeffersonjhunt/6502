/**
 * @file video_card.cpp
 * @brief The backplane's video card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "video_card.h"

#include <cstdio>

namespace ooe::emu
{
    namespace
    {
        constexpr uint32_t BLACK = 0xFF000000;
        constexpr int16_t UNCOVERED = -1;
        constexpr uint16_t SCROLL_MASK = 0x3FF;
        constexpr unsigned BLINK_FRAMES = 16; ///< on, then off: about 1.9 Hz, as on the PC

        uint8_t lo(uint16_t v) { return static_cast<uint8_t>(v); }
        uint8_t hi(uint16_t v) { return static_cast<uint8_t>(v >> 8); }
        void set_lo(uint16_t &v, uint8_t b) { v = static_cast<uint16_t>((v & 0xFF00) | b); }
        void set_hi(uint16_t &v, uint8_t b, uint16_t mask) { v = static_cast<uint16_t>(((b << 8) | (v & 0xFF)) & mask); }
    } // namespace

    VideoCard::VideoCard() : front_(WIDTH * HEIGHT, BLACK), back_(WIDTH * HEIGHT, BLACK) { set_cpu_hz(NATIVE_HZ); }

    uint32_t VideoCard::rgba(uint16_t rgb12)
    {
        const uint32_t r = (rgb12 >> 8) & 0xF, g = (rgb12 >> 4) & 0xF, b = rgb12 & 0xF;
        return 0xFF000000 | (b * 17) << 16 | (g * 17) << 8 | (r * 17);
    }

    void VideoCard::reset()
    {
        ctrl_ = border_ = 0;
        palbase_ = live_palbase_ = sprbase_ = live_sprbase_ = 0;
        ien_ = isr_ = 0;
        coll_ = frame_coll_ = 0;
        match_line_ = 0;
        layers_ = {};
        x_ = y_ = 0;
        fraction_ = frames_ = 0;
        front_.assign(front_.size(), BLACK);
        back_.assign(back_.size(), BLACK);
    }

    void VideoCard::set_cpu_hz(uint32_t hz)
    {
        step_ = (uint64_t{PIXEL_HZ} << 32) / (hz ? hz : NATIVE_HZ);
        fraction_ = 0;
    }

    uint8_t VideoCard::peek(uint32_t offset) const
    {
        if (offset >= LAYER0 && offset < LAYER1 + 0x10)
        {
            const Layer &l = layers_[(offset - LAYER0) >> 4];
            switch (offset & 0xF)
            {
            case CONFIG:
                return l.config;
            case MAPBASE:
                return l.mapbase;
            case TILEBASE:
                return l.tilebase;
            case HSCROLL:
                return lo(l.hscroll);
            case HSCROLL + 1:
                return hi(l.hscroll);
            case VSCROLL:
                return lo(l.vscroll);
            case VSCROLL + 1:
                return hi(l.vscroll);
            case PALOFS:
                return l.palofs;
            default:
                return 0xFF;
            }
        }
        switch (offset)
        {
        case CTRL:
            return ctrl_;
        case BORDER:
            return border_;
        case PALBASE:
            return palbase_;
        case SPRBASE:
            return sprbase_;
        case IEN:
            return ien_;
        case ISR:
            return isr_;
        case LINE:
            return lo(static_cast<uint16_t>(beam_line()));
        case LINE + 1:
            return hi(static_cast<uint16_t>(beam_line()));
        case STATUS:
            return static_cast<uint8_t>((beam_line() >= HEIGHT ? IN_VBLANK : 0) | coll_);
        case ID_REG:
            return ID;
        default:
            return 0xFF;
        }
    }

    void VideoCard::write(uint32_t offset, uint8_t data)
    {
        if (offset >= LAYER0 && offset < LAYER1 + 0x10)
        {
            Layer &l = layers_[(offset - LAYER0) >> 4];
            switch (offset & 0xF)
            {
            case CONFIG:
                l.config = data;
                break;
            case MAPBASE:
                l.mapbase = data;
                break;
            case TILEBASE:
                l.tilebase = data;
                break;
            case HSCROLL:
                set_lo(l.hscroll, data);
                break;
            case HSCROLL + 1:
                set_hi(l.hscroll, data, SCROLL_MASK);
                break;
            case VSCROLL:
                set_lo(l.vscroll, data);
                break;
            case VSCROLL + 1:
                set_hi(l.vscroll, data, SCROLL_MASK);
                break;
            case PALOFS:
                l.palofs = data & 0x0F;
                break;
            default:
                break;
            }
            return;
        }
        switch (offset)
        {
        case CTRL:
            ctrl_ = data & 0x0F;
            break;
        case BORDER:
            border_ = data;
            break;
        case PALBASE:
            palbase_ = data;
            break;
        case SPRBASE:
            sprbase_ = data;
            break;
        case IEN:
            ien_ = data & 0x07;
            break;
        case ISR:
            isr_ = static_cast<uint8_t>(isr_ & ~data);
            break;
        case LINE:
            set_lo(match_line_, data);
            break;
        case LINE + 1:
            set_hi(match_line_, data, SCROLL_MASK);
            break;
        default:
            break;
        }
    }

    void VideoCard::tick(uint64_t cycles)
    {
        constexpr uint64_t CHUNK = uint64_t{1} << 28; // x step_ (under 2^36) stays inside 64 bits
        for (; cycles > CHUNK; cycles -= CHUNK)
            tick(CHUNK);
        fraction_ += cycles * step_;
        const uint64_t clocks = fraction_ >> 32;
        fraction_ &= 0xFFFF'FFFF;
        advance(clocks);
    }

    void VideoCard::advance(uint64_t clocks)
    {
        while (clocks)
        {
            const unsigned to = (x_ < WIDTH ? WIDTH : LINE_CLOCKS) - x_; // the next event
            if (clocks < to)
            {
                x_ += static_cast<unsigned>(clocks);
                return;
            }
            clocks -= to;
            if (x_ < WIDTH)
            {
                x_ = WIDTH;
                if (y_ < HEIGHT)
                    draw_line(y_);
            }
            else
            {
                x_ = 0;
                y_ = y_ + 1 == LINES ? 0 : y_ + 1;
                start_line(y_);
            }
        }
    }

    void VideoCard::start_line(unsigned y)
    {
        if (y == match_line_)
            isr_ |= LINE_MATCH;
        if (y != HEIGHT)
            return;
        // vertical blank: latch, and the frame is done
        live_palbase_ = palbase_;
        live_sprbase_ = sprbase_;
        for (Layer &l : layers_)
        {
            l.live_mapbase = l.mapbase;
            l.live_tilebase = l.tilebase;
            l.live_hscroll = l.hscroll;
            l.live_vscroll = l.vscroll;
        }
        front_.swap(back_);
        ++frames_;
        isr_ |= VBLANK;
        if (frame_coll_) // the frame just drawn had a collision
        {
            coll_ = frame_coll_;
            frame_coll_ = 0;
            isr_ |= COLLISION;
        }
    }

    void VideoCard::draw_line(unsigned y)
    {
        uint32_t *out = &back_[static_cast<size_t>(y) * WIDTH];
        if (!(ctrl_ & DISPLAY))
        {
            for (unsigned x = 0; x < WIDTH; ++x)
                out[x] = BLACK;
            return;
        }
        // The palette as it is now: a fade by DMA shows from the next line.
        std::array<uint32_t, 256> pal;
        const uint32_t base = uint32_t{live_palbase_} * 1024;
        for (unsigned i = 0; i < 256; ++i)
            pal[i] = rgba(static_cast<uint16_t>((vram(base + 2 * i + 1) & 0x0F) << 8 | vram(base + 2 * i)));

        // Back to front: border or entry 0, sprites at depth 1, layer 0, depth 2, layer 1, depth 3.
        std::array<int16_t, WIDTH> l0, l1;
        l0.fill(UNCOVERED);
        l1.fill(UNCOVERED);
        if (ctrl_ & LAYER0_ON)
            draw_layer(layers_[0], y, l0.data());
        if (ctrl_ & LAYER1_ON)
            draw_layer(layers_[1], y, l1.data());
        std::array<SpritePixel, SPRITE_WIDTH> spr{};
        if (ctrl_ & SPRITES_ON)
            draw_sprites(y / 2, spr.data());
        for (unsigned x = 0; x < WIDTH; ++x)
        {
            const SpritePixel s = spr[x / 2];
            int c = l0[x] == UNCOVERED && l1[x] == UNCOVERED ? UNCOVERED : 0;
            if (s.depth == 1)
                c = s.colour;
            if (l0[x] > 0)
                c = l0[x];
            if (s.depth == 2)
                c = s.colour;
            if (l1[x] > 0)
                c = l1[x];
            if (s.depth == 3)
                c = s.colour;
            out[x] = c == UNCOVERED ? pal[border_] : pal[static_cast<uint8_t>(c)];
        }
    }

    void VideoCard::draw_sprites(unsigned ly, SpritePixel *line)
    {
        // 64 entries of 8 bytes (VERA's): the lowest-numbered 32 on this line are drawn, and a
        // lower number is in front. Collision groups (masks) record where opaque pixels overlap.
        std::array<uint8_t, SPRITE_WIDTH> masks{};
        const uint32_t table = uint32_t{live_sprbase_} * 1024;
        unsigned drawn = 0;
        for (unsigned n = 0; n < SPRITES && drawn < SPRITES_PER_LINE; ++n)
        {
            const uint32_t e = table + n * 8;
            const uint8_t flags = vram(e + 6), size = vram(e + 7);
            const unsigned depth = (flags >> 2) & 3;
            if (!depth)
                continue;
            const unsigned w = 8u << ((size >> 4) & 3), h = 8u << (size >> 6);
            const unsigned sx = (vram(e + 2) | (vram(e + 3) & 3u) << 8);
            const unsigned sy = (vram(e + 4) | (vram(e + 5) & 3u) << 8);
            const unsigned row = (ly - sy) & 0x3FF;
            if (row >= h)
                continue;
            ++drawn;
            const unsigned image = vram(e) | (vram(e + 1) & 0x0Fu) << 8;
            const bool bpp8 = vram(e + 1) & 0x80;
            const unsigned ty = flags & 0x02 ? h - 1 - row : row;
            const uint32_t at = uint32_t{image} * 64 + ty * (bpp8 ? w : w / 2);
            const uint8_t group = flags >> 4, palofs = size & 0x0F;
            for (unsigned i = 0; i < w; ++i)
            {
                const unsigned x = (sx + i) & 0x3FF;
                if (x >= SPRITE_WIDTH)
                    continue;
                const unsigned tx = flags & 0x01 ? w - 1 - i : i;
                unsigned v;
                if (bpp8)
                    v = vram(at + tx);
                else
                {
                    const uint8_t b = vram(at + tx / 2);
                    v = tx & 1 ? b & 0x0F : b >> 4;
                }
                if (!v)
                    continue; // transparent
                if (masks[x] & group)
                    frame_coll_ |= static_cast<uint8_t>(masks[x] & group);
                masks[x] |= group;
                if (line[x].depth)
                    continue; // a lower-numbered sprite is already here, in front
                line[x].colour = static_cast<uint8_t>(bpp8 ? v : (unsigned{palofs} << 4) | v);
                line[x].depth = static_cast<uint8_t>(depth);
            }
        }
    }

    void VideoCard::draw_layer(const Layer &l, unsigned y, int16_t *line) const
    {
        switch (l.config & 0x07)
        {
        case TEXT80:
            draw_text(l, y, line, false);
            break;
        case TEXT40:
            draw_text(l, y, line, true);
            break;
        case MONO:
            draw_mono(l, y, line);
            break;
        case BITMAP256:
            draw_bitmap(l, y, line, true);
            break;
        case BITMAP512:
            draw_bitmap(l, y, line, false);
            break;
        case TILE:
            draw_tiles(l, y, line);
            break;
        default:
            break; // types 6 and 7: nothing
        }
    }

    namespace
    {
        /// One layer pixel: a nonzero value (before PALOFS) covers what is below.
        void put(int16_t &px, unsigned value, uint8_t palofs)
        {
            if (value)
                px = static_cast<int16_t>(static_cast<uint8_t>((unsigned{palofs} << 4) + value));
            else if (px == UNCOVERED)
                px = 0; // the layer covers it, transparently: palette entry 0 shows
        }
    } // namespace

    void VideoCard::draw_text(const Layer &l, unsigned y, int16_t *line, bool wide) const
    {
        const unsigned cols = wide ? 40 : 80, width = cols * 8;
        const unsigned sy = (y + l.live_vscroll) % HEIGHT;
        const uint32_t map = uint32_t{l.live_mapbase} * 1024 + (sy / 16) * cols * 2;
        const uint32_t font = uint32_t{l.live_tilebase} * 1024 + sy % 16;
        const bool blink_mode = l.config & BLINK;
        const bool blink_off = (frames_ / BLINK_FRAMES) & 1;
        for (unsigned x = 0; x < WIDTH; ++x)
        {
            const unsigned sx = ((wide ? x / 2 : x) + l.live_hscroll) % width;
            const uint32_t cell = map + (sx / 8) * 2;
            const uint8_t ch = vram(cell), attr = vram(cell + 1);
            unsigned fg = attr & 0x0F, bg = attr >> 4;
            bool on = (vram(font + uint32_t{ch} * 16) >> (7 - sx % 8)) & 1;
            if (blink_mode)
            {
                if ((bg & 0x08) && blink_off)
                    on = false;
                bg &= 0x07;
            }
            put(line[x], on ? fg : bg, l.palofs);
        }
    }

    void VideoCard::draw_mono(const Layer &l, unsigned y, int16_t *line) const
    {
        const unsigned sy = (y + l.live_vscroll) % HEIGHT;
        const uint32_t row = uint32_t{l.live_mapbase} * 1024 + sy * (WIDTH / 8);
        for (unsigned x = 0; x < WIDTH; ++x)
        {
            const unsigned sx = (x + l.live_hscroll) % WIDTH;
            put(line[x], (vram(row + sx / 8) >> (7 - sx % 8)) & 1, l.palofs);
        }
    }

    void VideoCard::draw_bitmap(const Layer &l, unsigned y, int16_t *line, bool bpp8) const
    {
        // Both are 256 bytes a row, centred: 256x240 doubled, or 512x480 at 4 bits, left pixel high.
        constexpr unsigned LEFT = (WIDTH - 512) / 2;
        const unsigned h = bpp8 ? 240 : HEIGHT, w = bpp8 ? 256 : 512;
        const unsigned sy = ((bpp8 ? y / 2 : y) + l.live_vscroll) % h;
        const uint32_t row = uint32_t{l.live_mapbase} * 1024 + sy * 256;
        for (unsigned x = LEFT; x < LEFT + 512; ++x)
        {
            const unsigned sx = ((bpp8 ? (x - LEFT) / 2 : x - LEFT) + l.live_hscroll) % w;
            if (bpp8)
                put(line[x], vram(row + sx), 0); // PALOFS is for 4-bit pixels
            else
            {
                const uint8_t b = vram(row + sx / 2);
                put(line[x], sx & 1 ? b & 0x0F : b >> 4, l.palofs);
            }
        }
    }

    void VideoCard::draw_tiles(const Layer &l, unsigned y, int16_t *line) const
    {
        // 320x240, doubled. The map is 32 or 64 entries each way of 2 bytes (VERA's): tile number
        // bits 0-7, then bits 0-1 tile bits 8-9, bit 2 H flip, bit 3 V flip, bits 4-7 palette offset.
        const unsigned size = l.config & TILE16 ? 16 : 8;
        const bool bpp8 = l.config & TILE8BPP;
        const unsigned map_w = l.config & MAP_W64 ? 64 : 32, map_h = l.config & MAP_H64 ? 64 : 32;
        const unsigned tile_bytes = size * size / (bpp8 ? 1 : 2);
        const unsigned ly = (y / 2 + l.live_vscroll) % (map_h * size);
        const uint32_t map_row = uint32_t{l.live_mapbase} * 1024 + (ly / size) * map_w * 2;
        const uint32_t tiles = uint32_t{l.live_tilebase} * 1024;
        for (unsigned x = 0; x < WIDTH; ++x)
        {
            const unsigned lx = (x / 2 + l.live_hscroll) % (map_w * size);
            const uint32_t entry = map_row + (lx / size) * 2;
            const uint8_t lo = vram(entry), hi = vram(entry + 1);
            const unsigned tile = lo | (hi & 0x03u) << 8;
            const unsigned tx = hi & 0x04 ? size - 1 - lx % size : lx % size;
            const unsigned ty = hi & 0x08 ? size - 1 - ly % size : ly % size;
            const uint32_t at = tiles + tile * tile_bytes;
            if (bpp8)
                put(line[x], vram(at + ty * size + tx), 0); // 8 bits: no offsets
            else
            {
                const uint8_t b = vram(at + (ty * size + tx) / 2);
                put(line[x], tx & 1 ? b & 0x0F : b >> 4, static_cast<uint8_t>(((hi >> 4) + l.palofs) & 0x0F));
            }
        }
    }

    bool write_ppm(const VideoCard &card, const std::string &path)
    {
        std::FILE *f = std::fopen(path.c_str(), "wb");
        if (!f)
            return false;
        std::fprintf(f, "P6\n%u %u\n255\n", VideoCard::WIDTH, VideoCard::HEIGHT);
        std::vector<uint8_t> rgb;
        rgb.reserve(size_t{VideoCard::WIDTH} * VideoCard::HEIGHT * 3);
        for (size_t i = 0; i < size_t{VideoCard::WIDTH} * VideoCard::HEIGHT; ++i)
        {
            const uint32_t px = card.frame()[i];
            rgb.push_back(static_cast<uint8_t>(px));
            rgb.push_back(static_cast<uint8_t>(px >> 8));
            rgb.push_back(static_cast<uint8_t>(px >> 16));
        }
        const bool ok = std::fwrite(rgb.data(), 1, rgb.size(), f) == rgb.size();
        return std::fclose(f) == 0 && ok;
    }
} // namespace ooe::emu
