/**
 * @file loader.cpp
 * @brief Raw binary image loader.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 */

#include "loader.h"

#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

#include "emu_log.h"

namespace ooe::emu
{
    namespace
    {
        const char *TAG = "loader";

        int hex_digit(char c)
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            return -1;
        }
    } // namespace

    long load_image(Bus &bus, const std::string &path, uint16_t address)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            EMU_LOGE(TAG, "could not open %s", path.c_str());
            return -1;
        }

        const std::vector<char> image((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (address + image.size() > 0x10000)
        {
            EMU_LOGE(TAG, "%s: %zu bytes do not fit at $%04X", path.c_str(), image.size(), address);
            return -1;
        }

        for (size_t i = 0; i < image.size(); ++i)
            bus.poke(static_cast<uint16_t>(address + i), static_cast<uint8_t>(image[i]));

        EMU_LOGI(TAG, "loaded %s (%zu bytes) at $%04X", path.c_str(), image.size(), address);
        return static_cast<long>(image.size());
    }

    long load_hex(Bus &bus, const std::string &path, std::string &error)
    {
        std::ifstream in(path);
        if (!in)
        {
            error = path + ": cannot open";
            return -1;
        }

        std::vector<std::pair<uint16_t, uint8_t>> bytes; // all of it, before any poke
        uint32_t base = 0;
        bool eof = false;
        std::string line;
        for (int n = 1; std::getline(in, line); ++n)
        {
            auto fail = [&](const char *why) {
                error = path + ":" + std::to_string(n) + ": " + why;
                return -1L;
            };
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (line.empty())
                continue;
            if (eof)
                return fail("data after the end-of-file record");
            if (line[0] != ':' || line.size() < 11 || line.size() % 2 == 0)
                return fail("not an Intel HEX record");

            std::vector<uint8_t> rec;
            for (size_t i = 1; i < line.size(); i += 2)
            {
                const int hi = hex_digit(line[i]), lo = hex_digit(line[i + 1]);
                if (hi < 0 || lo < 0)
                    return fail("not a hex digit");
                rec.push_back(static_cast<uint8_t>(hi << 4 | lo));
            }
            unsigned sum = 0;
            for (const uint8_t b : rec)
                sum += b;
            const size_t count = rec[0];
            if (rec.size() != count + 5)
                return fail("byte count does not match the record length");
            if ((sum & 0xFF) != 0)
                return fail("bad checksum");

            const uint32_t offset = static_cast<uint32_t>(rec[1] << 8 | rec[2]);
            const uint8_t *data = rec.data() + 4;
            switch (rec[3])
            {
            case 0x00:
                if (base + offset + count > 0x10000)
                    return fail("data past $FFFF");
                for (size_t i = 0; i < count; ++i)
                    bytes.emplace_back(static_cast<uint16_t>(base + offset + i), data[i]);
                break;
            case 0x01:
                eof = true;
                break;
            case 0x02:
            case 0x04:
                if (count != 2)
                    return fail("extended address record needs 2 bytes");
                base = static_cast<uint32_t>(data[0] << 8 | data[1]) << (rec[3] == 0x02 ? 4 : 16);
                break;
            case 0x03:
            case 0x05:
                break; // start address: the reset vector decides
            default:
                return fail("unknown record type");
            }
        }
        if (!eof)
        {
            error = path + ": no end-of-file record (truncated?)";
            return -1;
        }

        for (const auto &[addr, value] : bytes)
            bus.poke(addr, value);
        EMU_LOGI(TAG, "loaded %s (%zu bytes)", path.c_str(), bytes.size());
        return static_cast<long>(bytes.size());
    }
} // namespace ooe::emu
