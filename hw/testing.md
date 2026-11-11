# Testing: run what a change touches #

Nov 11, 2026 · @Jefferson

**Status: approved by the owner (2026-11-11); built 2026-11-11 (feat/test-changed): items 1-7 below, as "What was
built" says.** The owner's answers to questions 1-3 are recorded under "Open questions".


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

**What the tiers became (built 2026-11-11):**

| Tier | When | What |
| --- | --- | --- |
| `quick` | Before staging, as now | Unchanged: every suite, fast build, a few minutes |
| `changed` | Each branch, before review and merge | The suites the branch's paths choose, in the fast build; sanitizers on those that are emulator suites (`emu`), when emulator C++ changed; the oodos checks if `oodos` is chosen, the books' check if `docs` is; if `web` is, the WebAssembly build, vitest, the page build, the whole e2e suite on Chromium and WebKit's smoke set; interop if `oodos` or `sd` is |
| `full` | A release whose emulator C++ changed | `quick`, plus the sanitizer build of the emulator suites only (label `emu`), the whole e2e suite on Chromium and WebKit's smoke set, interop |

`quick` already runs every suite in the fast build, so nothing goes untested. What goes is the sanitizers on
suites where they find nothing.


## What was built (2026-11-11) ##

**1. Labels.** One table at the end of `emulator/test/host/CMakeLists.txt` ("Labels"), a suite and its labels a
line; a split suite's entries all get its labels. Configuring fails on a CTest entry with none. The labels:

