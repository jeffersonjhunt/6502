#pragma once

/**
 * @file session.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief One running machine, driven a budget of cycles at a time: the WebUI's core.
 * @version 0.1.0
 * @date 2026-10-23
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * A Session owns a machine built from a description, its CPU and its
 * console (the serial terminal), and runs it for a budget of cycles per
 * call. It never sleeps and never touches a terminal or a clock: the
 * caller paces it (the browser runs one budget per animation frame), which
 * is what makes the whole thing testable on the host and portable to
 * WebAssembly (apps/web) unchanged.
 *
 * The run loop is the app's (apps/emu/emulator.cpp) without the debugger:
 * before each instruction the Console card may stop it (a HALT, a
 * breakpoint), IRQ is sampled after each, the card's /NMI and START are
 * taken after each, and the console is serviced about every 64 cycles,
 * offering the next queued key until the machine takes it. A run stops at
 * the budget (overshooting by at most one instruction), at a halt, or at
 * STP, which ends the run at once rather than idling through the budget.
 *
 * The panel goes through the card exactly as hw/design.md describes it:
 * the switches and keys are the card's inputs, and card_read()/card_write()
 * are its SPI header, which is what the ESP32 bridge will reach. So the
 * WebUI's Run, Halt and Step are the panel's HALT switch and CONT. A
 * machine without a `console` line has no panel: those calls do nothing,
 * card_read() returns $FF, and only STP and the budget stop a run.
 *
 * Inspection never takes a bus cycle: registers, peek() (CPU address, with
 * the windows) and peek_physical() (A0-A19), the lamps, and a ring of the
 * latest bus cycles in the bus trace's terms (docs/BUS_TRACE.md), recorded
 * only while turned on.
 */

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bus_cycle.h"
#include "console.h"
#include "console_card.h"
#include "cpu6502.h"
#include "machine.h"
#include "sid_file.h"
#include "symbols.h"

namespace ooe::emu
{
    class Session
    {
    public:
        struct Config
        {
            std::string machine;  ///< machine description file
            std::string rom;      ///< replaces the first rom line's image; empty keeps it
            std::string symbols;  ///< an ld65 -Ln label file, or empty
            /// The owner's Apple IIe ROM for the cartridge (MachineOptions::apple_rom), or none.
            std::vector<std::string> apple_rom{};
            std::string apple_chars{}; ///< and its character ROM, or empty
            /// The Disk II card's PROMs (MachineOptions::disk2_boot, disk2_sequencer), or empty.
            std::string disk2_boot{}, disk2_sequencer{};
        };

        enum class Stop : uint8_t
        {
            Budget,  ///< ran the whole budget
            Halted,  ///< the Console card holds the CPU (STATUS says whether a breakpoint did it)
            Stopped, ///< STP: only START, a reset or a power cycle gets out
        };

        struct RunResult
        {
            uint64_t cycles{0}; ///< cycles actually run
            Stop why{Stop::Budget};
        };

        /// One bus cycle, as the bus trace writes it.
        struct TraceRow
        {
            uint64_t cycle{0};
            uint32_t physical{0}; ///< A0-A19
            uint16_t cpu{0};      ///< the CPU's address
            uint8_t data{0};
            uint8_t module{NO_MODULE};
            uint8_t flags{0};     ///< TRACE_WRITE, TRACE_SYNC, TRACE_VPB
        };
        static constexpr uint8_t TRACE_WRITE = 0x01, TRACE_SYNC = 0x02, TRACE_VPB = 0x04;

        /// Build the machine and reset it. @return nullptr with `error` on failure
        static std::unique_ptr<Session> create(const Config &config, std::string &error);

        ~Session();
        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;

        /// Run up to `budget` cycles.
        RunResult run(uint64_t budget);

        /// The keyswitch from OFF to RUN: a new machine from the same files
        /// (RAM forgets), the panel's switches as they were, a cold start.
        /// @return false, with `error`, if the files no longer build; the old machine stays
        bool power_cycle(std::string &error);
        /// Plug the owner's Apple IIe ROM into the cartridge (Config::apple_rom and
        /// apple_chars; both empty: take it out), as with the power off: a power cycle.
        /// @return false, with `error`, if it does not fit; the old machine and cartridge stay
        bool set_cartridge(const std::vector<std::string> &apple_rom, const std::string &apple_chars,
                           std::string &error);
        /// The same, with the Disk II card's PROMs: they come with the IIe ROM, and go with it.
        bool set_cartridge(const std::vector<std::string> &apple_rom, const std::string &apple_chars,
                           const std::string &disk2_boot, const std::string &disk2_sequencer, std::string &error);

