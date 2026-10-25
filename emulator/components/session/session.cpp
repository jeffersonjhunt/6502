/**
 * @file session.cpp
 * @brief A machine driven a budget of cycles at a time.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "session.h"

#include <algorithm>

namespace ooe::emu
{
    namespace
    {
        constexpr uint64_t SERVICE_CYCLES = 64; ///< as the app's slice: 64 us at 1 MHz

        bool parse_hex16(const std::string &s, uint16_t &v)
        {
            const size_t start = !s.empty() && s[0] == '$' ? 1 : 0;
            if (s.size() == start || s.size() - start > 4)
                return false;
            unsigned n = 0;
            for (size_t i = start; i < s.size(); ++i)
            {
                const char c = s[i];
                const int d = c >= '0' && c <= '9'   ? c - '0'
                              : c >= 'A' && c <= 'F' ? c - 'A' + 10
                              : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                                     : -1;
                if (d < 0)
                    return false;
                n = n << 4 | static_cast<unsigned>(d);
            }
            v = static_cast<uint16_t>(n);
            return true;
        }
    } // namespace

    /// The trace ring: the newest `depth` cycles.
    class Session::Recorder : public CycleObserver
    {
    public:
        void attach(const Decoder *decoder) { decoder_ = decoder; }
        void set(bool on, size_t depth)
        {
            on_ = on;
            rows_.assign(on ? std::max<size_t>(depth, 1) : 0, TraceRow{});
            next_ = 0;
            count_ = 0;
        }
        bool on() const { return on_; }

        void on_cycle(const BusCycle &c) override
        {
            if (!on_)
                return;
            const Access &a = decoder_->last_access();
            rows_[next_] = TraceRow{c.cycle,
                                    a.physical,
                                    c.addr,
                                    c.data,
                                    a.module,
                                    static_cast<uint8_t>((c.write ? TRACE_WRITE : 0) | (c.sync ? TRACE_SYNC : 0) |
                                                         (c.vector_pull ? TRACE_VPB : 0))};
            next_ = (next_ + 1) % rows_.size();
            count_ = std::min(count_ + 1, rows_.size());
        }

        size_t copy(TraceRow *out, size_t max) const
        {
            const size_t n = std::min(max, count_);
            const size_t first = (next_ + rows_.size() - n) % (rows_.empty() ? 1 : rows_.size());
            for (size_t i = 0; i < n; ++i)
                out[i] = rows_[(first + i) % rows_.size()];
            return n;
        }

    private:
        const Decoder *decoder_{nullptr};
        bool on_{false};
        std::vector<TraceRow> rows_;
        size_t next_{0};
        size_t count_{0};
    };

    Session::~Session()
    {
        if (cpu_)
            cpu_->set_observer(nullptr);
    }

    std::unique_ptr<Session> Session::create(const Config &config, std::string &error)
    {
        std::unique_ptr<Session> s(new Session());
        s->config_ = config;
        s->recorder_ = std::make_unique<Recorder>();
        if (!config.symbols.empty() && !s->symbols_.load(config.symbols, error))
            return nullptr;
        if (!s->build(error))
            return nullptr;
        return s;
    }

    bool Session::build(std::string &error)
    {
        MachineOptions options;
        options.rom_override = config_.rom;
        options.apple_rom = config_.apple_rom;
        options.apple_chars = config_.apple_chars;
        options.disk2_boot = config_.disk2_boot;
        options.disk2_sequencer = config_.disk2_sequencer;
        std::unique_ptr<Machine> machine = Machine::load(config_.machine, options, error);
        if (!machine)
            return false;
        std::unique_ptr<Console> console = make_console(*machine, error);
        if (!console)
            return false;
        const auto model = machine->cpu() == "w65c02s" ? Cpu6502::Model::W65C02S : Cpu6502::Model::Nmos6502;
        auto cpu = std::make_unique<Cpu6502>(machine->bus(), model);

        // Everything built: replace the old machine.
        if (cpu_)
            cpu_->set_observer(nullptr);
        machine_ = std::move(machine);
        console_ = std::move(console);
        cpu_ = std::move(cpu);
        card_ = machine_->console_card();
        clock_cards();
        if (SoundCard *s = machine_->sound_card())
            s->set_audio_rate(audio_rate_);
        if (Disk2Card *d = machine_->disk2_card()) // the disks are still in their drives
            for (unsigned i = 0; i < Disk2Card::DRIVES; ++i)
                d->insert(i, disks_[i]);

        recorder_->attach(&machine_->bus());
        observers_ = CycleTee{};
        observers_.add(recorder_.get());
        observers_.add(card_);
        cpu_->set_observer(&observers_);

        if (card_) // the panel's switches stay where they were
        {
            card_->set_switches(switches_);
            card_->set_halt_switch(halt_switch_);
            card_->set_bus_cycle_switch(bus_cycle_switch_);
            card_->set_lock_switch(lock_);
            card_->set_lights(lights_on_);
        }
        input_.clear();
        input_next_ = 0;
        reset();
        return true;
    }

    void Session::reset()
    {
        machine_->bus().reset();
        cpu_->reset();
        next_service_ = cpu_->cycles() + SERVICE_CYCLES;
    }

    bool Session::power_cycle(std::string &error) { return build(error); }

    bool Session::set_cartridge(const std::vector<std::string> &apple_rom, const std::string &apple_chars,
                                std::string &error)
    {
        return set_cartridge(apple_rom, apple_chars, "", "", error);
    }

    bool Session::set_cartridge(const std::vector<std::string> &apple_rom, const std::string &apple_chars,
                                const std::string &disk2_boot, const std::string &disk2_sequencer, std::string &error)
    {
        const Config old = config_;
        config_.apple_rom = apple_rom;
        config_.apple_chars = apple_chars;
        config_.disk2_boot = disk2_boot;
        config_.disk2_sequencer = disk2_sequencer;
        if (build(error))
            return true;
        config_ = old; // build() kept the old machine
        return false;
    }

    bool Session::insert_disk(unsigned drive, const std::vector<uint8_t> &bytes, const std::string &name,
                              std::string &error)
    {
        if (!machine_->disk2_card())
        {
            error = "this machine has no Disk II card";
            return false;
        }
        std::unique_ptr<DiskImage> disk = DiskImage::load(bytes, name, error);
        if (!disk)
            return false;
        disks_[drive % Disk2Card::DRIVES] = std::move(disk);
        machine_->disk2_card()->insert(drive, disks_[drive % Disk2Card::DRIVES]);
        return true;
    }

    void Session::eject_disk(unsigned drive)
    {
        disks_[drive % Disk2Card::DRIVES].reset();
        if (Disk2Card *d = machine_->disk2_card())
            d->insert(drive, nullptr);
    }

    void Session::set_cpu_hz(uint32_t hz)
    {
        cpu_hz_ = hz;
        clock_cards();
    }

    void Session::set_audio_rate(uint32_t rate)
    {
        audio_rate_ = rate;
        if (SoundCard *s = machine_->sound_card())
            s->set_audio_rate(rate);
    }

    size_t Session::take_audio(float *out, size_t max_frames)
    {
        SoundCard *s = machine_->sound_card();
        return s ? s->take_audio(out, max_frames) : 0;
    }

    void Session::clock_cards()
    {
        // The mode's clock if it fixes one (Apple II mode), else the caller's.
        const uint32_t hz = machine_->bus().clock_hz() ? machine_->bus().clock_hz() : cpu_hz_;
        if (VideoCard *video = machine_->video_card())
            video->set_cpu_hz(hz);
        if (UsbCard *usb = machine_->usb_card())
            usb->set_cpu_hz(hz);
        if (SoundCard *sound = machine_->sound_card())
            sound->set_cpu_hz(hz);
    }

    Session::RunResult Session::run(uint64_t budget)
    {
        RunResult r;
        const uint64_t begin = cpu_->cycles();
        const uint64_t end = begin + budget;
        Decoder &bus = machine_->bus();
        while (cpu_->cycles() < end)
        {
            if (card_ && card_->take_start())
                reset(); // START: through the north bridge, as the reset button
            if (stopped())
            {
                r.why = Stop::Stopped;
                break;
            }
            if (card_ && !card_->may_run(cpu_->regs().pc))
            {
                r.why = Stop::Halted;
                break;
            }
            cpu_->step();
            // Level-sensitive, sampled after every instruction, as the app does.
            if (bus.irq())
                cpu_->irq();
            UsbCard *usb = machine_->usb_card();
            if (machine_->bus().take_reset() || (usb && usb->take_reset())) // entering Apple II mode, Ctrl-Reset
            {
                machine_->bus().pulse_reset();
                cpu_->reset();
                clock_cards(); // and its clock
            }
            if (card_ && card_->take_nmi())
                cpu_->nmi();
            if (usb && usb->take_nmi()) // Pause/Break
                cpu_->nmi();
            if (cpu_->cycles() >= next_service_)
            {
                service();
                next_service_ = cpu_->cycles() + SERVICE_CYCLES;
            }
        }
        service(); // what the last stretch printed
        r.cycles = cpu_->cycles() - begin;
        return r;
    }

    void Session::service()
    {
        console_->service();
        uint8_t ch = 0;
        while (console_->output(ch))
            output_ += static_cast<char>(ch);
        if (input_next_ < input_.size() && console_->key(static_cast<uint8_t>(input_[input_next_])))
        {
            if (++input_next_ == input_.size())
            {
                input_.clear();
                input_next_ = 0;
            }
        }
    }

    void Session::send(const std::string &bytes) { input_ += bytes; }

    std::string Session::take_output()
    {
        std::string out;
        out.swap(output_);
        return out;
    }

    void Session::set_switches(uint32_t swr)
    {
        switches_ = swr & (PHYSICAL_SPACE - 1);
        if (card_)
            card_->set_switches(switches_);
    }

    void Session::set_panel(bool halt, bool bus_cycle)
    {
        halt_switch_ = halt;
        bus_cycle_switch_ = bus_cycle;
        if (card_)
        {
            card_->set_halt_switch(halt);
            card_->set_bus_cycle_switch(bus_cycle);
        }
    }

    void Session::set_lock(bool locked)
    {
        lock_ = locked;
        if (card_)
            card_->set_lock_switch(locked);
    }

    void Session::press(uint8_t keys)
    {
        if (card_)
            card_->press(keys);
    }

    void Session::start()
    {
        if (card_)
            card_->press_start(); // taken by the next run(); LOCK may ignore it
        else
            reset();
    }

    uint8_t Session::card_read(uint8_t offset) const { return card_ ? card_->peek(offset) : Decoder::UNMAPPED_READ; }

    void Session::card_write(uint8_t offset, uint8_t data)
    {
        if (card_)
            card_->write(offset, data);
    }

    bool Session::resolve(const std::string &text, uint16_t &addr) const
    {
        if (!text.empty() && text[0] != '$' && symbols_.find(text, addr))
            return true;
        return parse_hex16(text, addr);
    }

    ConsoleCard::Lights Session::lights() { return card_ ? card_->lights() : ConsoleCard::Lights{}; }

    void Session::set_lights(bool on)
    {
        lights_on_ = on;
        if (card_)
            card_->set_lights(on);
    }

    void Session::set_trace(bool on, size_t depth) { recorder_->set(on, depth); }

    size_t Session::trace(TraceRow *out, size_t max) const { return recorder_->copy(out, max); }
} // namespace ooe::emu
