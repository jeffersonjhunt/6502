#pragma once

/**
 * @file machine.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A machine built from a plain-text description.
 * @version 0.6.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * One line per chip, so a memory map can be read, diffed and commented
 * like the schematic it mirrors (see machines/apple1.machine):
 *
 *   cpu      nmos6502                (or w65c02s; nmos6502 if omitted)
 *   ram      0000-7FFF
 *   rom      8000-FFFF               ibasic.rom
 *   pia6821  D010-D013  name=terminal
 *
 * `<type> <first>-<last> [name=ID] [module=0-F] [irq] [image]`. Addresses
 * are physical (A0-A19) hex, optionally with a `$`. `#` starts a comment.
 * `module=` tags the mapping with a backplane module ID; `irq` wires the
 * device's IRQ output to the CPU. Chips: ram, rom (needs an image),
 * pia6821, via6522, acia6551. A mapping larger than the chip mirrors it
 * (a 6522 at 6000-7FFF repeats every 16 bytes, as on Eater's board).
 *
 * Peripherals hang off a named 6522's pins instead of the bus, so they
 * take no range:
 *
 *   hd44780  name=lcd  via=via  data=PB0-3  rs=PB4  rw=PB5  e=PB6  [size=16x2]
 *   ps2kbd   name=keyboard  via=via  data=PA0-7        (strobes CA1)
 *
 * `data=` is 4 or 8 consecutive pins of one port (8 for ps2kbd). Errors
 * name the file and line.
 */

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "decoder.h"
#include "device.h"
#include "peripheral.h"

namespace ooe::emu
{
    struct MachineOptions
    {
        /// If set, replaces the image of the first `rom` line.
        std::string rom_override;
        /// North bridge for the decoder; nullptr = A16-A19 low. Not owned.
        const NorthBridge *bridge{nullptr};
    };

    class Machine
    {
    public:
        /**
         * @brief Parse `text` and build the machine, loading ROM images
         * @param origin name for error messages (usually the file name)
         * @return nullptr with `error` set ("origin:line: what") on failure
         */
        static std::unique_ptr<Machine> build(const std::string &text, const std::string &origin,
                                              const MachineOptions &options, std::string &error);

        /// build() from a file.
        static std::unique_ptr<Machine> load(const std::string &path, const MachineOptions &options,
                                             std::string &error);

        Decoder &bus() { return bus_; }
        const std::string &cpu() const { return cpu_; }

        /// The device given `name=` in the description, or nullptr.
        Device *find(const std::string &name) const;

        template <typename T>
        T *find_as(const std::string &name) const
        {
            return dynamic_cast<T *>(find(name));
        }

        /// The peripheral given `name=`, or nullptr.
        template <typename T>
        T *find_peripheral(const std::string &name) const
        {
            const auto it = named_peripherals_.find(name);
            return it == named_peripherals_.end() ? nullptr : dynamic_cast<T *>(it->second);
        }

    private:
        explicit Machine(const NorthBridge *bridge) : bus_(bridge) {}

        Decoder bus_;
        std::string cpu_{"nmos6502"};
        std::vector<std::unique_ptr<Device>> devices_;
        std::unordered_map<std::string, Device *> named_;
        std::vector<std::unique_ptr<Peripheral>> peripherals_;
        std::unordered_map<std::string, Peripheral *> named_peripherals_;
    };
} // namespace ooe::emu
