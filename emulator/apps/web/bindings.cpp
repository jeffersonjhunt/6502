/**
 * @file bindings.cpp
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The WebUI's view of a Session (session.h), through embind.
 * @version 0.1.0
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * JavaScript gets one class, `Machine`, and the files built into the
 * module (CMakeLists.txt): /machines/backplane.machine, /roms/bios.rom and
 * /roms/bios.rom.lbl.
 *
 *   const oo1 = await createOO1();
 *   const m = new oo1.Machine();
 *   if (!m.open("/machines/backplane.machine", "/roms/bios.rom", "/roms/bios.rom.lbl")) throw m.error();
 *   m.run(119318);  m.takeOutput();  m.send("C680.C684\r");
 *
 * open() first: every other call needs an open machine. Everything is
 * plain numbers and strings, so it can sit behind the UI's
 * MachinePort as a WebSocket bridge will. Cycles are doubles (exact to
 * 2^53, centuries at 7 MHz). Strings are bytes, Latin-1 both ways
 * (-sEMBIND_STD_STRING_IS_UTF8=0), as a serial line is.
 *
 * Bulk reads are views into the Machine's own buffers, valid until the
 * next call that fills them (or until memory grows), so a frame copies
 * nothing it does not use:
 *
 * - lights(): Float32Array(32): A0-A19, D0-D7, then SYNC, RWB, VPB, RDY.
 * - trace(max): Uint32Array(4 per row, oldest first): cycle low 32 bits,
 *   cycle high bits, A0-A19 | module << 24, CPU address | data << 16 |
 *   flags << 24 (1 write, 2 SYNC, 4 VPB).
 * - videoFrame(): Uint8Array(640 x 480 x 4): the video card's last finished
 *   frame, R G B A per pixel, row by row; empty without a card. A new one
 *   each frame (videoFrames() counts them), so copy it before running on.
 *
 * setClock(hz) is the clock the caller runs the CPU at (0 for flat out),
 * so the video card keeps 60 frames a second at any speed.
 *
 * The USB card's host side (usb_card.h): usbKey(usage, down) with HID usage
 * codes (queued at the keyboard's 8 ms polling pace), usbMouseTo(x, y) (absolute, in its MAXX/MAXY), usbMouseMove(dx, dy),
 * usbMouseButtons(bits), usbMouseWheel(steps). Without a card they do nothing.
 *
 * Apple II mode: loadCartridge(rom, chars, boot, sequencer) plugs the
 * owner's IIe ROM (16 KB, $C000-$FFFF) and character ROM (4 or 8 KB, or ""
 * for none) into the cartridge, and the Disk II card's two PROMs (256 bytes
 * each, or ""), as with the power off (a power cycle); all "" takes them
 * out. The bytes go to the module's own memory file system and no
 * further, a new file for each, so one that is refused leaves the files
 * the old cartridge loads from (at the next power cycle) as they were. appleMode() is the north bridge's MODE; appleSwitches() packs
 * the IIe's soft switches (APPLE_* below), or -1 without the backplane's
 * bridge. peekIo(offset) reads I/O space ($000-$FFF, the cards' registers)
 * with no bus cycle in either mode.
 *
 * The Disk II's drives (0 and 1): insertDisk(drive, bytes, name) takes a
 * .woz, .dsk, .do or .po file's bytes (name only breaks a tie); ejectDisk;
 * diskInfo(drive) is -1 for none, else bit 0 changed, bit 1 write-protected,
 * bits 4-5 the format (0 WOZ, 1 DOS order, 2 ProDOS order); diskWrites(drive)
 * counts writes, so a copy kept elsewhere can tell it is behind;
 * saveDisk(drive, "woz" | "dsk" | "po") is the file, or "" with error();
 * setWriteProtect(drive, on). diskStatus() is -1 without the card, else bit
 * 0 motor on, 1 spinning, 2 drive 2 selected, 3 Q6, 4 Q7, 8-15 the data
 * register, 16-19 the phases; diskQuarter(drive) is that drive's head, in
 * quarter tracks (0-159).
 *
 * The SD card's slot (sd_card.h): hasSd() if the machine has the card;
 * insertSd(bytes, name) puts a card image (a Uint8Array, whole 512-byte sectors) in, held
 * in memory, or false with error(); ejectSd(); sdInfo() is -1 for an empty
 * slot, else bit 0 written to since it went in or was last saved, bit 1
 * write-protected; sdSectors(); sdSave() is a copy of the image (a Uint8Array, null for none), and counts
 * as saved; setSdWriteProtect(on) is the card's switch; sdStatus() is -1
 * without the card, else its STATUS register; sdCommands() counts the
 * commands it has started (a light: one between two frames is activity).
 *
 * The sound card's 6510 (sound_card.h): soundCpu() is its registers, state
 * (0 running, 2 jammed), cycles, port (ddr, port) and CIA 1's timer A
 * (timer, latch, timerOn), or has: false without the card. Its registers
 * at $C500 are peekIo's. soundVoice(chip, voice) is that voice's envelope:
 * bits 0-7 the level, 8-9 the phase (0 attack, 1 decay/sustain, 2
 * release), bit 10 held at 0; -1 without the card.
 *
 * Its sound: setAudioRate(rate) makes it at `rate` samples a second (0:
 * none), kept across power cycles; takeAudio() is what was made since the
 * last call, left and right interleaved, as a Float32Array view into the
 * module's memory (copy it before the next call).
 *
 * A `.sid` tune: loadSid(bytes, song) starts it on the card (song 0: its
 * start song; false, with error(), if it is not one or there is no card),
 * sidSong(song) another song of it, stopSid() stops and forgets it.
 * sidInfo() is the tune (has: false for none): name, author, released,
 * songs, song, rsid, ntsc, mos8580, and warnings, one a line, for what the
 * card can't give it. soundRunning() is whether the card's 6510 runs.
 */

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "session.h"

