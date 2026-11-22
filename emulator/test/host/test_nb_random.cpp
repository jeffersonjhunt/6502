/**
 * @file test_nb_random.cpp
 * @brief North bridge revision 3's random source: the ATECC608B model, its CRC, and the bridge's engine.
 * @copyright Copyright (c) 2026 Jefferson J. Hunt (MIT)
 *
 * hw/future/random-source.md is the spec (its tests C1-C9; C5 is test_rtc's).
 * The chip's CRC is checked against tools/atecc_crc.py, an oracle written
 * apart from the C++ routine. Then the bridge's engine on revision 3, with
 * the fixed stream (Atecc608::splitmix(1)): every byte a test sees is known.
 */

#include "harness.h"

#include "atecc608.h"
#include "backplane_bridge.h"
#include "ds3231.h"
#include "machine.h"

#include <cstdio>
#include <memory>
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

// --- The bridge's engine (N4: C1-C4, C6-C9; C5 is test_rtc's) -------------------------------------------

namespace
{
    constexpr uint16_t NB = 0xC680;
    constexpr uint16_t RNGST = NB + BackplaneBridge::RNGST;
    constexpr uint16_t RNGCNT = NB + BackplaneBridge::RNGCNT;
    constexpr uint16_t RNGDATA = NB + BackplaneBridge::RNGDATA;
    constexpr uint16_t SEC = NB + BackplaneBridge::SEC;
    constexpr uint8_t READY = BackplaneBridge::RNG_READY, CHIP = BackplaneBridge::RNG_CHIP,
                      LOCKED = BackplaneBridge::RNG_LOCKED, FAULT = BackplaneBridge::RNG_FAULT,
                      BUSY = BackplaneBridge::RNG_BUSY;

    /// The fixed stream: splitmix(1)'s first `n` bytes after `skip`.
    std::vector<uint8_t> stream(size_t skip, size_t n)
    {
        Atecc608::Source s = Atecc608::splitmix(1);
        for (size_t i = 0; i < skip; ++i)
            s();
        std::vector<uint8_t> v(n);
        for (uint8_t &b : v)
            b = s();
        return v;
    }

    /// A revision 3 bridge on 1 MB of RAM, `chip` its rng_chip= ("" for the default, locked), powered on.
    struct Bridge
    {
        /// `fitted`: a chip of the test's own in place of the option's, before the power-on reset.
        explicit Bridge(const std::string &chip = "", std::unique_ptr<Atecc608> fitted = nullptr)
        {
            MachineOptions opt;
            opt.rtc = std::make_shared<Ds3231>(Ds3231::Time{2026, 11, 22, 9, 30, 0, 1});
            std::string error;
            m = Machine::build("cpu w65c02s\nnorthbridge backplane bootsel revision=3" +
                                   (chip.empty() ? "" : " rng_chip=" + chip) + "\nram 00000-FFFFF name=ram\n",
                               "rng.machine", opt, error);
            if (!m)
                std::printf("  %s\n", error.c_str());
            bridge = m->backplane_bridge();
            if (fitted)
                bridge->set_rng_chip(std::move(fitted));
            m->bus().reset();
        }
        Decoder &bus() { return m->bus(); }
        void idle(uint64_t n)
        {
            for (uint64_t i = 0; i < n; ++i)
                bus().idle();
            bus().sync();
        }
        /// Idle until RNGST's BUSY clears, at most `limit_us`. @return the µs it took, or limit_us + 1
        uint64_t settle(uint64_t limit_us = 1'000'000)
        {
            for (uint64_t t = 0; t <= limit_us; t += 100)
            {
                bus().sync();
                if (!(bridge->rngst() & BUSY))
                    return t;
                idle(us(100));
            }
            return limit_us + 1;
        }
        std::vector<uint8_t> take(size_t n)
        {
            std::vector<uint8_t> v;
            for (size_t i = 0; i < n; ++i)
                v.push_back(bus().read(RNGDATA));
            return v;
        }

        std::unique_ptr<Machine> m;
        BackplaneBridge *bridge{nullptr};
    };
} // namespace

