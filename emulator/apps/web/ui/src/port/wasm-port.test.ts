/**
 * WasmPort against the real WebAssembly module (public/core, copied by
 * `npm run core`) under node, with the clock stepped by hand.
 */
import { existsSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { beforeAll, describe, expect, test } from "vitest";
import type { FrameState } from "./machine-port";
import { usageFor } from "../crt/usb-keyboard";
import { startApple, typeReset } from "../ui/apple-bar";
import { fakeIIeRom } from "./fake-iie";
import { fakeSid } from "./fake-sid";
import { coreUrl, FIXED_FRAME_BUDGET_MS, MAX_FRAME_BUDGET_MS, MAX_SLICE, WasmPort, type CoreModule, type Scheduler } from "./wasm-port";

const core = fileURLToPath(new URL("../../public/core/oo1.mjs", import.meta.url));
const GREETING = "\rBACKPLANE BIOS 0.8\r\\\r";
const FRAME_MS = 1000 / 60;
const NATIVE_HZ = 7_159_090;

class ManualClock implements Scheduler {
  t = 0;
  requests = 0;
  /** How far now() moves each time it is read (for the Max loop). */
  tick = 0;
  now(): number {
    const t = this.t;
    this.t += this.tick;
    return t;
  }
  request(): number {
    ++this.requests;
    return 1;
  }
  cancel(): void {}
}

let load: () => Promise<CoreModule>;
beforeAll(() => {
  // No core is a failure, not a skip: the UI is nothing without it.
  expect(existsSync(core), `${core}: run npm run core (needs emulator/tools/web_build.sh)`).toBe(true);
  load = async () => ((await import(pathToFileURL(core).href)) as { default: () => Promise<CoreModule> }).default();
});

const ascii = (s: string) => Uint8Array.from(s, (c) => c.charCodeAt(0));

async function open() {
  const clock = new ManualClock();
  const port = new WasmPort({ load, scheduler: clock });
  const frames: FrameState[] = [];
  port.onFrame((f) => frames.push(f));
  await port.connect();
  let out = "";
  const step = (ms = FRAME_MS) => {
    clock.t += ms;
    port.frame();
    const f = frames[frames.length - 1];
    out += String.fromCharCode(...f.serialOut);
    return f;
  };
  const until = (want: string, max = 60) => {
    for (let i = 0; i < max && !out.includes(want); ++i) step();
    return out.includes(want);
  };
  return { port, clock, step, until, output: () => out, clear: () => (out = "") };
}

describe("coreUrl", () => {
  test("next to the page, with the build's version", () => {
    expect(coreUrl("oo1.wasm", "https://x.test/projects/oo-1/emulator/", "abc123")).toBe(
      "https://x.test/projects/oo-1/emulator/core/oo1.wasm?v=abc123",
    );
  });
  test("no version: no query", () => {
    expect(coreUrl("oo1.mjs", "http://127.0.0.1:8000/index.html", undefined)).toBe("http://127.0.0.1:8000/core/oo1.mjs");
  });
});

describe("WasmPort", () => {
  test("boots in frame budgets: clock x frame time", async () => {
    const m = await open();
    const before = m.step().cycles;
    const after = m.step().cycles;
    const budget = Math.round((NATIVE_HZ * FRAME_MS) / 1000);
    expect(after - before).toBeGreaterThanOrEqual(budget);
    expect(after - before).toBeLessThan(budget + 16);
    expect(m.until(GREETING)).toBe(true);
    expect(m.output()).toBe(GREETING);
  });

  test("a long gap runs at most 100 ms", async () => {
    const m = await open();
    const before = m.step().cycles;
    const after = m.step(60_000).cycles; // a minute in a background tab
    expect(after - before).toBeGreaterThanOrEqual(715_909);
    expect(after - before).toBeLessThan(715_909 + 16);
  });

  test("the clock setting scales the budget", async () => {
    const m = await open();
    m.port.setClock(1_020_484);
    const before = m.step().cycles;
    const after = m.step(100).cycles;
    expect(Math.abs(after - before - 102_048)).toBeLessThan(16);
  });

  test("Max runs slices until 10 ms of the frame are used", async () => {
    const m = await open();
    m.port.setClock("max");
    m.clock.tick = 1; // each read of the clock "takes" 1 ms
    const before = m.step().cycles;
    const after = m.step().cycles;
    const slices = Math.round((after - before) / MAX_SLICE);
    expect(slices).toBeGreaterThanOrEqual(MAX_FRAME_BUDGET_MS - 2);
    expect(slices).toBeLessThanOrEqual(MAX_FRAME_BUDGET_MS + 2);
  });

  test("a fixed clock the host can't keep up with stops at its share of the frame, and the status says how fast it really ran", async () => {
    const m = await open();
    m.step();
    let f = m.step(100);
    for (let i = 0; i < 12; ++i) f = m.step(100); // over a second at full speed: the measured clock is the set one
    expect(Math.abs(f.hz - NATIVE_HZ) / NATIVE_HZ).toBeLessThan(0.01);
    m.clock.tick = 1; // each read of the clock "takes" 1 ms: a slow host
    const before = m.step(100).cycles;
    const after = m.step(100).cycles;
    const slices = Math.round((after - before) / MAX_SLICE);
    expect(slices).toBeGreaterThanOrEqual(FIXED_FRAME_BUDGET_MS - 2);
    expect(slices).toBeLessThanOrEqual(FIXED_FRAME_BUDGET_MS + 1); // not the 14.3 slices 100 ms wants
    for (let i = 0; i < 12; ++i) f = m.step(100);
    expect(f.hz).toBeGreaterThan(0);
    expect(f.hz).toBeLessThan(NATIVE_HZ * 0.9);
  });

  test("a listener that throws is reported and the others still hear; the loop goes on", async () => {
    const m = await open();
    const heard: number[] = [];
    const errors: unknown[] = [];
    const error = console.error;
    console.error = (...args: unknown[]) => void errors.push(args);
    try {
      m.port.onFrame(() => {
        throw new Error("a bug in the page");
      });
      m.port.onFrame((f) => void heard.push(f.cycles));
      const asked = m.clock.requests;
      m.step();
      m.step();
      expect(heard.length).toBe(2);
      expect(m.clock.requests).toBe(asked + 2);
      expect(errors.length).toBe(1); // once, not once a frame
    } finally {
      console.error = error;
    }
  });

  test("a core that throws (a wasm abort) stops the loop and says why, once", async () => {
    const m = await open();
    m.until(GREETING);
    const failures: string[] = [];
    m.port.onFail((why) => failures.push(why));
    (m.port as unknown as { m: { run: () => number } }).m.run = () => {
      throw new Error("RuntimeError: unreachable");
    };
    const asked = m.clock.requests;
    m.step();
    m.step();
    expect(m.clock.requests).toBe(asked); // not rescheduled
    expect(failures).toEqual(["RuntimeError: unreachable"]);
    m.port.sendSerial(ascii("C680\r")); // sends are dropped, as before connect()
    await expect(m.port.readMemory(0xc680, 1, "cpu")).rejects.toThrow();
  });

  test("serial both ways: a monitor command", async () => {
    const m = await open();
    expect(m.until(GREETING)).toBe(true);
    m.port.sendSerial(ascii("C680.C684\r"));
    expect(m.until("C680: 00 01 02 7F 7F")).toBe(true);
  });

  test("halt through the panel, then CONT steps one instruction", async () => {
    const m = await open();
    m.until(GREETING);
    m.port.setPanel({ halt: true, busCycle: false });
    const a = m.step();
    const b = m.step();
    expect(b.halted).toBe(true);
    expect(b.cycles).toBe(a.cycles);
    m.port.press("cont");
    const c = m.step();
    expect(c.cycles - b.cycles).toBeGreaterThanOrEqual(2);
    expect(c.cycles - b.cycles).toBeLessThanOrEqual(7);
    expect(c.halted).toBe(true);
  });

  test("lights, registers and the trace, only when asked", async () => {
    const m = await open();
    m.until(GREETING);
    let f = m.step();
    expect(f.lights).toBeNull();
    expect(f.regs).toBeNull();
    m.port.setLights(true);
    m.port.watchRegisters(true);
    m.port.setTrace(true, 64);
    f = m.step();
    expect(f.lights!.addr.length).toBe(20);
    expect(f.lights!.data.length).toBe(8);
    // The monitor's loop: code at $FCBxx, the 6551 at $0C301 and the USB card at $0C5xx, all reads.
    expect(f.lights!.addr[19]).toBeGreaterThan(0.5);
    expect(f.lights!.addr[14]).toBe(1); // high in all three
    expect(f.lights!.addr[13]).toBe(0); // low in all three
    expect(f.lights!.rwb).toBe(1);
    expect(f.lights!.vpb).toBe(0);
    expect(f.regs!.pc).toBeGreaterThanOrEqual(0xc800);
    const rows = await m.port.readTrace(64);
    expect(rows.length).toBe(64);
    for (let i = 1; i < rows.length; ++i) expect(rows[i].cycle).toBe(rows[i - 1].cycle + 1);
    const fetch = rows.find((r) => r.sync)!;
    expect(fetch.physical).toBe(0xf0000 | fetch.cpu);
    expect(fetch.module).toBe(3);
    expect(rows[rows.length - 1].cycle).toBeLessThanOrEqual(f.cycles);
  });

  test("the video card's frames: only when watched, only when new, at 60 a second", async () => {
    const m = await open();
    m.until(GREETING);
    expect(m.step().video).toBeNull();          // not watched
    m.port.watchVideo(true);
    let f = m.step();
    const v = f.video!;
    expect(v.width * v.height * 4).toBe(v.pixels.length);
    expect(v.frames).toBeGreaterThan(0);
    const at = (x: number, y: number) => Array.from(v.pixels.subarray((y * 640 + x) * 4, (y * 640 + x) * 4 + 4));
    expect(at(0, 18)).toEqual([170, 170, 170, 255]); // the B of BACKPLANE (row 1), in light grey
    expect(at(6, 18)).toEqual([0, 0, 0, 255]);
    expect(m.step(0).video).toBeNull();         // no time, no new frame
    f = m.step(2 * FRAME_MS);
    expect(f.video!.frames).toBeGreaterThan(v.frames);
    m.port.setClock(1_020_484);                 // slow CPU: still 60 frames a second
    const before = f.video!.frames;
    let last = before;
    for (let i = 0; i < 10; ++i) last = m.step(100).video?.frames ?? last;
    expect(last - before).toBeGreaterThanOrEqual(59);
    expect(last - before).toBeLessThanOrEqual(61);
    m.port.watchVideo(false);
    expect(m.step().video).toBeNull();
  });

  test("the USB keyboard and mouse: typed into the monitor, the mouse read back by it", async () => {
    const m = await open();
    m.until(GREETING);
    m.clear();
    const code = (ch: string) =>
      /[A-Z]/.test(ch) ? `Key${ch}` : /[0-9]/.test(ch) ? `Digit${ch}` : ({ ".": "Period", ":": "Semicolon", " ": "Space", "\r": "Enter" } as Record<string, string>)[ch];
    const type = (text: string) => {
      for (const ch of text) {
        const shift = ch === ":";
        if (shift) m.port.sendKey(0xe1, true);
        m.port.sendKey(usageFor(code(ch))!, true);
        m.step(5);
        m.port.sendKey(usageFor(code(ch))!, false);
        if (shift) m.port.sendKey(0xe1, false);
        m.step(5);
      }
    };
    type("C680.C684\r");
    expect(m.until("C680: 00 01 02 7F 7F")).toBe(true);
    type("C590: 1\r");                              // MCTRL: absolute
    m.port.sendMouse({ x: 320, y: 240, buttons: 1 });
    type("C592.C595\r");                            // the monitor reads MX: that latches
    expect(m.until("C592: 40 01 F0 00")).toBe(true);
    type("C591\r");
    expect(m.until("C591: 01")).toBe(true);         // the left button
  });

  test("Apple II mode fixes the clock: the budget follows it, whatever setClock said", async () => {
    const m = await open();
    m.until(GREETING);
    expect(m.step().fixedHz).toBe(0);
    m.port.sendSerial(ascii("C685: 1\r"));        // MODE, from the monitor: no ROM needed for this
    let f = m.step();
    for (let i = 0; i < 60 && !f.fixedHz; ++i) f = m.step();
    expect(f.fixedHz).toBe(1_020_484);
    m.port.setClock("max");                       // ignored while the mode fixes it
    const before = m.step().cycles;
    const after = m.step(100).cycles;
    expect(Math.abs(after - before - 102_048)).toBeLessThan(16);
  });

  test("the IRQ line, for the panel's lamp", async () => {
    const m = await open();
    m.until(GREETING);
    expect(m.step().irq).toBe(false);
    m.port.cardWrite(0x18, 0x01); // IEN: interrupt on a panel key
    m.port.press("load");
    expect(m.step().irq).toBe(true);
    m.port.cardWrite(0x19, 0x01); // ISR: write 1 to clear
    expect(m.step().irq).toBe(false);
    m.port.setPower(false);
    expect(m.step().irq).toBe(false); // nothing is lit with the power off
  });

  test("labels, physical addresses and memory reads", async () => {
    const m = await open();
    m.until(GREETING);
    const echo = await m.port.resolve("ECHO");
    expect(echo).not.toBeNull();
    expect(await m.port.resolve("NOWHERE")).toBeNull();
    expect(await m.port.physical(echo!)).toBe(0xf0000 | echo!);
    expect(Array.from(await m.port.readMemory(0xc680, 5, "cpu"))).toEqual([0x00, 0x01, 0x02, 0x7f, 0x7f]);
    expect(Array.from(await m.port.readMemory(0xf0000 | echo!, 4, "physical"))).toEqual(
      Array.from(await m.port.readMemory(echo!, 4, "cpu")),
    );
  });

  test("power off stops the clock; power on is a cold start", async () => {
    const m = await open();
    m.until(GREETING);
    m.port.sendSerial(ascii("300: 42\r"));
    m.until("0300: 00");
    expect(Array.from(await m.port.readMemory(0x300, 1, "cpu"))).toEqual([0x42]);
    m.port.setPower(false);
    const off = m.step();
    expect(off.powered).toBe(false);
    expect(m.step().cycles).toBe(off.cycles);
    m.port.setPower(true);
    expect(Array.from(await m.port.readMemory(0x300, 1, "cpu"))).toEqual([0]);
    m.clear();
    expect(m.until(GREETING)).toBe(true);
  });

  test("the sound card's sound: only when listening, a frame's worth a frame, kept through a power cycle", async () => {
    const m = await open();
    m.until(GREETING);
    expect(m.step().audio).toBeNull(); // not listening: none made
    m.port.listen(48_000);
    // SID A from the monitor: $1000 (about 245 Hz), a triangle, attack 0, sustain F, volume 15
    m.port.sendSerial(ascii("C500: 00 10 00 00 11 00 F0\r"));
    expect(m.until("C500:")).toBe(true);
    m.port.sendSerial(ascii("C518: 0F\r"));
    expect(m.until("C518:")).toBe(true);
    let frames = 0;
    let sq = 0;
    for (let i = 0; i < 30; ++i) {
      const a = m.step().audio!;
      expect(a).toBeInstanceOf(Float32Array);
      frames += a.length / 2;
      for (let k = 0; k < a.length; k += 2) sq += a[k] * a[k];
      if (i === 0) expect(a.length).toBeGreaterThan(0);
    }
    expect(Math.abs(frames - 30 * 800)).toBeLessThan(800); // 48 kHz x 30 frames of 1/60 s
    const rms = Math.sqrt(sq / frames);
    expect(rms).toBeGreaterThan(0.05); // a triangle at full envelope: 1/8 peak, 0.072 RMS
    expect(rms).toBeLessThan(0.1);
    m.port.setPower(false);
    expect(m.step().audio).toBeNull();
    m.port.setPower(true); // a new machine: still listening, and silent
    const after = m.step().audio!;
    expect(after.length).toBeGreaterThan(0);
    expect(Math.max(...after.map(Math.abs))).toBeLessThan(0.01);
    m.port.listen(0);
    expect(m.step().audio).toBeNull();
  });

  test("the speaker ($C030 in Apple II mode, $C5A6 here) is in the sound too, on both sides", async () => {
    const m = await open();
    m.until(GREETING);
    m.port.listen(48_000);
    // LDA $C5A6, then three rounds of 256 DEXes, JMP $0300: a 926 Hz square.
    m.port.sendSerial(ascii("300: AD A6 C5 A0 03 A2 00 CA D0 FD 88 D0 F8 4C 00 03\r"));
    expect(m.until("0300:")).toBe(true);
    m.port.sendSerial(ascii("300R\r"));
    for (let i = 0; i < 6; ++i) m.step();
    let sq = 0;
    let n = 0;
    for (let i = 0; i < 20; ++i) {
      const a = m.step().audio!;
      for (let k = 0; k < a.length; k += 2) {
        expect(a[k + 1]).toBe(a[k]);
        sq += a[k] * a[k];
        ++n;
      }
    }
    const rms = Math.sqrt(sq / n);
    expect(rms).toBeGreaterThan(0.18); // a square at +-0.2
    expect(rms).toBeLessThan(0.21);
  });

  test("a .sid tune: onto the card, its songs, heard, stopped; a bad file refused, the tune playing on", async () => {
    const m = await open();
    m.until(GREETING);
    m.port.listen(48_000);
    expect(m.step().soundRunning).toBe(false);
    const t = await m.port.playSid(fakeSid({ songs: 3, name: "Test Tone", flags: 0x24 }));
    expect(t).toEqual({ name: "Test Tone", author: "OO-1 tests", released: "2026", songs: 3, song: 1, rsid: false, clock: "PAL", model: "8580", warnings: [] });
    expect(m.step().soundRunning).toBe(true);
    const loud = () => {
      let sq = 0;
      let n = 0;
      for (let i = 0; i < 20; ++i) {
        const a = m.step().audio!;
        for (const v of a) sq += v * v;
        n += a.length;
      }
      return Math.sqrt(sq / n);
    };
    expect(loud()).toBeGreaterThan(0.05); // a triangle at full envelope, on both sides (a mono tune)
    expect((await m.port.playSong(3)).song).toBe(3);
    m.step(); // the card runs init again
    expect(Array.from(await m.port.readMemory(0x501, 1, "io"))).toEqual([0x18]); // SID A's pitch: song 3's
    await expect(m.port.playSong(4)).rejects.toThrow(/song 4 of 3/);
    await expect(m.port.playSid(new Uint8Array([1, 2, 3]))).rejects.toThrow(/not a \.sid file/);
    expect(m.step().soundRunning).toBe(true); // the tune plays on
    m.port.stopSid();
    expect(m.step().soundRunning).toBe(false);
    loud(); // the DC settling
    expect(loud()).toBeLessThan(0.002);
    const big = await m.port.playSid(fakeSid({ rsid: true, sid3: 0x44 }));
    expect(big.rsid).toBe(true);
    expect(big.warnings).toEqual(["it needs a third SID, at $D440", "an RSID tune expects a whole C64 (VIC-II, CIA 2, ROMs)"]);
    m.port.setPower(false);
    m.port.setPower(true);
    expect(m.step().soundRunning).toBe(false); // a power cycle: gone
  });

  test("a machine that will not open is a rejection with the reason", async () => {
    const port = new WasmPort({ load, scheduler: new ManualClock(), machine: "/machines/nope.machine" });
    await expect(port.connect()).rejects.toThrow(/nope\.machine/);
  });

  test("a cartridge in, Start Apple IIe: Apple II mode, its switches, the cards in I/O space, Ctrl-Reset", async () => {
    const m = await open();
    expect(m.until(GREETING)).toBe(true);
    expect(m.step().apple).toBe(false);
    expect((await m.port.readApple())?.mode).toBe(false);
    m.clear();
    await m.port.insertCartridge({ rom: fakeIIeRom(), chars: null, diskBoot: null, diskSequencer: null });
    expect(m.until(GREETING)).toBe(true); // plugged in with the power off: a fresh boot
    m.port.press("cont"); // anything running...
    startApple(m.port); // ...START gets back to the monitor first
    let f = m.step();
    for (let i = 0; i < 120 && !f.apple; ++i) f = m.step();
    expect(f.apple).toBe(true);
    expect(f.fixedHz).toBe(1_020_484);
    m.step();
    expect((await m.port.readMemory(0x0400, 2, "physical"))[0]).toBe(0xc1); // the stand-in's "A"
    expect((await m.port.readMemory(0x0300, 1, "physical"))[0]).toBe(1); // reset once
    const s = await m.port.readApple();
    expect(s?.mode).toBe(true);
    expect(s?.altchar).toBe(true); // its STA $C00F
    expect(f.disk?.motor).toBe(false); // its motor on and off: the drive runs on
    expect(f.disk?.spinning).toBe(true);
    expect(s?.text).toBe(true);
    expect((await m.port.readMemory(0xc4ff, 1, "io"))[0]).toBe(0x91); // the video card, though...
    expect((await m.port.readMemory(0xc4ff, 1, "cpu"))[0]).not.toBe(0x91); // ...the CPU sees the Apple's $C4FF
    typeReset(m.port, false); // Ctrl-Delete on the USB keyboard
    for (let i = 0; i < 10; ++i) f = m.step();
    expect((await m.port.readMemory(0x0300, 1, "physical"))[0]).toBe(2);
    expect(f.apple).toBe(true); // Ctrl-Reset stays in Apple II mode
    m.port.setPower(false);
    expect(m.step().apple).toBe(false);
  });

  test("a cartridge that does not fit is refused, the old one stays; out, C806R says so", async () => {
    const m = await open();
    await m.port.insertCartridge({ rom: fakeIIeRom(), chars: null, diskBoot: null, diskSequencer: null });
    await expect(m.port.insertCartridge({ rom: new Uint8Array(100), chars: null, diskBoot: null, diskSequencer: null })).rejects.toThrow(/16 KB/);
    await expect(m.port.insertCartridge({ rom: fakeIIeRom(), chars: new Uint8Array(100), diskBoot: null, diskSequencer: null })).rejects.toThrow(/character ROM/);
    m.port.setPower(false);
    m.port.setPower(true); // the next power cycle reads the cartridge's files again: the refused ones did not touch them
    m.clear();
    expect(m.until(GREETING)).toBe(true);
    m.port.sendSerial(ascii("C806R\r"));
    let f = m.step();
    for (let i = 0; i < 120 && !f.apple; ++i) f = m.step();
    expect(f.apple).toBe(true); // the first one is still in
    await m.port.insertCartridge(null);
    m.clear();
    expect(m.until(GREETING)).toBe(true);
    m.port.sendSerial(ascii("C806R\r"));
    expect(m.until("NO APPLE IIE ROM")).toBe(true);
  });

  test("the SD card: in, written by the machine, saved, kept through a power cycle, out", async () => {
    const m = await open();
    expect(m.until(GREETING)).toBe(true);
    expect(m.step().sd).toEqual({ present: false, busy: false, changed: false });
    await expect(m.port.insertSd(new Uint8Array(100), "tiny.img")).rejects.toThrow(/tiny\.img: not an SD card image/);
    expect(await m.port.insertSd(new Uint8Array(64 * 512), "raw.img")).toEqual({ name: "raw.img", sectors: 64, writeProtected: false, changed: false });
    for (let i = 0; i < 4; ++i) m.step(); // 50 ms to start up
    // WRITE sector 0 of the raw card, $AA throughout, by a CPU loop; back to the monitor.
    m.clear();
    m.port.sendSerial(ascii("1000: A9 01 8D 8A C3 9C 8B C3 9C 84 C3 9C 86 C3 A9 30 8D 83 C3\r"));
    m.port.sendSerial(ascii("1013: A0 00 A9 AA 8D 8C C3 C8 D0 FA 8D 8C C3 C8 D0 FA 4C 80 C8\r1000R\r"));
    let lit = false;
    for (let i = 0; i < 30; ++i) lit ||= m.step().sd!.busy; // the command's light, though it's done within a frame
    expect(m.output()).toContain("1000R");
    expect(lit).toBe(true);
    expect(m.step().sd).toEqual({ present: true, busy: false, changed: true });
    expect((await m.port.readSdInfo())?.changed).toBe(true);
    const saved = await m.port.saveSd();
    expect(saved.length).toBe(64 * 512);
    expect(Array.from(saved.subarray(0, 512)).every((b) => b === 0xaa)).toBe(true);
    expect(saved[512]).toBe(0);
    expect(m.step().sd!.changed).toBe(false); // saved
    m.port.setSdWriteProtect(true);
    expect((await m.port.readSdInfo())?.writeProtected).toBe(true);
    m.port.setPower(false);
    m.port.setPower(true); // a new machine: the card is still in the slot
    expect((await m.port.readSdInfo())?.name).toBe("raw.img");
    expect((await m.port.saveSd())[0]).toBe(0xaa);
    m.port.ejectSd();
    expect(await m.port.readSdInfo()).toBeNull();
    await expect(m.port.saveSd()).rejects.toThrow(/no card in the slot/);
  });

  test("disks: in, saved in each format, write-protected, kept through a power cycle, out", async () => {
    const m = await open();
    const dsk = Uint8Array.from({ length: 143_360 }, (_, i) => (i * 7 + (i >> 8)) & 0xff);
    await expect(m.port.insertDisk(0, new Uint8Array(5), "notes.txt")).rejects.toThrow(/notes\.txt: 5 bytes is no disk image/);
    const info = await m.port.insertDisk(0, dsk, "Random.dsk");
    expect(info).toEqual({ name: "Random.dsk", format: "dsk", writeProtected: false, changed: false, writes: 0 });
    expect(m.step().disk).toEqual({ motor: false, spinning: false, drive: 0, q6: false, q7: false, data: 0, phases: 0, quarterTracks: [0, 0] });
    expect(await m.port.saveDisk(0, "dsk")).toEqual(dsk); // the same 140 KB, by way of the tracks
    const po = await m.port.saveDisk(0, "po");
    expect(po.length).toBe(143_360);
    expect(po).not.toEqual(dsk);                          // the same sectors, another order
    const woz = await m.port.saveDisk(0, "woz");
    expect(String.fromCharCode(...woz.subarray(0, 4))).toBe("WOZ2");
    m.port.setWriteProtect(0, true);
    expect((await m.port.readDiskInfo(0))?.writeProtected).toBe(true);
    m.port.setPower(false);
    m.port.setPower(true);                                // a new machine: the disk is still in the drive
    expect((await m.port.readDiskInfo(0))?.name).toBe("Random.dsk");
    expect(await m.port.saveDisk(0, "dsk")).toEqual(dsk);
    const again = await m.port.insertDisk(1, woz, "Copy.woz");
    expect(again.format).toBe("woz");
    expect(await m.port.saveDisk(1, "dsk")).toEqual(dsk); // the WOZ reads back as the same sectors
    m.port.ejectDisk(0);
    expect(await m.port.readDiskInfo(0)).toBeNull();
    await expect(m.port.saveDisk(0, "woz")).rejects.toThrow(/no disk in drive 1/);
    expect((await m.port.readDisk2())?.drive).toBe(0);
  });
});
