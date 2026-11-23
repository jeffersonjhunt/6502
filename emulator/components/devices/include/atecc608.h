#pragma once

/**
 * @file atecc608.h
 * @author Jefferson J. Hunt (jefferson@oneoffendeavors.com)
 * @brief The ATECC608B random number chip on the north bridge's I²C bus, as the bridge sees it.
 * @version 0.1.0
 * @date 2026-11-22
 *
 * @copyright Copyright (c) 2026 Jefferson J. Hunt (MIT)
 *
 * hw/design.md, "The random source", is the design; this models only what the north
 * bridge (revision 3) uses of the part, from its documented behaviour, in our
 * own words. The bus is modelled a transaction at a time: the bridge says
 * when SDA was held low (wake()), offers an address byte (address(), which
 * answers ACK or NAK), then writes a word address and its bytes (write()) or
 * reads bytes (read()).
 *
 * - Asleep, it NAKs its address. SDA held low for 60 µs or more wakes it;
 *   after the wake delay (1.5 ms; NAKs until then) the first read gives the
 *   wake answer, the status group $04 $11 $33 $43. The watchdog puts it back
 *   to sleep 1.3 s after the wake, whatever it is doing.
 * - Word address $00 resets the output buffer, $01 sleeps, $02 idles (both
 *   asleep here: the bridge needs nothing kept), $03 is a command group:
 *   count (itself and the CRC included), opcode, param1, param2 (low byte
 *   first), data, CRC. A count that isn't the group's length, or a bad CRC,
 *   answers status $FF. Random ($1B) answers count 35, 32 bytes and a CRC;
 *   a Read ($02) of configuration word $15 answers its 4 bytes (84-87: byte
 *   86 LockValue, 87 LockConfig, $55 for unlocked, $00 locked). Anything else
 *   answers status $0F (can't execute).
 * - While a command executes (timing(): Random 23 ms, Read 5 ms) it NAKs its
 *   address; the bridge polls.
 * - Locked or not: with the configuration zone unlocked, Random answers a
 *   fixed pattern, $FF $FF $00 $00 repeated.
 * - The bytes come from an injected source: a seeded splitmix64 by default
 *   (tests and CcpRig), std::random_device in the native app, the browser's
 *   crypto.getRandomValues in the WebUI.
 * - Test hooks: corrupt the next k responses (a data byte flipped after the
 *   CRC is made), NAK the next k addresses, answer the next k Randoms with a
 *   health test error ($08), or answer every Random with one constant byte.
 *
 * The CRC (crc()): polynomial $8005, the register starting at 0, each byte
 * least significant bit first, no final reflection, sent low byte first.
 * tools/atecc_crc.py is its independent oracle.
 */

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace ooe::emu
{
    class Atecc608
    {
    public:
        static constexpr uint8_t ADDRESS = 0x35; ///< 7-bit: the Trust&GO TLS part (ATECC608B-TNGTLSS)
        static constexpr uint32_t NATIVE_HZ = 7'159'090;
        // Word addresses
        static constexpr uint8_t WA_RESET = 0x00, WA_SLEEP = 0x01, WA_IDLE = 0x02, WA_COMMAND = 0x03;
        // Opcodes, and the configuration word holding the lock bytes
        static constexpr uint8_t OP_READ = 0x02, OP_RANDOM = 0x1B;
        static constexpr uint16_t LOCK_WORD = 0x0015; ///< bytes 84-87
        static constexpr uint8_t UNLOCKED = 0x55;     ///< LockValue / LockConfig unlocked
        // Status codes
        static constexpr uint8_t ST_WOKEN = 0x11, ST_HEALTH = 0x08, ST_EXEC = 0x0F, ST_CRC = 0xFF;

        /// What the model times, in microseconds: the spec's planning figures, in one table.
        struct Timing
        {
            uint32_t random_us = 23'000;
            uint32_t read_us = 5'000;
            uint32_t wake_low_us = 60;
            uint32_t wake_delay_us = 1'500;
            uint32_t watchdog_us = 1'300'000;
        };

        using Source = std::function<uint8_t()>;
        /// A seeded splitmix64, a byte at a time: the fixed stream.
        static Source splitmix(uint64_t seed);
        /// The CRC over `n` bytes (as a number; sent low byte first).
        static uint16_t crc(const uint8_t *data, size_t n);

        /// `config_locked`, `data_locked`: the lock bytes; `source`: empty for splitmix(1).
        explicit Atecc608(bool config_locked = true, bool data_locked = true, Source source = {});

        /// The CPU's clock: the deadlines already set keep their time, rescaled from now (as Ds3231::rate does).
        void set_cpu_hz(uint32_t hz);
        void advance(uint64_t cycles);
        Timing &timing() { return timing_; }

        // The bus
        /// SDA held low for `cycles`: a wake if long enough.
        void wake(uint64_t cycles);
        /// The address byte, for a read or a write: true for ACK.
        bool address(bool read);
        /// After an ACKed write: the word address and the bytes after it.
        void write(uint8_t word, const std::vector<uint8_t> &bytes);
        /// After an ACKed read: the next byte of the output buffer ($FF past its end).
        uint8_t read();

        bool awake() const { return awake_; }
        bool busy() const { return awake_ && now_ < busy_until_; }
        bool config_locked() const { return config_locked_; }
        /// Random commands executed, and the opcode of the last command parsed (0: none).
        unsigned randoms() const { return randoms_; }

        // Test hooks
        void corrupt_next(unsigned k) { corrupt_ = k; }
        void nak_next(unsigned k) { nak_ = k; }
        void health_next(unsigned k) { health_ = k; }
        /// Every Random answers 32 of `byte` (-1: off).
        void set_constant(int byte) { constant_ = byte; }
        /// Every Random answers the unlocked part's fixed pattern ($FF $FF $00 $00 repeated), though the chip reads
        /// locked: one 4-byte group repeated (review r1's T5).
        void set_fixed_pattern(bool on) { pattern_ = on; }

    private:
        uint64_t cycles_of(uint32_t us) const { return uint64_t{us} * hz_ / 1'000'000; }
        void status(uint8_t code);
        void group(const std::vector<uint8_t> &payload);
        void execute(const std::vector<uint8_t> &g);
        void finish();

        bool config_locked_, data_locked_;
        Source source_;
        Timing timing_;
        uint32_t hz_{NATIVE_HZ};
        uint64_t now_{0};
        bool awake_{false};
        uint64_t ready_at_{0}, watchdog_at_{0}, busy_until_{0};
        bool pending_{false};            ///< a command's result waits for busy_until_
        std::vector<uint8_t> result_;    ///< what it will answer
        std::vector<uint8_t> out_;       ///< the output buffer
        size_t at_{0};
        unsigned randoms_{0};
        unsigned corrupt_{0}, nak_{0}, health_{0};
        int constant_{-1};
        bool pattern_{false};
    };
} // namespace ooe::emu
