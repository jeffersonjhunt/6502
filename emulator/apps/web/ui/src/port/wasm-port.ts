/**
 * MachinePort over the emulator compiled to WebAssembly (emulator/apps/web).
 *
 * It runs on the main thread, one budget of cycles per animation frame:
 * clock rate x the time since the last frame, capped at 100 ms so a tab
 * that was in the background does not try to catch up on minutes. At
 * "max" it runs 50,000-cycle slices until 10 ms of the frame are used.
 * Everything else is a direct call into the module. Calls that only send
 * are dropped until connect() has finished (the panel sends its state once
 * connected); reads before then reject.
 *
 * The scheduler and the module loader are injectable, so tests can step
 * frames by hand against the real module under node.
 */

import type {
  Clock,
  FrameState,
  Lights,
  MachinePort,
  PanelKey,
  PortCaps,
  Registers,
  TraceRow,
} from "./machine-port";
import { CLOCKS_HZ } from "./machine-port";

/** The embind class from bindings.cpp. */
export interface CoreMachine {
  open(machine: string, rom: string, symbols: string): boolean;
  error(): string;
  run(budget: number): number;
  lastCycles(): number;
  powerCycle(): boolean;
  hasCard(): boolean;
  setSwitches(swr: number): void;
  setPanel(halt: boolean, busCycle: boolean): void;
  setLock(locked: boolean): void;
  press(keys: number): void;
  start(): void;
  cardRead(offset: number): number;
  cardWrite(offset: number, value: number): void;
  send(bytes: string): void;
  takeOutput(): string;
  regs(): Registers;
  cycles(): number;
  halted(): boolean;
  stopped(): boolean;
  irq(): boolean;
  peek(addr: number): number;
  peekPhysical(addr: number): number;
  physical(addr: number): number;
  resolve(text: string): number;
  lights(): Float32Array;
  setLights(on: boolean): void;
  setTrace(on: boolean, depth: number): void;
  trace(max: number): Uint32Array;
  delete(): void;
}

export interface CoreModule {
  Machine: new () => CoreMachine;
}

export interface Scheduler {
  now(): number;
  request(callback: () => void): number;
  cancel(handle: number): void;
}

export interface WasmPortOptions {
  /** Default: import core/oo1.mjs next to the page. */
  load?: () => Promise<CoreModule>;
  scheduler?: Scheduler;
  /** The files built into the module. */
  machine?: string;
  rom?: string;
  symbols?: string;
}

export const MAX_FRAME_MS = 100;
export const MAX_SLICE = 50_000;
export const MAX_FRAME_BUDGET_MS = 10;

const KEY_BITS: Record<Exclude<PanelKey, "start">, number> = { load: 1, exam: 2, dep: 4, cont: 8 };

const browserScheduler: Scheduler = {
  now: () => performance.now(),
  request: (callback) => requestAnimationFrame(() => callback()),
  cancel: (handle) => cancelAnimationFrame(handle),
};

/** The core's URL: next to the page, with the build's hash of it so a cache never serves an old one. */
export function coreUrl(file: string, base: string, version: string | undefined): string {
  const url = new URL(`core/${file}`, base);
  if (version) url.searchParams.set("v", version);
  return url.href;
}

async function loadFromPage(): Promise<CoreModule> {
  const version = import.meta.env.VITE_CORE_VERSION as string | undefined;
  const at = (file: string) => coreUrl(file, document.baseURI, version);
  type Factory = (options: { locateFile: (file: string) => string }) => Promise<CoreModule>;
  const mod = (await import(/* @vite-ignore */ at("oo1.mjs"))) as { default: Factory };
  return mod.default({ locateFile: at }); // oo1.wasm too, not just the script
}

/** Latin-1: one char per byte, as the module's strings are. */
function bytesToString(bytes: Uint8Array): string {
  let s = "";
  for (const b of bytes) s += String.fromCharCode(b);
  return s;
}

function stringToBytes(s: string): Uint8Array {
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; ++i) out[i] = s.charCodeAt(i) & 0xff;
  return out;
}

export class WasmPort implements MachinePort {
  readonly caps: PortCaps = { registers: true, trace: true, busCycleStep: false, video: false, power: true };

  private readonly load: () => Promise<CoreModule>;
  private readonly scheduler: Scheduler;
  private readonly files: { machine: string; rom: string; symbols: string };
  private m: CoreMachine | null = null;
  private handle: number | null = null;
  private last = 0;
  private clock: Clock = CLOCKS_HZ[3];
  private powered = true;
  private lightsOn = false;
  private regsOn = false;
  private readonly listeners = new Set<(frame: FrameState) => void>();
  private speedCycles = 0;
  private speedMs = 0;
  private hz = 0;

  constructor(options: WasmPortOptions = {}) {
    this.load = options.load ?? loadFromPage;
    this.scheduler = options.scheduler ?? browserScheduler;
    this.files = {
      machine: options.machine ?? "/machines/backplane.machine",
      rom: options.rom ?? "/roms/bios.rom",
      symbols: options.symbols ?? "/roms/bios.rom.lbl",
    };
  }

