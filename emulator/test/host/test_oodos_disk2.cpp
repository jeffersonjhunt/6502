/**
 * @file test_oodos_disk2.cpp
 * @brief OODOS's Disk II driver, O: and P:, phase 6c (hw/oodos.md, "The plan, phase 6").
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * OODOS.SYS booted as test_oodos_apple boots it (oodos_rig.h's CcpRig), on a backplane whose Disk II has
 * the owner's sequencer PROM, loaded from fixtures/apple2e at run time: the suite exits 77 without it
 * (nothing of it is in the repo, and nothing of it is printed). The disks are ours: the committed card's
 * /DISKS/CPM.DSK and BLANK.DSK (cpmtools's apple-do, make_card.sh), read off the card and written out by
 * DiskImage::from_sectors, as a .dsk is in the WebUI's drive. The oracle is the same image as an SD unit
 * (C:), and DiskImage::to_sectors() of the drive's disk after: the two must be the same bytes.
 */

#include "harness.h"
#include "oodos_rig.h"
#include "disk2_rig.h"

#include "bus_cycle.h"
#include "disk2_card.h"
#include "disk_image.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace ooe::emu;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

using namespace disk2_rig;

TEST(o_reads_cpmtools_files_as_c_does)
{
    Dr r;
    REQUIRE(r.ok);
    std::string o = r.c("dir o:");
    has(o, "O: README   TXT : PATTERN  BIN");
    o = r.c("type o:readme.txt");
    has(o, "THIS IS AN APPLE CP/M DISK, MADE ON LINUX BY CPMTOOLS.\r\nOODOS READS IT AS AN SD DRIVE, /DISKS/CPM.DSK ON THE CARD.\r\n");
    const uint64_t t0 = r.cpu->cycles();
    r.c("pip m:=o:pattern.bin");
    const uint64_t took = r.cpu->cycles() - t0;
    std::printf("  PIP of 20,000 bytes from O: to M:: %.2f s\n", static_cast<double>(took) / HZ);
    CHECK(m_data(r, 0, "PATTERN BIN") == records(gen(20000, 5)));
    r.c("pip m:small.bin=o:small.bin[g2]");
    CHECK(m_data(r, 0, "SMALL   BIN") == records(gen(300, 7)));
}

TEST(the_same_pips_on_o_and_on_c_give_the_same_bytes)
{
    // Done means this: the image in the drive and the same image as an SD unit, the same PIPs onto each,
    // and the disk read back (DiskImage::to_sectors) is the SD unit's file byte for byte.
    Dr r(true, true, {{0, "X       BIN", gen(5120, 11)}, {0, "Y       BIN", gen(19200, 13)}});
    REQUIRE(r.ok);
    for (const char *d : {"c", "o"})
    {
        r.c(std::string("pip ") + d + ":=m:x.bin");
        r.c(std::string("pip ") + d + ":=m:y.bin");
        r.c(std::string("era ") + d + ":small.bin[g2]");
        r.c(std::string("ren ") + d + ":z.bin=" + d + ":y.bin");
    }
    const std::vector<uint8_t> c = r.unit_image(), o = r.drive_image(0);
    REQUIRE(o.size() == IMAGE);
    unsigned differ = 0;
    for (size_t s = 0; s < IMAGE / 256; ++s)
        differ += !std::equal(c.begin() + s * 256, c.begin() + (s + 1) * 256, o.begin() + s * 256);
    std::printf("  %u of 560 sectors differ between C: and O:\n", differ);
    CHECK(differ == 0);
    CHECK(c != r.cpm); // and they did change
    // and OODOS reads them back off the disk: a fresh look (^C) at O:
    r.wait(ms(1200));
    r.type("\x03");
    REQUIRE(r.reprompt());
    r.c("pip m:z2.bin=o:z.bin");
    CHECK(m_data(r, 0, "Z2      BIN") == gen(19200, 13));
}

TEST(o_to_p_and_back)
{
    Dr r;
    REQUIRE(r.ok);
    r.c("pip p:=o:pattern.bin");
    r.c("pip o:p2.bin=p:pattern.bin");
    r.c("pip m:=o:p2.bin");
    CHECK(m_data(r, 0, "P2      BIN") == records(gen(20000, 5)));
    has(r.c("dir p:"), "P: PATTERN  BIN");
}


