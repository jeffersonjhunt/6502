#pragma once

/**
 * @file scratch.h
 * @brief A suite's scratch directory: files a test writes go there, not in the working directory.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * One directory per process, under the system's temporary directory and named for the suite and its process ID,
 * made on first use and removed at exit. A suite split into shares runs its entries at once from the same working
 * directory; each is its own process, so none reads a file another is still writing, and a run leaves the tree
 * clean.
 */

#include <filesystem>
#include <string>
#include <system_error>

#include <unistd.h>

namespace ooe::test
{
    class ScratchDir
    {
    public:
        explicit ScratchDir(const std::string &suite)
            : path_(std::filesystem::temp_directory_path() / (suite + "_" + std::to_string(::getpid())))
        {
            std::filesystem::create_directories(path_);
        }
        ScratchDir(const ScratchDir &) = delete;
        ScratchDir &operator=(const ScratchDir &) = delete;
        ~ScratchDir()
        {
            std::error_code ec; // never throw from a destructor; a leftover directory is no failure of the suite's
            std::filesystem::remove_all(path_, ec);
        }

        const std::filesystem::path &path() const { return path_; }
        /// @return the path of `name` in the directory
        std::string file(const std::string &name) const { return (path_ / name).string(); }

    private:
        std::filesystem::path path_;
    };
} // namespace ooe::test
