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
 */

#include "harness.h"
#include "test_support.h"

#include "loader.h"

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

int main()
{
    return run_all();
}
