/**
 * @file quiet_logging.cpp
 * @brief easylogging++ storage for the test binaries, with every logger off.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * The core logs through easylogging++, which needs exactly one
 * INITIALIZE_EASYLOGGINGPP per binary and, left at its defaults, writes every
 * level to stdout. The CPU logs each opcode at TRACE, so a 30-million
 * instruction conformance run would bury its own report. The quieting object
 * is defined after the storage in this one file, which is what guarantees it
 * is constructed second. Goes away with easylogging++ in Phase 2.
 */

#include "logging.h"

INITIALIZE_EASYLOGGINGPP

namespace
{
    struct Quiet
    {
        Quiet()
        {
            el::Configurations conf;
            conf.setToDefault();
            conf.setGlobally(el::ConfigurationType::Enabled, "false");
            conf.setGlobally(el::ConfigurationType::ToFile, "false");
            el::Loggers::reconfigureAllLoggers(conf);
        }
    };

    const Quiet quiet;
} // namespace
