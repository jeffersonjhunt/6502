/**
 * A scripted MachinePort for UI tests: it records every call and sends the
 * frames a test gives it. No machine behind it.
 */

import type {
  AppleState,
  Cartridge,
  Clock,
  DiskFormat,
  DiskInfo,
  DiskStatus,
  FrameState,
  MachinePort,
  MouseInput,
  PanelKey,
  PortCaps,
  SidTune,
  SoundCpu,
  TraceRow,
} from "./machine-port";

export type FakeCall = [name: string, ...args: unknown[]];

export class FakePort implements MachinePort {
  readonly caps: PortCaps;
  readonly calls: FakeCall[] = [];
  /** What cardRead() returns, by offset. */
  readonly card = new Map<number, number>();
  /** What readMemory() returns, by address. */
  readonly memory = new Map<number, number>();
  /** What readMemory(..., "io") returns where set (else `memory`): the cards, whatever the CPU sees. */
  readonly io = new Map<number, number>();
  labels = new Map<string, number>();
  traceRows: TraceRow[] = [];
  /** What readApple() returns. */
  apple: AppleState | null = null;
  /** insertCartridge() rejects with this, if set. */
  cartridgeError: string | null = null;
  /** The drives: what readDiskInfo() returns, and insertDisk() sets. */
  readonly disks: (DiskInfo | null)[] = [null, null];
  /** insertDisk() and saveDisk() reject with this, if set. */
  diskError: string | null = null;
  /** What saveDisk() returns. */
  saved = new Uint8Array([1, 2, 3]);
  /** What readDisk2() returns. */
  disk2: DiskStatus | null = null;
  /** What playSid() and playSong() return (the song set to the one asked for), or reject with, if a string. */
  tune: SidTune | string = { name: "Fake Tune", author: "Nobody", released: "2026", songs: 3, song: 1, rsid: false, clock: "PAL", model: "8580", warnings: [] };
  /** What readSound() returns. */
  sound: SoundCpu | null = null;
  private readonly listeners = new Set<(frame: FrameState) => void>();

  constructor(caps: Partial<PortCaps> = {}) {
    this.caps = { registers: true, trace: true, busCycleStep: false, video: false, usb: false, power: true, cartridge: true, disk: true, audio: false, sid: false, ...caps };
  }

  /** Send a frame to the listeners; unset fields take idle defaults. */
  emit(frame: Partial<FrameState> = {}): void {
    const full: FrameState = {
      cycles: 0,
      powered: true,
      halted: false,
      stopped: false,
      irq: false,
      hz: 0,
      serialOut: new Uint8Array(0),
      lights: null,
      regs: null,
      fixedHz: 0,
      apple: false,
      disk: null,
      video: null,
      audio: null,
      soundRunning: false,
      ...frame,
    };
    for (const listener of this.listeners) listener(full);
  }

  callsTo(name: string): unknown[][] {
    return this.calls.filter((c) => c[0] === name).map((c) => c.slice(1));
  }

