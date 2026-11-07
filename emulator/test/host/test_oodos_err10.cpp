/**
 * @file test_oodos_err10.cpp
 * @brief Error 10 (Media Change) said by the CCP's ERASE, RENAME and TYPE and by PIP (OODOS todo item 17).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * O:'s floppy changed in the middle of a command. ERASE, RENAME and a PIP copy change it within the
 * second of O:'s last access, so the select takes the disk as the same and the first directory write
 * finds another disk: the BDOS returns 10 and writes nothing. TYPE's floppy is changed while it waits at
 * a page's end: its next read is 10. Each must say so and fail, not take 10 for success (they took only
 * $FF as a failure). The review's: a source's floppy changed under a copy to P: (P:'s NAME.$$$ deleted,
 * as for any failure), a make's 10 before any write (PIP's S), PIP's cleanup after a close's 10 (the new
 * floppy's NAME.$$$ kept), and the CCP's open of a program with access stamps. disk2_rig.h's rig, the
 * owner's sequencer PROM from the fixtures (77 without it).
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
    /// `line` run with O:'s disk made `with` just before it, within the second of O:'s last access (the
    /// dir before), so the select trusts the old disk.
    std::string swapped_under(Dr &r, const std::string &line, const std::vector<uint8_t> &with)
    {
        has(r.c("dir o:"), "README");
        r.insert(0, with);
        return r.c(line);
    }
    std::string lines(unsigned n)
    {
        std::string t;
        for (unsigned i = 0; i < n; ++i)
            t += "LINE " + std::to_string(i) + "\r\n";
        return t;
    }
} // namespace

TEST(erase_says_media_change)
{
    Dr r;
    REQUIRE(r.ok);
    const std::string o = swapped_under(r, "era o:readme.txt", r.blank);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "Media Change");
    r.run(ms(1500));
    CHECK(r.drive_image(0) == r.blank);
}

TEST(rename_says_media_change)
{
    Dr r;
    REQUIRE(r.ok);
    const std::string o = swapped_under(r, "ren o:new.txt=o:readme.txt", r.blank);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "Media Change");
    r.run(ms(1500));
    CHECK(r.drive_image(0) == r.blank);
}

TEST(pip_says_media_change)
{
    Dr r(true, true, {{0, "X       BIN", gen(1024, 11)}});
    REQUIRE(r.ok);
    const std::string o = swapped_under(r, "pip o:=m:x.bin", r.blank);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "MEDIA CHANGE - X.BIN");
    hasnt(o, "DISK WRITE ERROR");
    r.run(ms(1500));
    CHECK(r.drive_image(0) == r.blank);
}

TEST(type_says_media_change_after_its_page)
{
    // A long text file on O:, TYPE paged (the SCB's page mode): at the page's end the floppy is changed
    // (the motor stops while TYPE waits for the key), and the next read finds another disk: 10, said on a
    // line of its own (it failed silently).
    const std::string t = lines(80);
    Dr r(true, true, {{0, "LONG    TXT", records(std::vector<uint8_t>(t.begin(), t.end()), 0x1A)}});
    REQUIRE(r.ok);
    has(r.c("pip o:=m:long.txt"), "A>");
    r.wait(ms(1500));
    const size_t from = r.out.size();
    r.type("type o:long.txt\r");               // paged by the SCB's page mode (on)
    REQUIRE(r.until("Press a key for more", ms(30000), from));
    r.wait(ms(1500));
    r.insert(0, r.blank);
    r.type(" ");
    r.until("\r\nA>", ms(60000), from + 10);
    const std::string o = r.out.substr(from);
    std::printf("  %s\n", vis(o.substr(o.size() > 160 ? o.size() - 160 : 0)).c_str());
    has(o, "\r\nMedia Change\r\n");
}

TEST(pip_from_a_changed_source_drops_its_temp_file)
{
    // O:'s floppy changed while PIP copies from it to P:: the read is 10, MEDIA CHANGE - BIG.BIN, and
    // P:'s BIG.$$$, on a floppy that didn't change, is deleted as for any failure (round 1 left it).
    Dr r(true, true, {{0, "BIG     BIN", gen(16384, 7)}});
    REQUIRE(r.ok);
    has(r.c("pip o:=m:big.bin"), "A>");
    r.wait(ms(1500));
    const size_t from = r.out.size();
    r.type("pip p:=o:big.bin\r");
    r.run(ms(3000));
    r.insert(0, r.blank);
    r.until("\r\nA>", ms(120000), from + 10);
    const std::string o = r.out.substr(from);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "MEDIA CHANGE - BIG.BIN");
    r.wait(ms(1500));
    const std::string d = r.c("dir p:");
    hasnt(d, "BIG");
}

TEST(pip_s_make_s_media_change)
{
    // With S (start at a string), PIP makes X.$$$ and reads its source before any write: the make's 10 is
    // the only one (without its check: START NOT FOUND).
    Dr r(true, true, {{0, "X       TXT", records(std::vector<uint8_t>(200, 'A'), 0x1A)}});
    REQUIRE(r.ok);
    const std::string o = swapped_under(r, "pip o:=m:x.txt[snope\x1a]", r.blank);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "MEDIA CHANGE - X.TXT");
    hasnt(o, "START NOT FOUND");
    r.run(ms(1500));
    CHECK(r.drive_image(0) == r.blank);
}

TEST(pip_s_cleanup_spares_the_new_floppy)
{
    // PIP makes X.$$$ on O: and waits for CON:; the floppy is changed for one with an X.$$$ of its own;
    // the next source isn't there, so PIP fails and cleans up: its close is 10, and the delete is skipped
    // (it would have taken the new floppy's X.$$$).
    Dr r(true, true, {{0, "Y       BIN", gen(256, 3)}});
    REQUIRE(r.ok);
    r.insert(0, r.blank);
    r.wait(ms(1500));
    has(r.c("pip o:x.tmp=m:y.bin"), "A>");          // (PIP to a .$$$ name writes its own .$$$ first)
    has(r.c("ren o:x.$$$=o:x.tmp"), "A>");
    r.wait(ms(1500));
    const std::vector<uint8_t> other = r.drive_image(0);
    r.insert(0, r.cpm);
    r.wait(ms(1500));
    const size_t from = r.out.size();
    r.type("pip o:x.bin=con:,m:nofile.txt\r");
    r.run(ms(3000));
    r.insert(0, other);
    r.type("hi\x1a");
    r.until("\r\nA>", ms(60000), from + 10);
    std::printf("  %s\n", vis(r.out.substr(from)).c_str());
    r.wait(ms(1500));
    has(r.c("dir o:"), "X        $$$");
}

TEST(a_program_s_open_with_access_stamps)
{
    // O: given access stamps (INITDIR, SET [ACCESS=ON]): the CCP's open of O:GO.COM stamps the access, a
    // directory write; with the floppy changed within the second, it is 10: Media Change, not NO PROGRAM.
    Dr r;
    REQUIRE(r.ok);
    r.put_util("initdir", UTILS);
    r.put_util("set", UTILS);
    has(r.to("initdir o:", "(Y/N)?", ms(60000)), "(Y/N)?");
    has(r.c("y"), "A>");
    has(r.c("set o: [access=on]"), "Access = ON");
    has(r.c("pip o:go.com=m:pip.com[r]"), "A>");
    const std::string o = swapped_under(r, "o:go", r.blank);
    std::printf("  %s\n", vis(o).c_str());
    has(o, "Media Change");
    hasnt(o, "NO PROGRAM");
    r.run(ms(1500));
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
