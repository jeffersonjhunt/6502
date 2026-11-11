#!/usr/bin/env python3
"""Paths to test labels to suites (hw/testing.md, "The plan", items 2-4), from tools/test-map.txt.

    tools/test_map.py check [--build DIR]          every path git tracks is mapped; the map's labels are real
    tools/test_map.py select --build DIR --out D [--] PATH...
                                                   the suites PATH... choose, for tools/check.sh changed
    tools/test_map.py entries [--build DIR] SUITE...  each suite's CTest entries (a split suite has several)

DIR is a configured CMake build of the emulator with its tests (default emulator/build/fast): the suites
and their labels are read from it (ctest --show-only=json-v1). `select` prints which path chose which labels
and which suites, and writes to D: suites (the CTest names chosen, one a line), sanitize (those of them
that are the emulator's own: the label emu), steps (the steps beyond ctest: web,
docs, oodos, interop), labels and cxx (1 if a path is the emulator's C++ or its build, else 0). It fails on a path no rule
maps, so a change can't go unexamined.
"""

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAP = os.path.join(REPO, "tools", "test-map.txt")
SPECIAL = {"all", "self", "web", "none"}
# The label that marks the emulator's own suites (CPU, bus, machine, devices), the only ones the sanitizers
# run (hw/testing.md, item 4): a kind of suite, not a part, so no path chooses it.
EMU = "emu"
# The steps a label adds beyond its ctest suites: interop (cpmtools reading the cards OODOS writes) for
# OODOS and the SD card's code, the cards, make_card.sh, the diskdefs.
STEPS = {"web": ["web"], "docs": ["docs"], "oodos": ["oodos", "interop"], "sd": ["interop"]}


def die(msg):
    print(f"tools/test_map.py: {msg}", file=sys.stderr)
    sys.exit(1)