TEST(no_disk_and_a_write_protected_one)
{
    // No disk in O:: Disk I/O, soon (the reads time out; nothing waits for ever).
    Dr r(false, true);
    REQUIRE(r.ok);
    const uint64_t t0 = r.cpu->cycles();
    has(r.c("dir o:"), "OODOS Error On O: Disk I/O");
    const double s = double(r.cpu->cycles() - t0) / HZ;
    std::printf("  no disk: Disk I/O after %.2f s\n", s);
    CHECK(s < 3.0);
    // Write-protected: Read Only, the disk unchanged.
    Dr w(true, true, {{0, "X       BIN", gen(1024, 11)}});
    REQUIRE(w.ok);
    w.card().disk(0)->set_write_protected(true);
    has(w.c("dir o:"), "README");
    has(w.c("pip o:=m:x.bin"), "Read Only");
    CHECK(w.drive_image(0) == w.cpm);
}

TEST(a_changed_floppy_is_seen_and_an_unchanged_one_kept)
{
    Dr r;
    REQUIRE(r.ok);
    has(r.c("dir o:"), "README");
    r.wait(ms(1200));                    // the motor stopped: the disk may have been changed
    CHECK(!r.card().status().spinning);
    has(r.c("dir o:"), "README");        // it wasn't: the directory checked, the same
    r.wait(ms(1200));
    r.insert(0, r.blank);                // now it is
    const std::string o = r.c("dir o:");
    has(o, "No File");
    hasnt(o, "README");
    // a file open across the change: Media Change, 10, at its next read and its close (MEMTEST's case O)
    Dr f;
    REQUIRE(f.ok);
    const size_t from = start(f, "memtest.com", " O");
    REQUIRE(f.until("SWAP?", ms(20000), from));
    f.wait(ms(1200));
    f.insert(0, f.blank);
    f.type(" ");
    REQUIRE(f.until("MEMTEST DONE", ms(20000), from));
    const std::string m = f.out.substr(from);
    std::printf("  %s\n", vis(m).c_str());
    has(m, "ORD 00 00");
    has(m, "ORD2 0A 00");
    has(m, "OCL 0A 00");
    has(m, "OOP2 FF");
    CHECK(f.drive_image(0) == f.blank); // and nothing of the old disk's written on the new one
}

TEST(the_head_lost_is_found_and_a_bad_sector_is_only_its_own)
{
    // The head where the driver doesn't think it is: the address fields say another track, so it
    // recalibrates and reads.
    Dr r;
    REQUIRE(r.ok);
    has(r.c("dir o:"), "README");
    r.wait(ms(1200));
    r.poke(SYS_BLOCK_PHYS + sym("D2HT"), 20); // track 10, it thinks
    has(r.c("type o:readme.txt"), "THIS IS AN APPLE CP/M DISK");
    // A data field spoiled on track 3 (CP/M's sector 15 there, the file's 1, physical 13): the rest of the
    // track reads, the directory with it; a read of that sector is Disk I/O.
    Dr b;
    REQUIRE(b.ok);
    REQUIRE(spoil_data(*b.card().disk(0), 3, 13));
    has(b.c("dir o:"), "README");
    const std::string t = b.c("pip m:=o:pattern.bin");
    std::printf("  %s\n", vis(t).c_str());
    has(t, "Disk I/O");
}

TEST(every_track_and_the_motor)
{
    // Tracks 0-34 through the BIOS (MEMTEST's case T): all read, the head on track 34.
    Dr r;
    REQUIRE(r.ok);
    const size_t from = start(r, "memtest.com", " T");
    REQUIRE(r.until("MEMTEST DONE", ms(60000), from));
    has(r.out.substr(from), "TERR 00");
    CHECK_EQ(r.card().status().quarter_track[0], 136);
    // The motor: on for each access, turning a second after it, then stopped. An access within the
    // second finds its track in the buffer; after it, the track is read again (it may be another disk).
    Dr m;
    REQUIRE(m.ok);
    unsigned tracks = 0;
    const uint16_t a_track = sym("D2TRACK");
    m.watch = [&](uint16_t pc) {
        if (pc == a_track && m.nb(2) == 61)
            ++tracks;
    };
    has(m.c("type o:readme.txt"), "THIS IS");
    CHECK(m.card().status().spinning && !m.card().status().motor);
    tracks = 0;
    m.wait(ms(300));
    has(m.c("type o:readme.txt"), "THIS IS");
    CHECK_EQ(tracks, 0u);
    m.wait(ms(1200));
    CHECK(!m.card().status().spinning);
    has(m.c("type o:readme.txt"), "THIS IS");
    CHECK(tracks >= 1u);
    m.watch = nullptr;
}