        // The Disk II's drives (nothing without a disk2 line). A disk stays in
        // its drive through power cycles and cartridge changes, as a real one does.
        /// A disk image's bytes into drive 0 or 1 (DiskImage::load()). @return false with `error`
        bool insert_disk(unsigned drive, const std::vector<uint8_t> &bytes, const std::string &name, std::string &error);
        void eject_disk(unsigned drive);
        /// The disk in a drive, or nullptr.
        DiskImage *disk(unsigned drive) const { return disks_[drive % Disk2Card::DRIVES].get(); }
        Disk2Card *disk2() const { return machine_->disk2_card(); }

        // The mass storage card's slot (nothing without an sd line). The card stays in
        // through power cycles, as a real one does.
        /// A card image's bytes (whole 512-byte sectors) into the slot, held in memory.
        /// @return false with `error`: no card slot, not a sector
        bool insert_sd(std::vector<uint8_t> bytes, const std::string &name, std::string &error);
        /// Any media into the slot (a host file, say). @return false with `error`: no slot
        bool insert_sd(std::shared_ptr<SdMedia> media, std::string &error);
        void eject_sd();
        /// What is in the slot, or nullptr; the memory image insert_sd() made, or nullptr.
        SdMedia *sd_media() const { return sd_.get(); }
        MemoryMedia *sd_image() const { return dynamic_cast<MemoryMedia *>(sd_.get()); }
        SdCard *sd() const { return machine_->sd_card(); }
        /// The sound card, or nullptr: its registers, RAM and 6510.
        SoundCard *sound() const { return machine_->sound_card(); }
        /// The machine's sound at `rate` samples a second (0: none, the default), kept across
        /// power cycles: the sound card's two SIDs, left and right, and the USB card's speaker
        /// (the Apple's $C030) in both. Time is the CPU's: halted or stopped, it is silent too.
        void set_audio_rate(uint32_t rate);
        uint32_t audio_rate() const { return audio_rate_; }
        /// Up to `max_frames` stereo frames (left, right) made since the last call, the sources
        /// mixed. @return frames
        size_t take_audio(float *out, size_t max_frames);
        /// A `.sid` file's song on the sound card (0: its start song), started as a host
        /// program would start it (sid_file.h), though no program runs: the session writes
        /// the card's registers itself. @return false, with `error`: not a .sid, no card...
        bool play_sid(const std::vector<uint8_t> &bytes, unsigned song, std::string &error);
        /// Another song of the tune last played. @return false, with `error`
        bool play_song(unsigned song, std::string &error);
        /// Stop the tune: the card's 6510 stopped, the SIDs silent. The tune is forgotten
        /// (a power cycle forgets it too; START's /RESET stops the card without forgetting).
        void stop_sid();
        /// The tune last played (nullptr: none), its song, and what the card can't give it.
        const SidFile *sid() const { return sid_.get(); }
        unsigned sid_song() const { return sid_song_; }
        const std::vector<std::string> &sid_warnings() const { return sid_warnings_; }
        const std::vector<std::string> &cartridge() const { return config_.apple_rom; }

        // The panel: the card's inputs (nothing without a card)
        bool has_card() const { return card_ != nullptr; }
        void set_switches(uint32_t swr);
        void set_panel(bool halt, bool bus_cycle);
        void set_lock(bool locked);
        /// KEYS bits: ConsoleCard::LOAD_ADRS, EXAM, DEP, CONT.
        void press(uint8_t keys);
        /// START: the hard reset (without a card, a plain reset).
        void start();
        // The card's SPI header
        uint8_t card_read(uint8_t offset) const;
        void card_write(uint8_t offset, uint8_t data);

        // The serial terminal
        /// Queue keys for the machine; each is offered until taken.
        void send(const std::string &bytes);
        /// Everything the machine printed since the last call.
        std::string take_output();

        // Inspection
        const Registers &regs() const { return cpu_->regs(); }
        uint64_t cycles() const { return cpu_->cycles(); }
        bool halted() const { return card_ && card_->halted(); }
        bool stopped() const { return cpu_->state() == Cpu6502::State::Stopped; }
        /// The /IRQ line: some device with a wired interrupt is asserting it
        /// (the panel's IRQ lamp). Brings timed devices up to now, which
        /// changes nothing they would not do anyway.
        bool irq() { return machine_->bus().irq(); }
        uint8_t peek(uint16_t addr) const { return machine_->bus().peek(addr); }
        uint8_t peek_physical(uint32_t physical) const { return machine_->bus().peek_physical(physical); }
        /// What A0-A19 carry for a CPU address now (the windows as they are).
        uint32_t physical(uint16_t addr) const { return machine_->bus().physical(addr); }
        /// "C800", "$C800" or a label (a label wins over hex; `$` forces hex).
        bool resolve(const std::string &text, uint16_t &addr) const;
        const Symbols &symbols() const { return symbols_; }
        const std::string &machine_name() const { return config_.machine; }