def read_map(path=MAP):
    rules = []
    with open(path, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            words = line.split()
            if len(words) < 2:
                die(f"{path}:{n}: a pattern and no labels")
            rules.append((n, words[0], words[1:]))
    return rules


def match(rules, path):
    """The first rule that matches path: (line, pattern, labels), or None."""
    for rule in rules:
        _, pattern, _ = rule
        if pattern.endswith("/") and path.startswith(pattern):
            return rule
        if not pattern.endswith("/") and fnmatch.fnmatchcase(path, pattern):
            return rule
    return None


def tracked():
    out = subprocess.run(["git", "-c", "safe.directory=*", "-C", REPO, "ls-files", "-z"],
                         check=True, capture_output=True).stdout
    return [p for p in out.decode().split("\0") if p]


def ctest_json(build):
    """ctest --show-only=json-v1 for a build, run on a copy of its CTestTestfile.cmake files: ctest writes
    Testing/Temporary/LastTest.log wherever it runs, even to list, and in the build itself that would clobber
    the log of a run in progress (this runs inside one, as a test) or just finished (tools/check.sh reads it)."""
    import shutil
    import tempfile
    with tempfile.TemporaryDirectory(prefix="ctest_json.") as tmp:
        for root, _, names in os.walk(build):
            if "CTestTestfile.cmake" in names:
                dest = os.path.join(tmp, os.path.relpath(root, build))
                os.makedirs(dest, exist_ok=True)
                shutil.copy(os.path.join(root, "CTestTestfile.cmake"), dest)
        out = subprocess.run(["ctest", "--test-dir", tmp, "--show-only=json-v1"],
                             check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def suites(build):
    """{ctest name: (set of labels, [command words])} from a configured build."""
    if not os.path.isdir(build):
        die(f"{build}: no build there (cmake --preset fast first)")
    found = {}
    for t in ctest_json(build)["tests"]:
        labels = set()
        for prop in t.get("properties", []):
            if prop["name"] == "LABELS":
                labels = set(prop["value"])
        found[t["name"]] = (labels, t.get("command", []))
    if not found:
        die(f"{build}: ctest lists no tests")
    return found


def stem(word):
    return os.path.splitext(os.path.basename(word))[0]


def named_for(tests, name):
    """The CTest entries of the suite named `name`: those whose command runs test/host's `name` binary or script."""
    # ctest leaves out a command whose binary isn't built yet: such an entry is the suite by its own name.
    return {t for t, (_, cmd) in tests.items() if any(stem(w) == name for w in cmd) or (not cmd and t == name)}


def resolve(tests, path, labels):
    """(the labels, the suites) a rule's labels choose for path."""
    all_labels = set().union(*(l for l, _ in tests.values()))
    chosen_labels, chosen = set(), set()
    for label in labels:
        if label == "none":
            continue
        if label == "all":
            chosen_labels |= (all_labels - {EMU}) | {"web"}
            chosen |= set(tests)
        elif label == "self":
            chosen |= named_for(tests, stem(path))
            if stem(path) in web_suites():
                chosen_labels.add("web")  # the web preset runs it too (tools/web_build.sh)
        elif label.startswith("test_"):
            chosen |= named_for(tests, label)
            if label in web_suites():
                chosen_labels.add("web")
        else:
            chosen_labels.add(label)
            chosen |= {t for t, (l, _) in tests.items() if label in l}
    return chosen_labels, chosen


def _cmake(path):
    with open(os.path.join(REPO, path), encoding="utf-8") as f:
        return "\n".join(line.split("#", 1)[0] for line in f)


HOST_CMAKE = "emulator/test/host/CMakeLists.txt"
WEB_CMAKE = "emulator/apps/web/CMakeLists.txt"
_texts = {}  # check --web-cmake/--host-cmake: another text for one of those (a doctored copy, to see it fail)


def web_suites():
    """The suites the web preset builds and runs under node: the list in test/host/CMakeLists.txt's
    if(EMSCRIPTEN) block."""
    text = _texts.get(HOST_CMAKE) or _cmake(HOST_CMAKE)
    if "if(EMSCRIPTEN)" not in text:
        return set()
    block = text[text.index("if(EMSCRIPTEN)"):]
    m = re.search(r"foreach\(name\s+([^)]*)\)", block)
    return set(m.group(1).split()) if m else set()


def web_inputs(paths):
    """The tracked paths in oo1.wasm, the page's core: every file of the libraries it links, in the closure
    of target_link_libraries from apps/web/CMakeLists.txt's oo1, and every machines/ file those CMake files
    or apps/web's embed. Read from the CMake files, so a library or an embedded file added there is held to
    the map's `web` without a word here. Returns (paths, the libraries, problems): a derivation that finds
    nothing (no suites, no closure, no emu_session in it, a library linked with no add_library) is a problem,
    not a pass."""
    problems = []
    libs = {}  # emu_x: (its component directory, the emu_ libraries it links)
    for p in paths:
        if re.fullmatch(r"emulator/components/[^/]+/CMakeLists.txt", p):
            text = _cmake(p)
            for name in re.findall(r"add_library\((emu_\w+)", text):
                libs[name] = (os.path.dirname(p) + "/", set())
            for name, args in re.findall(r"target_link_libraries\((emu_\w+)([^)]*)\)", text):
                if name in libs:
                    libs[name][1].update(re.findall(r"\bemu_\w+", args))
    web = _texts.get(WEB_CMAKE) or _cmake(WEB_CMAKE)
    todo = set()
    for args in re.findall(r"target_link_libraries\(oo1([^)]*)\)", web):
        todo |= set(re.findall(r"\bemu_\w+", args))
    seen = set()
    while todo:
        lib = todo.pop()
        if lib in seen:
            continue
        if lib not in libs:
            problems.append(f"{lib}: linked into oo1's closure, but no component CMakeLists.txt has its add_library")
            seen.add(lib)
            continue
        seen.add(lib)
        todo |= libs[lib][1]
    if not seen:
        problems.append(f"{WEB_CMAKE}: no target_link_libraries(oo1 emu_...) found, so no link closure")
    elif "emu_session" not in seen:
        problems.append(f"oo1's link closure ({' '.join(sorted(seen))}) has no emu_session")
    if not web_suites():
        problems.append(f"{HOST_CMAKE}: no suite list in its if(EMSCRIPTEN) block")
    dirs = {libs[l][0] for l in seen if l in libs}
    texts = [web] + [_cmake(d + "CMakeLists.txt") for d in dirs]
    machines = {"emulator/" + m for t in texts for m in re.findall(r"\$\{PROJECT_SOURCE_DIR\}/(machines/[\w.-]+)", t)}
    out = {p for p in paths if any(p.startswith(d) for d in dirs) and not p.endswith("CMakeLists.txt")}
    out |= {p for p in paths if p in machines}
    out |= {f"emulator/test/host/{n}.cpp" for n in web_suites()} & set(paths)
    return out, sorted(seen), problems


def is_cxx(path):
    """The emulator's C++, or what builds it: what the sanitizer build checks."""
    if not path.startswith("emulator/") or path.startswith("emulator/apps/web/"):
        return False
    return path.endswith((".cpp", ".h", ".hpp", ".cc", "CMakeLists.txt", ".cmake", "CMakePresets.json"))


def cmd_check(args):
    rules = read_map(args.map)
    paths = tracked()
    bad = [p for p in paths if match(rules, p) is None]
    for p in bad:
        print(f"unmapped: {p}")
    used = set()
    for p in paths:
        r = match(rules, p)
        if r:
            used.add(r[0])
    problems = len(bad)
    for n, pattern, _ in rules:
        if n not in used:
            print(f"{args.map}:{n}: {pattern} matches no tracked path")
            problems += 1
    if args.build:
        tests = suites(args.build)
        all_labels = set().union(*(l for l, _ in tests.values()))
        for t, (l, _) in sorted(tests.items()):
            if not l:
                print(f"{t}: no labels")
                problems += 1
        chosen_by_map = set()
        for n, pattern, labels in rules:
            for label in labels:
                if label in SPECIAL:
                    continue
                if label == EMU:
                    print(f"{args.map}:{n}: {EMU} is a kind of suite, not a part a path can choose")
                    problems += 1
                    continue
                if label.startswith("test_"):
                    if not named_for(tests, label):
                        print(f"{args.map}:{n}: {label} is no suite")
                        problems += 1
                elif label not in all_labels:
                    print(f"{args.map}:{n}: {label} is no suite's label")
                    problems += 1
                chosen_by_map.add(label)
        for label in sorted(all_labels - chosen_by_map - {EMU}):
            print(f"label {label}: no path chooses it, but a suite carries it")
            problems += 1
        for p in paths:
            r = match(rules, p)
            if r and "self" in r[2] and not named_for(tests, stem(p)):
                print(f"{p}: self, but no suite is named {stem(p)}")
                problems += 1
    # Everything in the page's core, or run by the web preset's tests, must choose `web` (or `all`).
    for opt, key in ((args.web_cmake, WEB_CMAKE), (args.host_cmake, HOST_CMAKE)):
        if opt:
            with open(opt, encoding="utf-8") as f:
                _texts[key] = "\n".join(line.split("#", 1)[0] for line in f)
    inputs, libs, derive_problems = web_inputs(paths)
    for msg in derive_problems:
        print(msg)
        problems += 1
    for p in sorted(inputs):
        r = match(rules, p)
        if not r:
            continue
        words = set(r[2])
        named = stem(p) in web_suites() and "self" in words
        if not (words & {"web", "all"} or named):
            print(f"{p}: in oo1.wasm or the web preset's tests ({' '.join(libs)}), but its rule ({r[1]}) doesn't choose web")
            problems += 1
    print(f"{len(paths)} tracked paths, {len(rules)} rules, {len(inputs)} in the web build, {problems} problem(s)")
    return 1 if problems else 0


def cmd_select(args):
    rules = read_map(args.map)
    tests = suites(args.build)
    if not args.paths:
        die("no paths")
    labels, chosen, unmapped, chose_nothing, cxx = set(), set(), [], [], False
    print("paths, and the labels and suites each chose:")
    for p in args.paths:
        r = match(rules, p)
        if r is None:
            unmapped.append(p)
            print(f"  {p}: NOT IN THE MAP")
            continue
        n, pattern, words = r
        l, s = resolve(tests, p, words)
        labels |= l
        chosen |= s
        cxx = cxx or is_cxx(p)
        what = " ".join(words)
        # A rule that isn't `none` and chose no suite and no step (a self rule for a suite no CMake file
        # registers, a label no suite carries) examined nothing for that path.
        nothing = set(words) != {"none"} and not s and not any(x in STEPS for x in l)
        if nothing:
            chose_nothing.append(p)
        print(f"  {p}: {what} (line {n}, {pattern}): {len(s)} suite(s){': NOTHING' if nothing else ''}")
    if unmapped:
        print(f"{len(unmapped)} path(s) not in {args.map}: add a rule for each", file=sys.stderr)
        return 1
    if chose_nothing:
        print(f"{len(chose_nothing)} path(s) whose rule chose no suite and no step: register the suite "
              f"(emu_add_test, a line in the labels table) or fix the rule", file=sys.stderr)
        return 1
    sanitize = sorted(t for t in chosen if EMU in tests[t][0]) if cxx else []
    steps = sorted({step for l in labels if l in STEPS for step in STEPS[l]})
    print(f"labels: {' '.join(sorted(labels)) or '(none)'}")
    print(f"suites ({len(chosen)}): {' '.join(sorted(chosen)) or '(none)'}")
    if cxx:
        print(f"under the sanitizers ({len(sanitize)}): {' '.join(sanitize) or '(none)'}")
    else:
        print("under the sanitizers: none (no path is the emulator's C++)")
    print(f"steps beyond ctest: {' '.join(steps) or '(none)'}")
    os.makedirs(args.out, exist_ok=True)
    # The labels the chosen entries carry: what decides the tools they need (an eater suite is chosen by
    # via, acia, lcd and self as well as by eater).
    entry_labels = sorted(set().union(*(tests[t][0] for t in chosen)) if chosen else set())
    for name, items in (("suites", sorted(chosen)), ("sanitize", sanitize), ("steps", steps),
                        ("labels", sorted(labels)), ("entry_labels", entry_labels)):
        with open(os.path.join(args.out, name), "w", encoding="utf-8") as f:
            f.write("".join(f"{i}\n" for i in items))
    with open(os.path.join(args.out, "cxx"), "w", encoding="utf-8") as f:
        f.write("1\n" if cxx else "0\n")
    return 0


def cmd_entries(args):
    """The CTest entries of each suite named: its own, or its split's parts and _cases."""
    tests = suites(args.build)
    bad = 0
    for name in args.names:
        got = named_for(tests, name) | ({name} if name in tests else set())
        if not got:
            print(f"tools/test_map.py: {name} is no suite in {args.build}", file=sys.stderr)
            bad = 1
        for t in sorted(got):
            print(t)
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--build")
    c.add_argument("--map", default=MAP, help="another map (a broken copy, to see the check fail)")
    c.add_argument("--web-cmake", help="another text for apps/web/CMakeLists.txt (likewise)")
    c.add_argument("--host-cmake", help="another text for test/host/CMakeLists.txt (likewise)")
    s = sub.add_parser("select")
    s.add_argument("--map", default=MAP)
    s.add_argument("--build", default=os.path.join(REPO, "emulator", "build", "fast"))
    s.add_argument("--out", required=True)
    s.add_argument("paths", nargs="*")
    e = sub.add_parser("entries")
    e.add_argument("--build", default=os.path.join(REPO, "emulator", "build", "fast"))
    e.add_argument("names", nargs="+")
    args = ap.parse_args()
    return {"check": cmd_check, "select": cmd_select, "entries": cmd_entries}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
