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
  AppleState,
  Cartridge,
  Clock,
  DiskFormat,
  DiskInfo,
  DiskStatus,
  FrameState,
  Lights,
  MachinePort,
  PanelKey,
  PortCaps,
  Registers,
  MouseInput,
  SidTune,
  SoundCpu,
  TraceRow,
  VideoFrame,
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
  hasVideo(): boolean;
  videoFrames(): number;
  videoFrame(): Uint8Array;
  setAudioRate(rate: number): void;
  loadSid(bytes: string, song: number): boolean;
  sidSong(song: number): boolean;
  stopSid(): void;
  sidInfo(): { has: boolean; name: string; author: string; released: string; warnings: string; songs: number; song: number; rsid: boolean; ntsc: boolean; mos8580: boolean };
  soundRunning(): boolean;
  takeAudio(): Float32Array;
  setClock(hz: number): void;
  fixedHz(): number;
  loadCartridge(rom: string, chars: string, boot: string, sequencer: string): boolean;
  insertDisk(drive: number, bytes: string, name: string): boolean;
  ejectDisk(drive: number): void;
  diskInfo(drive: number): number;
  diskWrites(drive: number): number;
  saveDisk(drive: number, format: string): string;
  setWriteProtect(drive: number, on: boolean): void;
  diskStatus(): number;
  diskQuarter(drive: number): number;
  soundVoice(chip: number, voice: number): number;
  soundCpu(): { has: boolean; pc: number; a: number; x: number; y: number; s: number; p: number; state: number; cycles: number; ddr: number; port: number; timer: number; latch: number; timerOn: boolean };
  appleMode(): boolean;
  appleSwitches(): number;
  peekIo(offset: number): number;
  hasUsb(): boolean;
  usbKey(usage: number, down: boolean): void;
  usbMouseTo(x: number, y: number): void;
  usbMouseMove(dx: number, dy: number): void;
  usbMouseButtons(buttons: number): void;
  usbMouseWheel(steps: number): void;
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
  readonly caps: PortCaps = { registers: true, trace: true, busCycleStep: false, video: true, usb: true, power: true, cartridge: true, disk: true, audio: true, sid: true };

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
  private videoOn = false;
  private videoSeen = -1;
  private audioRate = 0;
  private readonly listeners = new Set<(frame: FrameState) => void>();
  private speedCycles = 0;
  private speedMs = 0;
  private hz = 0;
  private readonly diskNames = ["", ""];

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
    this.setClock(this.clock); // the video card keeps time by it
    m.setAudioRate(this.audioRate);
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
    const fixed = this.powered ? m.fixedHz() : 0; // Apple II mode: the clock card follows the mode
    const clock: Clock = fixed || this.clock;
    if (this.powered) {
      if (clock === "max") {
        const until = now + MAX_FRAME_BUDGET_MS;
        do {
          const why = m.run(MAX_SLICE);
          ran += m.lastCycles();
          if (why !== 0) break;
        } while (this.scheduler.now() < until);
      } else {
        m.run(Math.round((clock * dt) / 1000));
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
      fixedHz: this.powered ? m.fixedHz() : 0, // as the run left it, as everything here is
      apple: this.powered && m.appleMode(),
      soundRunning: this.powered && m.soundRunning(),
      disk: this.powered ? this.readDiskStatus(m) : null,
      serialOut: out,
      lights: this.lightsOn && this.powered ? this.readLights(m) : null,
      regs: this.regsOn && this.powered ? m.regs() : null,
      video: this.videoOn && this.powered ? this.readVideo(m) : null,
      audio: this.audioRate && this.powered ? new Float32Array(m.takeAudio()) : null, // copied out of the module's memory
    };
    for (const listener of this.listeners) listener(state);
    this.schedule();
  }

  private readDiskStatus(m: CoreMachine): DiskStatus | null {
    const v = m.diskStatus();
    if (v < 0) return null;
    return {
      motor: (v & 1) !== 0,
      spinning: (v & 2) !== 0,
      drive: (v >> 2) & 1,
      q6: (v & 8) !== 0,
      q7: (v & 16) !== 0,
      data: (v >> 8) & 0xff,
      phases: (v >> 16) & 0xf,
      quarterTracks: [m.diskQuarter(0), m.diskQuarter(1)],
    };
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

  /** A frame the card finished since the last one sent, copied out of the module's memory; else null. */
  private readVideo(m: CoreMachine): VideoFrame | null {
    if (!m.hasVideo()) return null;
    const frames = m.videoFrames();
    if (frames === this.videoSeen) return null;
    this.videoSeen = frames;
    return { width: 640, height: 480, frames, pixels: new Uint8ClampedArray(m.videoFrame()) };
  }

  setClock(clock: Clock): void {
    this.clock = clock;
    this.m?.setClock(clock === "max" ? 0 : clock); // "max": the card keeps 7.16 MHz time
  }

  setPower(on: boolean): void {
    if (on === this.powered) return;
    this.powered = on;
    this.hz = 0;
    this.videoSeen = -1;
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

  sendKey(usage: number, down: boolean): void {
    this.m?.usbKey(usage, down);
  }

  sendMouse(mouse: MouseInput): void {
    const m = this.m;
    if (!m) return;
    if (mouse.x !== undefined && mouse.y !== undefined) m.usbMouseTo(Math.round(mouse.x), Math.round(mouse.y));
    if (mouse.buttons !== undefined) m.usbMouseButtons(mouse.buttons);
    if (mouse.wheel) m.usbMouseWheel(mouse.wheel);
  }

  async readMemory(addr: number, length: number, space: "cpu" | "physical" | "io"): Promise<Uint8Array> {
    const m = this.machine;
    const read =
      space === "cpu" ? (a: number) => m.peek(a & 0xffff) : space === "io" ? (a: number) => m.peekIo(a & 0xfff) : (a: number) => m.peekPhysical(a & 0xfffff);
    const out = new Uint8Array(length);
    for (let i = 0; i < length; ++i) out[i] = read(addr + i);
    return out;
  }

  async insertCartridge(cartridge: Cartridge | null): Promise<void> {
    const m = this.machine;
    const rom = cartridge ? bytesToString(cartridge.rom) : "";
    const chars = cartridge?.chars ? bytesToString(cartridge.chars) : "";
    const boot = cartridge?.diskBoot ? bytesToString(cartridge.diskBoot) : "";
    const sequencer = cartridge?.diskSequencer ? bytesToString(cartridge.diskSequencer) : "";
    if (!m.loadCartridge(rom, chars, boot, sequencer)) throw new Error(m.error());
    this.videoSeen = -1;
  }

  async insertDisk(drive: number, bytes: Uint8Array, name: string): Promise<DiskInfo> {
    const m = this.machine;
    if (!m.insertDisk(drive, bytesToString(bytes), name)) throw new Error(m.error());
    this.diskNames[drive & 1] = name;
    return (await this.readDiskInfo(drive))!;
  }

  ejectDisk(drive: number): void {
    this.m?.ejectDisk(drive);
    this.diskNames[drive & 1] = "";
  }

  async readDiskInfo(drive: number): Promise<DiskInfo | null> {
    const m = this.machine;
    const v = m.diskInfo(drive);
    if (v < 0) return null;
    const formats: DiskFormat[] = ["woz", "dsk", "po"];
    return {
      name: this.diskNames[drive & 1],
      format: formats[(v >> 4) & 3] ?? "woz",
      writeProtected: (v & 2) !== 0,
      changed: (v & 1) !== 0,
      writes: m.diskWrites(drive),
    };
  }

  async saveDisk(drive: number, format: DiskFormat): Promise<Uint8Array> {
    const m = this.machine;
    const s = m.saveDisk(drive, format);
    if (!s) throw new Error(m.error());
    return stringToBytes(s);
  }

  setWriteProtect(drive: number, on: boolean): void {
    this.m?.setWriteProtect(drive, on);
  }

  async readDisk2(): Promise<DiskStatus | null> {
    return this.readDiskStatus(this.machine);
  }

  async readSound(): Promise<SoundCpu | null> {
    const { has, state, ...c } = this.machine.soundCpu();
    if (!has) return null;
    const phases = ["attack", "decay/sustain", "release"] as const;
    const envelopes = [0, 1, 2, 3, 4, 5].map((i) => {
      const v = this.machine.soundVoice(Math.floor(i / 3), i % 3);
      return { level: v & 0xff, phase: phases[(v >> 8) & 3] ?? "release", held: !!(v & 0x400) };
    });
    return { ...c, jammed: state === 2, envelopes };
  }

  async playSid(bytes: Uint8Array, song = 0): Promise<SidTune> {
    const m = this.machine;
    if (!m.loadSid(bytesToString(bytes), song)) throw new Error(m.error());
    return this.tune();
  }

  async playSong(song: number): Promise<SidTune> {
    const m = this.machine;
    if (!m.sidSong(song)) throw new Error(m.error());
    return this.tune();
  }

  stopSid(): void {
    this.m?.stopSid();
  }

  private tune(): SidTune {
    const t = this.machine.sidInfo();
    return {
      name: t.name,
      author: t.author,
      released: t.released,
      songs: t.songs,
      song: t.song,
      rsid: t.rsid,
      clock: t.ntsc ? "NTSC" : "PAL",
      model: t.mos8580 ? "8580" : "6581",
      warnings: t.warnings ? t.warnings.split("\n") : [],
    };
  }

  async readApple(): Promise<AppleState | null> {
    const v = this.machine.appleSwitches();
    if (v < 0) return null;
    const on = (bit: number) => (v & (1 << bit)) !== 0;
    return {
      mode: this.machine.appleMode(),
      text: on(0),
      mixed: on(1),
      page2: on(2),
      hires: on(3),
      col80: on(4),
      store80: on(5),
      altchar: on(6),
      dhires: on(7),
      ramrd: on(8),
      ramwrt: on(9),
      altzp: on(10),
      intcxrom: on(11),
      slotc3rom: on(12),
      intc8rom: on(13),
      lcBank2: on(14),
      lcRead: on(15),
      lcWrite: on(16),
      ioudis: on(17),
      an: (v >>> 20) & 0x0f,
    };
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

  watchVideo(on: boolean): void {
    this.videoOn = on;
    this.videoSeen = -1; // the next frame goes out, even if it is not new
  }

  listen(rate: number): void {
    this.audioRate = Math.max(0, Math.round(rate));
    this.m?.setAudioRate(this.audioRate);
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