namespace
{
    using namespace ooe::emu;
    using emscripten::val;

    /// appleSwitches()'s bits; the annunciators AN0-AN3 are bits 20-23.
    enum : unsigned
    {
        APPLE_TEXT = 1u << 0,
        APPLE_MIXED = 1u << 1,
        APPLE_PAGE2 = 1u << 2,
        APPLE_HIRES = 1u << 3,
        APPLE_COL80 = 1u << 4,
        APPLE_STORE80 = 1u << 5,
        APPLE_ALTCHAR = 1u << 6,
        APPLE_DHIRES = 1u << 7,
        APPLE_RAMRD = 1u << 8,
        APPLE_RAMWRT = 1u << 9,
        APPLE_ALTZP = 1u << 10,
        APPLE_INTCXROM = 1u << 11,
        APPLE_SLOTC3ROM = 1u << 12,
        APPLE_INTC8ROM = 1u << 13,
        APPLE_LC_BANK2 = 1u << 14,
        APPLE_LC_READ = 1u << 15,
        APPLE_LC_WRITE = 1u << 16,
        APPLE_IOUDIS = 1u << 17,
        APPLE_AN_SHIFT = 20,
    };

    constexpr size_t LIGHT_COUNT = ConsoleCard::ADDRESS_LIGHTS + ConsoleCard::DATA_LIGHTS + 4;

    struct Regs
    {
        unsigned pc, a, x, y, s, p;
    };

    struct SidInfo
    {
        bool has{false};
        std::string name, author, released, warnings; ///< warnings: one a line
        unsigned songs{0}, song{0};
        bool rsid{false}, ntsc{false}, mos8580{false};
    };

    struct SoundCpu
    {
        bool has;
        unsigned pc, a, x, y, s, p, state;
        double cycles;
        unsigned ddr, port, timer, latch;
        bool timer_on;
    };

    /// `Machine` in JavaScript (ooe::emu::Machine is the description).
    class WebMachine
    {
    public:
        bool open(const std::string &machine, const std::string &rom, const std::string &symbols)
        {
            error_.clear();
            session_ = Session::create({machine, rom, symbols}, error_);
            return session_ != nullptr;
        }
        bool is_open() const { return session_ != nullptr; }
        const std::string &error() const { return error_; }