TEST(the_write_loop_loads_every_224_cycles_with_dma_off)
{
    // Every load of the data bus while a sector is written, from the bus: the first five 280 cycles apart
    // (sync bytes), the rest 224; the DMA controller's master enable off at each, and back after.
    Dr r(true, true, {{0, "X       BIN", gen(1024, 11)}});
    REQUIRE(r.ok);
    has(r.c("dir o:"), "README");
    Loads loads;
    loads.bus = &r.machine->bus();
    const uint8_t before = r.machine->bus().peek(0xC441);
    r.cpu->set_observer(&loads);
    r.c("pip o:=m:x.bin");
    r.cpu->set_observer(nullptr);
    CHECK_EQ(r.machine->bus().peek(0xC441), before);
    REQUIRE(loads.at.size() >= 354);
    // the first sector's field: 5 syncs and 349 bytes
    unsigned wrong = 0;
    for (size_t i = 1; i < 354; ++i)
    {
        const uint64_t d = loads.at[i] - loads.at[i - 1], want = i <= 5 ? 280 : 224;
        if (d != want)
        {
            if (wrong < 5)
                std::printf("  load %zu: %llu cycles after the last, not %llu\n", i, static_cast<unsigned long long>(d),
                            static_cast<unsigned long long>(want));
            ++wrong;
        }
    }
    unsigned dma_on = 0;
    for (bool off : loads.dma_off)
        dma_on += !off;
    std::printf("  %zu loads; %u at the wrong time in the first field; DMA on at %u\n", loads.at.size(), wrong, dma_on);
    CHECK_EQ(wrong, 0u);
    CHECK_EQ(dma_on, 0u);
}

TEST(an_interrupt_mid_write_waits_for_the_write)
{
    // As if interrupts were on in the BIOS (OOLIB's tick, later): the I flag cleared at D2WSEC's entry,
    // and an interrupt asked for in the middle of the write loop. The driver's SEI holds it off: every
    // load still 224 cycles after the last (an interrupt taken would put one late by its handler's time,
    // which the disk may even pass over, a 0 bit or two more; the loads say it plainly), and the sector
    // written right: O: and C: the same after the same PIP.
    Dr r(true, true, {{0, "X       BIN", gen(5120, 11)}});
    REQUIRE(r.ok);
    r.c("pip c:=m:x.bin");
    const uint16_t a_w = sym("D2WSEC"), a_loop = sym("WLA");
    unsigned loops = 0, asked = 0;
    r.watch = [&](uint16_t pc) {
        if (r.nb(2) != 61)
            return;
        if (pc == a_w)
        {
            Registers g = r.cpu->regs();
            g.p = static_cast<uint8_t>(g.p & ~Cpu6502::FLAG_I);
            r.cpu->set_regs(g);
            loops = 0;
        }
        if (pc == a_loop && ++loops == 100)
        {
            r.cpu->irq();
            ++asked;
        }
    };
    Loads loads;
    r.cpu->set_observer(&loads);
    r.c("pip o:=m:x.bin");
    r.cpu->set_observer(nullptr);
    r.watch = nullptr;
    CHECK(asked > 0);
    // each field: 5 syncs 280 apart then 349 bytes 224 apart; between fields, anything
    unsigned late = 0;
    for (size_t f = 0; f + 354 <= loads.at.size(); f += 354)
        for (size_t i = f + 1; i < f + 354; ++i)
            late += loads.at[i] - loads.at[i - 1] != (i - f <= 5 ? 280u : 224u);
    std::printf("  %zu loads in %zu fields, %u not on time\n", loads.at.size(), loads.at.size() / 354, late);
    CHECK_EQ(loads.at.size() % 354, size_t{0});
    CHECK_EQ(late, 0u);
    const std::vector<uint8_t> c = r.unit_image(), o = r.drive_image(0);
    const bool same = o.size() == IMAGE && c == o;
    std::printf("  %u interrupts asked for mid-write; O: %s C:\n", asked, same ? "is" : "is not");
    CHECK(same);
}

