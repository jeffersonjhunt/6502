#!/usr/bin/env python3
"""The suites' labels against what they run (hw/testing.md, "The plan", item 7).

    cd emulator && cmake --preset coverage && cmake --build --preset coverage -j8
    tools/test_coverage.py [--build emulator/build/coverage] [--jobs 8] [--work DIR] [-R REGEX]
    tools/test_coverage.py --from DIR/files.json     # the comparison again, the suites not rerun

Runs each suite of a `coverage` build (gcc --coverage) on its own, its gcov counters written to a tree of
its own (GCOV_PREFIX), so suites run side by side; lists the emulator's source files each executes (a line
run at least once); and maps each file through tools/test-map.txt. A file whose labels the suite doesn't
carry is a mismatch: a change to that file would not choose the suite, though the suite runs it. Fix the
map or the suite's labels (emulator/test/host/CMakeLists.txt, "Labels"), and rerun.

Run it when a device or a suite is added, not every release. It prints, for each suite, the labels it
carries, the labels its files call for and, as MISSING, those it lacks; and writes the files each suite
executed to WORK/files.json. The exit status is 1 if a label is missing. A file the map gives `all` (the
core) never mismatches; a test's own files (emulator/test/host) are left out, since a suite runs its own.
"""

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_map  # noqa: E402

REPO = test_map.REPO
EMU = os.path.join(REPO, "emulator")
# Labels that name software or a kind, not C++: coverage of the emulator can't call for them.
NOT_CXX = {"bios", "oodos", "apple", "apple1", "eater", "docs", "tools", "split", "emu", "app"}


def entries(build):
    """{suite: [(name, command, cwd, env)]}, grouped by the binary or script they run."""
    groups = {}
    for t in test_map.ctest_json(build)["tests"]:
        cmd = t.get("command") or []
        if not cmd:
            continue
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        env = dict(v.split("=", 1) for v in props.get("ENVIRONMENT", []))
        if t["name"].endswith("_cases"):
            continue  # the split's check lists cases and runs none
        labels = set(props.get("LABELS", []))
        groups.setdefault(suite_of(t["name"]), []).append((t["name"], cmd, props.get("WORKING_DIRECTORY", build), env,
                                             labels, props.get("SKIP_RETURN_CODE")))
    return groups


def suite_of(name):
    """A split suite's entries (NAME_KofN) are one suite."""
    head, _, tail = name.rpartition("_")
    if head and "of" in tail and tail.replace("of", "", 1).isdigit():
        return head
    return name


def run_suite(suite, runs, work):
    prefix = os.path.join(work, "gcda", suite)
    shutil.rmtree(prefix, ignore_errors=True)
    os.makedirs(prefix)
    results = []
    t0 = time.time()
    for name, cmd, cwd, env, _, skip in runs:
        e = dict(os.environ, GCOV_PREFIX=prefix, GCOV_PREFIX_STRIP="0", **env)
        try:
            r = subprocess.run(cmd, cwd=cwd, env=e, capture_output=True, timeout=7200)
            rc = r.returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
        results.append((name, rc if rc != skip else "skipped"))
    return suite, prefix, results, time.time() - t0


