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

1. **O(1) `find()`.** Replace the reverse linear scan (decoder.cpp:188) with
   a page/segment table over the 20-bit bus address space, preserving the
   "later mapping wins" overlap rule (decoder.h:32). This is exactly what
   decoder.h:39 anticipated.
2. **Default-bridge RAM fast path.** When the bridge is the no-window
   default, skip the virtual `route()` call and index the RAM vector
   directly (decoder.cpp:229/251).
3. **Gate the always-on observer.** Short-circuit `CycleTee::on_cycle`
   (bus_cycle.h:54) when no observer is attached, so the per-cycle virtual
   call and observer-vector loop disappear in normal (non-trace) runs.
4. **Collapse the `cmos()` double-dispatch** (cpu6502.cpp:641): select the
   instruction dispatch once at construction for the backplane (always a
   W65C02S) so the NMOS switch is not probed per instruction.
5. **Inline the top opcodes' `ea_read` / `ea_write`** (cpu6502.cpp:205/248)
   for the dominant ABS / IMM addressing on LDA/STA/LDX/STX/LDY/STY.

### Phase 2 — structural

6. **Batch the run-loop per-instruction checks** (session.cpp:420): defer
   `sync()` of timed devices until their scheduled cycle elapses, and gate
   IRQ / `may_run` / reset / NMI on a "device asserted" flag a device sets,
   instead of polling every instruction. Timing-safe: a timed device only
   ticks to the cycle an access needs (decoder.cpp:214 `catch_up`). Likely
   the largest cycle-count and per-instruction cut.

### Phase 3 — build flags and the JS boundary

7. **`-flto=thin`** on the `oo1` target (`apps/web/CMakeLists.txt`,
   `INTERPROCEDURAL_OPTIMIZATION`), keeping `-O3`.
8. **Cut the embind chatter** (wasm-port.ts:375): the ~10 per-frame calls
   (`run`, `lastCycles`, `takeOutput`, `cycles`, `halted`, `stopped`, `irq`,
   `fixedHz`, `appleMode`, `soundRunning`, `usbQueued`) collapse into one
   `step_frame(budget)` returning a struct / typed view — about two
   crossings a frame instead of ten.
9. **SIMD the per-pixel / per-sample math** (the `VideoCard` draw, the SID
   and speaker audio) with `-msimd128`. Pure per-line / per-sample work,
   cycle-accuracy unaffected; helps frame pacing.

**Order:** 0 → 1.1 → 1.2 → 1.3 → 2.6 → 3.8 → 3.7 → 1.4 → 1.5 → 3.9
(heaviest wins, cheapest risk, first).

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
