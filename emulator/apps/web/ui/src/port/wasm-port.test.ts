/**
 * WasmPort against the real WebAssembly module (public/core, copied by
 * `npm run core`) under node, with the clock stepped by hand.
 */
import { existsSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { beforeAll, describe, expect, test } from "vitest";
import type { FrameState } from "./machine-port";
import { coreUrl, MAX_FRAME_BUDGET_MS, MAX_SLICE, WasmPort, type CoreModule, type Scheduler } from "./wasm-port";

const core = fileURLToPath(new URL("../../public/core/oo1.mjs", import.meta.url));
const GREETING = "\rBACKPLANE BIOS 0.1\r\\\r";
const FRAME_MS = 1000 / 60;
const NATIVE_HZ = 7_159_090;

class ManualClock implements Scheduler {
  t = 0;
  /** How far now() moves each time it is read (for the Max loop). */
  tick = 0;
  now(): number {
    const t = this.t;
    this.t += this.tick;
    return t;
  }
  request(): number {
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
    // The monitor's loop: code at $FC8xx and the 6551 at $0C301, all reads.
    expect(f.lights!.addr[19]).toBeGreaterThan(0.5);
    expect(f.lights!.addr[14]).toBe(1); // high in both
    expect(f.lights!.addr[13]).toBe(0); // low in both
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

  test("a machine that will not open is a rejection with the reason", async () => {
    const port = new WasmPort({ load, scheduler: new ManualClock(), machine: "/machines/nope.machine" });
    await expect(port.connect()).rejects.toThrow(/nope\.machine/);
  });
});