def executed(prefix, build):
    """The repo's source files with a line run, from the .gcda files under prefix."""
    files = set()
    for root, _, names in os.walk(prefix):
        for n in names:
            if not n.endswith(".gcda"):
                continue
            gcda = os.path.join(root, n)
            real_dir = "/" + os.path.relpath(root, prefix)  # GCOV_PREFIX_STRIP=0: the object's own path
            gcno = os.path.join(real_dir, n[:-5] + ".gcno")
            if not os.path.exists(gcno):
                continue
            link = gcda[:-5] + ".gcno"
            if not os.path.exists(link):
                os.symlink(gcno, link)
            r = subprocess.run(["gcov", "--json-format", "--stdout", gcda], cwd=root,
                               capture_output=True, text=True)
            for line in r.stdout.splitlines():
                line = line.strip()
                if not line.startswith("{"):
                    continue
                doc = json.loads(line)
                cwd = doc.get("current_working_directory", build)
                for f in doc.get("files", []):
                    path = os.path.normpath(os.path.join(cwd, f["file"]))
                    if not path.startswith(REPO + os.sep):
                        continue
                    if any(l.get("count", 0) > 0 for l in f.get("lines", [])):
                        files.add(os.path.relpath(path, REPO))
    return files


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", default=os.path.join(EMU, "build", "coverage"))
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--work", default=None, help="where the counters go (default: a new temporary directory)")
    ap.add_argument("-R", dest="regex", default=None, help="only the suites whose names match")
    ap.add_argument("--from", dest="previous", default=None,
                    help="a files.json an earlier run wrote: compare it with the map and labels as they are now")
    args = ap.parse_args()
    rules = test_map.read_map()
    groups = entries(args.build)  # the labels as configured now
    if args.regex:
        import re
        groups = {s: r for s, r in groups.items() if re.search(args.regex, s)}
    failed = 0
    if args.previous:
        with open(args.previous, encoding="utf-8") as f:
            report = {s: v for s, v in json.load(f).items() if s in groups}
        work = os.path.dirname(os.path.abspath(args.previous))
        for s in sorted(set(groups) - set(report)):
            print(f"{s}: not in {args.previous} (a new suite? run it again)")
            failed = 1
    else:
        work = args.work or tempfile.mkdtemp(prefix="test_coverage.")
        os.makedirs(work, exist_ok=True)
        print(f"tools/test_coverage.py: {len(groups)} suite(s), {args.jobs} at once; counters in {work}", flush=True)
        report = {}
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            futures = [pool.submit(run_suite, s, r, work) for s, r in sorted(groups.items())]
            for fut in concurrent.futures.as_completed(futures):
                suite, prefix, results, secs = fut.result()
                files = sorted(executed(prefix, args.build))
                report[suite] = {"runs": results, "seconds": round(secs, 1), "files": files}
                bad = [f"{n}: {rc}" for n, rc in results if rc not in (0, "skipped")]
                if bad:
                    failed = 1
                print(f"  {suite}: {secs:.0f} s, {len(files)} file(s){'; FAILED ' + ', '.join(bad) if bad else ''}",
                      flush=True)
        with open(os.path.join(work, "files.json"), "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1, sort_keys=True)

    print()
    for suite in sorted(report):
        labels = set().union(*(r[4] for r in groups[suite]))
        want, why = set(), {}
        for path in report[suite]["files"]:
            if path.startswith("emulator/test/host/"):
                continue
            rule = test_map.match(rules, path)
            if rule is None:
                print(f"{suite}: {path} is not in the map")
                failed = 1
                continue
            words = set(rule[2])
            if words & {"all", "none"}:
                continue
            if any(w.startswith("test_") for w in words) and suite in words:
                continue
            need = {w for w in words if not w.startswith("test_") and w != "self"}
            if need and not need & labels:
                for w in need:
                    want.add(w)
                    why.setdefault(w, []).append(path)
        skipped = [n for n, rc in report[suite]["runs"] if rc == "skipped"]
        missing = sorted(want)
        extra = sorted(labels - NOT_CXX - want - {
            w for p in report[suite]["files"] for r in [test_map.match(rules, p)] if r for w in r[2]})
        line = f"{suite}: carries {' '.join(sorted(labels))}"
        if missing:
            failed = 1
            line += "; MISSING " + ", ".join(f"{w} ({' '.join(why[w])})" for w in missing)
        if extra:
            line += f"; runs no file of {' '.join(extra)}"
        if skipped:
            line += f"; skipped: {' '.join(skipped)}"
        print(line)
    print(f"\ntools/test_coverage.py: {'FAILED: labels missing, or a suite failed' if failed else 'every label the files call for is there'};"
          f" {os.path.join(work, 'files.json')}")
    return failed


if __name__ == "__main__":
    sys.exit(main())
