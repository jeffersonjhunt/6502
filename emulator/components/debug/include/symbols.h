#pragma once

/**
 * @file symbols.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Labels from ld65's -Ln output, for the debugger and disassembler.
 * @version 0.5.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * ld65 -Ln writes VICE's label format, one `al <hex address> .<name>` per
 * line. Without debug info that is only the exported symbols; assemble with
 * `ca65 -g` to get every label. Exported names appear twice in that case,
 * which is harmless here.
 *
 * Where several names share an address, name_at() gives the first one
 * loaded. Lookups by name are case sensitive, as in ca65.
 */

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace ooe::emu
{
    class Symbols
    {
    public:
        /**
         * @brief Add every label in an ld65 -Ln file
         * @return false, with `error` as "path:line: why", on the first bad
         *         line; labels before it are kept
         */
        bool load(const std::string &path, std::string &error);

        /// Add one label. A name already present keeps its first address.
        void add(const std::string &name, uint16_t addr);

        /// @return true and set `addr` if `name` is a label
        bool find(const std::string &name, uint16_t &addr) const;
        /// The label at `addr`, or nullptr.
        const std::string *name_at(uint16_t addr) const;

        size_t size() const { return by_name_.size(); }

    private:
        std::map<std::string, uint16_t> by_name_;
        std::map<uint16_t, std::string> by_addr_;
    };
} // namespace ooe::emu
