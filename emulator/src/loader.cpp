#include "loader.h"

#include <fstream>
#include <iterator>
#include <vector>

#include "fmt/core.h"

#include "logging.h"

namespace ooe
{
    long LoadImage(BusInterface *bus, const std::string &filename, uint16_t address)
    {
        std::ifstream in(filename, std::ios::binary);
        if (!in)
        {
            LOG(ERROR) << fmt::format("could not open program file: {}", filename);
            return -1;
        }

        // Read it all before writing anything, so a refusal leaves memory as it was.
        std::vector<char> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (address + image.size() > 0x10000)
        {
            LOG(ERROR) << fmt::format("{}: {} bytes do not fit at {:#06x}", filename, image.size(), address);
            return -1;
        }

        for (size_t i = 0; i < image.size(); i++)
        {
            bus->Poke((uint16_t)(address + i), (uint8_t)image[i]);
        }

        LOG(INFO) << fmt::format("loaded {} ({} bytes) at {:#06x}", filename, image.size(), address);
        return (long)image.size();
    }
} // namespace ooe
