#pragma once

/**
 * @file memory_devices.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief RAM and ROM chips.
 * @version 0.3.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Both start zero-filled, as the flat Apple-1 memory did. A ROM ignores CPU
 * writes, which is what lets Applesoft find the top of RAM (Phase 1); its
 * contents arrive through poke() or load().
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "device.h"

namespace ooe::emu
{
    class Ram : public Device
    {
    public:
        explicit Ram(size_t size) : mem_(size, 0) {}

        size_t size() const override { return mem_.size(); }
        uint8_t read(uint32_t offset) override { return peek(offset); }
        uint8_t peek(uint32_t offset) const override { return offset < mem_.size() ? mem_[offset] : 0xFF; }
        void write(uint32_t offset, uint8_t data) override
        {
            if (offset < mem_.size())
                mem_[offset] = data;
        }

        /// The bytes, for a card that keeps a snooped copy of them (the video card).
        const uint8_t *data() const { return mem_.data(); }

    private:
        std::vector<uint8_t> mem_;
    };

    class Rom : public Device
    {
    public:
        explicit Rom(size_t size) : mem_(size, 0) {}

        size_t size() const override { return mem_.size(); }
        uint8_t read(uint32_t offset) override { return peek(offset); }
        uint8_t peek(uint32_t offset) const override { return offset < mem_.size() ? mem_[offset] : 0xFF; }
        void write(uint32_t, uint8_t) override {}
        void poke(uint32_t offset, uint8_t data) override
        {
            if (offset < mem_.size())
                mem_[offset] = data;
        }

        /**
         * @brief Fill from an image file, from offset 0
         * @return false (and log why) if it cannot be read or is larger than the ROM
         */
        bool load(const std::string &path);

    private:
        std::vector<uint8_t> mem_;
    };
} // namespace ooe::emu
