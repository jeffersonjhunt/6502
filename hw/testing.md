# Testing: run what a change touches #

Nov 11, 2026 · @Jefferson

**Status: approved by the owner (2026-11-11).** Nothing here is built yet. Until it is, the tiers in
[../README.md](../README.md) and `tools/check.sh` stand as they are, and a branch's sanitizer check is the suites
its change touches, chosen by hand and named in its report. Questions 1-3 below are still open.


## The problem ##

Every release runs every suite, and the full tier runs them all under ASan/UBSan. That costs hours of real time
on every release, and an AWS builder didn't fix it (below). The cost is in what is run and how big the suites
are, not in the hardware.

The numbers, from the full tier's native sanitizer suite (`native-dev`) on a 16-core c8a.4xlarge at 16 jobs
(2026-11-11, main at d07ebee):

| | |
| --- | --- |
| Suites | 94 (95 in the fast build) |
| CPU time under the sanitizers | 8.8 hours; 84 minutes of real time on 16 cores |
| `test_oodos_*` (36 suites) | **94% of that time** |
| The 15 slowest suites, all `test_oodos_*` | 76% |
| The slowest | `test_oodos_disk2_media` 53 min, `test_oodos_disk2` 45, `test_oodos_utils_dir` 45, `test_oodos_utils_files` 35 |
| At or near their limits | `test_session`, `test_bios_sd` timed out; `test_bios` 98%, `test_oodos_sys` 91%, `test_oodos_dos33` 89% |

The same suites built fast, with no sanitizers, take minutes in all.

**Why the sanitizers buy little there.** ASan and UBSan check the emulator's C++. An OODOS suite tests OODOS,
which is 6502 code running inside the emulator: it runs the same few emulator paths (the CPU's step, the bus,
a device or two) millions of times under the checker. A memory error in those paths shows up in the short device
and CPU suites just as well. An OODOS suite matters to the sanitizers only where it drives an emulator path no
device suite reaches, and the coverage check (item 7) is how to find those.

**Why the whole suite every time is wasted.** A change to the BIOS can't break the SID filter. A change to the
video card can't break a FAT mount. Yet both run every suite. Every test links the whole emulator
(`emu_add_test_exe` in `emulator/test/host/CMakeLists.txt`), so CMake's dependency graph can't say which suites a
change affects: that has to be written down, and checked against coverage.


## The plan ##

1. **Labels.** Every ctest suite gets labels for the parts it covers: `cpu`, `bus`, `machine`, one per device
   (`video`, `sound`, `sid`, `es5503`, `dma`, `disk2`, `sd`, `usb`, `via`, `rtc`, ...), `bios`, `oodos`, `apple`,
   `web`, `docs`. A suite can carry several labels (`test_oodos_video` is `oodos` and `video`).
2. **A map from paths to labels,** one file in the repo (`tools/test-map.txt`): `emulator/components/devices/
   video_card.cpp` → `video`; `oodos/` → `oodos`; `backplane/bios/` → `bios`; and so on. The emulator's core
   (the CPU, the bus, the machine, the session) maps to every label, since everything depends on it.
3. **`tools/check.sh changed BASE`:** a new tier that diffs the branch against BASE (main by default), maps the
   changed paths to labels, and runs only the suites with those labels (`ctest -L`). It prints which paths
   chose which suites. A changed path the map doesn't cover is an error, never "nothing to run" (a check that
   examined nothing must fail).
4. **Sanitizers only on the emulator's own suites** (CPU, bus, machine, devices), and only when the branch changed
   emulator C++. The OODOS, BIOS, Apple-mode and docs suites never run under the sanitizers. On this host that
   should be minutes, not hours.
5. **OODOS suites in the fast build only,** and only when OODOS, the BIOS, the cards, or a device they use
   changed.
6. **Smaller suites.** Split the long Disk II and utilities suites (`test_oodos_disk2_media`, `test_oodos_disk2`,
   `test_oodos_utils_dir`, `test_oodos_utils_files`, ...) so that what does run finishes quickly, fails at a
   precise point, and spreads across cores. The split keeps every case; no expectation changes.
7. **Check the map against coverage, once.** Build with coverage (gcc `--coverage`), run each suite alone, and
   list which source files it executes. A suite that executes a file its labels don't cover means the map is
   wrong: fix the map. Repeat when a new device or suite is added, not every release.

**What the tiers become (proposed):**

| Tier | When | What |
| --- | --- | --- |
| `quick` | Before staging, as now | Unchanged: every suite, fast build, a few minutes |
| `changed` | Each branch, before review and merge | The suites the branch's paths choose; sanitizers on those that are emulator suites, when emulator C++ changed |
| `full` | A release whose emulator C++ changed | `quick`, plus the sanitizer build of the emulator suites only, the whole e2e suite (Chromium; WebKit smoke), interop |

`quick` already runs every suite in the fast build, so nothing goes untested. What goes is the sanitizers on
suites where they find nothing.


## The AWS builder: tried and retired ##

From 2026-11-10 to 2026-11-11 the full tier ran on throw-away EC2 instances (`tools/cloud-full.sh`, two
CloudFormation stacks, an S3 bucket for the protected fixtures, an ECR image). It ran, but it didn't help:

- The run's length is its slowest sanitizer suite's, which runs on one core, so more cores barely shorten it. At 16
  jobs on c8a, 84 minutes, and suites still timed out.
- Spot instances were taken back on two of three runs, losing the run each time.
- One run went at 4 jobs because the branch's `check.sh` predated `--jobs`, costing hours.

It was removed from the repo on feat/test-plan. A teardown script deleted what was left in the account (the
stacks, the bucket and its 61 objects, the ECR repository and its 4 images; the owner ran it on 2026-11-11, and
nothing tagged `builder=true` is left but two KMS keys pending deletion), and was then removed itself.
`check.sh`'s `--jobs` and `--exclude` stay: they are useful on any host.


## For the current work ##

**Video card revision 2, stream A1** (feat/video-rev2-emu) changes the video card's emulation, and renames the
old card's files. Under this plan its gate is the `video`, `bus` and `machine` suites under the sanitizers on
this host, plus the fast build of every suite (95/95 passed, 2026-11-10). Its run on the builder at 14 jobs was
lost to spot after 31 minutes. In it, `test_bios` and `test_oodos_boot` timed out, but they run no slower on A1
than on main in the fast build (three alternating rounds, 2026-11-11), so the timeouts are the limits, not A1.

Until this plan is built, a branch's sanitizer check is the relevant suites, chosen by hand and named in its
report.


## Open questions ##

1. Is `full` still wanted at all, or are `changed` on each branch and `quick` before each release enough?
2. `test_session` and `test_bios_sd` time out under the sanitizers on main as well. Should they get longer limits,
   or should we find out why they're slow there first? (Proposed: find out first.)
3. Should the coverage check (item 7) become part of `full`, so the map can't drift?
4. `perf/wasm` is on GitHub and not merged (2026-11-05, "2.6 is not a win"): keep it or delete it? **Answered
   (2026-11-11):** deleted, on the owner's word.
