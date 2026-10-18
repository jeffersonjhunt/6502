#pragma once

/**
 * @file loader.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Raw binary images onto a bus.
 * @version 0.2.0
 * @date 2026-10-18
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Writes with poke(), so it can fill ROM. The whole file is read before
 * anything is written, so a refusal leaves memory untouched; an image that
 * would run past $FFFF is refused rather than wrapped into zero page.
 */

#include <cstdint>
#include <string>

#include "bus.h"

namespace ooe::emu
{
    /// @return bytes loaded, or -1 if the file cannot be read or does not fit
    long load_image(Bus &bus, const std::string &path, uint16_t address);
} // namespace ooe::emu
