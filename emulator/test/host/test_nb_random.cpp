/**
 * @file test_nb_random.cpp
 * @brief North bridge revision 3's random source: the ATECC608B model, its CRC, and the bridge's engine.
 * @copyright Copyright (c) 2026 Jefferson J. Hunt (MIT)
 *
 * hw/future/random-source.md is the spec (its tests C1-C9; C5 is test_rtc's).
 * The chip's CRC is checked against tools/atecc_crc.py, an oracle written
 * apart from the C++ routine.
 */

#include "harness.h"

#include "atecc608.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;

namespace
{
    constexpr uint32_t HZ = Atecc608::NATIVE_HZ;
    uint64_t us(uint64_t n) { return n * HZ / 1'000'000; }

    /// What tools/atecc_crc.py says the CRC of `bytes` is, as the two bytes sent ("" if it couldn't run).
    std::string oracle(const std::vector<uint8_t> &bytes)
    {
        std::string cmd = "python3 -I " ATECC_CRC_PY;
        char b[8];
        for (uint8_t v : bytes)
        {
            std::snprintf(b, sizeof b, " %02X", v);
            cmd += b;
        }
        std::string out;
        if (FILE *p = popen(cmd.c_str(), "r"))
        {
            char line[64];
            while (std::fgets(line, sizeof line, p))
                out += line;
            if (pclose(p) != 0)
                out.clear();
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
            out.pop_back();
        return out;
    }
    std::string sent(uint16_t crc)
    {
        char b[8];
        std::snprintf(b, sizeof b, "%02X %02X", crc & 0xFF, crc >> 8);
        return b;
    }
} // namespace

TEST(the_crc_matches_the_oracle)
{
    // C3's second half. The C++ routine, which the chip model and the bridge's checker share, against
    // tools/atecc_crc.py on the spec's four check values: the wake answer, the Random command, the lock Read, and
    // "123456789". Must fail with the bit order reversed.
    const std::vector<std::pair<std::vector<uint8_t>, std::string>> checks = {
        {{0x04, 0x11}, "33 43"},
        {{0x07, 0x1B, 0x00, 0x00, 0x00}, "24 CD"},
        {{0x07, 0x02, 0x00, 0x15, 0x00}, "17 5D"},
        {{'1', '2', '3', '4', '5', '6', '7', '8', '9'}, "DD BC"},
    };
    for (const auto &[bytes, want] : checks)
    {
        const std::string got = sent(Atecc608::crc(bytes.data(), bytes.size()));
        const std::string py = oracle(bytes);
        CHECK_EQ(py, want);
        CHECK_EQ(got, py);
    }
}

TEST(the_model_wakes_naks_and_sleeps)
{
    // Asleep, the chip NAKs. SDA low for 60 µs wakes it (59 doesn't); it NAKs through the 1.5 ms wake delay, then
    // answers $04 $11 $33 $43. A Random makes it NAK while it works (23 ms), then it answers 35 bytes with a good
    // CRC. 1.3 s after the wake the watchdog puts it to sleep: it NAKs again. Must fail with the watchdog never
    // expiring.
    Atecc608 chip;
    CHECK(!chip.address(true));
    chip.wake(us(59));
    CHECK(!chip.awake());
    chip.wake(us(80));
    CHECK(chip.awake());
    CHECK(!chip.address(true)); // the wake delay
    chip.advance(us(1500));
    REQUIRE(chip.address(true));
    std::vector<uint8_t> w;
    for (int i = 0; i < 4; ++i)
        w.push_back(chip.read());
    CHECK(w == std::vector<uint8_t>({0x04, 0x11, 0x33, 0x43}));
    REQUIRE(chip.address(false));
    chip.write(Atecc608::WA_COMMAND, {0x07, 0x1B, 0x00, 0x00, 0x00, 0x24, 0xCD});
    CHECK(!chip.address(true)); // executing
    chip.advance(us(22'000));
    CHECK(!chip.address(true));
    chip.advance(us(23'000) - us(22'000)); // to the 23 ms, to the cycle
    REQUIRE(chip.address(true));
    std::vector<uint8_t> r;
    for (int i = 0; i < 35; ++i)
        r.push_back(chip.read());
    CHECK_EQ(r[0], 35);
    CHECK_EQ(Atecc608::crc(r.data(), 33), static_cast<uint16_t>(r[33] | r[34] << 8));
    CHECK_EQ(chip.randoms(), 1u);
    chip.advance(us(1'300'000) - us(1500) - us(23'000) - us(10)); // (from the wake)
    CHECK(chip.address(true)); // still awake, just before the watchdog
    chip.advance(us(20));
    CHECK(!chip.awake());
    CHECK(!chip.address(true));
}

int main()
{
    return run_all();
}
