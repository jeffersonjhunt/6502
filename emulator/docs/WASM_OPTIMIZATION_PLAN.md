# WASM optimization plan — the OO-1 emulator's browser core

The WebAssembly core (`apps/web`, built by the `web` preset) runs the whole
backplane machine — CPU, video, sound, every card — behind one embind
`Machine`, driven by `requestAnimationFrame` in `ui/src/port/wasm-port.ts`.
It is slow in the browser. This is the plan to make it reach the native
7.16 MHz near-realtime on desktop Chrome, without changing what the machine
does.

**The hard constraint:** the core is **bus-cycle accurate** — one bus cycle
per `read()`/`write()`/`idle()`, dummy reads, wait states, VPB vector reads,
and per-instruction IRQ sampling are all real (`decoder.h`, `session.cpp`).
No change may coalesce cycles, drop a dummy read, skip a wait state, or
reorder the sampling. Every phase is gated on identical cycle counts and a
faster clock.

## Where the time goes

Every instruction runs 1–6 bus cycles. Each bus cycle is a chain, all on the
hot path:

```
Cpu6502::read / write        cpu6502.cpp:44/55
  -> Decoder::read / write   decoder.cpp:241/256
       -> access()           decoder.cpp:224
            -> route()       virtual NorthBridge::route, decoder.h:121
            -> find()        reverse linear scan of mappings_, decoder.cpp:188
       -> catch_up()         tick the mapping's timed device, decoder.cpp:214
       -> device->read()     decoder.cpp:251
  -> observer_->on_cycle()   CycleTee loops its observers, bus_cycle.h:54
```

After **every** instruction, `Session::run` (session.cpp:420) re-checks
`take_start`, `stopped`, `may_run`, `irq()` — which calls `sync()` and ticks
**all** timed devices (video card, clock, SID) — `take_reset` (twice, for the
bridge and the USB card), `take_nmi` (twice), and the console-service timer.

The `web_boot` test already measures speed (boot.mjs:157–166): 60 frames of
`FRAME = 119318` cycles at 7.16 MHz, asserting the result is faster than real
time. That is the functional gate; it is not precise enough to A/B a change.

## Phases

Each phase: build the `web` target, run `--bench` and `web_boot`, compare
against the Phase 0 baseline, keep the change only if cycle counts are
unchanged and it is faster. `tools/web_build.sh` (configure + build +
`ctest --preset web`) must be green, and the host `fast` preset's Harte and
Dormann suites must still pass (cycle accuracy).

### Phase 0 — baseline and hotspot accounting (this work)

1. `--bench [cycles]` mode in `apps/web/test/boot.mjs`: after the boot
   checks, run a long, representative loop (default 100 M cycles, ~14 s of
   machine time) and report **MHz emulated** and **wall-clock % of one core**,
   averaged over at least 3 trials. The existing 60-frame check stays as the
   functional gate.
2. An `EMU_PROFILE` compile-definition: `Decoder::access` / `find` / `route`
   tally how many cycles each stage consumed (decode-scan, route, RAM index,
   `catch_up`, wait states), exposed through `Machine.profile()`. The bench
   prints the breakdown. This is the measurement `decoder.h:39` was waiting
   for ("anything cleverer waits for a measurement that says it is needed").
3. Record the baseline MHz and the stage breakdown here.

**Exit:** a repeatable `node boot.mjs --bench <oo1.mjs>` that prints MHz and
a stage breakdown; `web_boot` still green; the baseline committed below.

### Phase 1 — low-risk, high-confidence (each independent, bench-gated)

