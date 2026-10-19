#pragma once

/**
 * @file machine.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A machine built from a plain-text description.
 * @version 0.3.0
 * @date 2026-10-18
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
 * device's IRQ output to the CPU. Types today: ram, rom (needs an image),
 * pia6821. Errors name the file and line.
 */

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "decoder.h"
#include "device.h"

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

    private:
        explicit Machine(const NorthBridge *bridge) : bus_(bridge) {}

        Decoder bus_;
        std::string cpu_{"nmos6502"};
        std::vector<std::unique_ptr<Device>> devices_;
        std::unordered_map<std::string, Device *> named_;
    };
} // namespace ooe::emu
