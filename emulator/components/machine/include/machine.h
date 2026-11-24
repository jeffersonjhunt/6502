#pragma once

/**
 * @file machine.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief A machine built from a plain-text description.
 * @version 0.11.0
 * @date 2026-11-16
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
 * `<type> <first>-<last> [space=S] [name=ID] [module=0-F] [wait=N] [drq] [irq] [image]`;
 * a rom's or a flash's image `-` is erased flash, all $FF (an empty cartridge).
 * Addresses are hex, optionally with a `$`, within the space: memory (the
 * default, A0-A23, the 24-bit bus), flash (00000-FFFFF, ROM A then ROM B:
 * the same as memory F00000-FFFFFF) or io (the I/O hole, 000-7FF,
 * whose mappings take the module ID of their 128-byte block unless
 * `module=` says otherwise, and may not cross into the next: a card
 * answers one module ID and decodes A0-A6). `#` starts a comment. `module=` tags the
 * mapping with a backplane module ID (a memory line behind the backplane's
 * bridge has no use for one: memory is chosen by address, and its cycles
 * carry the master's ID); `wait=` (0-15) pulls /WAIT for that many extra
 * cycles on every access, as a slow card does, the CPU's (the north bridge
 * makes it RDY) and a DMA grant's alike; `drq` wires the device's DMA
 * request to the backplane's shared /DREQ (Dreq::Wired): the DMA controller
 * polls it and grants it by its module ID, which a channel's CARD register
 * names (dma_controller.h), so the channel isn't the machine file's to say;
 * `drq=N`, revision 1's channel, is refused; `irq` wires the device's
 * IRQ output to the CPU. Chips: ram, rom (needs an image; CPU writes do
 * nothing), flash (the same, but two chips, even and odd bytes, that take
 * their command sequences: flash.h; an even size), pia6821, via6522,
 * acia6551, dma (the DMA controller, dma_controller.h; at most one, and it
 * takes the bus from the CPU), console (the Console card, console_card.h;
 * at most one; the run loop wires it to the CPU, see console_card()), video
 * (the video card, video_card.h; at most one; `rev=1` or `rev=2`, 1 if
 * omitted, or MachineOptions::video_revision; on revision 1 banks 8-B,
 * 80000-BFFFF, must be one ram line, which it draws from), aperture
 * (revision 2's 64 KB window on card memory, video_card.h's VideoAperture,
 * at E00000-E0FFFF on the backplane: on a revision 2 card it answers, as if
 * mapped last, and the CPU card sends bank 8, 080000-08FFFF, to it: a
 * revision 2 north bridge while MAP's AP8 is set, a revision 1 bridge always
 * (backplane_bridge.h); a line at 80000-8FFFF, as machine files had it
 * before the 24-bit bus, means the same. On a revision 1 card, or with no
 * video line, it is inert: a revision 1 bridge leaves bank 8 the RAM's, and
 * a revision 2 one sends it to the empty E00000 while AP8 is set), usb (the USB card, usb_card.h;
 * at most one; the run loop takes its /NMI), disk2 (the Disk II controller,
 * disk2_card.h; at most one, slot 6 in Apple II mode), sound (the sound
 * card, the device, sound_card3.h: at most one; `rev=3`, its only revision, may be given, and `memsize=2`,
 * `4` or `8`, its store in MB, 8 the default), sd (the mass storage card, sd_card.h; at most
 * one). A mapping larger than the chip mirrors it (a 6522 at 6000-7FFF
 * repeats every 16 bytes, as on Eater's board; a 4-byte 6551 in an I/O
 * block, every 4). Mappings may overlap, and the later line answers: so a
 * chip can sit in another's mirror, as the backplane's VIA does at io
 * 340-34F, inside the stand-in 6551's 300-37F.
 *
 * Without a `northbridge` line every CPU address is memory with A16-A23
 * low. `northbridge backplane [bootsel] [revision=1|2|3]` fits the backplane's
 * north bridge (backplane_bridge.h): the windows, VEC, ROM space, the I/O
 * hole and the DS3231 real-time clock; revision 2 (ID $D2) adds WIN0H-WIN3H,
 * VECH and MAP, revision 3 (ID $D3) the random source and the timebase
 * (`rng_chip=locked|unlocked|none`, default locked: the ATECC608B fitted),
 * revision 1 (ID $D1) is B1's, and the default is
 * BackplaneBridge::REVISION, 3 (OODOS 0.16 needs it). Its registers are at
 * io 680-6FF (module D, its IRQ wired: the timebase's tick), and it is named
 * `northbridge`. The clock starts at the host's local time unless the
 * options give one.
 *
 * Peripherals hang off a named 6522's pins instead of the bus, so they
 * take no range:
 *
 *   hd44780  name=lcd  via=via  data=PB0-3  rs=PB4  rw=PB5  e=PB6  [size=16x2]
 *   ps2kbd   name=keyboard  via=via  data=PA0-7        (strobes CA1)
 *   joyports name=joysticks  via=via                    (PA0-4 and PB0-4; joystick_ports.h)
 *
 * `data=` is 4 or 8 consecutive pins of one port (8 for ps2kbd); joyports
 * takes the whole of both ports, at most one per machine. Errors
 * name the file and line.
 */

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "backplane_bridge.h"
#include "console_card.h"
#include "disk2_card.h"
#include "dma_controller.h"
#include "flash.h"
#include "sd_card.h"
#include "sound_card3.h"
#include "decoder.h"
#include "device.h"
#include "joystick_ports.h"
#include "peripheral.h"
#include "usb_card.h"
#include "video_card.h"