  async connect(): Promise<{ machine: string }> {
    const core = await this.load();
    const m = new core.Machine();
    if (!m.open(this.files.machine, this.files.rom, this.files.symbols)) {
      const why = m.error();
      m.delete();
      throw new Error(`cannot open ${this.files.machine}: ${why}`);
    }
    this.m = m;
    this.last = this.scheduler.now();
    this.schedule();
    return { machine: this.files.machine };
  }

  close(): void {
    if (this.handle !== null) this.scheduler.cancel(this.handle);
    this.handle = null;
    this.m?.delete();
    this.m = null;
  }

  private get machine(): CoreMachine {
    if (!this.m) throw new Error("not connected");
    return this.m;
  }

  private schedule(): void {
    this.handle = this.scheduler.request(() => this.frame());
  }

  /** One animation frame: run a budget, then tell the listeners. Public for tests. */
  frame(): void {
    const m = this.m;
    if (!m) return;
    const now = this.scheduler.now();
    const dt = Math.min(Math.max(now - this.last, 0), MAX_FRAME_MS);
    this.last = now;

    let ran = 0;
    if (this.powered) {
      if (this.clock === "max") {
        const until = now + MAX_FRAME_BUDGET_MS;
        do {
          const why = m.run(MAX_SLICE);
          ran += m.lastCycles();
          if (why !== 0) break;
        } while (this.scheduler.now() < until);
      } else {
        m.run(Math.round((this.clock * dt) / 1000));
        ran = m.lastCycles();
      }
    }
    this.measure(ran, dt);

    const out = this.powered ? stringToBytes(m.takeOutput()) : new Uint8Array(0);
    const state: FrameState = {
      cycles: m.cycles(),
      powered: this.powered,
      halted: this.powered && m.halted(),
      stopped: this.powered && m.stopped(),
      irq: this.powered && m.irq(),
      hz: this.hz,
      serialOut: out,
      lights: this.lightsOn && this.powered ? this.readLights(m) : null,
      regs: this.regsOn && this.powered ? m.regs() : null,
      video: null,
    };
    for (const listener of this.listeners) listener(state);
    this.schedule();
  }

  private measure(cycles: number, ms: number): void {
    this.speedCycles += cycles;
    this.speedMs += ms;
    if (this.speedMs >= 1000) {
      this.hz = (this.speedCycles * 1000) / this.speedMs;
      this.speedCycles = 0;
      this.speedMs = 0;
    }
  }

  private readLights(m: CoreMachine): Lights {
    const v = m.lights().slice(); // a view into the module's memory: copy before it moves
    return { addr: v.subarray(0, 20), data: v.subarray(20, 28), sync: v[28], rwb: v[29], vpb: v[30], rdy: v[31] };
  }

  setClock(clock: Clock): void {
    this.clock = clock;
  }

  setPower(on: boolean): void {
    if (on === this.powered) return;
    this.powered = on;
    this.hz = 0;
    if (on && !this.machine.powerCycle()) throw new Error(`power on failed: ${this.machine.error()}`);
  }

  setSwitches(swr: number): void {
    this.m?.setSwitches(swr);
  }

  setPanel(panel: { halt: boolean; busCycle: boolean }): void {
    this.m?.setPanel(panel.halt, panel.busCycle);
  }

  setLock(locked: boolean): void {
    this.m?.setLock(locked);
  }

  press(key: PanelKey): void {
    if (key === "start") this.m?.start();
    else this.m?.press(KEY_BITS[key]);
  }

  async cardRead(offset: number): Promise<number> {
    return this.machine.cardRead(offset);
  }

  cardWrite(offset: number, value: number): void {
    this.m?.cardWrite(offset, value);
  }

  sendSerial(bytes: Uint8Array): void {
    this.m?.send(bytesToString(bytes));
  }

  async readMemory(addr: number, length: number, space: "cpu" | "physical"): Promise<Uint8Array> {
    const out = new Uint8Array(length);
    for (let i = 0; i < length; ++i)
      out[i] = space === "cpu" ? this.machine.peek((addr + i) & 0xffff) : this.machine.peekPhysical((addr + i) & 0xfffff);
    return out;
  }

  async physical(cpuAddr: number): Promise<number> {
    return this.machine.physical(cpuAddr);
  }

  async resolve(text: string): Promise<number | null> {
    const addr = this.machine.resolve(text);
    return addr < 0 ? null : addr;
  }

  setLights(on: boolean): void {
    this.lightsOn = on;
    this.m?.setLights(on);
  }

  watchRegisters(on: boolean): void {
    this.regsOn = on;
  }

  setTrace(on: boolean, depth = 4096): void {
    this.m?.setTrace(on, depth);
  }

  async readTrace(max: number): Promise<TraceRow[]> {
    const w = this.machine.trace(max);
    const rows: TraceRow[] = [];
    for (let i = 0; i < w.length; i += 4) {
      const flags = w[i + 3] >>> 24;
      rows.push({
        cycle: w[i] + w[i + 1] * 2 ** 32,
        physical: w[i + 2] & 0xfffff,
        cpu: w[i + 3] & 0xffff,
        data: (w[i + 3] >>> 16) & 0xff,
        module: w[i + 2] >>> 24,
        write: (flags & 1) !== 0,
        sync: (flags & 2) !== 0,
        vpb: (flags & 4) !== 0,
      });
    }
    return rows;
  }

  onFrame(listener: (frame: FrameState) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }
}
