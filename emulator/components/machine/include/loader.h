#pragma once

/**
 * @file loader.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief Raw binary and Intel HEX images onto a bus.
 * @version 0.5.0
 * @date 2026-10-19
 *
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Writes with poke(), so it can fill ROM. The whole file is read before
 * anything is written, so a refusal leaves memory untouched; an image that
 * would run past $FFFF is refused rather than wrapped into zero page.
 *
 * Intel HEX follows the same rules. Every record's checksum is checked and
 * the file must end with an EOF record, so a truncated download is refused
 * rather than half-loaded. Extended segment (02) and linear (04) addresses
 * are accepted as long as the data still lands below $10000: the bus is the
 * CPU's 64 KB, and where A16-A19 go is the north bridge's business. Start
 * address records (03, 05) are ignored; the reset vector says where to start.
 */

#include <cstdint>
#include <string>

#include "bus.h"

namespace ooe::emu
{
    /// @return bytes loaded, or -1 if the file cannot be read or does not fit
    long load_image(Bus &bus, const std::string &path, uint16_t address);

    /// @return bytes loaded, or -1 with `error` as "path:line: why"
    long load_hex(Bus &bus, const std::string &path, std::string &error);
} // namespace ooe::emu