namespace ooe::emu
{
    struct MachineOptions
    {
        /// If set, replaces the image of the first `rom` or `flash` line.
        std::string rom_override;
        /// The video card's revision (1 or 2), over the `video` line's `rev=`; 0: the line's. Anything
        /// else fails the build. The machine wires the bus for it, so the card's revision is then fixed
        /// (VideoCard::set_revision() returns false).
        unsigned video_revision{0};
        /// An Apple IIe ROM for ROM A's image 0 (the flash line named `rom_a`, at its
        /// start): one 16 KB file ($C000-$FFFF), or the two 8 KB halves, CD then EF.
        /// Apple's ROMs are copyrighted: the owner supplies them.
        std::vector<std::string> apple_rom;
        /// The IIe's video character ROM (4 or 8 KB) for image 0: character set 0, ROM A's $10000.
        std::string apple_chars;
        /// Images 0-3 the same way (hw/design.md, "ROM A's layout"): image n at ROM A's n x $4000,
        /// its set n at $10000 + n x $2000. Image 0's parts follow apple_rom's; set 0 is apple_chars
        /// or apple_charsets[0], not both. A slot given is erased before it is written; one not given
        /// is left as the ROM image has it.
        std::array<std::vector<std::string>, 4> apple_roms{};
        std::array<std::string, 4> apple_charsets{};
        /// The Disk II card's PROMs (256 bytes each): the boot ROM (341-0027) and the
        /// sequencer (341-0028). Apple's too; empty: the card has none.
        std::string disk2_boot, disk2_sequencer;
        /// North bridge for the decoder; nullptr = the description's
        /// (`northbridge` line), or A16-A23 low. Not owned.
        NorthBridge *bridge{nullptr};
        /// The backplane's DS3231, kept from another machine (a power cycle): its battery
        /// held the time. nullptr: a new chip, at `rtc_time`, or the host's local time.
        std::shared_ptr<Ds3231> rtc;
        std::optional<Ds3231::Time> rtc_time;
        /// The random number chip's bytes (north bridge revision 3, atecc608.h): the native app's and the
        /// WebUI's from the host (std::random_device); empty, the fixed stream (Atecc608::splitmix(1)), so a
        /// test sees known bytes.
        std::function<uint8_t()> rng_source;
        /// ROM A's first bytes kept from another machine (a power cycle): the flash held them
        /// through the power-off. Written into the flash line named `rom_a` from its start, after
        /// the line's image loads and before the IIe images above, so a slot given there still
        /// replaces its part. The Session keeps blocks 0-5 (FLASH_USER_BYTES); empty: none.
        std::vector<uint8_t> flash_user;
        /// ROM A's blocks 0-5: the IIe images 0-3 and their character sets (hw/design.md, "ROM A's layout").
        static constexpr size_t FLASH_USER_BYTES = 6 * 0x4000;
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
        /// The Disk II controller, if the description has one (`disk2`).
        Disk2Card *disk2_card() const { return disk2_card_; }
        /// The sound card, the device (revision 3), if the description has one (`sound`).
        SoundCard3 *sound_device() const { return sound_device_; }
        /// The mass storage card, if the description has one (`sd`): an SD card behind an RP2350.
        SdCard *sd_card() const { return sd_card_; }
        /// The `flash` lines' chips, in the description's order (set_cpu_hz() or the Session tells them the clock).
        const std::vector<Flash *> &flashes() const { return flashes_; }
        /// The DMA controller, if the description has one (`dma`): its channels through
        /// DmaController::peek_channel(), which has no effect on it.
        DmaController *dma() const { return dma_; }
        /// The joystick ports, if the description has them (`joyports`).
        JoystickPorts *joysticks() const { return joysticks_; }
        /// The backplane's north bridge (Apple II mode's switches), if the description has one.
        const BackplaneBridge *backplane_bridge() const { return dynamic_cast<const BackplaneBridge *>(bridge_); }
        BackplaneBridge *backplane_bridge() { return dynamic_cast<BackplaneBridge *>(bridge_); }

        /// Tells each card and chip that keeps real time how fast the CPU's cycles are (0: flat out,
        /// each keeping its 7.16 MHz time): the video, USB, sound and SD cards, the north bridge (its
        /// DS3231), the Disk II controller and every `flash` line's chips (their busy times). All on the
        /// one clock, as the CLI runs them, at its start and on each clock change; the Session has its
        /// own rule for Apple II mode's clock (Session::clock_cards).
        void set_cpu_hz(uint32_t hz);

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
        Disk2Card *disk2_card_{nullptr};     ///< owned through devices_
        SoundCard3 *sound_device_{nullptr};  ///< owned through devices_
        SdCard *sd_card_{nullptr};           ///< owned through devices_
        DmaController *dma_{nullptr};        ///< owned through devices_
        std::vector<Flash *> flashes_;       ///< owned through devices_
        JoystickPorts *joysticks_{nullptr};  ///< owned through peripherals_
        std::vector<std::unique_ptr<Peripheral>> peripherals_;
        std::unordered_map<std::string, Peripheral *> named_peripherals_;
    };
} // namespace ooe::emu
