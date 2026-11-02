/**
 * @file test_docs_examples.cpp
 * @brief The books' examples (docs/examples), run from OODOS's CCP on backplane.machine.
 * @copyright Copyright (c) 2024-2026 Jefferson J. Hunt (MIT)
 *
 * Each docs/examples/NAME.s is built as a .COM by docs/examples/Makefile
 * into the build tree. OODOS.SYS is booted as test_oodos_ccp boots it
 * (oodos_rig.h's CcpRig), every example put on M: behind OODOS's back, and
 * each one run by typing M:NAME at the prompt. What it prints, from after
 * the command's echo to the CCP's next prompt, with CR LF as a newline, must
 * be NAME.out, kept beside the source: so a book's listing (held to the file
 * by docs/tools/check_docs.py) shows a program that runs as it says.
 *
 * DOCS_EXAMPLES_DIR names another directory of NAME.out files (and
 * DOCS_EXAMPLES_BUILD another build of the .COMs), to see the suite fail on
 * a wrong expectation put in on purpose.
 */

#include "harness.h"
#include "oodos_rig.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace ooe::test;
using namespace oodos_rig;
using namespace oodos_rig::ccp;

namespace
{
    const std::string SRC = std::getenv("DOCS_EXAMPLES_DIR") ? std::getenv("DOCS_EXAMPLES_DIR") : EMU_DOCS_EXAMPLES_SRC;
    const std::string BUILD = std::getenv("DOCS_EXAMPLES_BUILD") ? std::getenv("DOCS_EXAMPLES_BUILD") : EMU_DOCS_EXAMPLES_BUILD;

    /// The examples: each NAME with a NAME.out in SRC, sorted.
    std::vector<std::string> examples()
    {
        std::vector<std::string> out;
        for (const auto &f : std::filesystem::directory_iterator(SRC))
            if (f.path().extension() == ".out")
                out.push_back(f.path().stem().string());
        std::sort(out.begin(), out.end());
        return out;
    }

    /// NAME (8 characters at most: the test requires it) as M:'s directory has it: 8 + 3, blank-padded, capitals.
    std::string name83(const std::string &name)
    {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        n.resize(8, ' ');
        return n + "COM";
    }

    /// What a command printed: after its echo and CR LF, to the CR LF before the next prompt; CR LF as \n.
    std::string printed(const std::string &line, const std::string &got)
    {
        const std::string echo = line + "\r\n", prompt = "\r\nA>";
        if (got.compare(0, echo.size(), echo) != 0 || got.size() < echo.size() + prompt.size() ||
            got.compare(got.size() - prompt.size(), prompt.size(), prompt) != 0)
            return "<not a command's output: " + vis(got) + ">";
        std::string body = got.substr(echo.size(), got.size() - echo.size() - prompt.size()), out;
        for (size_t i = 0; i < body.size(); ++i)
            if (!(body[i] == '\r' && i + 1 < body.size() && body[i + 1] == '\n'))
                out += body[i];
        return out;
    }
} // namespace

TEST(each_example_prints_what_its_out_file_says)
{
    const std::vector<std::string> names = examples();
    std::printf("  %zu examples in %s\n", names.size(), SRC.c_str());
    REQUIRE(!names.empty()); // none examined is a failure, not a pass
    std::vector<std::string> on_m;
    for (const std::string &n : names)
    {
        if (n.size() > 8)
            std::printf("  %s: longer than M:'s 8.3 names hold\n", n.c_str());
        REQUIRE(n.size() <= 8);
        on_m.push_back(name83(n));
    }
    std::sort(on_m.begin(), on_m.end());
    REQUIRE(std::adjacent_find(on_m.begin(), on_m.end()) == on_m.end()); // two examples, one name on M:
    CcpRig r;
    REQUIRE(r.ok);
    std::vector<MFile> files;
    for (const std::string &n : names)
    {
        const std::vector<uint8_t> com = read_bytes(BUILD + "/" + n + ".com");
        CHECK(!com.empty());
        if (com.empty())
            std::printf("  %s: no %s/%s.com\n", n.c_str(), BUILD.c_str(), n.c_str());
        else
            files.push_back({0, name83(n), com});
    }
    r.put_m(files);
    unsigned same = 0;
    for (const std::string &n : names)
    {
        const std::string line = "m:" + n, want = read_text(SRC + "/" + n + ".out");
        const std::string got = printed(line, r.cmd(line));
        CHECK(got == want);
        if (got == want)
            ++same;
        else
            std::printf("  %s:\n    got  '%s'\n    want '%s'\n", n.c_str(), vis(got).c_str(), vis(want).c_str());
    }
    std::printf("  %u of %zu examples printed their .out\n", same, names.size());
}

int main()
{
    return run_all();
}