  async connect(): Promise<{ machine: string }> {
    this.calls.push(["connect"]);
    return { machine: "fake.machine" };
  }
  close(): void {
    this.calls.push(["close"]);
  }
  setClock(clock: Clock): void {
    this.calls.push(["setClock", clock]);
  }
  setPower(on: boolean): void {
    this.calls.push(["setPower", on]);
  }
  setSwitches(swr: number): void {
    this.calls.push(["setSwitches", swr]);
  }
  setPanel(panel: { halt: boolean; busCycle: boolean }): void {
    this.calls.push(["setPanel", { ...panel }]);
  }
  setLock(locked: boolean): void {
    this.calls.push(["setLock", locked]);
  }
  press(key: PanelKey): void {
    this.calls.push(["press", key]);
  }
  async cardRead(offset: number): Promise<number> {
    return this.card.get(offset) ?? 0xff;
  }
  cardWrite(offset: number, value: number): void {
    this.calls.push(["cardWrite", offset, value]);
    this.card.set(offset, value);
  }
  sendSerial(bytes: Uint8Array): void {
    this.calls.push(["sendSerial", Array.from(bytes)]);
  }
  async insertCartridge(cartridge: Cartridge | null): Promise<void> {
    this.calls.push(["insertCartridge", cartridge]);
    if (this.cartridgeError) throw new Error(this.cartridgeError);
  }
  async readApple(): Promise<AppleState | null> {
    return this.apple;
  }
  async insertDisk(drive: number, bytes: Uint8Array, name: string): Promise<DiskInfo> {
    this.calls.push(["insertDisk", drive, bytes, name]);
    if (this.diskError) throw new Error(this.diskError);
    const format: DiskFormat = /\.woz$/i.test(name) ? "woz" : /\.po$/i.test(name) ? "po" : "dsk";
    return (this.disks[drive] = { name, format, writeProtected: false, changed: false, writes: 0 });
  }
  ejectDisk(drive: number): void {
    this.calls.push(["ejectDisk", drive]);
    this.disks[drive] = null;
  }
  async readDiskInfo(drive: number): Promise<DiskInfo | null> {
    return this.disks[drive] ? { ...this.disks[drive]! } : null;
  }
  async saveDisk(drive: number, format: DiskFormat): Promise<Uint8Array> {
    this.calls.push(["saveDisk", drive, format]);
    if (this.diskError) throw new Error(this.diskError);
    return this.saved;
  }
  async readDisk2(): Promise<DiskStatus | null> {
    return this.disk2;
  }
  async playSid(bytes: Uint8Array, song = 0): Promise<SidTune> {
    this.calls.push(["playSid", bytes.length, song]);
    if (typeof this.tune === "string") throw new Error(this.tune);
    return { ...this.tune, song: song || 1 };
  }
  async playSong(song: number): Promise<SidTune> {
    this.calls.push(["playSong", song]);
    if (typeof this.tune === "string") throw new Error(this.tune);
    return { ...this.tune, song };
  }
  stopSid(): void {
    this.calls.push(["stopSid"]);
  }
  async readSound(): Promise<SoundCpu | null> {
    return this.sound;
  }
  setWriteProtect(drive: number, on: boolean): void {
    this.calls.push(["setWriteProtect", drive, on]);
    if (this.disks[drive]) this.disks[drive]!.writeProtected = on;
  }
  async readMemory(addr: number, length: number, space: "cpu" | "physical" | "io" = "cpu"): Promise<Uint8Array> {
    return Uint8Array.from({ length }, (_, i) => (space === "io" ? this.io.get(addr + i) : undefined) ?? this.memory.get(addr + i) ?? 0);
  }
  async physical(cpuAddr: number): Promise<number> {
    return cpuAddr >= 0xc800 ? 0xf0000 | cpuAddr : cpuAddr;
  }
  async resolve(text: string): Promise<number | null> {
    if (this.labels.has(text) && !text.startsWith("$")) return this.labels.get(text)!;
    const m = /^\$?([0-9a-f]{1,4})$/i.exec(text);
    return m ? parseInt(m[1], 16) : null;
  }
  setLights(on: boolean): void {
    this.calls.push(["setLights", on]);
  }
  sendKey(usage: number, down: boolean): void {
    this.calls.push(["sendKey", usage, down]);
  }
  sendMouse(mouse: MouseInput): void {
    this.calls.push(["sendMouse", mouse]);
  }
  listen(rate: number): void {
    this.calls.push(["listen", rate]);
  }

  watchVideo(on: boolean): void {
    this.calls.push(["watchVideo", on]);
  }

  watchRegisters(on: boolean): void {
    this.calls.push(["watchRegisters", on]);
  }
  setTrace(on: boolean, depth?: number): void {
    this.calls.push(["setTrace", on, depth]);
  }
  async readTrace(max: number): Promise<TraceRow[]> {
    return this.traceRows.slice(-max);
  }
  onFrame(listener: (frame: FrameState) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }
}