TEST(pool_full_at_power_on)
{
    // C1. At the first instruction: RNGCNT 32, READY, CHIP and LOCKED, BUSY and FAULT clear, and the bytes are the
    // fixed stream's first 32, in order. Must fail with the power-on draw skipped, or the pool read from the wrong
    // end.
    Bridge b;
    REQUIRE(b.bridge != nullptr);
    CHECK_EQ(b.bus().read(RNGCNT), 32);
    CHECK_EQ(b.bus().read(RNGST), READY | CHIP | LOCKED);
    CHECK(b.take(32) == stream(0, 32));
    CHECK_EQ(b.bus().read(RNGCNT), 0);
    CHECK_EQ(b.bus().read(RNGDATA), 0x00); // empty: $00, nothing taken
}

TEST(an_unlocked_chip_is_never_drawn)
{
    // C2. rng_chip=unlocked: CHIP set, LOCKED clear, the pool empty, and no Random ever sent, not at power-on nor
    // at DRAW. A chip whose configuration is locked but its data zone not (LockValue $55, LockConfig $00) is
    // LOCKED: only byte 87 counts. Must fail with the lock check removed, or byte 86 tested in its place.
    {
        Bridge b("unlocked");
        CHECK_EQ(b.bus().read(RNGST), CHIP);
        CHECK_EQ(b.bus().read(RNGCNT), 0);
        b.bus().write(RNGST, BackplaneBridge::RNG_DRAW);
        b.idle(us(50'000));
        CHECK_EQ(b.bus().read(RNGST), CHIP);
        REQUIRE(b.bridge->rng_chip() != nullptr);
        CHECK_EQ(b.bridge->rng_chip()->randoms(), 0u);
    }
    Bridge c("", std::make_unique<Atecc608>(true, false)); // LockConfig $00, LockValue $55
    CHECK_EQ(c.bus().read(RNGST), READY | CHIP | LOCKED);
    CHECK_EQ(c.bus().read(RNGCNT), 32);
}

TEST(crc_and_count_are_checked)
{
    // C3's first half. One corrupted response: the try fails, the next succeeds, and the pool holds that try's 32
    // bytes (the stream's 64-95: the corrupted try used 32-63). A health test error's status group (count 4) is
    // not taken as data: the next try's bytes are. Three corrupted responses: FAULT, and the pool empty. Must fail
    // with the CRC check removed, or the count check removed, or the CRC's bit order reversed.
    Bridge b;
    b.bridge->rng_chip()->corrupt_next(1);
    b.take(32); // the refill
    CHECK(b.settle() <= 400'000);
    CHECK_EQ(b.bus().read(RNGST), READY | CHIP | LOCKED);
    CHECK_EQ(b.bridge->rng_counts().failures, 1u);
    CHECK(b.take(32) == stream(64, 32));
    b.bridge->rng_chip()->health_next(1);
    CHECK(b.settle() <= 400'000);
    CHECK_EQ(b.bus().read(RNGCNT), 32);
    CHECK(b.take(32) == stream(96, 32)); // the health error used no bytes
    b.bridge->rng_chip()->corrupt_next(3);
    CHECK(b.settle() <= 1'000'000);
    CHECK_EQ(b.bus().read(RNGST), CHIP | LOCKED | FAULT);
    CHECK_EQ(b.bus().read(RNGCNT), 0);
}

TEST(no_chip_no_hang)
{
    // C4. rng_chip=none: CHIP clear, BUSY never set, DRAW does nothing, RNGDATA $00, and the clock's second goes
    // on. A chip that never finishes its command: the bridge's polls give up at 100 ms a try, so after three
    // tries FAULT and BUSY clear: the first try gives up after 100 ms of polls, and the chip, still at its command,
    // NAKs the next two tries' wake checks. Must fail with a poll that never gives up.
    {
        Bridge b("none");
        CHECK_EQ(b.bus().read(RNGST), 0x00);
        CHECK_EQ(b.bus().read(RNGCNT), 0);
        b.bus().write(RNGST, BackplaneBridge::RNG_DRAW);
        CHECK_EQ(b.bus().read(RNGST) & BUSY, 0);
        CHECK_EQ(b.bus().read(RNGDATA), 0x00);
        const uint8_t sec = b.bus().read(SEC);
        b.idle(HZ + HZ / 10);
        CHECK(b.bus().read(SEC) != sec);
    }
    Bridge b;
    b.bridge->rng_chip()->timing().random_us = 10'000'000;
    b.take(32);
    const uint64_t t = b.settle(1'000'000);
    std::printf("  a chip that never answers: FAULT after %llu us\n", static_cast<unsigned long long>(t));
    CHECK(t >= 100'000 && t <= 330'000);
    CHECK_EQ(b.bus().read(RNGST), CHIP | LOCKED | FAULT);
}

TEST(refill_after_the_last_byte)
{
    // C6. Taking the 32nd byte starts a draw (BUSY at once); 32 more come, the stream's next. After a FAULT nothing
    // is drawn until DRAW, which clears FAULT and fills the pool again. Must fail with the refill trigger removed,
    // or DRAW not clearing FAULT.
    Bridge b;
    CHECK(b.take(32) == stream(0, 32));
    CHECK_EQ(b.bus().read(RNGST) & (READY | BUSY), BUSY);
    CHECK(b.settle() <= 40'000);
    CHECK(b.take(32) == stream(32, 32));
    b.bridge->rng_chip()->corrupt_next(3);
    b.settle();
    REQUIRE((b.bus().read(RNGST) & FAULT) != 0);
    b.idle(us(100'000));
    CHECK_EQ(b.bus().read(RNGST) & (READY | BUSY), 0); // no draw of its own after a FAULT
    b.bus().write(RNGST, BackplaneBridge::RNG_DRAW);
    CHECK_EQ(b.bus().read(RNGST) & (FAULT | BUSY), BUSY);
    CHECK(b.settle() <= 40'000);
    CHECK_EQ(b.bus().read(RNGST), READY | CHIP | LOCKED);
    CHECK(b.take(32) == stream(160, 32)); // the three failed tries used 64-159
}

TEST(the_chip_is_woken_for_every_draw)
{
    // C7. A second draw 2 s after the first (the chip asleep since, past its watchdog too) succeeds. Must fail with
    // the wake skipped after the first draw.
    Bridge b;
    b.take(32);
    CHECK(b.settle() <= 40'000);
    CHECK(!b.bridge->rng_chip()->awake());
    b.idle(2 * HZ);
    b.take(32);
    CHECK(b.settle() <= 40'000);
    CHECK_EQ(b.bus().read(RNGST), READY | CHIP | LOCKED);
    CHECK(b.take(32) == stream(64, 32));
    CHECK_EQ(b.bridge->rng_counts().failures, 0u); // each draw's first try: none failed for a sleeping chip
}

TEST(a_peek_takes_nothing)
{
    // C8. The debugger reads RNGDATA ten times (peek, as the WebUI's memory view does): RNGCNT is still 32 and the
    // first byte the CPU takes is the stream's first. Must fail with peek() calling read().
    Bridge b;
    for (int i = 0; i < 10; ++i)
        CHECK_EQ(b.bus().peek(RNGDATA), stream(0, 1)[0]);
    CHECK_EQ(b.bus().read(RNGCNT), 32);
    CHECK_EQ(b.bus().read(RNGDATA), stream(0, 1)[0]);
}

TEST(the_chip_is_polled)
{
    // C9. The chip's Random takes 40 ms (a slower part than the planning figure): the draw still succeeds, and the
    // bridge's first poll comes 1 ms after the command. Must fail with a fixed 23 ms wait in place of the poll.
    Bridge b;
    b.bridge->rng_chip()->timing().random_us = 40'000;
    b.take(32);
    CHECK(b.settle() <= 60'000);
    CHECK_EQ(b.bus().read(RNGST), READY | CHIP | LOCKED);
    CHECK(b.take(32) == stream(32, 32));
    const uint64_t first = b.bridge->rng_counts().first_poll;
    std::printf("  the first poll %llu cycles after the command; %u polls in all\n",
                static_cast<unsigned long long>(first), b.bridge->rng_counts().polls);
    CHECK(first >= us(1'000) - 2 && first <= us(1'000) + 2);
}

int main()
{
    return run_all();
}