| Kind | Labels |
| --- | --- |
| The core (a change there chooses every suite) | `cpu`, `bus`, `machine` |
| The rest of the emulator | `debug` (the debugger, the trace, symbols), `app` (apps/emu) |
| A device each | `acia`, `bridge`, `console`, `disk2`, `dma`, `es5503`, `iie` (Apple II mode's I/O), `joy`, `lcd`, `pia`, `rtc`, `sd`, `sid`, `sound`, `usb`, `via`, `video` |
| The whole machine | `backplane`: the suite builds `machines/backplane.machine`, so it runs every card on it |
| The software a suite runs | `bios`, `oodos`, `apple` (the owner's Apple IIe ROMs and disks), `apple1`, `eater`, `docs`, `tools` (the tiers' own scripts), `split` (a split's `_cases` check) |
| A kind of suite | `emu`: the emulator's own suites, the only ones the sanitizers run |

The sanitizer rule is the label `emu`, not "no `oodos`, `bios`, `apple` or `docs` label", because the labels say
what a suite depends on: `test_session` boots the BIOS in every case and `test_sd` in one, so both carry `bios`,
yet they are the session's and the SD card's own suites. `emu` is on the CPU, bus, machine, device, debugger and
app suites (50 CTest entries); not on the OODOS, BIOS (`test_bios`, `test_bios_sd`), Apple-mode (`test_iie`),
docs or Python-only suites.

**2. The map,** `tools/test-map.txt`: a glob or a directory a line, then its labels; the first rule that matches a
path counts. Besides the labels: `all` (the core: `emulator/components/{cpu6502,bus,machine,session,emu_log}`,
`memory_devices`, the CMake files, `harness.h`, `test_support.h`, `machines/backplane.machine`), `self` (a suite's
own `test_NAME.cpp` or `.py` chooses that suite), a suite's name (a file only it reads: `sid_known_failures.txt`,
`test/host/sound/`, `fetch_fixtures.py`, `oodos/lib/mudrv/`), `web` (the web steps), and `none` (licenses,
editor settings, `basic/`, `oodos/spike/`, `apps/sidcheck/`: nothing tests them; every build still builds the
code among them). A part inside another chooses both (the RTC and Apple II mode's I/O are the bridge's, the DOC
and the SIDs the sound card's), and each card of the backplane machine chooses `backplane` too.

`web` is chosen by everything in the page: `emulator/apps/web/`, `web_build.sh`, and what `oo1.wasm` embeds or
runs (every backplane card, the card media, `.sid` files and `components/debug`; the BIOS, its ROM and labels;
`backplane.machine`; `test/host/sd/`, whose `demo.sparse` the page boots: `apps/web/CMakeLists.txt`,
`ui/scripts/copy-core.mjs`), and a suite's own file when the web preset runs that suite. `test_map.py check`
derives three things from the CMake files and fails on any of them whose rule doesn't choose `web` (or `all`):
the tracked files of the component directories in oo1's link closure (`target_link_libraries` followed from
`apps/web/CMakeLists.txt` through `emulator/components/*/CMakeLists.txt`: `emu_session`, `emu_machine`,
`emu_devices`, `emu_debug` and the rest), the `machines/` files those CMake files name literally
(`${PROJECT_SOURCE_DIR}/machines/...`), and the `.cpp` of each suite in the web preset's list
(`test/host/CMakeLists.txt`'s `if(EMSCRIPTEN)` block). It fails too when that derives nothing: no suite list, no
closure, a closure without `emu_session`, or a library in it with no `add_library`. The rest is kept to `web` by
hand in the map, and nothing checks it: `third_party/minimp3/`, the BIOS's sources (`backplane/bios/`, of which
the embedded `bios.rom` and its labels are built), the web suites' headers (`sd_images.h`, `test_support.h`),
and the data they read (`test/host/sd/`, `test/host/mp3/`, `oodos/diskdefs`). `docs` is chosen by everything `docs/tools/check_docs.py` reads besides the books:
`backplane/bios/` (17 listings of its sources, byte for byte; `backplane.inc`'s equates), all of `oodos/` (an
entry for each utility and each built-in of `ccp.s`; `oodos.inc`'s functions), the files the books link to
(`apple_io.cpp`, `apps/emu/`, `apps/web/`, `oodos_rig.h`, `test_docs_examples.cpp`, the Apple-1 and Eater
machines, the emulator's READMEs and plan), and hw/, README.md and AGENTS.md, whose links into the books it
checks.

The ROM BIOS's `sd.s` and `bios.s` (its reset of the card) choose `sd` too, so interop: they are on the path of
the cards the tests write.

The selection is broad, and the owner accepted that (2026-11-11): a change to any card chooses every suite that
builds the backplane machine, about 60 of them, in the fast build (a minute or two), and the web steps. The
sanitizers still run only the `emu` suites among them.
`tools/test_map.py check` (the suite `test_test_map`) fails on a tracked path no rule matches, a rule that
matches nothing, a label no suite carries, or a suite's label no rule chooses.

**3. `tools/check.sh changed [BASE]`.** The paths `git diff --no-renames --name-only` gives from the merge base of
BASE (main) and HEAD to the working tree (committed or not; a new file counts once it is added; an untracked
file fails it, named, unless `--allow-untracked`). It builds `fast`,
then prints each path, its rule, its labels and how many suites they choose, and the labels, the suites and the
sanitizer suites in all. It runs the chosen suites (`ctest --preset fast -R` with their exact names: the same as
`-L` with those labels, and it takes the suites a path names by `self` too); the oodos checks if `oodos` is
chosen, `make -C docs check` if `docs` is, the WebAssembly build, vitest, the page build, the whole e2e suite on
Chromium and WebKit's smoke set if `web` is, and interop if `oodos` or `sd` is. Before any of it, it checks for the
tools those need (ca65, ld65 and cc65 from the build's cache; vasm when a chosen entry carries `eater`, whatever
path chose it; docker for the web steps and interop) and fails naming what's missing; it fails on a path whose
rule isn't `none` but chose no suite and no step (a `test_foo.cpp` no CMake file registers); and when every
chosen CTest entry skipped. A misspelt `--exclude` fails the step. Every
tier's summary counts the entries skipped or not run, by name. If a path is the emulator's C++ or its build (`emulator/**/*.cpp`, `.h`, the CMake files; not
`apps/web`), it builds `dev` and runs the chosen `emu` suites under ASan/UBSan, `--jobs` at once (4 by default).
An unmapped path fails it before anything runs; so does an empty diff, unless `--allow-empty`. A diff of `none`
paths alone says there is nothing to run, and passes.

**4. `full`** is `quick`'s steps, then `ctest --preset dev -L '^emu$'`, the whole e2e suite on Chromium
(`--project chromium`) and WebKit's smoke set, and interop (on the fast build's `test_oodos_card`).
**5.** Nothing runs an OODOS suite under the sanitizers now: neither tier's `dev` step can choose one.

**6. The split.** `harness.h` takes `OOE_TEST_CASES=a,b` (only those), `OOE_TEST_EXCEPT=a,b` (all but those),
`OOE_TEST_SHARD=K/N` (of the rest, the cases at K-1 modulo N, in the order they are registered) and
`OOE_TEST_LIST=1` (their names, none run); a name that is no case is an error. `set(EMU_SPLIT_<name> N)` before
a suite's `emu_add_test` makes N CTest entries `<name>_KofN` of one binary; `set(EMU_SPLIT_ALONE_<name> case...)`
gives each such case an entry of its own, `<name>_<case>`, the N shares taking the rest; and `<name>_cases`,
`check_split.py`, reads the entries' selections from `ctest --show-only` and fails unless every case of the whole
binary's is in exactly one. Every entry sets all four variables (empty where unused), and `check.sh` unsets
them, so one exported by hand changes nothing. No case or expectation changed. Measured in the fast build, each alone, on this host (2026-11-11; no
other fast-build suite was over 40 s, the next being `test_oodos_sys` and `test_oodos_model` at about 35):

| Suite | Cases | Alone before | Entries | Each, alone | Its longest case |
| --- | --- | --- | --- | --- | --- |
| `test_oodos_disk2_media` | 24 | 51 s | 4 | 20, 7, 16, 8 s | 9.4 s |
| `test_oodos_disk2` | 12 | 41 s | 3 | 10, 11, 19 s | 12.5 s |
| `test_oodos_utils_dir` | 17 | 40 s | 3 | 26, 6, 8 s | 16.9 s (the page and sheet sweep) |
| `test_oodos_utils_files` | 17 | 33 s | 2 | 26, 5 s | 22.5 s (files past 16 KB) |

A share can't be shorter than its longest case; splitting those cases would change the tests, so they stay.

`test_session` is split for the sanitizers (the owner's decision on question 2): its long case,
`the_speaker_and_the_sids_mix_into_the_same_stream`, has an entry of its own, and the other 21 cases two shares.
Under `dev`, in `check.sh changed main` at 4 jobs with the host's load about 12 (2026-11-11): the long case's
entry 71.8 s, the shares 31.3 and 40.1 s, against 120 s each (the whole suite had been 104-120 s, and timed out
once at 120.1 under load 17).

**7. The coverage check,** `tools/test_coverage.py`, on the `coverage` preset (Debug, gcc `--coverage`, no
sanitizers). It runs every suite alone (each with its own `GCOV_PREFIX`, so 8 run at once), reads with `gcov` the
repo's files each one executes (a line run at least once), maps each through the map, and fails on a suite that
runs a file whose labels it carries none of. Run once on 2026-11-11 (about 17 minutes at 8 at once): every suite
that builds the backplane machine ran every card's code (the cards' resets, the BIOS's probes, the splash's
sound), the bridge's suites ran the RTC and Apple II mode's I/O, and the sound card's ran the DOC and the SIDs.
Hence the `backplane` label and the parts-in-parts rules above, and a few labels besides (`debug` where a suite
loads symbols, `pia` and `debug` for the app's suites, `bridge` where a suite builds the bridge alone);
`test_disk2_prom` lost `bridge`, which it doesn't run. After those it finds nothing. It also showed that no
source file is run only by a non-`emu` suite: the OODOS, BIOS and Apple-mode suites reach no emulator file the
sanitized suites don't (by file, not by line).

To run it again, when a device or a suite is added (the owner's answer to question 3):

```
cd emulator && cmake --preset coverage && cmake --build --preset coverage -j8
cd .. && python3 -I -B tools/test_coverage.py --jobs 8 --work /tmp/cov    # MISSING lines: fix the map or the labels
python3 -I -B tools/test_coverage.py --from /tmp/cov/files.json           # after a fix: compare again, no rerun
```

(reconfigure the coverage build after changing the labels, so `--from` reads them).

**Later (accepted 2026-11-11, not built):** derive the hand-kept part of the web inputs above too (the files the
CMake builds into `bios.rom` and embeds, the headers and data the web suites read), so the map can't drift there
either; for now it is a known limit (review finding H1).

**Later (accepted 2026-11-11, not built):** extend `test_coverage.py` to compare line coverage between the `emu`
suites and the others, and give `emu` to a non-`emu` suite that alone reaches emulator lines; run it with the
coverage check, when a device or a suite is added.

pinled (the firmware this harness was copied from) has its own `harness.h`, without the case selection; the
two now differ there.


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

Now that the plan is built, a branch's check is `tools/check.sh changed`, and its report names what that chose.


## Open questions ##

1. Is `full` still wanted at all, or are `changed` on each branch and `quick` before each release enough?
   **Answered (2026-11-11):** `full` stays, only for a release whose emulator C++ changed: `quick`, plus the
   sanitizer build of the emulator's own suites, the whole e2e suite on Chromium with WebKit's smoke set, and
   interop.
2. `test_session` and `test_bios_sd` time out under the sanitizers on main as well. Should they get longer limits,
   or should we find out why they're slow there first? **Answered (2026-11-11):** find out first. Found (alone,
   this host, 2026-11-11), each whole suite in four builds:

   | Build | `test_session` | `test_bios_sd` |
   | --- | --- | --- |
   | `fast` (Release, no sanitizers) | 2.8 s | 4.6 s |
   | Debug, no sanitizers | 27.8 s | 45.6 s |
   | Release with ASan/UBSan | 36.5 s | 68.4 s |
   | `dev` (Debug with ASan/UBSan) | 105.6 s, limit 120 | 188.7 s, limit 300 |

   So `dev` is about 40 times `fast`, and it is two factors that multiply: -O0 (about 10 times) and the
   sanitizers (about 13-15 times at -O2). Neither suite does anything odd: both spend their time running machine
   time at the emulator's -O0, ASan speed. In `test_bios_sd` three cases each wait out the BIOS's 5 s splash (or its
   2 s without a video card), 152 of the 189 s; in `test_session` one case,
   `the_speaker_and_the_sids_mix_into_the_same_stream`, runs 5.1 s of machine time with the speaker and the SIDs
   mixed, 50 of the 106 s, and a few others power-cycle through the splash at 4-6 s each. Alone they fit their
   limits (88% and 63%); with 16 suites at once on the builder they didn't. Under this plan `test_bios_sd` (a BIOS
   suite) no longer runs under the sanitizers at all, and `test_session` (`emu`) does. Ways on, for the owner (none
   taken): build `dev` at -O1 (ASan's own advice; it would cut the -O0 factor most of the way, for every suite);
   let a test skip the splash (a BIOS option the machine sets, not a change to the BIOS's behaviour on hardware);
   or split `test_session` so its long case runs alone.
3. Should the coverage check (item 7) become part of `full`, so the map can't drift? **Answered (2026-11-11):** no;
   it runs when a device or a suite is added (item 7 says how).
4. `perf/wasm` is on GitHub and not merged (2026-11-05, "2.6 is not a win"): keep it or delete it? **Answered
   (2026-11-11):** deleted, on the owner's word.
