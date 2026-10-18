#pragma once

#include <cstdint>
#include <string>

#include "bus.h"

namespace ooe
{
    /**
     * @brief Copy a raw binary image onto the bus at `address`
     *
     * Writes with Poke, so it can fill read-only ROM. An image that would run
     * past $FFFF is refused outright rather than wrapped into zero page or
     * loaded in part.
     *
     * @return bytes loaded, or -1 if the file cannot be read or does not fit
     */
    long LoadImage(BusInterface *bus, const std::string &filename, uint16_t address);
} // namespace ooe