        // The DS3231 on the CPU card (nothing without the backplane's bridge). Its battery
        // keeps it through power cycles, as a real one does.
        /// Set its time (the host's clock, say), `ms` into the second, and the bridge's SEC-YEAR with it.
        void set_rtc_time(const Ds3231::Time &t, unsigned ms = 0);
        /// Keep it with the host's clock, `host_ms` since 1970 on the same wall clock: call it
        /// as the machine runs. The clock counts emulated time, so a slow host (or a pause, or
        /// the power off) leaves it behind; once RTC_SYNC_MS out it steps by whole seconds to
        /// within half of one, its phase kept. A time someone set (the CPU, set_rtc_time()) is kept as
        /// its difference from the host's clock, so the clock keeps that time instead.
        void sync_rtc(int64_t host_ms);
        static constexpr int64_t RTC_SYNC_MS = 2000;
        /// The north bridge, which owns the clock, or nullptr.
        const BackplaneBridge *bridge() const { return machine_->backplane_bridge(); }

        /// The video card, if the machine has one: frame() is the screen.
        const VideoCard *video() const { return machine_->video_card(); }
        /// The USB card, if the machine has one: the keyboard, mouse and game port.
        UsbCard *usb() const { return machine_->usb_card(); }
        /// The clock the caller runs the CPU at, so the video and USB cards keep
        /// real time: 60 frames a second, key repeat and the paddles, at any
        /// speed (0: 7.16 MHz). Kept across power cycles.
        void set_cpu_hz(uint32_t hz);
        /// The clock the machine's mode fixes (Apple II mode's 1,020,484 Hz), or 0: the
        /// caller paces by it instead of its own choice. The cards follow it already.
        uint32_t fixed_hz() const { return machine_->bus().clock_hz(); }
        /// In Apple II mode (the north bridge's MODE).
        bool apple_mode() const
        {
            const BackplaneBridge *b = machine_->backplane_bridge();
            return b && b->apple();
        }
        /// Apple II mode's soft switches (as they stand, in either mode), or nullptr without the backplane's bridge.
        const AppleSwitches *apple_switches() const
        {
            const BackplaneBridge *b = machine_->backplane_bridge();
            return b ? &b->apple_switches() : nullptr;
        }
        /// A card's register in I/O space, with no bus cycle, whatever the map (Decoder::peek_io()).
        uint8_t peek_io(uint16_t offset) const { return machine_->bus().peek_io(offset); }

        /// Lamp duty since the last call (zeros without a card).
        ConsoleCard::Lights lights();
        void set_lights(bool on);

        /// Record the latest `depth` bus cycles (off: none, and no cost).
        void set_trace(bool on, size_t depth = 4096);
        /// Copy up to `max` of the newest rows, oldest first. @return rows copied
        size_t trace(TraceRow *out, size_t max) const;

    private:
        class Recorder;

        Session() = default;
        /// Build everything from config_. @return false with `error`
        bool build(std::string &error);
        void reset();
        /// Tell the cards the clock they keep time by: the mode's fixed one, or the caller's.
        void clock_cards();
        /// Move console bytes both ways.
        void service();

        Config config_;
        Symbols symbols_;
        std::unique_ptr<Machine> machine_;
        std::unique_ptr<Console> console_;
        std::unique_ptr<Cpu6502> cpu_;
        ConsoleCard *card_{nullptr};
        std::unique_ptr<Recorder> recorder_;
        CycleTee observers_;

        // The panel's physical state, kept across power cycles
        uint32_t switches_{0};
        uint32_t cpu_hz_{0};
        uint32_t audio_rate_{0};
        std::vector<float> sid_audio_, speaker_audio_; ///< made, not yet mixed: the sources end a frame or so apart
        bool halt_switch_{false}, bus_cycle_switch_{false}, lock_{false}, lights_on_{false};

        std::string input_;   ///< keys not yet taken
        size_t input_next_{0};
        std::string output_;
        uint64_t next_service_{0};
        std::array<std::shared_ptr<DiskImage>, Disk2Card::DRIVES> disks_{};
        std::shared_ptr<SdMedia> sd_;
        std::shared_ptr<Ds3231> rtc_; ///< the DS3231, once a machine has made one
        const Ds3231 *rtc_synced_{nullptr}; ///< sync_rtc()'s chip, its sets() then and its time less the host's
        uint32_t rtc_sets_{0};
        int64_t rtc_offset_ms_{0};
        std::unique_ptr<SidFile> sid_;
        unsigned sid_song_{0};
        std::vector<std::string> sid_warnings_;
    };
} // namespace ooe::emu
