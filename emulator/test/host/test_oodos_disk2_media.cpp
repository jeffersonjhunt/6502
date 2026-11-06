/**
 * @file test_oodos_disk2_media.cpp
 * @brief O: and P: changed under OODOS and not, phase 6c's review (hw/oodos.md, "The plan, phase 6").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * disk2_rig.h's rig, the owner's sequencer PROM from the fixtures (77 without it). The directory
 * checksums kept by the BDOS's own writes (F1), the card's one motor (F2), write-backs that wait for a
 * close, 48 or the warm boot, each checked first (F3, and the close's), and a change seen at the first
 * directory write even within a drive's second (F6), through MEMTEST's cases K, V and C.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "disk2_rig.h"

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;
using namespace disk2_rig;

namespace
{
    /// MEMTEST's case c to its end, `then` run at its prompt (after the motor's second, if `wait`).
    std::string memcase(Dr &r, char c, const std::string &prompt, const std::function<void()> &then)
    {
        const size_t from = start(r, "memtest.com", std::string(" ") + c);
        if (!r.until(prompt, ms(30000), from))
            return r.out.substr(from);
        then();
        r.type(" ");
        r.until("MEMTEST DONE", ms(60000), from);
        r.until("\r\nA>", ms(60000), from);
        return r.out.substr(from);
    }
    unsigned dgen(Dr &r, unsigned drive) { return r.phys(SYS_BLOCK_PHYS + sym("DGEN") + drive); }
    /// A file of O:'s image (as cpmtools takes it: the disk read back by DiskImage), or empty.
    std::vector<uint8_t> o_file(Dr &r, const std::string &name83)
    {
        const std::vector<uint8_t> img = r.drive_image(0);
        if (img.size() != IMAGE)
            return {};
        static const unsigned SKEW[16] = {0, 6, 12, 3, 9, 15, 14, 5, 11, 2, 8, 7, 13, 4, 10, 1};
        auto at = [&](unsigned l) { return (size_t{l / 16} * 16 + SKEW[l % 16]) * 256; };
        std::vector<uint8_t> out;
        for (unsigned i = 0; i < 64; ++i)
        {
            const size_t e = at(48 + i * 32 / 256) + i * 32 % 256;
            bool same = img[e] == 0;
            for (unsigned c = 0; c < 11 && same; ++c)
                same = (img[e + 1 + c] & 0x7F) == static_cast<uint8_t>(name83[c]);
            if (!same)
                continue;
            for (unsigned rec = 0; rec < img[e + 15]; ++rec)
            {
                const unsigned b = img[e + 16 + rec / 8], off = rec % 8 * 128;
                const size_t p = at(48 + b * 4 + off / 256) + off % 256;
                out.insert(out.end(), img.begin() + static_cast<long>(p), img.begin() + static_cast<long>(p + 128));
            }
        }
        return out;
    }
} // namespace

TEST(the_bdos_s_own_directory_writes_are_not_a_change)
{
    // F1: a make and a write, then the motor's second over (the disk checked again at the next select),
    // then another write and the close: all 00, the disk the same, nothing dropped (MEMTEST's K).
    Dr r;
    REQUIRE(r.ok);
    const unsigned g = dgen(r, 0);
    const std::string o = memcase(r, 'K', "PAUSE?", [&] { r.wait(ms(1500)); });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "MK 00 00");
    has(o, "WR 00 00");
    has(o, "WR2 00 00");
    has(o, "CL 00 00");
    CHECK_EQ(dgen(r, 0), g);
    std::vector<uint8_t> want(256, 0x5A);
    CHECK(o_file(r, "NEW     DAT") == want);
    // and a PIP onto O:, the second over, a DIR: no log-in again
    Dr p(true, true, {{0, "X       BIN", gen(1024, 11)}});
    REQUIRE(p.ok);
    p.c("pip o:=m:x.bin");
    p.wait(ms(1500));
    has(p.c("dir o:"), "X        BIN");
    CHECK_EQ(dgen(p, 0), g);
}

TEST(a_changed_p_is_seen_however_recently_o_ran)
{
    // F2, the review's case: P: idle over a second, its disk changed; then O: and P: touched within a
    // second. The card's one motor runs P: once it is selected, but P:'s own last access is old, and the
    // drive the card didn't have is never taken as turning: P: is checked, the new disk seen.
    Dr r;
    REQUIRE(r.ok);
    has(r.c("dir p:"), "No File");       // BLANK.DSK
    r.wait(ms(1500));
    r.insert(1, r.cpm);                  // P: changed while idle
    has(r.c("dir o:"), "README");        // O: just ran
    has(r.c("dir p:"), "README");        // and P: at once: the new disk
}

TEST(a_change_under_a_pending_write_drops_it)
{
    // F3, dirty.com's case (MEMTEST's V): a record written, not yet on the disk; the disk changed during
    // the key wait; the program's end writes back through the check, which finds another disk and drops
    // the record: the new disk unchanged.
    Dr r;
    REQUIRE(r.ok);
    const std::string o = memcase(r, 'V', "SWAP?", [&] {
        r.wait(ms(1500));
        r.insert(0, r.blank);
    });
    std::printf("  %s\n", vis(o).c_str());
    r.wait(ms(500));
    CHECK(r.drive_image(0) == r.blank);
}

TEST(a_closed_file_is_on_the_disk_with_no_warm_boot)
{
    // The close writes the file's buffers back (MEMTEST's C): with the program still waiting, before any
    // warm boot, the file is on the image, as it would be if the machine were switched off then.
    Dr r;
    REQUIRE(r.ok);
    std::vector<uint8_t> want(128, 0x5A);
    std::vector<uint8_t> got;
    const std::string o = memcase(r, 'C', "LOOK?", [&] { got = o_file(r, "NEW     DAT"); });
    has(o, "CC 00 00");
    CHECK(got == want);
}

TEST(a_swap_within_the_second_is_seen_at_the_first_directory_write)
{
    // F6, the window the owner's rule leaves: a disk changed within a second of its drive's last access is
    // taken as the same at the select, but the call's first directory write reads the directory past the
    // buffers first, finds another disk, and writes nothing: the new disk unchanged.
    Dr r(true, true, {{0, "X       BIN", gen(1024, 11)}});
    REQUIRE(r.ok);
    has(r.c("dir o:"), "README");
    r.insert(0, r.blank);                // at once, within the second
    const std::string o = r.c("pip o:=m:x.bin");
    std::printf("  %s\n", vis(o).c_str());
    CHECK(r.drive_image(0) == r.blank);
}

int main()
{
    if (!have_prom())
    {
        std::printf("  SKIP: no Disk II sequencer PROM among the fixtures\n");
        return 77;
    }
    return run_all();
}