        /// @return why it stopped: 0 budget, 1 halted, 2 STP
        int run(double budget)
        {
            const Session::RunResult r = session_->run(static_cast<uint64_t>(budget));
            last_cycles_ = static_cast<double>(r.cycles);
            return static_cast<int>(r.why);
        }
        double last_cycles() const { return last_cycles_; }
        bool power_cycle()
        {
            error_.clear();
            return session_->power_cycle(error_);
        }

        bool has_card() const { return session_->has_card(); }
        void set_switches(unsigned swr) { session_->set_switches(swr); }
        void set_panel(bool halt, bool bus_cycle) { session_->set_panel(halt, bus_cycle); }
        void set_lock(bool locked) { session_->set_lock(locked); }
        void press(unsigned keys) { session_->press(static_cast<uint8_t>(keys)); }
        void start() { session_->start(); }
        unsigned card_read(unsigned offset) const { return session_->card_read(static_cast<uint8_t>(offset)); }
        void card_write(unsigned offset, unsigned data)
        {
            session_->card_write(static_cast<uint8_t>(offset), static_cast<uint8_t>(data));
        }

        void send(const std::string &bytes) { session_->send(bytes); }
        std::string take_output() { return session_->take_output(); }

        Regs regs() const
        {
            const Registers &r = session_->regs();
            return Regs{r.pc, r.a, r.x, r.y, r.s, r.p};
        }
        double cycles() const { return static_cast<double>(session_->cycles()); }
        bool halted() const { return session_->halted(); }
        bool stopped() const { return session_->stopped(); }
        bool irq() { return session_->irq(); }
        unsigned peek(unsigned addr) const { return session_->peek(static_cast<uint16_t>(addr)); }
        unsigned peek_physical(unsigned addr) const { return session_->peek_physical(addr); }
        unsigned physical(unsigned addr) const { return session_->physical(static_cast<uint16_t>(addr)); }
        /// @return the address, or -1 if `text` is neither a label nor hex
        int resolve(const std::string &text) const
        {
            uint16_t addr = 0;
            return session_->resolve(text, addr) ? addr : -1;
        }

