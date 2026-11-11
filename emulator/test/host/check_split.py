#!/usr/bin/env python3
"""A split suite's CTest entries keep every case (hw/testing.md, item 6).

    check_split.py BUILD EXE

BUILD is the CMake build directory, EXE the suite's binary. Reads the entries that run EXE with
OOE_TEST_SHARD set (ctest --show-only=json-v1), lists the cases each takes (OOE_TEST_LIST=1, harness.h),
and the cases EXE has with no shard; fails unless every case is in exactly one entry, and there are two
entries or more. Exits 77 (skipped) if EXE skips, as a suite does without its fixtures.
"""

import collections
import json
import os
import subprocess
import sys


def cases(exe, shard):
    env = dict(os.environ, OOE_TEST_LIST="1")
    env.pop("OOE_TEST_SHARD", None)
    if shard:
        env["OOE_TEST_SHARD"] = shard
    run = subprocess.run([exe], env=env, capture_output=True, text=True, cwd=os.path.dirname(exe))
    if run.returncode == 77:
        print(f"{os.path.basename(exe)}: skips (its fixtures?), so nothing to check")
        sys.exit(77)
    if run.returncode != 0:
        print(run.stdout + run.stderr)
        sys.exit(f"{exe} (OOE_TEST_SHARD={shard or 'unset'}): exit {run.returncode}")
    return [line[5:] for line in run.stdout.splitlines() if line.startswith("case ")]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    build, exe = sys.argv[1], os.path.realpath(sys.argv[2])
    out = subprocess.run(["ctest", "--test-dir", build, "--show-only=json-v1"],
                         check=True, capture_output=True, text=True).stdout
    shards = {}
    for t in json.loads(out)["tests"]:
        cmd = t.get("command") or []
        if not cmd or os.path.realpath(cmd[0]) != exe:
            continue
        for prop in t.get("properties", []):
            if prop["name"] == "ENVIRONMENT":
                for var in prop["value"]:
                    if var.startswith("OOE_TEST_SHARD="):
                        shards[t["name"]] = var.split("=", 1)[1]
    name = os.path.basename(exe)
    if len(shards) < 2:
        sys.exit(f"{name}: {len(shards)} split entr{'y' if len(shards) == 1 else 'ies'} in {build}; want 2 or more")
    whole = cases(exe, None)
    if not whole:
        sys.exit(f"{name}: no cases")
    if len(set(whole)) != len(whole):
        sys.exit(f"{name}: a case name twice: {sorted(c for c, n in collections.Counter(whole).items() if n > 1)}")
    taken = collections.Counter()
    for entry, shard in sorted(shards.items()):
        got = cases(exe, shard)
        print(f"{entry} (OOE_TEST_SHARD={shard}): {len(got)} case(s)")
        taken.update(got)
    lost = [c for c in whole if c not in taken]
    twice = sorted(c for c, n in taken.items() if n > 1)
    extra = sorted(set(taken) - set(whole))
    for c in lost:
        print(f"  lost: {c}")
    for c in twice:
        print(f"  in more than one entry: {c}")
    for c in extra:
        print(f"  not a case of the whole binary's: {c}")
    print(f"{name}: {len(whole)} case(s), {len(shards)} entries, {len(lost)} lost, {len(twice)} twice")
    return 1 if lost or twice or extra else 0


if __name__ == "__main__":
    sys.exit(main())
