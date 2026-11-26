# Testing: run what a change touches #

Nov 11, 2026 · @Jefferson

**Status: approved by the owner (2026-11-11); built 2026-11-11 (feat/test-changed): items 1-7 below, as "What was
built" says; item 8, test_bios_diag's slow share, approved and built 2026-11-18 (feat/test-diagslow).** The owner's answers to questions 1-3 are recorded under "Open questions".


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
| `quick` | Before staging, as now | Unchanged: every suite, fast build, a few minutes; the oodos checks and the GW-BASIC locale check |
| `changed` | Each branch, before review and merge | The suites the branch's paths choose, in the fast build; sanitizers on those that are emulator suites (`emu`), when emulator C++ changed; the oodos checks if `oodos` is chosen, the books' check if `docs` is; if `web` is, the WebAssembly build, vitest, the page build, the whole e2e suite on Chromium and WebKit's smoke set; interop if `oodos` or `sd` is; the GW-BASIC locale check if `gwlocale` is (`oodos/gwbasic/`, `oodos/lib/`) |
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
| A device each | `acia`, `bridge`, `console`, `disk2`, `dma`, `es5503`, `iie` (Apple II mode's I/O), `joy`, `lcd`, `pia`, `rng` (the north bridge's random number chip), `rtc`, `sd`, `sid`, `sound`, `usb`, `via`, `video` |
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
`fetch_fixtures.py`; until 2026-11-24 also `test/host/sound/` and `oodos/lib/mudrv/`), `web` (the web steps), and `none` (licenses,
editor settings, `basic/`, `oodos/spike/`, `apps/sidcheck/`: nothing tests them; every build still builds the
code among them). A part inside another chooses both (the RTC and Apple II mode's I/O are the bridge's, the DOC
and the SIDs the sound card's), and each card of the backplane machine chooses `backplane` too.

*The sound suites with sound card revision 3 (2026-11-24, OODOS 0.17).* Gone: `test_sound_driver` (revision 2's card
driven by a 6510 program of the host's, `test/host/sound/`) and `test_mudrv` (OOLIB's 6510 music driver), with what
they tested. New: the firmware alone (`test_sound_fw`), the card on the bus (`test_sound_device`), its streams
(`test_sound_stream3`), its sound bank (`test_sound_bank`) and its music (`test_sound_music`), labelled `sound`;
`test_oodos_sndfw` (SNDFW, `oodos bios sd sound dma backplane`); and the tools' `test_mksndflash` and `test_mksndfw`.
`test_oodos_sound`, `test_oodos_playsid`, `test_oodos_pb`, `test_oodos_mu`, `test_oodos_sysinfo`, `test_bios` and
`test_bios_diag` were rewritten for revision 3 ([Sound card revision 3's tests](#sound-card-revision-3s-tests)). In
the cleanup pass (2026-11-25) the suites of revisions moved past went too: `test_video` (video card revision 1; its
splash and Apple screen cases ported to `test_video_rev2` first) and `test_sound_pcm`, `test_sound_mix`,
`test_sound_mp3`, `test_sound_eq` and `test_sound_doc` (the sound card's revision 2 host interface; their EQ, vis
and MP3 cases ported to `test_sound_dsp` and `test_sound_stream3`). The list is
`emulator/test/host/CMakeLists.txt`'s.

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
Chromium and WebKit's smoke set if `web` is, interop if `oodos` or `sd` is, and the GW-BASIC locale check
(`tools/gwlocale.sh`, the step `gwbasic-locale`) if `gwlocale` is. Before any of it, it checks for the
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
`the_speaker_and_the_sound_card_mix_into_the_same_stream` (`the_speaker_and_the_sids_mix_into_the_same_stream`
until sound card revision 3), has an entry of its own, and the other 21 cases two shares.
Under `dev`, in `check.sh changed main` at 4 jobs with the host's load about 12 (2026-11-11): the long case's
entry 71.8 s, the shares 31.3 and 40.1 s, against 120 s each (the whole suite had been 104-120 s, and timed out
once at 120.1 under load 17).

**The GW-BASIC locale check** (the owner, 2026-11-20, fix/gwbasic-sort): `$(wildcard)` lists in the locale's
collation (glob), and GW-BASIC's link order is its images' layout, so an unsorted source list made GWBASIC.COM and
.OVL differ between LC_ALL=C and en_US.UTF-8 (`res/todo_i1.s` before `res/todo.s` under en_US). The Makefile's lists
are `$(sort ...)` now, and `tools/gwlocale.sh` builds GW-BASIC under both locales into directories of its own and
compares the two images byte for byte; it skips, saying so, where en_US.UTF-8 isn't installed. The map's special
label `gwlocale` (a step, no suite: `tools/test_map.py`'s STEPS) chooses it for `oodos/gwbasic/` and `oodos/lib/`;
`quick` and `full` run it always. Seen failing with RES_SRC's `$(sort)` removed (both images differ).

`test_gwbasic_gfx` is split in two (the owner, 2026-11-20, GW-BASIC 7e5's answer 18): with 7e5's cases (43 in
all, 36 before) the whole suite would have run near its 600 s. Its two shares carry the suite's labels, so any
graphics path chooses both, and `test_gwbasic_gfx_cases` holds every case to one of them (seen failing, as a
check, with a case left out of the second: "lost"). Alone, in the fast build (2026-11-20): 227 and 306 s, before
7e5's G43 joined them (about 15 s more), against 600 s each.

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

**8. A slow share** (the owner, 2026-11-18, after B7). `test_bios_diag` had become the gate's longest step: its
two entries ran 534 and 447 s in B7's last `changed main`, against 108 s for the next suite, and it is chosen by
nearly every change (the BIOS and most devices). Timed one case at a time at main b0100305 (fast, 8 at once): 64
cases, 1,030 s; the 41 that each make a whole diagnostics run (56 s of machine time, a full run more) take 13-67 s
each and 970 s in all, the other 23 60 s. So `set(EMU_SLOW_<name> case...)` with `EMU_SLOW_SPLIT_<name>` and
`EMU_SLOW_LABEL_<name>` (the split's code, item 6) puts 37 of the long cases (904 s) in four entries,
`test_bios_diag_slow_1of4` to `_4of4`, labelled `diagslow` only; the suite's own two shares take the 23 short
cases and four whole runs that cover the most: the golden quick run, the card stages on a good machine, bank 8's
stuck bit (on a revision 2 bridge then; on revision 3, the only one, since the cleanup pass), and block 0's stuck
bit (126 s). `check_split.py` still holds every case to
exactly one entry.

What chooses the slow share: a rule naming the suite (`test_bios_diag`: its golden reports, `fault_devices.h`) or
`self` (`test_bios_diag.cpp`), and `backplane/bios/diag*`, the diagnostics' own source, through the label
`diagslow`; not `bios.s` (its entries are the short cases'), nor another BIOS file or a device. `quick` and
`full` run every case, so all of it runs before a release. The risk taken: a device change that breaks only a
long fault case's text is found at the release's `quick` run, not at the branch's gate.

**Later (accepted 2026-11-11, not built):** derive the hand-kept part of the web inputs above too (the files the
CMake builds into `bios.rom` and embeds, the headers and data the web suites read), so the map can't drift there
either; for now it is a known limit (review finding H1).

**Later (accepted 2026-11-11, not built):** extend `test_coverage.py` to compare line coverage between the `emu`
suites and the others, and give `emu` to a non-`emu` suite that alone reaches emulator lines; run it with the
coverage check, when a device or a suite is added.

**Known fragility (found 2026-11-21; to fix later as its own item, the owner's answer 12):** `test_bios.cpp`'s
`Board::run` services the console only when `cpu->cycles() % 64 <= 7` after an instruction. With DMA channel 2
holding the bus under a burst limit, a monitor loop whose period, stolen cycles included, keeps clear of that window
never gets its keys: moving the code after ROMCALL by 86 bytes or more (so GETKEY's poll loop, `input.s`, crosses a
page and takes 24 cycles, not 23) made `c806_stops_a_programs_dma_before_its_fills` and
`apple_ii_mode_starts_with_the_dma_controller_as_reset_and_losing_no_cycle_to_it` stall ("only 3 of 6 keys were
taken"). The harness's sampling, not the ROM: real keys arrive whenever they come. Code added to the ROM goes at its
end, so that nothing moves that far, until the fix (sampling the console by elapsed cycles, not by a phase of the
count).

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


## North bridge revision 3's tests ##

Status: **built** (2026-11-22). The specs' test tables, moved here as built: each case, the failure it guards and the mutant it must fail under. The suites: `test_nb_timebase` and `test_nb_random` (labels `bridge`, `rng`), `test_oodos_random`, and new cases in the existing ones.

### The timebase's and the hole's ###

Each must-fail case is seen failing under its mutant, on its own case; each mutant is a defect plausible in real
bridge logic, wiring or hand-written 6502 code ([hw/testing.md](#): the new suite gets its labels,
`bridge` and `rtc`, and a map rule).

**The bridge** (`test_nb_timebase.cpp`, new):

| # | Case | The real failure it guards | Must fail under |
|---|---|---|---|
| B1 | `ticks_come_at_the_crystal_s_millisecond`: ticks 1-1,100 each on cycle ⌊78,750 k / 11⌋ from the release; MS 1,100 | drift: a divider that rounds (12.7 ppm fast, 1.1 s a day) | the 2-in-11 step removed (7,159 always); the long periods on the wrong count (3 in 11) |
| B2 | `the_latch_holds_four_bytes_across_a_carry`: `set_ms($00FFFFFE)`, MS0 read, the carry, then MS1-MS3: one moment; TBSUB from the same latch | a torn 32-bit read | MS1-MS3 read live; TBSUB not latched |
| B3 | `msf_interrupts_until_acknowledged`: MSF with MSIE pulls IRQH bit 5; a read leaves it; a 0 written leaves it; a 1 written lets it go; MSIE off lets it go | the interrupt not acknowledged, or acknowledged by a read | the acknowledge done by a read (as the VIA's `T1CL`); `irq()` not gated by MSIE |
| B4 | `a_tick_under_dma_is_counted`: a 64 KB copy holding the CPU 18 ms: MS +18 exactly, MSF and OVR set, one interrupt | a missed tick under DMA | the counter stepped by the acknowledge, not the tick; OVR never set |
| B5 | `mss_restarts_with_each_second`: the chip skewed +1,000 and -1,000 ppm, 30 s: at each SEC change MSS 0 or 1; never past 999; SEC and MSS from one latch never the old second with 0 | the ms-into-second not reset; a torn time | MSS from the free-running tick modulo 1,000 (not reset); MSSL/MSSH read live, not from SEC's latch |
| B6 | `the_tick_needs_no_clock_chip`: SQW stopped: SYNC clear 1.1 s after the last edge, MSS 999, the tick and MS unaffected; SYNC clear from power-on to the first edge | a bridge whose tick or MSS hangs on the DS3231 | the tick taken from SQW's divider; SYNC never cleared |
| B7 | `tie_is_gone_on_revision_3`: TICK set each second and cleared by 1; TIE written 1 reads 0, no interrupt (its revision 2 half went in the cleanup pass) | an unexpected 1 Hz interrupt in the tick's fast path | TIE kept on revision 3 |
| B8 | `resets_and_apple_ii_mode`: a hard reset: MS 0 at the first instruction, MSIE off; a /RESET pulse: MS runs on, MSIE off; in Apple II mode MS gains 1,000 a second | a counter that slows sevenfold in Apple II mode, or restarts on Ctrl-Reset | the divider counting PHI2; the pulse clearing MS |
| B9 | `a_peek_latches_nothing`: the debugger reads MS0 and SEC; a CPU read of MS1 still gives the CPU's latch | the WebUI's memory view tearing OODOS's reads | `peek()` calling `read()` |

**OODOS** (`test_oodos_clock`, `test_oodos_clock_disk2`, `test_oodos_input`, `test_oodos_clock_debug`, on CcpRig,
revision 3):

| # | Case | The real failure it guards | Must fail under |
|---|---|---|---|
| O1 | `the_tick_is_the_bridge_s`: at the prompt MSIE set, VIA IER 0, ACR 0, timer 1's latch as the test left it; `FC_MS`; the frames against the cycles (the ms printed as a record: the bridge's count can't be lost by OODOS). `FC_T1` is held by INFOB's and EV_CLOCKS's pins (`function_80_says_oolib_s_version...`, the event area's case) | OODOS still driving the VIA | CLKINIT still loading timer 1 |
| O2 | `ft_count_is_never_torn`: an event area open, `FT_COUNT` in a loop across MS's 24-bit carry, its start swept a cycle at a time against the tick: every result whole and monotonic | a torn read when the handler's latch lands between MS0 and MS3 | `FT_COUNT`'s interrupts-off removed; MS1 read before MS0 |
| O3 | `the_area_s_counter_is_whole_after_ticks_run_together`: just before a carry, interrupts held off 3 ms (a test program's SEI loop); EV_MS's four bytes right at the next tick | the area's high bytes a carry behind | OVR's test removed from the area path |
| O4 | `a_disk_ii_read_keeps_exact_time` (and the write's): a sector read and a write on O:; the frame counter within two frames. The ms are a record only (review r1's T8): the counter is the bridge's, which no OODOS defect can lose, so its half was removed | frames lost under the masked fields | (none for the ms: CATCHUP's removal can't show in the bridge's count) |
| O5 | `timer_1_is_the_program_s`: a program runs timer 1 one-shot and free-running with PB7, polling IFR bit 6, through 10 s; it sees every flag, OODOS's tick loses none; the warm boot puts ACR to 0, IER off | the program's flag cleared under it by OODOS | the dispatcher's VIASRV still taking timer 1's flag as the tick (reading `T1CL`) |
| O6 | the tick's cost (`test_oodos_input`): the measured figures, no area, layout 1, layout 2, pinned | a regression in the fast path | (the pin) |
| O7 | `oodos_refuses_an_older_bridge` (`test_oodos_sys`): `revision=1` and `revision=2`: the line naming `$D1` or `$D2`, then the monitor's prompt; no OODOS interrupt on, the card not written. *Removed in the cleanup pass (2026-11-25), with the older bridges and OODOS's refusal* | OODOS half-running on a bridge without the tick | the ID check accepting `$D2` and up |
| O8 | `ft_clock_reads_the_time_to_the_ms`: no block refused; two calls 250 ms apart are 250-300 ms apart by their own seconds and ms (they happen to fall across a second; the latch at the change isn't checked); the day and hour; `FK_SYNC` set, `FK_LOST` clear; 2156 refused | a torn or wrong time to the ms | MSSL read before SEC (no latch) |
| O9 | `hook_costs_by_the_bridge` (`test_oodos_clock_debug`): the tick hook measured within 4 cycles of OODOS 0.15's 479 less the tick's saving, timer 1 untouched | a debug build that disturbs the program's timer 1 | TBSUB read without MS0's latch |

The existing hook, stick, paddle, delay and "loses no tick" cases run on revision 3 unchanged.

**The diagnostics** (`test_bios_diag`): NB10 PASS on revision 3 (`nb10_passes`; its revision 2 SKIP went in the
cleanup pass), FAIL with a bridge fault option (MSS not restarted; MSF not cleared by a 1). Mutant: NB10's MSS compare removed, so the fault passes, and the case
must fail.

**Expectation changes (need approval):**

- the tick's cost pins (`test_oodos_input`) and the books' figures (measured in T3); the Disk II cases'
  tolerance (`test_oodos_clock_disk2`), made exact; `FT_INFO`'s interface 17 and its clocks byte; the tests'
  `ms_count()` and `sym("MS")` reads taking the bridge's counter;
- `test_rtc`'s `tick_with_tie_interrupts_as_irqh_bit_5` and `a_program_reads_the_time_and_takes_the_tick_interrupt`
  pinned to `revision=2` (the bridge's behaviour there is unchanged); both removed in the cleanup pass, with
  revision 2's bridge;
- **retired, with the VIA tick:** `test_oodos_clock`'s `a_stopped_timer_ends_a_delay_and_the_warm_boot_starts_it_again`
  and `a_changed_acr_is_put_back_by_the_warm_boot_losing_no_tick` (the second replaced by O5's ACR check), and
  `without_the_via_the_6551_s_interrupt_is_served_and_its_key_kept`'s expectation of no ms counter (it keeps its
  6551 checks; `FC_MS` is now set);
- **OODOS's revision 1 cases, retired** (OODOS 0.16 refuses `$D1`; O7 replaces them): `test_oodos_sysinfo`'s
  `on_a_revision_1_north_bridge_its_row_says_d1_and_revision_1`, `test_gwbasic_screen`'s
  `a_revision_1_bridge_has_peek_and_poke_but_not_bload`, and the `revision=1` cases of `test_oodos_video`,
  `test_oodos_gfx` and `test_oodos_sys` (each helper that inserts ` revision=1`);
- **unchanged** then: the ROM's and the emulator's own revision 1 and 2 cases (`test_bios`, `test_bios_diag`,
  `test_video_rev2`, `test_session`, `test_machine`), since the BIOS kept all three revisions; they went in the
  cleanup pass (2026-11-25), when the bridge became revision 3 only;
- the random source's O5 (`an_old_bridge_has_no_source`, `revision=2`) becomes O7's refusal.

Anything else that changes output is a finding to bring back.

### The random source's ###

Each must-fail case is seen failing under its mutant, on its own case; each mutant is a defect plausible in real
wiring, bridge logic or hand-written 6502 code.

**The bridge and the chip** (`test_nb_random.cpp`, new; `test_rtc` for C5):

| # | Case | The real failure it guards | Must fail under |
|---|---|---|---|
| C1 | `pool_full_at_power_on`: RNGCNT 32, READY, CHIP, LOCKED, BUSY clear at the first instruction; the bytes are the stream's first 32 | a board whose first program finds no bytes, or the wrong ones | the power-on draw skipped; the pool read from the wrong end |
| C2 | `an_unlocked_chip_is_never_drawn`: `rng_chip=unlocked`: CHIP set, LOCKED clear, RNGCNT 0, no Random on the bus | a mis-built board handing out the fixed pattern | the lock check removed; byte 86 tested instead of 87 |
| C3 | `crc_and_count_are_checked`: one corrupted response retried, the pool right; three, FAULT and an empty pool; a status group not taken as data. The bridge's CRC equals `atecc_crc.py`'s check values | corrupted bytes, or an error group read as 35 bytes | the CRC check removed; the count check removed; the bit order reversed |
| C4 | `no_chip_no_hang`: `rng_chip=none`: CHIP clear, BUSY never stuck, the clock's second unaffected | a board without the part, or a dead one, hanging the bus | a poll without its give-up |
| C5 | `the_clock_keeps_its_second_through_draws`: draws for 5 s straight: TICK each second, SEC right, RTCCTL's BUSY never set by a draw | a draw holding the bus across SQW's edge | one shared BUSY; a draw sent as one long transaction |
| C6 | `refill_after_the_last_byte`: 32 read, BUSY, 32 more that continue the stream; DRAW after FAULT clears it and refills | a pool that empties once and stays empty | the refill trigger removed; DRAW not clearing FAULT |
| C7 | `the_chip_is_woken_for_every_draw`: a second draw 2 s after the first | the second and later draws failing because the chip slept | the wake skipped after the first draw |
| C8 | `a_peek_takes_nothing`: the debugger reads RNGDATA ten times, RNGCNT unchanged | the WebUI's memory view draining the pool | `peek()` calling `read()` |
| C9 | `the_chip_is_polled`: the model's Random time set to 40 ms: the draw still succeeds, and the bridge's first read comes 1 ms after the command | a fixed wait that reads too early on a slower part | a fixed 23 ms wait in place of the poll |

**OODOS** (`test_oodos_random.cpp`, new, on CcpRig with the fixed stream):

| # | Case | The real failure it guards | Must fail under |
|---|---|---|---|
| O1 | `rn_fill_gives_the_chip_s_bytes`: n 1, 2, 32, 33, 255, exactly the stream's next n, nothing past +2+n | a write past the caller's block; bytes skipped across a refill | the count compare off by one; a refill byte read twice |
| O2 | `rn_fill_waits_for_ready`: n 64 straight after n 32, never `$00`s | an empty pool's `$00`s handed out | the READY wait removed |
| O3 | `rn_fill_recovers_from_a_fault`: one draw's three tries failed, DRAW written, bytes right; two draws failed, refused, no hang | a game hanging on a flaky chip, or given garbage | DRAW not written; the give-up removed |
| O4 | `rn_info_says_what_there_is`: locked; unlocked `RS_UNLOCKED`, RN_FILL refused; none 0, refused | a program unable to tell why it has no numbers | RS_UNLOCKED's test inverted |
| O5 | `an_old_bridge_has_no_source`: `revision=2`, RN_INFO 0, RN_FILL refused at once. *Gone: it became O7's refusal, and that went in the cleanup pass with the older bridges* | `$FF` from unused offsets taken as READY, CHIP and data | the NB_ID check removed |
| O6 | `the_interface_is_17`: FT_INFO; every existing function's cases unchanged. *Now `test_oodos_sound`'s `the_interface_is_18` (OOLIB interface 18)* | a program unable to tell whether 78 exists | (the pin) |
| O7 | SYSINFO's goldens: locked, unlocked, none (revision 2's went in the cleanup pass) | a wrong RANDOM line | the line's RS bit tested wrongly |

**The diagnostics** (`test_bios_diag`): NB9 PASS on a good machine; FAIL with `rng_chip=unlocked` and with the
injector making the chip answer a constant; SKIP with no chip. Mutant: NB9's byte compare removed, so the constant
chip passes, and the case must fail.

**Expectation changes (need approval):** NB_ID `$D2` to `$D3` in goldens; the version pins; SYSINFO's new line.
Anything else that changes output is a finding to bring back.

## Sound card revision 3's tests ##

Status: **built** (2026-11-24, the sound device's branches A, B and C; [hw/oodos.md, "Sound card revision 3, as
built"](oodos.md#sound-card-revision-3-as-built)). The card's spec's acceptance and its build plan's test cases, moved
here as built (hw/future/, removed in the cleanup pass, 2026-11-25): each must-fail case was seen failing under its
mutant, on its own case; each mutant is a defect plausible in a real card's logic or in hand-written 6502 code.

**The suites:** the firmware alone (`test_sound_fw`), the card on the bus (`test_sound_device`), its streams
(`test_sound_stream3`), its sound bank (`test_sound_bank`), its music (`test_sound_music`), the EQ and the vis page
alone (`test_sound_dsp`, from the cleanup pass), all labelled `sound`; `test_oodos_sndfw` (SNDFW); the tools'
`test_mksndflash` and `test_mksndfw`. `test_oodos_sound`, `test_oodos_playsid`, `test_oodos_pb`, `test_oodos_mu`,
`test_oodos_sysinfo`, `test_bios`, `test_bios_diag` and `test_gwbasic_sound` were rewritten for revision 3.
`test_usb`'s speaker cases and `test_iie`'s bell are as they were.

### The acceptance ###

| The spec's acceptance | Met by |
|---|---|
| The corpus plays identically: for every one of the 61,201 tunes, the 6510's SID writes (address, value, SID-clock cycle), the play calls and each chip's own output the same as revision 2's register path's, the known failures the same 148 | `test_sid_corpus`'s device mode (`SID_DEVICE=1`, `SID_COMPARE`) against S0's recorder (`SID_RECORD`), the full sweep at S11: 60,610 of 60,610 alike; by default a sample of 12, both ways (`a_sample_of_the_tunes_plays_on_the_device`, `the_device_records_a_tune_as_the_register_path_does`) |
| A stream bigger than card memory: from an OODOS program, 60 s of 44.1 kHz 16-bit stereo raw PCM (10.6 MB) on an 8 MB card, fed in chunks on NEED, plays with no underrun, byte for byte at the card's tap | `test_sound_stream3`'s `a_stream_bigger_than_the_store_plays_with_no_underrun`; `test_oodos_sound`'s `a_file_bigger_than_the_store_streams_from_b_with_no_underrun` |
| Gapless: two streams queued by STREAM_NEXT give exactly one stream's frames (raw PCM); an MP3 pair with gapless headers the trimmed frames | `two_streams_queued_are_one_stream_at_the_tap`, `a_join_s_output_is_one_stream_s`, `an_mp3_pair_with_gapless_headers_is_trimmed`; `test_oodos_sound`'s `two_files_queued_by_sn_next_join_with_no_gap` |
| Underrun: a feeder held back makes UNDERRUN events whose count and positions match STREAM_STATUS; the sound back at the next frame due | `underruns_counted_as_status_says_and_sound_back_at_the_next_frame_due`, `each_underrun_end_counts_its_own_frames` |
| Sharing: a one-SID tune with effects on SID B and samples on the ES5503 writes what it writes alone | `test_sound_bank`'s `a_tune_s_writes_are_the_same_with_effects_on_sid_b_and_the_es5503` |
| Music: 10,000 notes in one transfer end with one MUSIC_DONE; PLAY(n) and ON PLAY(n) exact; a 3,600 s SOUND one entry | `test_sound_music`'s `ten_thousand_notes_in_one_block_end_with_one_done`, `notes_waiting_are_exact_and_the_threshold_fires_once_below_n`, `a_3600_s_note_is_one_entry`; `test_gwbasic_sound`'s `sound_s_ranges_by_hand`, `play_mb_runs_in_the_background_and_play_n_counts`, `on_play_fires_when_the_buffer_falls_to_n_minus_1` |
| The beep plays over a tune and a stream, at its pitch within 0.1% | `the_beep_is_at_its_pitch_within_0_1_percent_over_a_tune_and_a_stream`; `test_oodos_sound`'s `sn_beep_lasts_its_length_at_its_pitch_on_the_card` |
| Time-independence: each feature's output the same at audio rate 0 and 48,000, however ticked | `each_source_s_output_is_the_same_at_rate_0_and_48000`, stream3's `the_same_at_rate_0_and_48000`, `a_record_runs_20_us_after_its_last_byte_wherever_in_a_sample_it_lands` |
| Native and WebAssembly builds pass; the sanitizers clean on the release candidate | the full tier at each branch's end; `the_mixed_output_is_the_same_bits_natively_and_in_webassembly` (from the cleanup pass, the owner's answer: one second of the card's mix hashed and pinned, the same in both builds) |

### The cases and their mutants ###

**The firmware and the card** (`test_sound_fw`, `test_sound_device`):

| Case | The real failure it guards | Must fail under |
|---|---|---|
| `every_command_gets_one_reply_in_order` | replies out of order under a waiting transfer | the reply of a command that waits for a transfer sent first |
| `a_full_event_fifo_loses_events_never_replies` (an event flood, then 20 commands: every reply comes; EVLOST and LOSTCLR's count right) | a real card's event FIFO overflowing under a chatty stream | no room reserved for replies |
| `a_record_with_the_wrong_n_is_refused_at_the_door` (ERROR, STATUS's ERR; the next record parses) | a parser that loses its place | the parser resyncing on the wrong byte |
| `the_store_refuses_what_doesn_t_fit_at_each_size` (2, 4, 8 MB) | a store size misread | MEMSIZE's units read as KB |
| `checksum_is_crc_32_of_what_came` (against a table computed in the test) | DMA's delivery not checked end to end | the last byte left out |
| `a_load_s_bytes_go_in_by_channel_2_demand_dma_and_checksum` (DRQ falls when the transfer ends) | a request between transfers | revision 2's standing request (DRQ with no transfer open) |
| `evt_is_a_level_and_no_event_is_lost_between_the_drain_and_the_clear` | an event arriving after the handler's last read, before its ISR write | EVT as an edge cleared by the write |
| `xleft_latches_all_three_bytes_on_the_low_read` | a torn read across a byte carry | no latch |
| `reset_abandons_a_transfer_and_empties_both_fifos_but_keeps_the_store`, `fwrestart_empties_the_store`, `a_byte_to_data_with_no_transfer_is_dropped`, `cmdfree_and_cmdrdy_hold_a_whole_record` | the resets and the door as the spec says | |
| `pause_holds_the_6510_s_cycles_and_silences_its_chips` | a paused tune that runs on silently | the 6510 left running at volume 0 (its cycle count moves) |
| `a_tune_plays_its_song_and_its_play_calls_are_counted`, `a_stalled_tune_says_so_after_2_s`, `a_jam_stops_the_tune_with_e_jam`, `the_length_and_fade_end_it_once`, `a_tune_takes_whole_chips_and_gives_them_back`, `a_one_sid_tune_s_read_of_d423_gives_sid_a_s_d403` | the tune player's events and chips | the read mirror removed (the last) |
| `the_beep_is_at_its_pitch_within_0_1_percent_over_a_tune_and_a_stream` | the beep taking a tune's voice | the beep on a SID voice (the tune's SID writes change) |
| `a_mono_tune_is_heard_in_both_ears`, `the_eq_and_vis_page_cover_the_sids`, `the_boot_sound_plays_from_the_card_s_flash` | the ADC path, the boot sound's sequence | |
| `a_good_image_boots_after_fwrestart_and_says_its_version`, `a_bad_crc_or_another_card_s_image_leaves_the_old_one`, `a_restart_during_the_flash_write_keeps_the_old_image`, `selftest_names_the_part_that_fails` | a bad update that boots | the CRC not checked |

**Streams, the bank, music** (`test_sound_stream3`, `test_sound_bank`, `test_sound_music`):

| Case | Must fail under |
|---|---|
| `a_stream_bigger_than_the_store_plays_with_no_underrun` | a NEED sent while one is outstanding (the feeder sends twice, `E_MEMORY`) |
| `two_streams_queued_are_one_stream_at_the_tap` | one frame of silence at the join |
| `underruns_counted_as_status_says_and_sound_back_at_the_next_frame_due` | a new prebuffer after an underrun |
| `a_tune_s_writes_are_the_same_with_effects_on_sid_b_and_the_es5503` | an effect allowed onto a tune's chip |
| `stealing_takes_the_lowest_priority_oldest_voice` | the newest stolen |
| `a_bank_bigger_than_wave_ram_plays_each_sample` (300 KB of samples) | |
| `ten_thousand_notes_in_one_block_end_with_one_done` | a done each note |
| `notes_waiting_are_exact_and_the_threshold_fires_once_below_n` | fired at n, not below |
| `a_3600_s_note_is_one_entry` | the length cut to 16 bits |
| `music_is_refused_e_busy_when_a_tune_holds_its_chips`, `a_block_that_doesn_t_parse_is_refused_at_xferdone` | |

The MP3 cases ported from revision 2's suite in the cleanup pass (`sixty_four_kb_with_no_frame_is_error_81` through
`damaged_frames_mid_stream_play_as_minimp3_with_the_end_at_once_or_late`) run through the firmware's stream decoder,
their reference minimp3 trimmed by an encoder's header where a file has one.

**OODOS** (`test_oodos_sound`, on CcpRig):

| Case | The real failure it guards | Must fail under |
|---|---|---|
| `a_program_s_record_and_the_handler_s_never_mix` (a stream's NEED lands while a program's IDENTIFY is being written; both get their replies; an interrupt anywhere inside the record write counts) | two records interleaved on CMD | the sender without SEI (`E_PARAM`) |
| `a_card_that_never_answers_is_sn_busy_in_100_ms` (and a late reply during the next call) | a call that hangs on a dead card | no time-out |
| `the_cold_boot_clears_a_half_record_left_by_the_rom` (a stray `$00` in CMD before the boot) | a record half written by a program before Break | no RESET strobe |
| `the_warm_boot_quiets_the_card_and_puts_back_its_defaults_by_wboot_ctrl_c_and_break` | a program's sound playing on | |
| `sn_beep_lasts_its_length_at_its_pitch_on_the_card` | GW-BASIC's BEEP not waiting | SN_BEEP returning at the REPLY |
| `a_bel_rings_the_card_s_bell`, `a_bel_while_the_frame_sends_its_records_reaches_the_card_whole` | the bell lost, or mixed with the handler's records | BEL dropped; HRECT's store before SEI |
| `each_load_s_address_is_checked_as_function_51_s_segment` | a load over OODOS's own memory | a block past memory accepted |
| `a_stream_s_buffers_chain_across_needs_with_no_gap`, `acceptance_44_1_khz_16_bit_stereo_raw_pcm_from_b_with_no_underrun`, `acceptance_an_mp3_from_b_plays_as_minimp3_decodes_it`, the review's stream cases (`with_no_area_a_full_ring_is_fed_at_each_need` through `sn_status_s_block_is_its_own_reply_s_with_the_frame_s_between`) | the stream on NEED | each review fix's own mutant |
| one case a group of the new operations: `tunes_load_play_pause_status_and_info`, `the_bank_sample_effects_play_and_stop`, `music_block_control_and_status`, `the_mix_eq_vis_sidregs_and_levels`, `sn_firmware_updates_the_card_s_image` | | |

`test_oodos_pb` (the stall rule on +116-119, `PB_LEVELS` the card's own, `PB_DOC` refused, no card or one never
ready), `test_oodos_mu` (MU_* on blocks, a note's count exact from the card), `test_oodos_playsid` (RSIDs play, the
overlay cases kept) and `test_oodos_sndfw` (an image from M: written, marked to boot and run after FWRESTART; a bad
one refused; the short files) follow.

**The ROM** (`test_bios`, `test_bios_diag`):

| Case | Must fail under |
|---|---|
| `break_stops_every_sound_on_the_card` (a tune and a stream playing; nothing at the tap after Break) | QUIET's write removed |
| `the_splash_sends_bootsound_at_frame_0_s_blank` | BOOTSOUND sent before the blank |
| `c818_beep_and_stopall` (carry clear with a card; set with none, and with a card that never comes READY, inside 250 ms + 100 ms; another's REPLY waiting is dropped) | no time-out; SREAD taking any REPLY |
| `stage_10_names_a_selftest_failure`, `stage_10_s_checksum_catches_a_dropped_byte`, `stage_10_s_checksum_catches_a_corrupted_byte`, `stage_10_s_event_path_catches_a_dead_interrupt`, `a_card_that_never_comes_ready_fails_s1_and_skips_the_rest` (the slow share) | each one's compare removed |

**The session and the page:** `test_session`'s `a_sid_tune_plays_on_the_card_changes_song_stops_and_goes_with_a_power_cycle`
(the firmware model's tune player; mutant: `soundRunning` left on revision 2's CTRL RUN, so the tune bar ends a
tune at once), `the_speaker_and_the_sound_card_mix_into_the_same_stream` and
`in_apple_ii_mode_the_sound_card_keeps_the_apple_clock_s_time`; vitest's wasm-port sound cases and the Devices
pane's revision 3 view; `e2e/sound.spec.ts`'s smoke case (a BEEP record typed into `$C503` at WOZMON, EVT's REPLY
read back).

**GW-BASIC** (`test_gwbasic_sound`, branch C's block F): `sound_plays_its_tone_on_the_tone_generator`,
`sound_s_ranges_by_hand`, `beep_and_ctrl_g_sound_the_bell`, the play_* cases in Hz and µs,
`on_play_given_after_the_notes_fires_as_the_count_falls_to_n_minus_1`, `stop_end_and_break_with_background_music`;
mutants: the instrument a SID voice's, the µs cut to 16 bits, 32,767 sent as a tone, the tick 55,000 µs, PLAY's µs
rounded to the ms, the threshold set to n.

**Measurement records** (not guards; each brought to the owner once): `play_s_note_cost_is_timed` (8,243 to 3,481
cycles, its bound 1,000-9,000), `the_stream_s_cost_to_the_host`, the full budget's end line with a stream playing,
the diagnostics' quick run (60.3 s), `pb_vis_costs`.

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
   `the_speaker_and_the_sids_mix_into_the_same_stream` (now `..._the_sound_card_mix_...`), runs 5.1 s of machine time with the speaker and the SIDs
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