        val lights()
        {
            const ConsoleCard::Lights l = session_->lights();
            size_t i = 0;
            for (float a : l.addr)
                lights_[i++] = a;
            for (float d : l.data)
                lights_[i++] = d;
            lights_[i++] = l.sync;
            lights_[i++] = l.rwb;
            lights_[i++] = l.vpb;
            lights_[i++] = l.rdy;
            return val(emscripten::typed_memory_view(lights_.size(), lights_.data()));
        }
        void set_lights(bool on) { session_->set_lights(on); }
        bool has_video() const { return session_->video() != nullptr; }
        double video_frames() const
        {
            const VideoCard *v = session_->video();
            return v ? static_cast<double>(v->frames()) : 0;
        }
        val video_frame() const
        {
            const VideoCard *v = session_->video();
            const size_t bytes = v ? size_t{VideoCard::WIDTH} * VideoCard::HEIGHT * 4 : 0;
            return val(emscripten::typed_memory_view(bytes, v ? reinterpret_cast<const uint8_t *>(v->frame()) : nullptr));
        }
        void set_clock(double hz) { session_->set_cpu_hz(static_cast<uint32_t>(hz)); }
        /// The clock the machine's mode fixes (Apple II mode), or 0: pace by it then.
        double fixed_hz() const { return session_->fixed_hz(); }
        /// @return false, with error(), if it does not fit; the old cartridge stays
        bool load_cartridge(const std::string &rom, const std::string &chars, const std::string &boot,
                            const std::string &sequencer)
        {
            error_.clear();
            const std::string stem = "/tmp/cartridge-" + std::to_string(++cartridges_);
            const auto file = [](const std::string &path, const std::string &bytes) {
                if (bytes.empty())
                    return std::string();
                std::ofstream(path, std::ios::binary | std::ios::trunc) << bytes;
                return path;
            };
            const std::string rom_file = file(stem + "-iie.rom", rom), chars_file = file(stem + "-chars.rom", chars),
                              boot_file = file(stem + "-disk2-boot.rom", boot),
                              sequencer_file = file(stem + "-disk2-sequencer.rom", sequencer);
            std::vector<std::string> parts;
            if (!rom_file.empty())
                parts.push_back(rom_file);
            const bool ok = session_->set_cartridge(parts, chars_file, boot_file, sequencer_file, error_);
            for (const std::string &old :
                 ok ? loaded_ : std::vector<std::string>{rom_file, chars_file, boot_file, sequencer_file})
                if (!old.empty())
                    std::remove(old.c_str());
            if (ok)
                loaded_ = {rom_file, chars_file, boot_file, sequencer_file};
            return ok;
        }
        bool apple_mode() const { return session_->apple_mode(); }
        int apple_switches() const
        {
            const AppleSwitches *s = session_->apple_switches();
            if (!s)
                return -1;
            const auto bit = [](bool on, unsigned b) { return on ? b : 0u; };
            const unsigned v = bit(s->text, APPLE_TEXT) | bit(s->mixed, APPLE_MIXED) | bit(s->page2, APPLE_PAGE2) |
                               bit(s->hires, APPLE_HIRES) | bit(s->col80, APPLE_COL80) |
                               bit(s->store80, APPLE_STORE80) | bit(s->altchar, APPLE_ALTCHAR) |
                               bit(s->dhires, APPLE_DHIRES) | bit(s->ramrd, APPLE_RAMRD) |
                               bit(s->ramwrt, APPLE_RAMWRT) | bit(s->altzp, APPLE_ALTZP) |
                               bit(s->intcxrom, APPLE_INTCXROM) | bit(s->slotc3rom, APPLE_SLOTC3ROM) |
                               bit(s->intc8rom, APPLE_INTC8ROM) | bit(s->lc_bank2, APPLE_LC_BANK2) |
                               bit(s->lc_read, APPLE_LC_READ) | bit(s->lc_write, APPLE_LC_WRITE) |
                               bit(s->ioudis, APPLE_IOUDIS) | unsigned{s->an & 0x0Fu} << APPLE_AN_SHIFT;
            return static_cast<int>(v);
        }
        bool insert_disk(unsigned drive, const std::string &bytes, const std::string &name)
        {
            error_.clear();
            return session_->insert_disk(drive, std::vector<uint8_t>(bytes.begin(), bytes.end()), name, error_);
        }
        void eject_disk(unsigned drive) { session_->eject_disk(drive); }
        int disk_info(unsigned drive) const
        {
            const DiskImage *d = session_->disk(drive);
            if (!d)
                return -1;
            return (d->changed() ? 1 : 0) | (d->write_protected() ? 2 : 0) | static_cast<int>(d->format()) << 4;
        }
        double disk_writes(unsigned drive) const
        {
            const DiskImage *d = session_->disk(drive);
            return d ? d->writes() : 0;
        }
        std::string save_disk(unsigned drive, const std::string &format)
        {
            error_.clear();
            const DiskImage *d = session_->disk(drive);
            if (!d)
            {
                error_ = "no disk in drive " + std::to_string(drive + 1);
                return "";
            }
            std::vector<uint8_t> out;
            if (format == "woz")
                out = d->to_woz();
            else if (!d->to_sectors(format == "po" ? DiskImage::Format::Prodos : DiskImage::Format::Dos, out, error_))
                return "";
            return std::string(out.begin(), out.end());
        }
        void set_write_protect(unsigned drive, bool on)
        {
            if (DiskImage *d = session_->disk(drive))
                d->set_write_protected(on);
        }
        int disk_status() const
        {
            Disk2Card *c = session_->disk2();
            if (!c)
                return -1;
            const Disk2Card::Status s = c->status();
            return (s.motor ? 1 : 0) | (s.spinning ? 2 : 0) | (s.drive ? 4 : 0) | (s.q6 ? 8 : 0) | (s.q7 ? 16 : 0) |
                   s.data << 8 | (s.phases & 0xF) << 16;
        }
        unsigned disk_quarter(unsigned drive) const
        {
            Disk2Card *c = session_->disk2();
            return c ? c->status().quarter_track[drive % Disk2Card::DRIVES] : 0;
        }

