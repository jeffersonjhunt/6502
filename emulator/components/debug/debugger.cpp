/**
 * @file debugger.cpp
 * @brief Debugger commands.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "debugger.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <vector>

#include "disasm.h"

namespace ooe::emu
{
    namespace
    {
        /// Steps up to this many are shown one instruction per line.
        constexpr unsigned SHOW_EACH_STEP = 32;

        const char HELP[] = "r  run                 leave the debugger and run\n"
                            "s  step [N]            execute N instructions (default 1)\n"
                            "f  fetch ADDR [N]      show N bytes (default 16); no side effects\n"
                            "st store ADDR BYTE...  write bytes as the CPU would (ROM stays read-only)\n"
                            "x  regs [R=V ...]      show registers, or set A X Y S P PC\n"
                            "l  list [ADDR] [N]     disassemble N instructions (default 8) from ADDR (default PC)\n"
                            "b  break [ADDR]        set a breakpoint, or list them\n"
                            "d  delete ADDR|all     remove breakpoints\n"
                            "t  trace on|off        pause or resume the bus trace\n"
                            "   reset               run the CPU's reset sequence (devices keep their state)\n"
                            "q  quit                end the emulator\n"
                            "Addresses and values are hex ($ optional) or labels; `$` forces hex. Counts are decimal.\n";

        std::vector<std::string> words(const std::string &line)
        {
            std::istringstream in(line);
            std::vector<std::string> w;
            for (std::string s; in >> s;)
                w.push_back(s);
            return w;
        }

        bool parse_hex(std::string s, unsigned max, unsigned &value)
        {
            if (!s.empty() && s[0] == '$')
                s.erase(0, 1);
            if (s.empty() || s.size() > 4)
                return false;
            for (const char c : s)
            {
                if (!std::isxdigit(static_cast<unsigned char>(c)))
                    return false;
            }
            value = static_cast<unsigned>(std::strtoul(s.c_str(), nullptr, 16));
            return value <= max;
        }

        bool parse_count(const std::string &s, unsigned &n)
        {
            char *end = nullptr;
            const unsigned long v = std::strtoul(s.c_str(), &end, 10);
            if (s.empty() || *end != '\0' || v == 0 || v > 1'000'000'000ul)
                return false;
            n = static_cast<unsigned>(v);
            return true;
        }

        std::string format(const char *fmt, unsigned a, unsigned b = 0)
        {
            char buf[128];
            std::snprintf(buf, sizeof buf, fmt, a, b);
            return buf;
        }
    } // namespace

    bool Debugger::parse_address(const std::string &s, uint16_t &addr) const
    {
        // A label wins over hex, so `ADD` can name a routine; `$ADD` is hex.
        if (!s.empty() && s[0] != '$' && symbols_.find(s, addr))
            return true;
        unsigned v = 0;
        if (!parse_hex(s, 0xFFFF, v))
            return false;
        addr = static_cast<uint16_t>(v);
        return true;
    }

    bool Debugger::add_breakpoint(uint16_t addr)
    {
        const bool added = !breaks_[addr];
        breaks_[addr] = true;
        return added;
    }

    std::string Debugger::halt(const std::string &why)
    {
        halted_ = true;
        return "halted (" + why + ")\n" + status();
    }

    std::string Debugger::line_at(uint16_t pc, unsigned *length) const
    {
        const Disassembly d = disassemble(cpu_.model(), bus_, pc, &symbols_);
        std::string bytes;
        for (unsigned i = 0; i < d.length; ++i)
            bytes += format("%02X ", bus_.peek(static_cast<uint16_t>(pc + i)));
        std::string label;
        if (const std::string *name = symbols_.name_at(pc))
            label = *name + ": ";
        if (length)
            *length = d.length;
        char buf[160];
        std::snprintf(buf, sizeof buf, "%04X  %-9s %s%s\n", pc, bytes.c_str(), label.c_str(), d.text.c_str());
        return buf;
    }

    std::string Debugger::status() const
    {
        const Registers &r = cpu_.regs();
        static const char NAMES[] = "NV-BDIZC";
        std::string flags;
        for (int bit = 7; bit >= 0; --bit)
        {
            const char c = NAMES[7 - bit];
            flags += (c == '-') ? '-' : (r.p & (1u << bit)) ? c : static_cast<char>(std::tolower(c));
        }
        char buf[160];
        std::snprintf(buf, sizeof buf, "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X %s cycles=%llu%s\n", r.pc, r.a, r.x,
                      r.y, r.s, r.p, flags.c_str(), static_cast<unsigned long long>(cpu_.cycles()),
                      cpu_.state() == Cpu6502::State::Waiting   ? " (waiting: WAI)"
                      : cpu_.state() == Cpu6502::State::Stopped ? " (stopped: STP)"
                                                                : "");
        return buf + line_at(r.pc);
    }

    std::string Debugger::step(unsigned count)
    {
        std::string out;
        for (unsigned i = 0; i < count; ++i)
        {
            const uint16_t pc = cpu_.regs().pc;
            if (i > 0 && breaks_[pc])
            {
                out += format("break at %04X after %u instruction(s)\n", pc, i);
                break;
            }
            if (count <= SHOW_EACH_STEP)
                out += line_at(pc);
            cpu_.step();
            if (bus_.irq())
                cpu_.irq();
        }
        return out + status();
    }

    std::string Debugger::fetch(uint16_t addr, unsigned count) const
    {
        std::string out;
        for (unsigned row = 0; row < count; row += 16)
        {
            const auto base = static_cast<uint16_t>(addr + row);
            std::string hex, text;
            for (unsigned i = 0; i < 16 && row + i < count; ++i)
            {
                const uint8_t v = bus_.peek(static_cast<uint16_t>(base + i));
                hex += format("%02X ", v);
                const auto c = static_cast<char>(v & 0x7F); // Apple-1 text has bit 7 set
                text += std::isprint(static_cast<unsigned char>(c)) ? c : '.';
            }
            char buf[128];
            std::snprintf(buf, sizeof buf, "%04X  %-48s %s\n", base, hex.c_str(), text.c_str());
            out += buf;
        }
        return out;
    }

    std::string Debugger::list(uint16_t addr, unsigned count) const
    {
        std::string out;
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned length = 1;
            out += line_at(addr, &length);
            addr = static_cast<uint16_t>(addr + length);
        }
        return out;
    }

    std::string Debugger::set_registers(const std::vector<std::string> &settings)
    {
        Registers r = cpu_.regs();
        for (const std::string &w : settings)
        {
            const size_t eq = w.find('=');
            std::string name = w.substr(0, eq);
            for (char &c : name)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            const std::string value = eq == std::string::npos ? "" : w.substr(eq + 1);
            unsigned v = 0;
            uint16_t addr = 0;
            if (name == "pc" && parse_address(value, addr))
                r.pc = addr;
            else if (name != "pc" && parse_hex(value, 0xFF, v) &&
                     (name == "a" || name == "x" || name == "y" || name == "s" || name == "p"))
            {
                const auto b = static_cast<uint8_t>(v);
                (name == "a" ? r.a : name == "x" ? r.x : name == "y" ? r.y : name == "s" ? r.s : r.p) = b;
            }
            else
                return "bad register setting '" + w + "' (want e.g. A=12 or PC=FF00)\n";
        }
        r.p = static_cast<uint8_t>((r.p | Cpu6502::FLAG_U) & ~Cpu6502::FLAG_B); // as the live register reads
        cpu_.set_regs(r);
        return status();
    }

    std::string Debugger::breakpoints() const
    {
        std::string out;
        for (unsigned a = 0; a < 0x10000; ++a)
        {
            if (!breaks_[a])
                continue;
            out += format("%04X", a);
            if (const std::string *name = symbols_.name_at(static_cast<uint16_t>(a)))
                out += "  " + *name;
            out += "\n";
        }
        return out.empty() ? "no breakpoints\n" : out;
    }

    std::string Debugger::command(const std::string &line)
    {
        const std::vector<std::string> w = words(line);
        if (w.empty())
            return "";
        const std::string &cmd = w[0];
        auto arg_address = [&](size_t i, uint16_t &addr) { return i < w.size() && parse_address(w[i], addr); };
        auto arg_count = [&](size_t i, unsigned fallback, unsigned &n) {
            n = fallback;
            return i >= w.size() || parse_count(w[i], n);
        };
        const std::string bad = "bad arguments; 'h' for help\n";

        if (cmd == "h" || cmd == "help" || cmd == "?")
            return HELP;
        if (cmd == "r" || cmd == "run")
        {
            halted_ = false;
            skip_once_ = true;
            return "running\n";
        }
        if (cmd == "q" || cmd == "quit")
        {
            quit_ = true;
            return "";
        }
        if (cmd == "s" || cmd == "step")
        {
            unsigned n = 1;
            return w.size() <= 2 && arg_count(1, 1, n) ? step(n) : bad;
        }
        if (cmd == "x" || cmd == "regs")
            return w.size() == 1 ? status() : set_registers({w.begin() + 1, w.end()});
        if (cmd == "f" || cmd == "fetch")
        {
            uint16_t addr = 0;
            unsigned n = 16;
            return w.size() <= 3 && arg_address(1, addr) && arg_count(2, 16, n) && n <= 0x10000 ? fetch(addr, n) : bad;
        }
        if (cmd == "st" || cmd == "store")
        {
            uint16_t addr = 0;
            if (w.size() < 3 || !arg_address(1, addr))
                return bad;
            std::vector<uint8_t> bytes;
            for (size_t i = 2; i < w.size(); ++i)
            {
                unsigned v = 0;
                if (!parse_hex(w[i], 0xFF, v))
                    return "bad byte '" + w[i] + "'\n";
                bytes.push_back(static_cast<uint8_t>(v));
            }
            for (size_t i = 0; i < bytes.size(); ++i)
                bus_.write(static_cast<uint16_t>(addr + i), bytes[i]);
            return fetch(addr, static_cast<unsigned>(bytes.size()));
        }
        if (cmd == "l" || cmd == "list")
        {
            uint16_t addr = cpu_.regs().pc;
            unsigned n = 8;
            if (w.size() > 3 || (w.size() > 1 && !arg_address(1, addr)) || !arg_count(2, 8, n) || n > 10000)
                return bad;
            return list(addr, n);
        }
        if (cmd == "b" || cmd == "break")
        {
            if (w.size() == 1)
                return breakpoints();
            uint16_t addr = 0;
            if (w.size() != 2 || !arg_address(1, addr))
                return bad;
            add_breakpoint(addr);
            return format("break at %04X\n", addr);
        }
        if (cmd == "d" || cmd == "delete")
        {
            if (w.size() == 2 && w[1] == "all")
            {
                breaks_.reset();
                return "no breakpoints\n";
            }
            uint16_t addr = 0;
            if (w.size() != 2 || !arg_address(1, addr))
                return bad;
            if (!breaks_[addr])
                return format("no breakpoint at %04X\n", addr);
            breaks_[addr] = false;
            return format("deleted %04X\n", addr);
        }
        if (cmd == "t" || cmd == "trace")
        {
            if (!trace_)
                return "no bus trace (start with --trace FILE)\n";
            if (w.size() == 2 && (w[1] == "on" || w[1] == "off"))
                trace_->set_enabled(w[1] == "on");
            else if (w.size() != 1)
                return bad;
            return std::string("trace ") + (trace_->enabled() ? "on" : "off") + ", " +
                   std::to_string(trace_->lines()) + " cycles written\n";
        }
        if (cmd == "reset")
        {
            bus_.reset();
            cpu_.reset();
            return status();
        }
        return "unknown command '" + cmd + "'; 'h' for help\n";
    }
} // namespace ooe::emu