1. ~~**O(1) `find()`.**~~ **Scrapped after the experiment.** A 2 KiB
   page/segment table (one index per page of the mapped span) is the wrong
   shape for this bus: mappings are added at build time but can be arbitrarily
   small and adjacent — the real `backplane.machine` has 18, and `test_decoder`
   maps a 4-byte PIA inside a 32 KB ROM and three 16-byte chips back to back.
   Mappings that share a page collide in the table and last-wins gives the
   wrong chip (the `later_mapping_wins` / `timed_devices` cases break).
   `find()`'s 15.3% is not the scan's fault — with ≤18 mappings the reverse
   scan is trivial; the 15.3% is the *per-access measurement equal to
   route and device by construction*, i.e. the cost of one more function in
    the per-access chain. The per-instruction cost was thought to be better
    attacked in the run loop (Phase 2.6), but 2.6 measured not-a-win in a
    clean build — the real per-access cost is the `route`+`find`+`device`
    dispatch this is about.
 2. **RAM fast path** (decoder.cpp `access`/`read`/`write`). See the
    "Where the wall-time actually goes" note: ~36% of wall-time is
    `route`+`find`+`device` on the four RAM blocks (91% of accesses), but a
    safe version must duplicate the backplane bridge's live window routing in
    the Decoder — real cycle-accuracy risk. The "skip `catch_up` for RAM"
    variant was built and measured: **0.0% gain** (the `catch_up` cost is the
    timed devices' `tick()`, not the RAM no-op). This is now the top
    remaining structural target, since 2.6 measured not-a-win.
3. **Gate the always-on observer.** Short-circuit `CycleTee::on_cycle`
   (bus_cycle.h:54) when no observer is attached, so the per-cycle virtual
   call and observer-vector loop disappear in normal (non-trace) runs.
4. **Collapse the `cmos()` double-dispatch** (cpu6502.cpp:641): select the
   instruction dispatch once at construction for the backplane (always a
   W65C02S) so the NMOS switch is not probed per instruction.
5. **Inline the top opcodes' `ea_read` / `ea_write`** (cpu6502.cpp:205/248)
   for the dominant ABS / IMM addressing on LDA/STA/LDX/STX/LDY/STY.

### Phase 2 — structural

6. **Batch the run-loop per-instruction checks** (session.cpp:420).
   **Investigated, not a win** — see the "2.6: the run loop's
   per-instruction polling — measured, not a win" note. In a *clean* Release
   build the loop is already ~30 MHz host; the `take_*` edges are 3-line
   bool reads (batching them: 30.3 → 30.5, noise), `irq()` and `may_run()`
   must stay per-instruction (level IRQ; fetch breakpoints), and ticking only
   the IRQ-wired timed devices saves 0.0%. The wall-sampler profile that made
   this look like ~54% of wall-time was the sampler's own overhead. The real
   per-access cost is the `route`→`find`→`device` dispatch — **1.2 / 2.3**.

### Phase 3 — build flags and the JS boundary

7. **`-flto=thin`** on the `oo1` target (`apps/web/CMakeLists.txt`,
   `INTERPROCEDURAL_OPTIMIZATION`), keeping `-O3`.
8. **Cut the embind chatter** (wasm-port.ts `advance`): the per-frame status
   reads (`run`, `lastCycles`, `cycles`, `halted`, `stopped`, `irq`,
   `fixedHz`, `appleMode`, `soundRunning`, `usbQueued`) collapse into one
   `stepFrame(budget)` returning a `StepFrame` struct — one crossing per run
   slice instead of two for the run plus eight for the status after.
   **Done.** Not a raw-MHz win (the bench's run loop bypasses it; 11.2 →
   11.3, noise) — a *frame-pacing* win: fewer JS↔wasm crossings a browser
   frame leaves more of the frame budget to the run. Verified: 276/276 UI
   tests + `tsc --noEmit` clean against the real wasm module; the two
   hand-clocked tests that wrapped `run` to advance a ManualClock now wrap
   `stepFrame` (and the wasm-abort test patches `stepFrame` to throw).
9. **SIMD the per-pixel / per-sample math** (the `VideoCard` draw, the SID
   and speaker audio) with `-msimd128`. Pure per-line / per-sample work,
   cycle-accuracy unaffected; helps frame pacing.

**Order:** 0 → 1.1 → 1.2 → 1.3 → 3.8 → 3.7 → 1.4 → 2.6 → 1.5 → 3.9
(heaviest wins, cheapest risk, first). 1.1/1.3/2.6/3.7/1.4/3.8 are
**measured, not a win** (see "Tested, not a win"); 1.2 is the top remaining
structural target.

## Tested, not a win (recorded so they are not retried)

These were built, benched against the 11.2 MHz baseline, and **reverted**:

- **1.1 O(1) `find()`** — the wrong structure (see the Phase 1.1 note above);
  a page table collides on tiny/adjacent mappings and returns the wrong chip.
- **1.3 gate `CycleTee` when empty** — does not apply: the tee is never empty.
  The session always attaches the bus recorder and the Console card
  (session.cpp:163-166), the latter watches every cycle for its lights and
  breakpoint, and the former already early-returns when tracing is off
  (session.cpp:62). There is no empty-tee fast path to take.
- **1.4 cache `cmos()`** — made it *slower* (11.2 → 9.8 MHz). A `cmos_` bool
  member pushed the hot `model_`/`regs_` out of a register at the ~15
  call-sites per instruction. The `== Model::W65C02S` comparison is cheaper
  than the extra load.
- **3.7 `-flto`** — no gain (11.2 → 11.1, within noise). The cost is dynamic
  dispatch (the per-access `route`/`find`/`device` virtual chain) and the
  embind boundary, not cross-translation-unit inlining.
- **2.6 batch the run-loop polling** — not a win in a *clean* build (30.3 →
  30.5 MHz for batching the latched `take_*`; 0.0% for ticking only the
  IRQ-wired timed devices in `irq()`). The per-instruction `irq()`/`may_run()`
  must stay per-instruction and the `take_*` are trivial bools. See the 2.6
  note. The wall-sampler that motivated it inflated the stages it timed.

Conclusion: the "safe" per-access, build-flag **and run-loop** levers do not
move the needle in a clean build. The remaining win is structural and carries
cycle-accuracy risk, so it must be gated on the host Harte/Dormann/
`test_decoder` suites *and* the Session-based card tests, not just the bench:
**1.2 (the RAM fast path, folding `route`+`find`+`device` for the four RAM
blocks)** — ~36% of wall-time is that dispatch on the four RAM blocks (91% of
accesses) — at the cost of duplicating the backplane bridge's live window
routing in the Decoder. **3.8 (one embind `step_frame`)**, already done, is
the JS-side cut that needed no C++ cycle reasoning.

### Where the wall-time actually goes (measured, host Release -O3)

The count profile (route=find=device=15.3% each) is equal *by construction*
and says nothing about wall-time. A throwaway `steady_clock` sampler per
stage, running the BIOS boot (40 M cycles, host Release, no ASan), measured:

| stage | % of wall-clock | what it is |
|---|---|---|
| `device` | 14.3% | the access into the card (RAM vector index; the timed cards' read/write) |
| `find` | 11.5% | the mapping reverse-scan |
| `route` | 10.5% | the bridge's `route()` (window/VEC/Apple math) — a virtual call through `NorthBridge*` |
| `catch_up` | 10.3% | **the timed devices' `tick()`** (video drawing the splash, the RTC, sound) — real work, 8.6% of accesses |
| `other` | 10.7% | `take_bus`/DMA/`granting` bookkeeping in `access()` |
| **the bus access chain** | **~57%** | the rest (~43%) is `execute()`'s opcodes |

Access mix: **91.4% untimed** (RAM + ROM), 8.6% timed. Two findings:

1. **Every function in the chain is a real, non-inlined call at -O3**
   (confirmed by `nm`): `Cpu6502` → `Bus::read` (virtual) → `Decoder::read`
   → `Decoder::access` → `NorthBridge::route` (virtual) → `Decoder::find`
   → `Decoder::catch_up` → `Device::read` (virtual). ~6 calls per bus access,
   3 of them virtual. That is why the chain is 57% of wall-time.
2. **Skipping `catch_up()` for the 91% untimed mappings saved 0.0%** (10.3%
   before, 10.3% after, total 7.3 → 7.2 MHz). The `catch_up` cost is the
   timed devices' `tick()` (real work), not the no-op call on RAM. So a
   "skip catch_up for RAM" fast path is **not a win** (reverted).

This reframes 1.2: the "fold `route`+`find`+`device` for the four RAM blocks"
target is ~36% of wall-time, but doing it safely means **duplicating the
backplane bridge's live window routing** (the `win_`/`vec_` registers, the
I/O hole, Apple II mode's map, the RO bits) in the Decoder, invalidated on
every window write — a real cycle-accuracy risk for a ~36% *ceiling* (the
measured gain is lower, since `route`/`find` are partly the call overhead and
the gain is bounded by what -O3 already inlines). The cleaner structural
attack is **2.6** (the run loop's per-instruction polling) plus making the
chain inline-able, which avoids duplicating bridge logic.

 ### 2.6: the run loop's per-instruction polling — measured, not a win

A throwaway `steady_clock` sampler around each stage of `Session::run`
(session.cpp:420) *looked* like the per-instruction polling dominated: `step`
~35%, `irq()`/`sync()` ~25%, `take_*` ~14.5%, `may_run` ~7.5%, `service`
~7.3% — ~54% of wall-time on polling. **That profile was the sampler's own
overhead, not the machine's cost.** Measured the right way — a *clean*
Release -O3 build, no sampler, the same 40 M-cycle BIOS boot the wasm bench
runs — the loop is already **~30 MHz host** (the wasm's 11.2 MHz is
wasm-side overhead, not the C++ loop), and the safe sub-wins are not wins:

- **Batching the latched `take_*` edges** (Apple/Ctrl-Reset `/RESET`,
  Pause/Break `/NMI`) from every instruction to the 64-cycle `service()`
  cadence: **30.3 → 30.5 MHz, within noise — not a win.** Every `take_*` is
  a 3-line bool read-and-clear (`console_card.cpp:115`, `usb_card.cpp:204`,
  `backplane_bridge.cpp:79`); calling them every instruction is a register
  load + branch. The sampler's "14.5%" was the cost of *timing* them. (The
  change was built and passed the Session-based card gate — test_session's
  Apple-II-mode reset tests, test_oodos_*, test_sd — so it is *safe*, it is
  just not worth it.)
- **Ticking only the timed∩IRQ-wired devices in `irq()`** (not all timed):
  **0.0%**. In the backplane 5 of 6 timed devices are IRQ-wired, so the set
  barely changes; `irq()`'s cost is the *fixed* per-instruction overhead
  (the 8 virtual `irq()` calls + the `tick()`s + the `synced` checks), paid
  every instruction even though the IRQ line almost never changes.

So the per-instruction polling is **already as cheap as it gets** in a clean
build: `irq()` must stay per-instruction (level-sensitive, sampled at
instruction boundaries), `may_run(pc)` must stay per-instruction (fetch
breakpoints halt *before* the instruction), and the `take_*` are trivial
bools. **2.6 has no safe win.** The real wall-time is the `step()` and its
bus chain (the `route`→`find`→`device` dispatch, ~the "step" bucket), which is
**1.2 / 2.3** territory — not the run loop. The wall-sampler method is now
known to *inflate* the stages it times; use a clean build for real numbers.

## Baseline

_Captured in Phase 0. Every later phase diffs against this._

**Phase 0 baseline** — 2026-11-04, this host (x86-64 Linux, Node 22, one
core), Emscripten 6.0.12, `-O3` Release, `EMU_PROFILE` on. Command:

```
tools/wasm_build_stdin.sh <dir> profile     # (the bind-mount build is unavailable here)
node apps/web/test/boot.mjs <dir>/oo1.mjs --bench 100000000
```

```
speed: 11.2 MHz emulated; real time at 7.16 MHz takes 64% of a core
bench: trial 1: 100004000 cycles in 8.93 s -> 11.2 MHz emulated (64% of one core)
bench: trial 2: 100004000 cycles in 8.92 s -> 11.2 MHz emulated (64% of one core)
bench: trial 3: 100004000 cycles in 8.93 s -> 11.2 MHz emulated (64% of one core)
bench: best of 3 trials: 11.2 MHz emulated
profile: 2303660682 bus cycles: route 15.3% | find 15.3% | device 15.3% | catchUp(timed) 4.0% | wait 0.0%
```

So the core runs the machine at **11.2× its 7.16 MHz clock** — about **1.6
times** real time, at 64% of a core. It is *faster than real time already*,
but the browser's frame driver (60 fps, audio, the video copy) eats the rest,
so there is roughly **6×** of headroom to reclaim for a comfortable margin.

The `wait` stage is always 0: no device on the backplane machine imposes
wait states (`m->wait` and the bridge's `wait_states()` are both 0), so
every access is one cycle plus its device time. The profile's
`route`/`find`/`device`/`catchUp` split is **counts of bus cycles**, not
wall time — `route` is always 1:1 with `find` (one `route()` per `find()`),
and `device` is 1 per access. The wall-time cost the plan is after is
where the *machine* time goes inside those counts: the profile says
whether the cycles are **device-driven** (a card's `tick()`/`read()` —
`catchUp` counts those) or **decode-driven** (the scan in `find`).

| | MHz emulated | % of one core | route | find | device | catchUp | wait |
|---|---|---|---|---|---|---|---|
| Phase 0 baseline | 11.2 | 64 | 15.3% | 15.3% | 15.3% | 4.0% | 0.0% |

`route` + `find` + `device` are **50%** of all bus cycles — the decode and
dispatch chain. `catchUp` is 4%, and `wait` is always 0 (the backplane
imposes no wait states). The profile's `total` is *accesses*, not cycles
(each access is 1 cycle here), so 2.3 billion accesses for 150 million
machine cycles is ~15 accesses per cycle of instruction stream — every
access carries its own `route`/`find`/`device` chain.

That split is what Phase 1 is measured against: **`find()` (15.3%)** is the
linear-scan decode and the first target (Phase 1.1, the O(1) page table);
**`device` (15.3%)** is the access into the card (the RAM fast path,
Phase 1.2); **`route` (15.3%)** is the bridge's address translate. `catchUp`
(4%) is where timed-device `tick()` time shows — if Phase 2 (deferring
timed-device `sync()`) helps, `catchUp` falls.

**Cycle-accuracy gate (verified):** the `fast` preset is green — `test_decoder`
(15/15, decode + wait states), and, with the fixtures fetched
(`tools/fetch_fixtures.py`), all of Harte (6502: 24 400 cases, wdc65c02:
25 400 cases, 0 failed) and Dormann (6502, w65c02s, and the 65C02 extended
suite). The Phase 0 counters changed `decoder.cpp` only to add tallies;
every bus cycle is still identical.