        bool has_sd() const { return session_->sd() != nullptr; }
        /// `bytes` a Uint8Array: a card is megabytes, too many for a string.
        bool insert_sd(const val &bytes, const std::string &name)
        {
            error_.clear();
            return session_->insert_sd(emscripten::convertJSArrayToNumberVector<uint8_t>(bytes), name, error_);
        }
        void eject_sd() { session_->eject_sd(); }
        int sd_info() const
        {
            const MemoryMedia *m = session_->sd_image();
            if (!m)
                return -1;
            return (m->dirty() ? 1 : 0) | (m->write_protected() ? 2 : 0);
        }
        double sd_sectors() const
        {
            const SdMedia *m = session_->sd_media();
            return m ? static_cast<double>(m->sectors()) : 0;
        }
        val sd_save() const
        {
            MemoryMedia *m = session_->sd_image();
            if (!m)
                return val::null();
            m->clean();
            const std::vector<uint8_t> &b = m->bytes();
            // A copy: the card's bytes stay the machine's, and a view moves if memory grows.
            return val(emscripten::typed_memory_view(b.size(), b.data())).call<val>("slice");
        }
        void set_sd_write_protect(bool on)
        {
            if (SdMedia *m = session_->sd_media())
                m->set_write_protected(on);
        }
        int sd_status() const
        {
            SdCard *c = session_->sd();
            return c ? c->peek(SdCard::STATUS) : -1;
        }
        double sd_commands() const
        {
            SdCard *c = session_->sd();
            return c ? static_cast<double>(c->commands()) : 0;
        }

        SoundCpu sound_cpu() const
        {
            const SoundCard *c = session_->sound();
            if (!c)
                return SoundCpu{};
            const Registers &r = c->cpu().regs();
            return SoundCpu{true, r.pc, r.a, r.x, r.y, r.s, r.p, static_cast<unsigned>(c->cpu().state()),
                            static_cast<double>(c->cpu().cycles()), c->port_ddr(), c->port_data(), c->timer(),
                            c->timer_latch(), c->timer_running()};
        }

        int sound_voice(unsigned chip, unsigned voice) const
        {
            const SoundCard *c = session_->sound();
            if (!c)
                return -1;
            const Sid::Voice &v = c->chip(chip).voice(voice);
            return v.envelope | static_cast<int>(v.phase) << 8 | (v.frozen ? 1 << 10 : 0);
        }

        void set_audio_rate(double rate) { session_->set_audio_rate(static_cast<uint32_t>(rate)); }
        /// The sound card's samples since the last call, left and right interleaved: a view
        /// into the module's memory, good until the next call.
        val take_audio()
        {
            const size_t frames = session_->take_audio(audio_.data(), audio_.size() / 2);
            return val(emscripten::typed_memory_view(2 * frames, audio_.data()));
        }

        /// @return false, with error()
        bool load_sid(const std::string &bytes, unsigned song)
        {
            const std::vector<uint8_t> data(bytes.begin(), bytes.end());
            return session_->play_sid(data, song, error_);
        }
        bool sid_song(unsigned song) { return session_->play_song(song, error_); }
        void stop_sid() { session_->stop_sid(); }
        SidInfo sid_info() const
        {
            const SidFile *f = session_->sid();
            if (!f)
                return SidInfo{};
            std::string warnings;
            for (const auto &w : session_->sid_warnings())
                warnings += (warnings.empty() ? "" : "\n") + w;
            return SidInfo{true, f->name, f->author, f->released, warnings, f->songs, session_->sid_song(),
                           f->rsid, f->ntsc(), f->model() == Sid::Model::Mos8580};
        }
        /// The card's 6510 runs (CTRL RUN): a tune plays. False without the card.
        bool sound_running() const
        {
            const SoundCard *c = session_->sound();
            return c && (c->ctrl() & SoundCard::CTRL_RUN);
        }

