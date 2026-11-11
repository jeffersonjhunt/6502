/**
 * @file video_card.cpp
 * @brief The backplane's video card.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "video_card.h"

#include "backplane_bridge.h"
#include "splash_rom.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

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

        // The splash ROM's layout, version 3 (splashgen.py's docstring).
        namespace splash
        {
            const uint8_t *const ROM = splash_rom::DATA;
            unsigned u16(unsigned at) { return static_cast<unsigned>(ROM[at] | ROM[at + 1] << 8); }
            unsigned parts() { return ROM[5]; }
            unsigned curves() { return ROM[6]; }
            unsigned band() { return ROM[7]; }
            unsigned frames() { return u16(8); }
            unsigned glint_start() { return u16(10); }
            unsigned glint_speed() { return ROM[12]; }
            unsigned fade() { return ROM[13]; }
            unsigned shake_start() { return u16(14); }
            unsigned shake_curve() { return ROM[16]; }
            constexpr unsigned PALETTE = 0x20, PARTS = 0x80, CURVES = 0xE0, PART_SIZE = 12;
            constexpr uint8_t MIRROR = 0x01, ZOOM = 0x02, INK = 0x04;
            /// Curve c's raw entry for frame t of it; 0 past its end (home, sharp, whole).
            unsigned raw(unsigned c, int t)
            {
                if (c >= curves() || t < 0 || t >= ROM[CURVES + 4 * c + 2])
                    return 0;
                return u16(u16(CURVES + 4 * c) + 2 * static_cast<unsigned>(t));
            }

            struct Step
            {
                bool visible{true};
                int offset{0};
                unsigned blur{0}, flash{0};
            };
            /// A moving part's (or the shake's) entry: offset, blur, flash.
            Step step(unsigned c, int t)
            {
                Step s;
                const unsigned e = raw(c, t);
                if (!e)
                    return s;
                s.visible = e & 0x8000;
                s.offset = static_cast<int>(e & 0x7FF);
                if (s.offset & 0x400)
                    s.offset -= 0x800;
                s.blur = (e >> 11) & 7;
                s.flash = (e >> 14) & 1;
                return s;
            }
        } // namespace splash
    } // namespace

    VideoCard::VideoCard() : front_(WIDTH * HEIGHT, BLACK), back_(WIDTH * HEIGHT, BLACK) { set_cpu_hz(NATIVE_HZ); }

    uint32_t VideoCard::rgba(uint16_t rgb12)
    {
        const uint32_t r = (rgb12 >> 8) & 0xF, g = (rgb12 >> 4) & 0xF, b = rgb12 & 0xF;
        return 0xFF000000 | (b * 17) << 16 | (g * 17) << 8 | (r * 17);
    }

    void VideoCard::reset()
    {
        bus_reset();
        x_ = y_ = 0;
        fraction_ = frames_ = 0;
        front_.assign(front_.size(), BLACK);
        back_.assign(back_.size(), BLACK);
    }

    void VideoCard::bus_reset()
    {
        ctrl_ = border_ = 0;
        palbase_ = live_palbase_ = sprbase_ = live_sprbase_ = 0;
        ien_ = isr_ = 0;
        coll_ = frame_coll_ = 0;
        match_line_ = 0;
        layers_ = {};
        for (unsigned n = 0; n < slot_.size(); ++n)
            slot_[n] = static_cast<uint16_t>(n); // the aperture shows card memory $000000-$00FFFF
        paddr_ = 0;
        pstep_ = 1;
        pctl_ = 0;
        splash_ = 0;
        splash_live_ = false;
        splash_frame_ = 0;
    }

    void VideoCard::set_cpu_hz(uint32_t hz)
    {
        step_ = (uint64_t{PIXEL_HZ} << 32) / (hz ? hz : NATIVE_HZ);
        fraction_ = 0;
    }

    bool VideoCard::set_revision(unsigned rev)
    {
        if (revision_locked_)
            return false; // the bus is wired for the revision it has
        rev_ = rev == 2 ? 2 : 1;
        if (rev_ == 2 && memory_.empty())
            memory_.assign(MEMORY_SIZE, 0);
        bus_reset();
        return true;
    }

    bool VideoCard::apple_mode() const { return bridge_ && bridge_->apple(); }

    uint8_t VideoCard::aperture_peek(uint32_t offset) const
    {
        if (rev_ != 2 || apple_mode())
            return 0xFF; // the card ignores the cycle: a floating bus
        offset &= APERTURE_SIZE - 1;
        return card_peek(uint32_t{slot_[offset / SLOT_SIZE]} * SLOT_SIZE + offset % SLOT_SIZE);
    }

    void VideoCard::aperture_write(uint32_t offset, uint8_t data)
    {
        if (rev_ != 2 || apple_mode())
            return;
        offset &= APERTURE_SIZE - 1;
        card_poke(uint32_t{slot_[offset / SLOT_SIZE]} * SLOT_SIZE + offset % SLOT_SIZE, data);
    }

    namespace
    {
        /// Hex, `$` optional, at most `digits` of them.
        bool parse_hex(std::string s, unsigned digits, uint32_t &value)
        {
            if (!s.empty() && s[0] == '$')
                s.erase(0, 1);
            if (s.empty() || s.size() > digits)
                return false;
            for (const char c : s)
                if (!std::isxdigit(static_cast<unsigned char>(c)))
                    return false;
            value = static_cast<uint32_t>(std::strtoul(s.c_str(), nullptr, 16));
            return true;
        }
    } // namespace

    std::string VideoCard::command(const std::vector<std::string> &args)
    {
        const char *usage = "vram ADDR [N] | vram ADDR = BYTE...: card memory, ADDR 24-bit hex, N decimal (1-4096)\n";
        if (rev_ != 2)
            return "no card memory: a revision 1 card draws from banks 8-B (f and st reach them)\n";
        uint32_t addr = 0;
        if (args.empty() || !parse_hex(args[0], 6, addr))
            return usage;
        unsigned count = 16;
        if (args.size() >= 2 && args[1] == "=")
        {
            if (args.size() == 2)
                return usage;
            std::vector<uint8_t> bytes;
            for (size_t i = 2; i < args.size(); ++i)
            {
                uint32_t v = 0;
                if (!parse_hex(args[i], 2, v))
                    return "bad byte '" + args[i] + "'\n";
                bytes.push_back(static_cast<uint8_t>(v));
            }
            for (size_t i = 0; i < bytes.size(); ++i)
                card_poke((addr + static_cast<uint32_t>(i)) & 0xFFFFFF, bytes[i]);
            count = static_cast<unsigned>(bytes.size());
        }
        else if (args.size() == 2)
        {
            char *end = nullptr;
            const unsigned long n = std::strtoul(args[1].c_str(), &end, 10);
            if (args[1].empty() || *end != '\0' || n == 0 || n > 4096)
                return usage;
            count = static_cast<unsigned>(n);
        }
        else if (args.size() > 2)
            return usage;
        std::string out;
        for (unsigned row = 0; row < count; row += 16)
        {
            const uint32_t at = (addr + row) & 0xFFFFFF;
            std::string hex, text;
            for (unsigned i = 0; i < 16 && row + i < count; ++i)
            {
                const uint8_t v = card_peek((at + i) & 0xFFFFFF);
                char b[4];
                std::snprintf(b, sizeof b, "%02X ", v);
                hex += b;
                text += std::isprint(v) ? static_cast<char>(v) : '.';
            }
            char line[96];
            std::snprintf(line, sizeof line, "%06X  %-48s %s\n", static_cast<unsigned>(at), hex.c_str(), text.c_str());
            out += line;
        }
        return out;
    }

    void VideoCard::port_step()
    {
        const uint32_t size = static_cast<uint32_t>(memory_.size());
        if (paddr_ < size) // wrap at the fitted memory's end
        {
            const uint32_t step = pstep_ % size;
            paddr_ = pctl_ & Rev2::PCTL_DOWN ? (paddr_ + size - step) % size : (paddr_ + step) % size;
        }
        else // past it (software wrote it there): the 24-bit counter
            paddr_ = (pctl_ & Rev2::PCTL_DOWN ? paddr_ - pstep_ : paddr_ + pstep_) & 0xFFFFFF;
    }

    uint8_t VideoCard::port_read()
    {
        const uint8_t v = card_peek(paddr_);
        port_step();
        return v;
    }

    void VideoCard::port_write(uint8_t data)
    {
        card_poke(paddr_, data);
        port_step();
    }

    uint8_t VideoCard::dack_read(bool)
    {
        if (rev_ != 2 || !(pctl_ & Rev2::PCTL_FROM_CARD))
            return 0xFF; // the port is taking bytes, not giving them: nothing drives the bus
        return port_read();
    }

    void VideoCard::dack_write(uint8_t data, bool)
    {
        if (rev_ == 2 && !(pctl_ & Rev2::PCTL_FROM_CARD))
            port_write(data);
    }

    uint8_t VideoCard::read(uint32_t offset)
    {
        if (rev_ == 2 && offset == Rev2::PDATA)
            return port_read();
        return peek(offset);
    }

    uint8_t VideoCard::peek_rev2(uint32_t offset) const
    {
        if (offset >= LAYER0 && offset < LAYER1 + 0x10)
        {
            const Layer &l = layers_[(offset - LAYER0) >> 4];
            switch (offset & 0xF)
            {
            case CONFIG: return l.config;
            case Rev2::MAPBASE: return lo(l.mapbase);
            case Rev2::MAPBASE + 1: return hi(l.mapbase);
            case Rev2::TILEBASE: return lo(l.tilebase);
            case Rev2::TILEBASE + 1: return hi(l.tilebase);
            case Rev2::HSCROLL: return lo(l.hscroll);
            case Rev2::HSCROLL + 1: return hi(l.hscroll);
            case Rev2::VSCROLL: return lo(l.vscroll);
            case Rev2::VSCROLL + 1: return hi(l.vscroll);
            case Rev2::PALOFS: return l.palofs;
            default: return 0; // reserved
            }
        }
        if (offset >= Rev2::SLOT0 && offset < Rev2::SLOT0 + 8)
        {
            const uint16_t v = slot_[(offset - Rev2::SLOT0) / 2];
            return offset & 1 ? hi(v) : lo(v);
        }
        switch (offset)
        {
        case CTRL: return ctrl_;
        case BORDER: return border_;
        case IEN: return ien_;
        case ISR: return isr_;
        case LINE: return lo(static_cast<uint16_t>(beam_line()));
        case LINE + 1: return hi(static_cast<uint16_t>(beam_line()));
        case STATUS: return static_cast<uint8_t>((beam_line() >= HEIGHT ? IN_VBLANK : 0) | coll_);
        case SPLASH: return splash_;
        case Rev2::PALBASE: return lo(palbase_);
        case Rev2::PALBASE + 1: return hi(palbase_);
        case Rev2::SPRBASE: return lo(sprbase_);
        case Rev2::SPRBASE + 1: return hi(sprbase_);
        case Rev2::PADDR: return static_cast<uint8_t>(paddr_);
        case Rev2::PADDR + 1: return static_cast<uint8_t>(paddr_ >> 8);
        case Rev2::PADDR + 2: return static_cast<uint8_t>(paddr_ >> 16);
        case Rev2::PDATA: return card_peek(paddr_); // peek: the address stays
        case Rev2::PSTEP: return lo(pstep_);
        case Rev2::PSTEP + 1: return hi(pstep_);
        case Rev2::PCTL: return pctl_;
        case Rev2::MEMSIZE: return static_cast<uint8_t>(memory_.size() >> 16);
        case ID_REG: return ID_V2;
        default: return 0; // reserved
        }
    }

    void VideoCard::write_rev2(uint32_t offset, uint8_t data)
    {
        if (offset >= LAYER0 && offset < LAYER1 + 0x10)
        {
            Layer &l = layers_[(offset - LAYER0) >> 4];
            switch (offset & 0xF)
            {
            case CONFIG: l.config = data; break;
            case Rev2::MAPBASE: set_lo(l.mapbase, data); break;
            case Rev2::MAPBASE + 1: set_hi(l.mapbase, data, 0xFFFF); break;
            case Rev2::TILEBASE: set_lo(l.tilebase, data); break;
            case Rev2::TILEBASE + 1: set_hi(l.tilebase, data, 0xFFFF); break;
            case Rev2::HSCROLL: set_lo(l.hscroll, data); break;
            case Rev2::HSCROLL + 1: set_hi(l.hscroll, data, SCROLL_MASK); break;
            case Rev2::VSCROLL: set_lo(l.vscroll, data); break;
            case Rev2::VSCROLL + 1: set_hi(l.vscroll, data, SCROLL_MASK); break;
            case Rev2::PALOFS: l.palofs = data & 0x0F; break;
            default: break;
            }
            return;
        }
        if (offset >= Rev2::SLOT0 && offset < Rev2::SLOT0 + 8)
        {
            uint16_t &v = slot_[(offset - Rev2::SLOT0) / 2];
            if (offset & 1)
                set_hi(v, data, 0xFFFF);
            else
                set_lo(v, data);
            return;
        }
        switch (offset)
        {
        case CTRL: ctrl_ = data & 0x0F; break;
        case BORDER: border_ = data; break;
        case IEN: ien_ = data & 0x07; break;
        case ISR: isr_ = static_cast<uint8_t>(isr_ & ~data); break;
        case LINE: set_lo(match_line_, data); break;
        case LINE + 1: set_hi(match_line_, data, SCROLL_MASK); break;
        case SPLASH: splash_ = data & SPLASH_ON; break;
        case Rev2::PALBASE: set_lo(palbase_, data); break;
        case Rev2::PALBASE + 1: set_hi(palbase_, data, 0xFFFF); break;
        case Rev2::SPRBASE: set_lo(sprbase_, data); break;
        case Rev2::SPRBASE + 1: set_hi(sprbase_, data, 0xFFFF); break;
        case Rev2::PADDR: paddr_ = (paddr_ & 0xFFFF00) | data; break;
        case Rev2::PADDR + 1: paddr_ = (paddr_ & 0xFF00FF) | uint32_t{data} << 8; break;
        case Rev2::PADDR + 2: paddr_ = (paddr_ & 0x00FFFF) | uint32_t{data} << 16; break;
        case Rev2::PDATA: port_write(data); break;
        case Rev2::PSTEP: set_lo(pstep_, data); break;
        case Rev2::PSTEP + 1: set_hi(pstep_, data, 0xFFFF); break;
        case Rev2::PCTL: pctl_ = data & 0x07; break;
        default: break; // MEMSIZE and ID are read-only; the rest reserved
        }
    }

    uint8_t VideoCard::peek(uint32_t offset) const
    {
        if (rev_ == 2)
            return peek_rev2(offset);
        if (offset >= LAYER0 && offset < LAYER1 + 0x10)
        {
            const Layer &l = layers_[(offset - LAYER0) >> 4];
            switch (offset & 0xF)
            {
            case CONFIG:
                return l.config;
            case MAPBASE:
                return lo(l.mapbase);
            case TILEBASE:
                return lo(l.tilebase);
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
            return lo(palbase_);
        case SPRBASE:
            return lo(sprbase_);
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
        case SPLASH:
            return splash_;
        case ID_REG:
            return ID;
        default:
            return 0xFF;
        }
    }

    void VideoCard::write(uint32_t offset, uint8_t data)
    {
        if (rev_ == 2)
        {
            write_rev2(offset, data);
            return;
        }
        if (offset >= LAYER0 && offset < LAYER1 + 0x10)
        {
            Layer &l = layers_[(offset - LAYER0) >> 4];
            switch (offset & 0xF)
            {
            case CONFIG:
                l.config = data;
                break;
            case MAPBASE:
                l.mapbase = data; // one byte on revision 1
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
        case SPLASH:
            splash_ = data & SPLASH_ON;
            break;
        default:
            break;
        }
    }

    void VideoCard::tick(uint64_t cycles)
    {
        const uint64_t chunk = (UINT64_MAX >> 1) / step_; // x step_, plus a fraction, stays inside 64 bits
        for (; cycles > chunk; cycles -= chunk)
            tick(chunk);
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
        splash_vblank();
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
        if (apple_mode())
        {
            draw_apple(y, out); // the IIe's display; the native registers are out of reach
            return;
        }
        if (splash_live_)
        {
            draw_splash(y, out); // the whole screen, whatever CTRL says
            return;
        }
        if (!(ctrl_ & DISPLAY))
        {
            for (unsigned x = 0; x < WIDTH; ++x)
                out[x] = BLACK;
            return;
        }
        // The palette as it is now: a fade by DMA shows from the next line.
        std::array<uint32_t, 256> pal;
        const uint32_t pal_at = base(live_palbase_);
        for (unsigned i = 0; i < 256; ++i)
            pal[i] = rgba(static_cast<uint16_t>((vram(pal_at + 2 * i + 1) & 0x0F) << 8 | vram(pal_at + 2 * i)));

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
        const uint32_t table = base(live_sprbase_);
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
            // The image: revision 1, 12 bits of 64-byte units; revision 2, 15 bits of 256-byte units.
            const unsigned image = vram(e) | (vram(e + 1) & (rev_ == 2 ? 0x7Fu : 0x0Fu)) << 8;
            const bool bpp8 = vram(e + 1) & 0x80;
            const unsigned ty = flags & 0x02 ? h - 1 - row : row;
            const uint32_t at = uint32_t{image} * (rev_ == 2 ? 256 : 64) + ty * (bpp8 ? w : w / 2);
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
        const uint32_t map = base(l.live_mapbase) + (sy / 16) * cols * 2;
        const uint32_t font = base(l.live_tilebase) + sy % 16;
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
        const uint32_t row = base(l.live_mapbase) + sy * (WIDTH / 8);
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
        const uint32_t row = base(l.live_mapbase) + sy * 256;
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
        const uint32_t map_row = base(l.live_mapbase) + (ly / size) * map_w * 2;
        const uint32_t tiles = base(l.live_tilebase);
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

    namespace
    {
        /// The IIe's 16 lo-res colours (the hi-res ones are among them), in 12 bits.
        constexpr std::array<uint16_t, 16> LORES = {0x000, 0xE26, 0x65B, 0xF4F, 0x0A6, 0x999, 0x1CF, 0xDCF,
                                                    0x670, 0xF64, 0x999, 0xFAD, 0x1F4, 0xDD9, 0x7FD, 0xFFF};
        constexpr unsigned VIOLET = 3, GREEN = 12, BLUE = 6, ORANGE = 9, WHITE = 15;
        constexpr unsigned APPLE_LEFT = (VideoCard::WIDTH - 560) / 2, APPLE_TOP = (VideoCard::HEIGHT - 384) / 2;

        /// Text and lo-res row r's offset in a page; hi-res line a's.
        uint16_t text_row(unsigned r) { return static_cast<uint16_t>((r & 7) * 0x80 + (r >> 3) * 0x28); }
        uint16_t hires_line(unsigned a)
        {
            return static_cast<uint16_t>((a & 7) * 0x400 + ((a >> 3) & 7) * 0x80 + (a >> 6) * 0x28);
        }
    } // namespace

    void VideoCard::draw_apple(unsigned y, uint32_t *out) const
    {
        const uint32_t black = rgba(0x000);
        for (unsigned x = 0; x < WIDTH; ++x)
            out[x] = black;
        if (y < APPLE_TOP || y >= APPLE_TOP + 384)
            return;
        const unsigned a = (y - APPLE_TOP) / 2; // the Apple's line, 0-191
        const AppleSwitches &s = bridge_->apple_switches();
        const bool page2 = s.page2 && !s.store80;
        uint32_t *px = out + APPLE_LEFT;

        if (s.text || (s.mixed && a >= 160))
        {
            const uint16_t row = static_cast<uint16_t>((page2 ? 0x0800 : 0x0400) + text_row(a / 8));
            const bool flash_on = (frames_ / BLINK_FRAMES) & 1;
            const uint32_t on = rgba(LORES[WHITE]);
            auto dots = [&](uint8_t code) {
                unsigned idx = code;
                if (!s.altchar && code >= 0x40 && code < 0x80) // flashing: inverse, then normal
                    idx = flash_on ? (code & 0x3Fu) | 0x80 : code & 0x3Fu;
                const uint32_t chars = rev_ == 2 ? APPLE_CHARS_V2 : APPLE_CHARS; // card memory on revision 2
                return static_cast<uint8_t>(~vram(chars + idx * 8 + a % 8)); // lit where 0
            };
            if (s.col80)
            {
                for (unsigned col = 0; col < 80; ++col)
                {
                    const uint8_t d = dots(apple(col & 1 ? 0 : 1, static_cast<uint16_t>(row + col / 2)));
                    for (unsigned b = 0; b < 7; ++b)
                        px[col * 7 + b] = (d >> b) & 1 ? on : black;
                }
            }
            else
            {
                for (unsigned col = 0; col < 40; ++col)
                {
                    const uint8_t d = dots(apple(0, static_cast<uint16_t>(row + col)));
                    for (unsigned b = 0; b < 7; ++b)
                        px[col * 14 + b * 2] = px[col * 14 + b * 2 + 1] = (d >> b) & 1 ? on : black;
                }
            }
            return;
        }
        const bool twice = s.col80 && s.dhires; // the double modes: 80COL with AN3 off
        if (!s.hires && twice) // double lo-res: 80 blocks of 7 dots, aux then main
        {
            const uint16_t row = static_cast<uint16_t>((page2 ? 0x0800 : 0x0400) + text_row(a / 8));
            for (unsigned col = 0; col < 80; ++col)
            {
                const uint8_t b = apple(col & 1 ? 0 : 1, static_cast<uint16_t>(row + col / 2));
                unsigned n = (a & 4) ? b >> 4 : b & 0x0F;
                if (!(col & 1))
                    n = (n >> 1 | n << 3) & 0x0F; // aux blocks fall a dot out of phase: shown rotated
                const uint32_t c = rgba(LORES[n]);
                for (unsigned i = 0; i < 7; ++i)
                    px[col * 7 + i] = c;
            }
            return;
        }
        if (s.hires && twice) // double hi-res: 560 dots, aux and main bytes in turn
        {
            const uint16_t line = static_cast<uint16_t>((page2 ? 0x4000 : 0x2000) + hires_line(a));
            std::array<uint8_t, 560> dots{};
            for (unsigned col = 0; col < 40; ++col)
                for (unsigned half = 0; half < 2; ++half)
                {
                    const uint8_t b = apple(half ? 0 : 1, static_cast<uint16_t>(line + col));
                    for (unsigned i = 0; i < 7; ++i)
                        dots[col * 14 + half * 7 + i] = (b >> i) & 1;
                }
            for (unsigned g = 0; g < 560; g += 4) // each 4 dots, by where they fall: a colour
            {
                const unsigned v = unsigned{dots[g]} | unsigned{dots[g + 1]} << 1 | unsigned{dots[g + 2]} << 2 | unsigned{dots[g + 3]} << 3;
                const uint32_t c = rgba(LORES[v]);
                for (unsigned i = 0; i < 4; ++i)
                    px[g + i] = c;
            }
            return;
        }
        if (!s.hires) // lo-res: two blocks a byte, the low nibble on top
        {
            const uint16_t row = static_cast<uint16_t>((page2 ? 0x0800 : 0x0400) + text_row(a / 8));
            for (unsigned col = 0; col < 40; ++col)
            {
                const uint8_t b = apple(0, static_cast<uint16_t>(row + col));
                const uint32_t c = rgba(LORES[(a & 4) ? b >> 4 : b & 0x0F]);
                for (unsigned i = 0; i < 14; ++i)
                    px[col * 14 + i] = c;
            }
            return;
        }
        // hi-res: 7 dots a byte, bit 0 first; bit 7 delays them half a dot
        const uint16_t line = static_cast<uint16_t>((page2 ? 0x4000 : 0x2000) + hires_line(a));
        std::array<bool, 282> dot{}; // one each side, off
        std::array<bool, 40> shift{};
        for (unsigned col = 0; col < 40; ++col)
        {
            const uint8_t b = apple(0, static_cast<uint16_t>(line + col));
            shift[col] = b & 0x80;
            for (unsigned i = 0; i < 7; ++i)
                dot[1 + col * 7 + i] = (b >> i) & 1;
        }
        auto colour = [&](unsigned d) { // a lone dot's colour, from where it falls
            const bool hi = shift[d / 7];
            return d & 1 ? (hi ? ORANGE : GREEN) : (hi ? BLUE : VIOLET);
        };
        for (unsigned d = 0; d < 280; ++d)
        {
            const bool hi = shift[d / 7];
            unsigned c = WHITE;
            if (!dot[1 + d])
            {
                // NTSC smears colour across a gap: off between two dots of one colour, that colour
                if (!(d > 0 && d < 279 && dot[d] && dot[2 + d]) || colour(d - 1) != colour(d + 1) ||
                    (d > 1 && dot[d - 1]) || (d < 278 && dot[3 + d]))
                    continue;
                c = colour(d - 1);
            }
            else if (!dot[d] && !dot[2 + d])
                c = colour(d);
            const uint32_t rgb = rgba(LORES[c]);
            const unsigned x = d * 2 + (hi ? 1 : 0);
            px[x] = rgb;
            if (x + 1 < 560)
                px[x + 1] = rgb;
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
    // --- the boot splash ----------------------------------------------------------

    void VideoCard::splash_vblank()
    {
        if (!(splash_ & SPLASH_ON))
        {
            splash_live_ = false; // stopped: the layers from this frame
            return;
        }
        if (!splash_live_)
        {
            splash_live_ = true; // started: frame 0 is the frame about to be drawn
            splash_frame_ = 0;
        }
        else if (++splash_frame_ >= splash::frames())
        {
            splash_live_ = false; // over: the layers again, and SPLASH reads 0
            splash_ = 0;
            splash_frame_ = 0;
        }
    }

    void VideoCard::draw_splash(unsigned y, uint32_t *out) const
    {
        const int f = static_cast<int>(splash_frame_);
        const int shake = splash::shake_curve() < splash::curves()
                              ? splash::step(splash::shake_curve(), f - static_cast<int>(splash::shake_start())).offset
                              : 0;
        std::array<uint8_t, WIDTH> v{};    // palette index 0-47 at each x
        std::array<uint8_t, WIDTH> best{}; // its intensity: the brightest part wins, a later one on a tie
        std::array<int, 1024> sums{};      // a row's coverage (0-3) as prefix sums
        auto put = [&](int x, int i, unsigned high) {
            const auto at = static_cast<size_t>(x);
            if (i > 0 && i >= best[at])
            {
                best[at] = static_cast<uint8_t>(i);
                v[at] = static_cast<uint8_t>(static_cast<unsigned>(i) | high);
            }
        };
        for (unsigned p = 0; p < splash::parts(); ++p)
        {
            const unsigned e = splash::PARTS + p * splash::PART_SIZE;
            const unsigned image = splash::u16(e), w = splash::u16(e + 2), h = splash::u16(e + 4);
            const int home_x = static_cast<int>(splash::u16(e + 6)), home_y = static_cast<int>(splash::u16(e + 8));
            const int t = f - splash::ROM[e + 10];
            const uint8_t flags = splash::ROM[e + 11];
            const unsigned curve = (flags >> 4) & 3, ink = flags & splash::INK ? 32 : 0;
            if (t < 0 || w + 1 > sums.size())
                continue;
            const unsigned stride = (w + 3) / 4;
            const int iw = static_cast<int>(w), ih = static_cast<int>(h);
            auto cov = [&](unsigned c, unsigned sy) { return (splash::ROM[image + sy * stride + (c >> 2)] >> (6 - 2 * (c & 3))) & 3; };
            auto floor_div256 = [](int n) { return n >= 0 ? n / 256 : -((-n + 255) / 256); };
            if (flags & splash::ZOOM)
            {
                // Scaled about its centre: the source steps inv/256 a pixel. (On the card: an
                // accumulator stepped by inv each pixel, another each line; one read a pixel.)
                const unsigned c = splash::raw(curve, t);
                if (c && !(c & 0x8000))
                    continue;
                const int inv = c ? static_cast<int>(c & 0x7FF) : 256, level = c ? static_cast<int>((c >> 11) & 15) : 15;
                const int cx = home_x + iw / 2, cy = home_y + ih / 2 + shake;
                const int sy = ih / 2 + floor_div256((static_cast<int>(y) - cy) * inv);
                if (sy < 0 || sy >= ih || inv <= 0)
                    continue;
                for (int x = 0; x < static_cast<int>(WIDTH); ++x)
                {
                    const int sx = iw / 2 + floor_div256((x - cx) * inv);
                    if (sx >= 0 && sx < iw)
                        put(x, (cov(static_cast<unsigned>(sx), static_cast<unsigned>(sy)) * 5 * (level + 1)) >> 4, ink);
                }
                continue;
            }
            splash::Step s = splash::step(curve, t);
            if (!s.visible)
                continue;
            if (flags & splash::MIRROR)
                s.offset = -s.offset;
            const int sy = static_cast<int>(y) - (home_y + shake);
            if (sy < 0 || sy >= ih)
                continue;
            // The row's coverage as prefix sums: a box of any width is one subtraction. (The card
            // keeps a running sum over a delay line instead, as the pixels stream past.)
            for (unsigned c = 0; c < w; ++c)
                sums[c + 1] = sums[c] + cov(c, static_cast<unsigned>(sy));
            const int bw = 1 << s.blur, lo = bw / 2, hi = bw - lo;
            const int left = home_x + s.offset;
            for (int x = std::max(0, left - hi + 1); x < std::min<int>(WIDTH, left + iw + lo); ++x)
            {
                const int sx = x - left; // the box is sx - lo .. sx + hi - 1, clipped to the image
                const int a = std::clamp(sx - lo, 0, iw), b = std::clamp(sx + hi, 0, iw);
                put(x, ((sums[static_cast<size_t>(b)] - sums[static_cast<size_t>(a)]) * 5) >> s.blur, s.flash << 4 | ink);
            }
        }
        if (f >= static_cast<int>(splash::glint_start()))
        {
            const int pos = (f - static_cast<int>(splash::glint_start())) * static_cast<int>(splash::glint_speed()) -
                            static_cast<int>(splash::band()) - 240;
            if (pos < static_cast<int>(WIDTH))
                for (unsigned x = 0; x < WIDTH; ++x)
                {
                    const int d = static_cast<int>(x + y / 2) - pos;
                    if (v[x] && v[x] < 32 && d >= 0 && d < static_cast<int>(splash::band())) // ink doesn't shine
                        v[x] |= 16;
                }
        }
        const int k = static_cast<int>(splash::frames()) - f;
        if (k <= 1 << splash::fade())
            for (uint8_t &c : v)
            {
                const int i = ((c & 15) * k) >> splash::fade();
                c = i ? static_cast<uint8_t>((c & 0x30) | i) : 0;
            }
        for (unsigned x = 0; x < WIDTH; ++x)
        {
            const unsigned at = splash::PALETTE + 2u * v[x];
            out[x] = rgba(static_cast<uint16_t>((splash::ROM[at + 1] & 0x0F) << 8 | splash::ROM[at]));
        }
    }
} // namespace ooe::emu
