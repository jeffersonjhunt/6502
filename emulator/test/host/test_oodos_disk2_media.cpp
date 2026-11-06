/**
 * @file test_oodos_disk2_media.cpp
 * @brief O: and P: changed under OODOS and not, phase 6c's review (hw/oodos.md, "The plan, phase 6").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * disk2_rig.h's rig, the owner's sequencer PROM from the fixtures (77 without it). The directory
 * checksums kept by the BDOS's own writes (F1), the card's one motor (F2), write-backs that wait for a
 * close, 48 or the warm boot, each checked first (F3, and the close's), and a change seen at the first
 * directory write even within a drive's second (F6), through MEMTEST's cases K, V and C. The second
 * review's: the guard's drop not losing another drive's data (R2F1), a disk taken out (R2F2), a clock that
 * doesn't tick or goes back (R2F3), a close within the second (R2F4), 10 from 19 and 22 (R2F5), and a
 * read through the BIOS after the call that checked the disk (R2F6), through cases U, R, E, X and Q.
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
    unsigned bcd(unsigned v) { return (v >> 4) * 10 + (v & 15); }
    /// The RTC set back to drive's last access (D2STAMP: second, minute, hour, date, month), so the
    /// next access is within its second whatever the time the test took.
    void at_stamp(Dr &r, unsigned drive, unsigned months = 0)
    {
        const uint32_t a = SYS_BLOCK_PHYS + sym("D2STAMP") + drive * 8;
        r.set_time(2026, bcd(r.phys(a + 4) & 0x1F) + months, bcd(r.phys(a + 3)), bcd(r.phys(a + 2) & 0x3F),
                   bcd(r.phys(a + 1)), bcd(r.phys(a)));
    }
    size_t count(const std::string &s, const std::string &what)
    {
        size_t n = 0;
        for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1))
            ++n;
        return n;
    }
    const std::vector<uint8_t> C_REC(128, 0xC3); ///< MEMTEST U's and R's record on C:
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

TEST(a_swap_at_the_warm_boot_doesnt_lose_another_drive_s_data)
{
    // R2F1: O:'s and C:'s records both waiting (MEMTEST's U); O:'s disk changed. The warm boot's 48 finds
    // O:'s another disk and drops its buffers, and carries on with C:'s: C:'s record is on the card, the
    // new disk unchanged. (The guard's drop once took FLUSHD's drive with it, and C:'s was skipped.)
    Dr r;
    REQUIRE(r.ok);
    const std::string o = memcase(r, 'U', "SWAP?", [&] {
        r.wait(ms(1500));
        r.insert(0, r.blank);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "CWR 00 00");
    CHECK(on_card(r, C_REC) > 0);
    r.wait(ms(500));
    CHECK(r.drive_image(0) == r.blank);
}

TEST(a_swap_before_13_doesnt_lose_another_drive_s_data)
{
    // R2F1 through 13 (MEMTEST's R): the same, the reset's write-back before the warm boot's.
    Dr r;
    REQUIRE(r.ok);
    const std::string o = memcase(r, 'R', "SWAP?", [&] {
        r.wait(ms(1500));
        r.insert(0, r.blank);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "RST 00 00");
    CHECK(on_card(r, C_REC) > 0);
    r.wait(ms(500));
    CHECK(r.drive_image(0) == r.blank);
}

TEST(a_disk_taken_out_keeps_its_data_and_the_others_are_written)
{
    // R2F2: O:'s disk taken out with O:'s and C:'s records waiting. The warm boot's 48 can't read O:'s
    // directory: O:'s record is kept as a write that failed is (48 says Disk I/O once), C:'s is written,
    // and the prompt is back; the warm boot's 98 then drops O:'s, a file never closed. ^C's 13 has nothing
    // left to say. The disk put back, O: is as it was: nothing written on it while it was out.
    Dr r;
    REQUIRE(r.ok);
    const std::string o = memcase(r, 'U', "SWAP?", [&] {
        r.wait(ms(1500));
        r.card().insert(0, nullptr);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "MEMTEST DONE");
    CHECK(count(o, "Disk I/O") <= 1u);
    CHECK(on_card(r, C_REC) > 0);
    for (int i = 0; i < 2; ++i)
    {
        const size_t from = r.out.size();
        r.type("\x03");
        REQUIRE(r.until("\r\nA>", ms(30000), from));
        const std::string c = r.out.substr(from);
        std::printf("  ^C %d: %s\n", i, vis(c).c_str());
        hasnt(c, "Disk I/O");
    }
    has(r.c("dir c:"), "CDATA");
    r.insert(0, r.cpm);
    r.wait(ms(1500));
    CHECK(r.drive_image(0) == r.cpm);
}

TEST(another_drive_s_writes_go_on_with_p_s_disk_out)
{
    // R2F2's eviction (MEMTEST's E): P:'s record waiting, P:'s disk taken out, then 160 records written to
    // O:. P:'s buffer is a victim that can't be written: it's kept, marked, and another taken, so none of
    // O:'s writes fails, and O:'s file is whole.
    Dr r;
    REQUIRE(r.ok);
    const std::string o = memcase(r, 'E', "SWAP?", [&] {
        r.wait(ms(1500));
        r.card().insert(1, nullptr);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "OFAIL 00");
    has(o, "OCL 00 00");
    CHECK(o_file(r, "BIG     DAT").size() == 160u * 128u);
}

TEST(an_eviction_drops_p_s_record_for_another_disk)
{
    // E with P:'s disk changed: the eviction's check finds another disk, drops P:'s record, and the close
    // of PEND.DAT is 10. The new disk unchanged.
    Dr r;
    REQUIRE(r.ok);
    const std::string o = memcase(r, 'E', "SWAP?", [&] {
        r.wait(ms(1500));
        r.insert(1, r.cpm);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "OFAIL 00");
    has(o, "PCL 0A 00");
    r.wait(ms(500));
    CHECK(r.drive_image(1) == r.cpm);
}

TEST(a_clock_that_doesnt_tick_trusts_no_drive)
{
    // R2F3: the RTC held at one second from before the first access. No access is within the second of
    // another while the clock hasn't been seen to tick: P:'s disk changed after its motor stopped is seen.
    Dr r;
    REQUIRE(r.ok);
    r.set_time(2026, 10, 27, 12, 0, 0);
    has(r.c("dir p:"), "No File");
    r.wait(ms(1500));
    r.insert(1, r.cpm);
    r.set_time(2026, 10, 27, 12, 0, 0);
    has(r.c("dir p:"), "README");
}

TEST(a_clock_set_back_forgets_each_drive_s_last_access)
{
    // R2F3: P:'s last access at T; the clock set back a second and read (an access to O:), then put at T
    // again: the drive's T is forgotten with the step back, so P:'s changed disk is seen.
    Dr r;
    REQUIRE(r.ok);
    r.wait(ms(1200));
    has(r.c("dir p:"), "No File");
    r.wait(ms(1500));
    r.insert(1, r.cpm);
    const uint32_t a = SYS_BLOCK_PHYS + sym("D2STAMP") + 8;
    const unsigned s = bcd(r.phys(a));
    r.set_time(2026, bcd(r.phys(a + 4) & 0x1F), bcd(r.phys(a + 3)), bcd(r.phys(a + 2) & 0x3F), bcd(r.phys(a + 1)),
               s ? s - 1 : s);
    has(r.c("dir o:"), "README");
    at_stamp(r, 1);
    has(r.c("dir p:"), "README");
}

TEST(a_month_later_is_not_within_the_second)
{
    // R2F3: the clock a month on at the very second, date and time of P:'s last access: not recent.
    Dr r;
    REQUIRE(r.ok);
    r.wait(ms(1200));
    has(r.c("dir p:"), "No File");
    r.wait(ms(1500));
    r.insert(1, r.cpm);
    at_stamp(r, 1, 1);
    has(r.c("dir p:"), "README");
}

TEST(a_close_within_the_second_writes_nothing_on_another_disk)
{
    // R2F4: MEMTEST's K, the disk changed at its pause and the clock put back to O:'s last access: the
    // second record goes to the buffers (the select takes the disk as the same), and the close's
    // write-back reads the directory first, finds another disk, drops the data and returns 10. Not a
    // sector of the new disk is written.
    Dr r;
    REQUIRE(r.ok);
    r.wait(ms(1200));
    const std::string o = memcase(r, 'K', "PAUSE?", [&] {
        r.insert(0, r.blank);
        at_stamp(r, 0);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "CL 0A 00");
    r.wait(ms(500));
    CHECK(r.drive_image(0) == r.blank);
}

TEST(delete_and_make_return_10_for_a_change_within_the_second)
{
    // R2F5: MEMTEST's X. Within O:'s second its disk is changed (a blank, then CPM.DSK again): 19's and
    // 22's first directory write finds another disk and returns 10, deleting and making nothing; the
    // last disk unchanged.
    Dr r;
    REQUIRE(r.ok);
    r.wait(ms(1200));
    const size_t from = start(r, "memtest.com", " X");
    REQUIRE(r.until("SWAP?", ms(30000), from));
    r.insert(0, r.blank);
    at_stamp(r, 0);
    r.type(" ");
    REQUIRE(r.until("SWAP2?", ms(30000), from));
    r.insert(0, r.cpm);
    at_stamp(r, 0);
    r.type(" ");
    r.until("MEMTEST DONE", ms(60000), from);
    r.until("\r\nA>", ms(60000), from);
    const std::string o = r.out.substr(from);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "XOP 0");
    has(o, "XDEL 0A 00");
    has(o, "XOP2 FF 00");
    has(o, "XMK 0A 00");
    r.wait(ms(500));
    CHECK(r.drive_image(0) == r.cpm);
}

TEST(a_bios_read_after_the_checking_call_sees_the_new_disk)
{
    // R2F6: dir o: has the BDOS check O: (VOUCH: the BIOS keeps its buffers in that call). The disk changed
    // after the motor stopped, a later call reads O:'s first directory sector through the BIOS with no
    // select: VOUCH was the earlier call's only, so the BIOS reads the new disk, not its buffers.
    Dr r;
    REQUIRE(r.ok);
    has(r.c("dir o:"), "README");
    r.wait(ms(1500));
    r.insert(0, r.blank);
    const size_t from = start(r, "memtest.com", " Q");
    r.until("MEMTEST DONE", ms(60000), from);
    r.until("\r\nA>", ms(60000), from);
    const std::string o = r.out.substr(from);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "QRD 00");
    has(o, "QB E5");
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