        unsigned peek_io(unsigned offset) const { return session_->peek_io(static_cast<uint16_t>(offset & 0xFFF)); }

        bool has_usb() const { return session_->usb() != nullptr; }
        void usb_key(unsigned usage, bool down)
        {
            if (UsbCard *u = session_->usb())
                u->queue_key(static_cast<uint8_t>(usage), down); // at the keyboard's pace
        }
        void usb_mouse_to(int x, int y)
        {
            if (UsbCard *u = session_->usb())
                u->mouse_to(x, y);
        }
        void usb_mouse_move(int dx, int dy)
        {
            if (UsbCard *u = session_->usb())
                u->mouse_move(dx, dy);
        }
        void usb_mouse_buttons(unsigned buttons)
        {
            if (UsbCard *u = session_->usb())
                u->mouse_buttons(static_cast<uint8_t>(buttons));
        }
        void usb_mouse_wheel(int steps)
        {
            if (UsbCard *u = session_->usb())
                u->mouse_wheel(steps);
        }

        void set_trace(bool on, unsigned depth) { session_->set_trace(on, depth); }
        val trace(unsigned max)
        {
            rows_.resize(max);
            const size_t n = session_->trace(rows_.data(), rows_.size());
            packed_.resize(4 * n);
            for (size_t i = 0; i < n; ++i)
            {
                const Session::TraceRow &r = rows_[i];
                packed_[4 * i] = static_cast<uint32_t>(r.cycle);
                packed_[4 * i + 1] = static_cast<uint32_t>(r.cycle >> 32);
                packed_[4 * i + 2] = r.physical | static_cast<uint32_t>(r.module) << 24;
                packed_[4 * i + 3] = r.cpu | static_cast<uint32_t>(r.data) << 16 | static_cast<uint32_t>(r.flags) << 24;
            }
            return val(emscripten::typed_memory_view(packed_.size(), packed_.data()));
        }

