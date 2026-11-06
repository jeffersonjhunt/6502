/**
 * @file test_oodos_disk2_noprom.cpp
 * @brief O: and P: with no sequencer PROM, and with no Disk II card (phase 6c): no fixtures needed.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The WebUI ships no PROM: its card answers its ID and works its switches, but its data register never
 * sets bit 7. Every wait of the driver's is bounded, so O: is a clean Disk I/O, soon, and the machine
 * goes on. A backplane with no Disk II card has no O: or P:.
 */

#include "harness.h"
#include "oodos_rig.h"

#include "disk2_card.h"
#include "disk_image.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    void has(const std::string &got, const std::string &want)
    {
        const bool ok = got.find(want) != std::string::npos;
        CHECK(ok);
        if (!ok)
            std::printf("  want '%s' in '%s'\n", vis(want).c_str(), vis(got).c_str());
    }
} // namespace

TEST(no_prom_o_is_a_clean_disk_io_error_not_a_hang)
{
    CcpRig r;
    REQUIRE(r.ok);
    REQUIRE(r.machine->disk2_card() != nullptr);
    CHECK(!r.machine->disk2_card()->has_sequencer());
    auto d = std::shared_ptr<DiskImage>(
        DiskImage::from_sectors(std::vector<uint8_t>(DiskImage::SECTOR_IMAGE, 0xE5), DiskImage::Format::Dos));
    r.machine->disk2_card()->insert(0, d);
    for (const char *line : {"dir o:", "dir p:"})
    {
        const uint64_t t0 = r.cpu->cycles();
        const std::string o = r.to(line, "\r\nA>", ms(10000));
        const double s = double(r.cpu->cycles() - t0) / HZ;
        std::printf("  %s: %.2f s\n", line, s);
        has(o, "Disk I/O");
        CHECK(s < 3.0);
    }
    has(r.cmd("dir m:"), "No File"); // and OODOS goes on
}

TEST(no_card_no_o_or_p)
{
    std::string text = read_text(EMU_MACHINE_DIR "/backplane.machine");
    const size_t at = text.find("\ndisk2");
    REQUIRE(at != std::string::npos);
    text.erase(at + 1, text.find('\n', at + 1) - at);
    CcpRig r({}, text);
    REQUIRE(r.ok);
    REQUIRE(r.machine->disk2_card() == nullptr);
    has(r.to("dir o:", "A>"), "Invalid Drive");
    has(r.to("dir p:", "A>"), "Invalid Drive");
}

int main() { return run_all(); }
