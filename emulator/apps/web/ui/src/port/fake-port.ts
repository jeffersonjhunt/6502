/**
 * A scripted MachinePort for UI tests: it records every call and sends the
 * frames a test gives it. No machine behind it.
 */

import type { Clock, FrameState, MachinePort, MouseInput, PanelKey, PortCaps, TraceRow } from "./machine-port";

export type FakeCall = [name: string, ...args: unknown[]];

export class FakePort implements MachinePort {
  readonly caps: PortCaps;
  readonly calls: FakeCall[] = [];
  /** What cardRead() returns, by offset. */
  readonly card = new Map<number, number>();
  /** What readMemory() returns, by address. */
  readonly memory = new Map<number, number>();
  labels = new Map<string, number>();
  traceRows: TraceRow[] = [];
  private readonly listeners = new Set<(frame: FrameState) => void>();

  constructor(caps: Partial<PortCaps> = {}) {
    this.caps = { registers: true, trace: true, busCycleStep: false, video: false, usb: false, power: true, ...caps };
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
      video: null,
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
  async readMemory(addr: number, length: number): Promise<Uint8Array> {
    return Uint8Array.from({ length }, (_, i) => this.memory.get(addr + i) ?? 0);
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
