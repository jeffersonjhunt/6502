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
 * The third's: a missing disk waited for once, not for each waiting buffer (R3F1, case F), a clock that
 * stalls (R3F2), and a write-protected disk found by an eviction (R3F5). The fourth's: a BIOS read with a
 * stalled clock (R4F1), and a write-protected disk's waiting buffers all dropped at once (R4F4). The
 * fifth's: a held Read Only meeting the call's own error, in each error mode (R5F1, case H).
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

namespace
{
    double secs(Dr &r, uint64_t from) { return double(r.cpu->cycles() - from) / 7159090.0; }
    /// MEMTEST's F (or G), `then` at its SWAP?: the seconds from SWAP? to MEMTEST DONE (the 160 records to O:),
    /// and from there to A> (the warm boot's 48 and 98).
    std::pair<double, double> case_f(Dr &r, const std::function<void()> &then, std::string &o, char c = 'F')
    {
        const size_t from = start(r, "memtest.com", std::string(" ") + c);
        if (!r.until("SWAP?", ms(60000), from))
            return {-1, -1};
        r.wait(ms(1500));
        then();
        const uint64_t t0 = r.cpu->cycles();
        r.type(" ");
        r.until("MEMTEST DONE", ms(600000), from);
        const double writes = secs(r, t0);
        const uint64_t t1 = r.cpu->cycles();
        r.until("\r\nA>", ms(600000), from);
        o = r.out.substr(from);
        return {writes, secs(r, t1)};
    }
}

TEST(a_missing_disk_is_waited_for_once_not_for_each_buffer)
{
    // R3F1: 64 records (16 buffers) wait for P:, P:'s disk is taken out, then 160 records go to O:. The
    // first eviction of P:'s finds no disk (a read that times out, about 1.8 s) and marks all 16, so the
    // later ones pass them over: the writes take at most a few seconds more than with P: there, and so
    // does the warm boot's 48 (one try a call), where each buffer once cost its own timeout.
    std::string base, out;
    std::pair<double, double> with, without;
    {
        Dr r;
        REQUIRE(r.ok);
        with = case_f(r, [] {}, base);
    }
    {
        Dr r;
        REQUIRE(r.ok);
        without = case_f(r, [&] { r.card().insert(1, nullptr); }, out);
    }
    std::printf("  P: there: writes %.1f s, end %.1f s; P: out: writes %.1f s, end %.1f s\n  %s\n", with.first,
                with.second, without.first, without.second, vis(out).c_str());
    has(base, "OFAIL 00");
    has(out, "OFAIL 00");
    CHECK(with.first > 0 && without.first > 0);
    CHECK(without.first < with.first + 3.0);
    CHECK(without.second < with.second + 3.0);
}

TEST(a_clock_that_stalls_after_a_tick_isnt_trusted)
{
    // R3F2, round 2's reproduction: the clock put forward (a tick), then held at one second. P:'s motor
    // has stopped though its last access is this second, so the clock has stalled: the change is seen.
    Dr r;
    REQUIRE(r.ok);
    r.set_time(2026, 11, 5, 12, 0, 0);
    has(r.c("dir p:"), "No File");
    r.wait(ms(1500));
    CHECK(!r.card().status().spinning);
    r.insert(1, r.cpm);
    r.set_time(2026, 11, 5, 12, 0, 0);
    has(r.c("dir p:"), "README");
}

TEST(a_missing_disk_s_buffers_among_others_are_waited_for_once)
{
    // R3F1, MEMTEST's G: P:'s 16 waiting buffers alternate in age with O:'s, so later writes to O: in
    // later calls would each want one of P:'s. The first finds no disk and marks them all, and none is
    // tried again: no slower than F's 160 records to O: with P: there.
    std::string base, out;
    std::pair<double, double> with, without;
    {
        Dr r;
        REQUIRE(r.ok);
        with = case_f(r, [] {}, base);
    }
    {
        Dr r;
        REQUIRE(r.ok);
        without = case_f(r, [&] { r.card().insert(1, nullptr); }, out, 'G');
    }
    std::printf("  P: there: writes %.1f s, end %.1f s; P: out: writes %.1f s, end %.1f s\n  %s\n", with.first,
                with.second, without.first, without.second, vis(out).c_str());
    has(base, "OFAIL 00");
    has(out, "OFAIL 00");
    CHECK(with.first > 0 && without.first > 0);
    CHECK(without.first < with.first + 3.0);
    CHECK(without.second < with.second + 3.0);
}

