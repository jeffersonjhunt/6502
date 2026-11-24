/**
 * @file test_oodos_sndfw.cpp
 * @brief SNDFW.COM (oodos/utils/sndfw.s): a firmware image for the sound card (revision 3, the card as a
 * device) sent from M: through function 82's SN_FIRMWARE, on OODOS.SYS booted as test_oodos_sound boots it.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Each image is built here, as the spec's firmware image says (hw/future/sound-device.md, "The firmware image"):
 * the 32-byte header (magic, card type, version, build, the body's length and CRC-32, the header's CRC-32) and a
 * body of noise, its length not a record's multiple, so the file's last record holds padding past the image.
 *   - a good image is written into the slot not running and marked to boot, and after FWRESTART the card runs
 *     its version (FWVER), so the update's whole path is exercised: begin, the pieces by DMA, commit, FWRESULT;
 *   - a bad image (its body's CRC) is refused (E_FIRMWARE): the old image stays the one that boots;
 *   - a file that isn't an image is refused by SNDFW itself, before the card is sent anything.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "sound_card3.h"
#include "sound_fw.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const bool REV3 = (sound_revision() = 3, true); ///< every rig here: revision 3

    SoundCard3 &dev(Rig &r) { return *r.machine->sound_device(); }
    SoundFw &fw(Rig &r) { return dev(r).firmware(); }

    /// Bytes from a fixed seed (no fixture).
    std::vector<uint8_t> noise(size_t n, uint32_t seed)
    {
        std::vector<uint8_t> v(n);
        for (uint8_t &b : v)
        {
            seed = seed * 1103515245u + 12345u;
            b = static_cast<uint8_t>(seed >> 16);
        }
        return v;
    }

    /// An image of version `major`.`minor`, build `build`, a body of `len` bytes; `good` false: the body's
    /// CRC-32 off by one bit.
    std::vector<uint8_t> image(uint8_t major, uint8_t minor, uint16_t build, size_t len, bool good)
    {
        const std::vector<uint8_t> body = noise(len, 1234);
        std::vector<uint8_t> h(32, 0);
        h[0] = 'O', h[1] = 'O', h[2] = 'S', h[3] = 'D', h[4] = 0xA3, h[5] = 1, h[6] = major, h[7] = minor;
        h[8] = static_cast<uint8_t>(build), h[9] = static_cast<uint8_t>(build >> 8);
        const uint32_t n = static_cast<uint32_t>(body.size());
        const uint32_t crc = Crc32::of(body.data(), body.size()) ^ (good ? 0u : 1u);
        for (unsigned i = 0; i < 4; ++i)
            h[12 + i] = static_cast<uint8_t>(n >> (8 * i)), h[16 + i] = static_cast<uint8_t>(crc >> (8 * i));
        const uint32_t hc = Crc32::of(h.data(), 28);
        for (unsigned i = 0; i < 4; ++i)
            h[28 + i] = static_cast<uint8_t>(hc >> (8 * i));
        h.insert(h.end(), body.begin(), body.end());
        return h;
    }

    /// SNDFW and NEW.FW (`file`) on M: (its last record padded, no byte count).
    void put(CcpRig &r, const std::vector<uint8_t> &file)
    {
        r.put_util("sndfw");
        r.put_m({{0, "NEW     FW ", file, false, false, false}});
    }

    /// FT_INFOX's LDA #18 (the interface, LDX #$2F after it) in OODOS's block 60 made LDA #`v`: an older OODOS.
    void report_interface(CcpRig &r, uint8_t v)
    {
        const uint32_t base = 60u * BLOCK - 0x4000u + sym("FT_INFOX");
        uint32_t p = base;
        while (p < base + 64 && !(r.phys(p) == 0xA9 && r.phys(p + 1) == 18 && r.phys(p + 2) == 0xA2 && r.phys(p + 3) == 0x2F))
            ++p;
        REQUIRE(p < base + 64);
        r.poke(p + 1, v);
    }
} // namespace

TEST(a_good_image_from_m_is_written_marked_to_boot_and_runs_after_fwrestart)
{
    // 10,000 bytes of body: 10,032 in all, three pieces of up to 4 KB, the last record 80 bytes of padding that
    // mustn't be sent (the card's transfer is the header's length). Must fail with SNDFW's cut of the last piece
    // left out (the padding sent: the card refuses the transfer), or the version printed from the wrong bytes.
    const std::vector<uint8_t> img = image(1, 7, 3, 10000, true);
    REQUIRE(img.size() % 128 != 0);
    CcpRig r;
    REQUIRE(r.ok);
    put(r, img);
    const uint8_t running = fw(r).running_slot();
    REQUIRE(fw(r).boot_slot() == running);
    const std::string o = r.cmd("M:SNDFW M:NEW");
    std::printf("  %s\n", vis(o).c_str());
    CHECK(o.find("Firmware 1.7 (build 3) written: the sound card runs it from its next start.") != std::string::npos);
    CHECK_EQ(fw(r).boot_slot(), 1 - running);
    CHECK_EQ(fw(r).running_slot(), running); // not until it starts again
    CHECK_EQ(fw(r).version_minor(), SoundFw::VERSION_MINOR);
    r.machine->bus().write(0xC500, SoundCard3::CTRL_FWRESTART);
    r.run(ms(300));
    CHECK_EQ(fw(r).running_slot(), 1 - running);
    CHECK_EQ(fw(r).version_major(), 1);
    CHECK_EQ(fw(r).version_minor(), 7);
}

TEST(a_bad_image_is_refused_and_the_old_one_stays_the_one_that_boots)
{
    // The body's CRC-32 wrong: FWRESULT's E_FIRMWARE, said in words; the boot slot unchanged. Must fail with
    // SNDFW taking every refusal for success.
    CcpRig r;
    REQUIRE(r.ok);
    put(r, image(1, 8, 4, 5000, false));
    const uint8_t running = fw(r).running_slot();
    const std::string o = r.cmd("M:SNDFW M:NEW");
    std::printf("  %s\n", vis(o).c_str());
    CHECK(o.find("The card refused the image: its old firmware stays.") != std::string::npos);
    CHECK(o.find("written") == std::string::npos);
    CHECK_EQ(fw(r).boot_slot(), running);
    CHECK_EQ(fw(r).version_minor(), SoundFw::VERSION_MINOR);
}

TEST(what_sndfw_refuses_before_it_sends)
{
    // A file without the magic, or an image for another card type: SNDFW's own words, the boot slot unchanged.
    // Must fail with the magic's check left out (the card's own refusal instead). And the usage, a missing file.
    CcpRig r;
    REQUIRE(r.ok);
    std::vector<uint8_t> other = image(1, 9, 5, 600, true);
    other[4] = 0xA2; // another card's
    put(r, std::vector<uint8_t>(700, 0x41));
    r.put_m({{0, "OTHER   FW ", other, false, false, false}});
    const std::vector<uint8_t> whole = image(1, 9, 6, 3000, true);
    r.put_m({{0, "SHORT   FW ", std::vector<uint8_t>(whole.begin(), whole.begin() + 20), false, false, false},
             {0, "CUT     FW ", std::vector<uint8_t>(whole.begin(), whole.begin() + 1024), false, false, false}});
    const uint8_t running = fw(r).running_slot();
    CHECK(r.cmd("M:SNDFW M:NEW").find("Not a sound card firmware image.") != std::string::npos);
    CHECK(r.cmd("M:SNDFW M:OTHER").find("Not a sound card firmware image.") != std::string::npos);
    CHECK_EQ(fw(r).boot_slot(), running);
    // Review T7: a file that ends before its header's length (the first 20 bytes of an image, SHORT, a record on the
    // card; an image with its last records lost, CUT): SNDFW's own words, the boot slot unchanged. Must fail with the
    // end-of-file check left out (a piece of no bytes sent: the card's refusal instead).
    for (const char *f : {"M:SNDFW M:SHORT", "M:SNDFW M:CUT"})
    {
        const std::string o = r.cmd(f);
        if (o.find("Not a sound card firmware image.") == std::string::npos)
            std::printf("  %s: %s\n", f, vis(o).c_str());
        CHECK(o.find("Not a sound card firmware image.") != std::string::npos);
    }
    CHECK_EQ(fw(r).boot_slot(), running);
    CHECK(r.cmd("M:SNDFW M:NONE").find("No File") != std::string::npos);
    CHECK(r.cmd("M:SNDFW").find("SNDFW filespec") != std::string::npos);
}

int main() { return run_all(); }

TEST(sndfw_refuses_an_oodos_before_interface_18)
{
    // Function 82's table is interface 18's (OODOS 0.17): under an OODOS that says 17, SNDFW refuses before it
    // opens the file. Must fail with its CMP #18 made CMP #17 (the missing file's "No File" instead).
    CcpRig r;
    REQUIRE(r.ok);
    r.put_util("sndfw");
    report_interface(r, 17);
    const std::string o = r.cmd("M:SNDFW M:NONE");
    CHECK(o.find("SNDFW needs OODOS 0.17 or newer.") != std::string::npos);
    CHECK(o.find("No File") == std::string::npos);
}