TEST(every_address_field_keeps_its_epilogue_after_writes)
{
    // 6c's review, F4: a write starts after the address field's DE AA, so the AA isn't written over. After
    // PIPs onto O:, every address field on every track still reads D5 AA 96 ... DE AA (DiskImage's
    // to_sectors() doesn't look; DOS 3.3 does).
    Dr r(true, true, {{0, "X       BIN", gen(5120, 11)}, {0, "Y       BIN", gen(19200, 13)}});
    REQUIRE(r.ok);
    r.c("pip o:=m:x.bin");
    r.c("pip o:=m:y.bin");
    unsigned bad = 0, fields = 0;
    for (unsigned t = 0; t < 35; ++t)
    {
        const TrackRead tr = read_track(*r.card().disk(0), t);
        bad += tr.bad_epilogues;
        fields += static_cast<unsigned>(tr.good.size());
    }
    std::printf("  %u sectors read whole, %u address fields without DE AA\n", fields, bad);
    CHECK_EQ(bad, 0u);
    CHECK(fields >= 560);
}

TEST(a_break_mid_write_loses_only_that_sector)
{
    // 6c's review, F5: an NMI (Break) in the middle of the write loop goes to the monitor, but turns the
    // card's write mode off first (and its motor, and the DMA controller's master back on): the sector
    // being written is lost, the track's other 15 kept.
    Dr r(true, true, {{0, "X       BIN", gen(1024, 11)}});
    REQUIRE(r.ok);
    has(r.c("dir o:"), "README");
    const TrackRead before = read_track(*r.card().disk(0), 3);
    const uint8_t dma = r.machine->bus().peek(0xC441);
    const uint16_t a_loop = sym("WLA");
    unsigned loops = 0;
    bool hit = false;
    r.watch = [&](uint16_t pc) {
        if (!hit && pc == a_loop && r.nb(2) == 61 && ++loops == 100)
        {
            r.cpu->nmi();
            hit = true;
        }
    };
    r.type("pip o:=m:x.bin\r");
    for (int i = 0; i < 400 && !hit; ++i)
        r.run(ms(50));
    r.run(ms(200));
    r.watch = nullptr;
    REQUIRE(hit);
    const Disk2Card::Status st = r.card().status();
    CHECK(!st.q7 && !st.motor);
    CHECK_EQ(r.machine->bus().peek(0xC441), dma);
    r.run(ms(1500)); // the run-on over
    unsigned kept = 0;
    for (unsigned t = 0; t < 35; ++t)
        if (t == 3)
            kept = static_cast<unsigned>(read_track(*r.card().disk(0), t).good.size());
    std::printf("  track 3: %zu sectors before, %u after the Break\n", before.good.size(), kept);
    CHECK(kept >= 15);
}

TEST(a_half_missing_is_read_again_and_a_run_of_d5s_ends)
{
    // F7: a sector that didn't read is not kept as missing while the motor runs on: once the disk reads
    // again, so does the sector. F8: a track of nothing but D5 bytes is Disk I/O in its bounded time,
    // every byte counted.
    Dr r;
    REQUIRE(r.ok);
    has(r.c("dir o:"), "README");
    DiskTrack &t3 = r.card().disk(0)->track_for_writing(12);
    const DiskTrack keep = t3;
    REQUIRE(spoil_data(*r.card().disk(0), 3, 13));
    r.wait(ms(1200)); // its buffer forgotten: the track read again with the bad sector
    has(r.c("pip m:=o:pattern.bin"), "Disk I/O");
    r.card().disk(0)->track_for_writing(12) = keep; // good again
    r.c("pip m:=o:pattern.bin");                     // at once, within the second
    CHECK(m_data(r, 0, "PATTERN BIN") == records(gen(20000, 5)));
    Dr d;
    REQUIRE(d.ok);
    DiskTrack &t = d.card().disk(0)->track_for_writing(12);
    // ten sync bytes (eight 1s, two 0s) to put a reader in step, then D5 after D5 round the track
    uint32_t n = 0;
    for (; n < 100; ++n)
        t.set_bit(n, n % 10 < 8);
    for (; n < t.bit_count; ++n)
        t.set_bit(n, (0xD5 >> (7 - (n - 100) % 8)) & 1);
    const uint64_t t0 = d.cpu->cycles();
    has(d.to("dir o:", "\r\nA>", ms(20000)), "Disk I/O");
    const double secs = double(d.cpu->cycles() - t0) / HZ;
    std::printf("  a track of D5s: Disk I/O after %.1f s\n", secs);
    CHECK(secs < 10.0);
}

int main()
{
    if (!std::ifstream(P6).good())
    {
        std::printf("  SKIP: no Disk II sequencer PROM among the fixtures\n");
        return 77;
    }
    return run_all();
}
