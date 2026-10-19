/**
 * @file test_loader.cpp
 * @brief Loading a binary image onto the bus.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The old Emulator::LoadProgram copied a whole 128-byte buffer after every
 * fread, so the tail of an image whose size was not a multiple of 128 was
 * followed by stale bytes from the previous chunk. It also reported a missing
 * file only in the log and carried on, so the emulator ran 64 KB of zeros
 * (BRK forever) when started from the wrong directory. And an image too big
 * for the space above its load address wrapped round into zero page.
 *
 * Phase 5 adds Intel HEX and ld65 label files. The HEX records below are
 * the worked example from Wikipedia's "Intel HEX" article, so the checksums
 * come from outside this code.
 */

#include "harness.h"
#include "test_support.h"

#include "loader.h"
#include "symbols.h"

#include <cstdio>
#include <string>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    /// Write `n` bytes of 0x01, 0x02, ... to a scratch file and return its path.
    std::string scratch_image(const char *name, size_t n)
    {
        std::FILE *f = std::fopen(name, "wb");
        for (size_t i = 0; i < n; ++i)
            std::fputc(static_cast<int>((i % 255) + 1), f);
        std::fclose(f);
        return name;
    }

    std::string scratch_text(const char *name, const std::string &text)
    {
        std::FILE *f = std::fopen(name, "w");
        std::fputs(text.c_str(), f);
        std::fclose(f);
        return name;
    }

    const std::string WIKI_HEX = ":10010000214601360121470136007EFE09D2190140\n"
                                 ":100110002146017E17C20001FF5F16002148011928\n"
                                 ":10012000194E79234623965778239EDA3F01B2CAA7\n"
                                 ":100130003F0156702B5E712B722B732146013421C7\n"
                                 ":00000001FF\n";
} // namespace

TEST(loads_whole_image_at_address)
{
    FlatBus bus;
    const std::string path = scratch_image("img_256.bin", 256);
    CHECK_EQ(load_image(bus, path, 0x8000), 256L);
    CHECK_EQ(bus.at(0x8000), 0x01);
    CHECK_EQ(bus.at(0x80FF), 0x01); // byte 255 -> (255 % 255) + 1
    CHECK_EQ(bus.at(0x8100), 0x00);
}

TEST(short_final_chunk_writes_only_what_was_read)
{
    FlatBus bus;
    bus.at(0x8000 + 130) = 0xEE; // sentinel just past the image
    const std::string path = scratch_image("img_130.bin", 130);
    CHECK_EQ(load_image(bus, path, 0x8000), 130L);
    CHECK_EQ(bus.at(0x8000 + 129), 130);
    CHECK_EQ(bus.at(0x8000 + 130), 0xEE);
}

TEST(missing_file_is_an_error)
{
    FlatBus bus;
    CHECK_EQ(load_image(bus, "no_such_image.bin", 0x8000), -1L);
}

TEST(image_past_top_of_memory_is_refused)
{
    FlatBus bus;
    const std::string path = scratch_image("img_64.bin", 64);
    CHECK_EQ(load_image(bus, path, 0xFFF0), -1L);
    CHECK_EQ(bus.at(0x0000), 0x00); // nothing wrapped into zero page
    CHECK_EQ(bus.at(0xFFF0), 0x00); // and nothing partial was written
}

TEST(image_ending_exactly_at_top_fits)
{
    FlatBus bus;
    const std::string path = scratch_image("img_16.bin", 16);
    CHECK_EQ(load_image(bus, path, 0xFFF0), 16L);
    CHECK_EQ(bus.at(0xFFFF), 16);
}

TEST(hex_loads_every_record)
{
    FlatBus bus;
    std::string error;
    CHECK_EQ(load_hex(bus, scratch_text("wiki.hex", WIKI_HEX), error), 64L);
    CHECK_EQ(bus.at(0x0100), 0x21);
    CHECK_EQ(bus.at(0x010F), 0x01);
    CHECK_EQ(bus.at(0x0110), 0x21);
    CHECK_EQ(bus.at(0x013F), 0x21);
    CHECK_EQ(bus.at(0x0140), 0x00);
    // CRLF line ends, as Windows tools write them.
    FlatBus crlf;
    std::string dos = WIKI_HEX;
    for (size_t at = 0; (at = dos.find('\n', at)) != std::string::npos; at += 2)
        dos.insert(at, "\r");
    CHECK_EQ(load_hex(crlf, scratch_text("wiki_crlf.hex", dos), error), 64L);
}

TEST(hex_refuses_bad_files_without_writing)
{
    struct Case
    {
        const char *name;
        std::string text;
        const char *why;
    };
    std::string bad_sum = WIKI_HEX;
    bad_sum[42] = '1'; // line 1's checksum $40 -> $41
    const Case cases[] = {
        {"bad_sum.hex", bad_sum, "bad_sum.hex:1: bad checksum"},
        {"no_eof.hex", WIKI_HEX.substr(0, WIKI_HEX.size() - 12), "no_eof.hex: no end-of-file record"},
        {"past_top.hex", ":02FFFF00AABB9B\n:00000001FF\n", "past_top.hex:1: data past $FFFF"},
        {"high_linear.hex", ":020000040001F9\n:0100000042BD\n:00000001FF\n", "high_linear.hex:2: data past $FFFF"},
        {"garbage.hex", "hello\n", "garbage.hex:1: not an Intel HEX record"},
        {"after_eof.hex", WIKI_HEX + ":0100000042BD\n", "after_eof.hex:6: data after the end-of-file record"},
    };
    for (const Case &c : cases)
    {
        FlatBus bus;
        std::string error;
        CHECK_EQ(load_hex(bus, scratch_text(c.name, c.text), error), -1L);
        if (error.find(c.why) == std::string::npos)
            std::printf("  %s: got '%s'\n", c.name, error.c_str());
        CHECK(error.find(c.why) != std::string::npos);
        CHECK_EQ(bus.at(0x0100), 0x00); // the good records before the bad one were not written
    }
    std::string error;
    FlatBus bus;
    CHECK_EQ(load_hex(bus, "no_such.hex", error), -1L);
}

TEST(hex_segment_address_below_64k_is_fine)
{
    FlatBus bus;
    std::string error;
    // 02 record: segment $0100 -> base $1000; one byte at offset 5.
    CHECK_EQ(load_hex(bus, scratch_text("segment.hex", ":020000020100FB\n:0100050042B8\n:00000001FF\n"), error), 1L);
    CHECK_EQ(bus.at(0x1005), 0x42);
}

TEST(ld65_labels)
{
    Symbols sym;
    std::string error;
    CHECK(sym.load(scratch_text("ok.lbl", "al 00FFEF .ECHO\nal 00E000 .START\n\nal 00FFEF .ECHO\nal 00FFEF .OTHER\n"),
                   error));
    CHECK_EQ(sym.size(), 3u);
    uint16_t a = 0;
    CHECK(sym.find("ECHO", a));
    CHECK_EQ(a, 0xFFEF);
    CHECK(!sym.find("echo", a)); // case sensitive, as in ca65
    CHECK(sym.name_at(0xFFEF) && *sym.name_at(0xFFEF) == "ECHO"); // first name wins
    CHECK(sym.name_at(0x1234) == nullptr);

    Symbols bad;
    CHECK(!bad.load(scratch_text("bad.lbl", "al 00E000 .START\nal zz .X\n"), error));
    CHECK_EQ(error, std::string("bad.lbl:2: expected 'al <hex address> .<name>'"));
    CHECK(!bad.load("no_such.lbl", error));
}

int main()
{
    return run_all();
}