TEST(a_clock_held_at_the_last_access_isnt_trusted_within_a_program)
{
    // R3F2 inside a program: MEMTEST's O holds README.TXT open on O:; the motor stops, the disk is changed and
    // the clock put back to O:'s last access. O:'s motor has stopped within its stamp's second: the clock
    // has stalled, so the select reads the directory, sees the change, and the read is 10.
    Dr r;
    REQUIRE(r.ok);
    r.wait(ms(1200));
    const std::string o = memcase(r, 'O', "SWAP?", [&] {
        r.wait(ms(1500));
        r.insert(0, r.blank);
        at_stamp(r, 0);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "ORD2 0A 00");
}

TEST(a_write_protected_disk_found_by_an_eviction_is_said)
{
    // R3F5: P:'s record waiting (MEMTEST's E), P:'s disk then write-protected. The O: write whose buffer
    // evicts one of P:'s gets Read Only (P:'s), said at the end of the call, not dropped in silence; P:'s
    // other waiting buffer (the block's other sector, zeroed when the write took it) goes with it (R4F4),
    // so it's said once.
    Dr r;
    REQUIRE(r.ok);
    const std::string o = memcase(r, 'E', "SWAP?", [&] {
        r.wait(ms(1500));
        r.card().disk(1)->set_write_protected(true);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "OFAIL 01");
    has(o, "PCL FF 02");
}

namespace
{
    /// The RTC held at the last second NOW read (STAMPH): a stalled clock, never a step back.
    void hold(Dr &r)
    {
        const uint32_t a = SYS_BLOCK_PHYS + sym("STAMPH");
        r.set_time(2026, bcd(r.phys(a + 4) & 0x1F), bcd(r.phys(a + 3)), bcd(r.phys(a + 2) & 0x3F),
                   bcd(r.phys(a + 1)), bcd(r.phys(a)));
    }
    /// Run in slices of 100 ms with the clock held, until `text` after `from` or `slices` run out.
    bool held_until(Dr &r, const std::string &text, size_t from, unsigned slices)
    {
        for (unsigned i = 0; i < slices; ++i)
        {
            hold(r);
            if (r.out.find(text, from) != std::string::npos)
                return true;
            r.run(ms(100));
        }
        return r.out.find(text, from) != std::string::npos;
    }
}

TEST(a_bios_read_with_a_stalled_clock_sees_the_new_disk)
{
    // R4F1, the review's: the clock ticks, then dir o:; the clock is held at that second while O:'s motor
    // stops, its disk is changed to the blank, and MEMTEST's Q reads O:'s first directory sector through
    // the BIOS. The BIOS's own RECENT sees the stall (the card's drive stopped within its stamp's second),
    // so its buffers aren't kept: the new disk's E5, not the old one's README entry.
    Dr r;
    REQUIRE(r.ok);
    r.set_time(2026, 11, 5, 12, 0, 0);
    r.wait(ms(1300));
    has(r.c("dir o:"), "README");
    held_until(r, "never", r.out.size(), 15);
    CHECK(!r.card().status().spinning);
    r.insert(0, r.blank);
    hold(r);
    const size_t from = start(r, "memtest.com", " Q");
    held_until(r, "\r\nA>", from, 600);
    const std::string o = r.out.substr(from);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "QRD 00");
    has(o, "QB E5");
}

TEST(a_write_protected_disk_s_16_buffers_fail_one_call)
{
    // R4F4: 64 records (16 buffers) wait for P:, whose disk is then write-protected (MEMTEST's F). The
    // first eviction finds Read Only: P:'s waiting buffers all go, and the one O: write that found it gets
    // the error, said at its end, its record written. The other 159 writes are untouched.
    Dr r;
    REQUIRE(r.ok);
    const std::string o = memcase(r, 'F', "SWAP?", [&] {
        r.wait(ms(1500));
        r.card().disk(1)->set_write_protected(true);
    });
    std::printf("  %s\n", vis(o).c_str());
    has(o, "OFAIL 01");
    has(o, "OCL 00 00");
    CHECK(o_file(r, "BIG     DAT").size() == 160u * 128u);
}

namespace
{
    /// MEMTEST's H in error mode `mode` (R, D, X), P:'s disk write-protected at its WP?: its output.
    std::string case_h(char mode)
    {
        Dr r(true, true, {{0, "BIG     BIN", gen(32768, 9)}});
        if (!r.ok)
            return "NO RIG";
        const size_t from = start(r, "memtest.com", std::string(" H ") + mode);
        if (!r.until("WP?", ms(60000), from))
            return r.out.substr(from);
        r.wait(ms(1500));
        r.card().disk(1)->set_write_protected(true);
        r.type(" ");
        r.until("\r\nA>", ms(600000), from + 10);
        return r.out.substr(from);
    }
}

TEST(a_held_read_only_and_the_call_s_own_error)
{
    // R5F1, the review's heldx: P:'s record waiting, its disk write-protected; reads of M: fill the
    // buffers, then 22 of O:'s README.TXT, which exists. The make's login of O: evicts P:'s buffer (Read
    // Only held for the end), and the make fails itself: File Exists, on O:. The call's own error is the
    // one returned, with its own drive; in display and default mode P:'s Read Only is said first, and
    // the default mode's end comes only after both.
    const std::string ret = case_h('R'), say = case_h('D'), dflt = case_h('X');
    std::printf("  R: %s\n  D: %s\n  X: %s\n", vis(ret).c_str(), vis(say).c_str(), vis(dflt).c_str());
    has(ret, "HMK FF 08");
    has(ret, "ERRDRV 0E");
    hasnt(ret, "Error On");
    has(say, "FF 08\r\nERRDRV 0E");   // HMK's result, after the two messages
    for (const std::string *o : {&say, &dflt})
    {
        const size_t p = o->find("Error On P: Read Only"), e = o->find("Error On O: File Exists");
        CHECK(p != std::string::npos && e != std::string::npos && p < e);
    }
    hasnt(dflt, "FF 08");                // the program ended at the error
    hasnt(dflt, "MEMTEST DONE");
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
