/**
 * @file test_oodos_err10.cpp
 * @brief Error 10 (Media Change) said by the CCP's ERASE, RENAME and TYPE and by PIP (OODOS todo item 17).
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * O:'s floppy changed in the middle of a command. ERASE, RENAME and a PIP copy change it within the
 * second of O:'s last access, so the select takes the disk as the same and the first directory write
 * finds another disk: the BDOS returns 10 and writes nothing. TYPE's floppy is changed while it waits at
 * a page's end: its next read is 10. Each must say so and fail, not take 10 for success (they took only
 * $FF as a failure). disk2_rig.h's rig, the owner's sequencer PROM from the fixtures (77 without it).
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

int main()
{
    if (!have_prom())
    {
        std::printf("  SKIP: no Disk II sequencer PROM among the fixtures\n");
        return 77;
    }
    return run_all();
}
