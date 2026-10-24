#pragma once

/**
 * @file machine.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A machine built from a plain-text description.
 * @version 0.8.0
 * @date 2026-10-23
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
 * `<type> <first>-<last> [space=S] [name=ID] [module=0-F] [wait=N] [drq=N] [irq] [image]`;
 * a rom's image `-` is erased flash, all $FF (an empty cartridge).
 * Addresses are hex, optionally with a `$`, within the space: memory (the
 * default, A0-A19), flash (ROM A then ROM B) or io (the I/O hole, 000-7FF,
 * whose mappings take the module ID of their 128-byte block unless
 * `module=` says otherwise). `#` starts a comment. `module=` tags the
 * mapping with a backplane module ID; `wait=` (0-15) holds RDY for that
 * many extra cycles on every access, as a slow card does; `drq=` (0-3)
 * wires the device's DMA request to that channel; `irq` wires the device's
 * IRQ output to the CPU. Chips: ram, rom (needs an image), pia6821, via6522,
 * acia6551, dma (the DMA controller, dma_controller.h; at most one, and it
 * takes the bus from the CPU), console (the Console card, console_card.h;
 * at most one; the run loop wires it to the CPU, see console_card()), video
 * (the video card, video_card.h; at most one, and banks 8-B, 80000-BFFFF,
 * must be one ram line, which it draws from), usb (the USB card, usb_card.h;
 * at most one; the run loop takes its /NMI). A mapping larger than the chip mirrors it (a 6522 at 6000-7FFF
 * repeats every 16 bytes, as on Eater's board).
 *
 * Without a `northbridge` line every CPU address is memory with A16-A19
 * low. `northbridge backplane [bootsel]` fits the backplane's north bridge
 * (backplane_bridge.h): the windows, VEC, ROM space and the I/O hole, with
 * its registers at io 680-6FF (module D), named `northbridge`.
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

#include "console_card.h"
#include "decoder.h"
#include "device.h"
#include "peripheral.h"
#include "usb_card.h"
#include "video_card.h"

namespace ooe::emu
{
    struct MachineOptions
    {
        /// If set, replaces the image of the first `rom` line.
        std::string rom_override;
        /// An Apple IIe ROM for the cartridge (the rom line named `rom_b`), at its
        /// start: one 16 KB file ($C000-$FFFF), or the two 8 KB halves, CD then EF.
        /// Apple's ROMs are copyrighted: the owner supplies them.
        std::vector<std::string> apple_rom;
        /// The IIe's video character ROM (4 or 8 KB), at cartridge offset $4000.
        std::string apple_chars;
        /// North bridge for the decoder; nullptr = the description's
        /// (`northbridge` line), or A16-A19 low. Not owned.
        NorthBridge *bridge{nullptr};
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
        /// The `console` line's card, or nullptr. The run loop makes it the
        /// CPU's observer and asks it before each instruction (may_run()).
        ConsoleCard *console_card() const { return console_card_; }
        /// The video card, if the description has one (`video`).
        VideoCard *video_card() const { return video_card_; }
        /// The USB card, if the description has one (`usb`): the keyboard, mouse and game port.
        UsbCard *usb_card() const { return usb_card_; }

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
        explicit Machine(NorthBridge *bridge) : bus_(bridge) {}

        Decoder bus_;
        std::string cpu_{"nmos6502"};
        std::vector<std::unique_ptr<Device>> devices_;
        std::unordered_map<std::string, Device *> named_;
        NorthBridge *bridge_{nullptr}; ///< the description's own, owned through devices_
        BusMaster *master_{nullptr};   ///< its DMA controller, owned through devices_
        ConsoleCard *console_card_{nullptr}; ///< owned through devices_
        VideoCard *video_card_{nullptr};     ///< owned through devices_
        UsbCard *usb_card_{nullptr};         ///< owned through devices_
        std::vector<std::unique_ptr<Peripheral>> peripherals_;
        std::unordered_map<std::string, Peripheral *> named_peripherals_;
    };
} // namespace ooe::emu