    private:
        std::unique_ptr<Session> session_;
        std::string error_;
        double last_cycles_{0};
        std::array<float, LIGHT_COUNT> lights_{};
        std::vector<float> audio_ = std::vector<float>(2 * 24'000); ///< half a second at 48 kHz, as much as the card keeps
        std::vector<Session::TraceRow> rows_;
        std::vector<uint32_t> packed_;
        unsigned cartridges_{0};
        std::vector<std::string> loaded_; ///< the cartridge's files, which a power cycle reads again
    };
} // namespace

EMSCRIPTEN_BINDINGS(oo1)
{
    using namespace emscripten;
    value_object<Regs>("Regs")
        .field("pc", &Regs::pc)
        .field("a", &Regs::a)
        .field("x", &Regs::x)
        .field("y", &Regs::y)
        .field("s", &Regs::s)
        .field("p", &Regs::p);
    value_object<SidInfo>("SidInfo")
        .field("has", &SidInfo::has)
        .field("name", &SidInfo::name)
        .field("author", &SidInfo::author)
        .field("released", &SidInfo::released)
        .field("warnings", &SidInfo::warnings)
        .field("songs", &SidInfo::songs)
        .field("song", &SidInfo::song)
        .field("rsid", &SidInfo::rsid)
        .field("ntsc", &SidInfo::ntsc)
        .field("mos8580", &SidInfo::mos8580);
    value_object<SoundCpu>("SoundCpu")
        .field("has", &SoundCpu::has)
        .field("pc", &SoundCpu::pc)
        .field("a", &SoundCpu::a)
        .field("x", &SoundCpu::x)
        .field("y", &SoundCpu::y)
        .field("s", &SoundCpu::s)
        .field("p", &SoundCpu::p)
        .field("state", &SoundCpu::state)
        .field("cycles", &SoundCpu::cycles)
        .field("ddr", &SoundCpu::ddr)
        .field("port", &SoundCpu::port)
        .field("timer", &SoundCpu::timer)
        .field("latch", &SoundCpu::latch)
        .field("timerOn", &SoundCpu::timer_on);
    class_<WebMachine>("Machine")
        .constructor<>()
        .function("open", &WebMachine::open)
        .function("isOpen", &WebMachine::is_open)
        .function("error", &WebMachine::error)
        .function("run", &WebMachine::run)
        .function("lastCycles", &WebMachine::last_cycles)
        .function("powerCycle", &WebMachine::power_cycle)
        .function("hasCard", &WebMachine::has_card)
        .function("setSwitches", &WebMachine::set_switches)
        .function("setPanel", &WebMachine::set_panel)
        .function("setLock", &WebMachine::set_lock)
        .function("press", &WebMachine::press)
        .function("start", &WebMachine::start)
        .function("cardRead", &WebMachine::card_read)
        .function("cardWrite", &WebMachine::card_write)
        .function("send", &WebMachine::send)
        .function("takeOutput", &WebMachine::take_output)
        .function("regs", &WebMachine::regs)
        .function("cycles", &WebMachine::cycles)
        .function("halted", &WebMachine::halted)
        .function("stopped", &WebMachine::stopped)
        .function("irq", &WebMachine::irq)
        .function("peek", &WebMachine::peek)
        .function("peekPhysical", &WebMachine::peek_physical)
        .function("physical", &WebMachine::physical)
        .function("resolve", &WebMachine::resolve)
        .function("setAudioRate", &WebMachine::set_audio_rate)
        .function("loadSid", &WebMachine::load_sid)
        .function("sidSong", &WebMachine::sid_song)
        .function("stopSid", &WebMachine::stop_sid)
        .function("sidInfo", &WebMachine::sid_info)
        .function("soundRunning", &WebMachine::sound_running)
        .function("takeAudio", &WebMachine::take_audio)
        .function("lights", &WebMachine::lights)
        .function("setLights", &WebMachine::set_lights)
        .function("hasVideo", &WebMachine::has_video)
        .function("videoFrames", &WebMachine::video_frames)
        .function("videoFrame", &WebMachine::video_frame)
        .function("setClock", &WebMachine::set_clock)
        .function("fixedHz", &WebMachine::fixed_hz)
        .function("loadCartridge", &WebMachine::load_cartridge)
        .function("appleMode", &WebMachine::apple_mode)
        .function("appleSwitches", &WebMachine::apple_switches)
        .function("peekIo", &WebMachine::peek_io)
        .function("insertDisk", &WebMachine::insert_disk)
        .function("ejectDisk", &WebMachine::eject_disk)
        .function("diskInfo", &WebMachine::disk_info)
        .function("diskWrites", &WebMachine::disk_writes)
        .function("saveDisk", &WebMachine::save_disk)
        .function("setWriteProtect", &WebMachine::set_write_protect)
        .function("diskStatus", &WebMachine::disk_status)
        .function("diskQuarter", &WebMachine::disk_quarter)
        .function("hasSd", &WebMachine::has_sd)
        .function("insertSd", &WebMachine::insert_sd)
        .function("ejectSd", &WebMachine::eject_sd)
        .function("sdInfo", &WebMachine::sd_info)
        .function("sdSectors", &WebMachine::sd_sectors)
        .function("sdSave", &WebMachine::sd_save)
        .function("setSdWriteProtect", &WebMachine::set_sd_write_protect)
        .function("sdStatus", &WebMachine::sd_status)
        .function("sdCommands", &WebMachine::sd_commands)
        .function("soundCpu", &WebMachine::sound_cpu)
        .function("soundVoice", &WebMachine::sound_voice)
        .function("hasUsb", &WebMachine::has_usb)
        .function("usbKey", &WebMachine::usb_key)
        .function("usbMouseTo", &WebMachine::usb_mouse_to)
        .function("usbMouseMove", &WebMachine::usb_mouse_move)
        .function("usbMouseButtons", &WebMachine::usb_mouse_buttons)
        .function("usbMouseWheel", &WebMachine::usb_mouse_wheel)
        .function("setTrace", &WebMachine::set_trace)
        .function("trace", &WebMachine::trace);
}
