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
 */

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "session.h"

namespace
{
    using namespace ooe::emu;
    using emscripten::val;

    constexpr size_t LIGHT_COUNT = ConsoleCard::ADDRESS_LIGHTS + ConsoleCard::DATA_LIGHTS + 4;

    struct Regs
    {
        unsigned pc, a, x, y, s, p;
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
        std::vector<Session::TraceRow> rows_;
        std::vector<uint32_t> packed_;
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
        .function("lights", &WebMachine::lights)
        .function("setLights", &WebMachine::set_lights)
        .function("setTrace", &WebMachine::set_trace)
        .function("trace", &WebMachine::trace);
}
