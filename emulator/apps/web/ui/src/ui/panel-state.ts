/**
 * Where the front panel's physical controls are: the HALT and S INST
 * toggles, the 20-bit switch register and the keyswitch. Everything that
 * moves them, the panel itself and the toolbar's Run/Halt/Step, goes
 * through here, and every change goes to the port, so there is one path to
 * the machine.
 *
 * Toolbar actions are the panel's own moves:
 *   Run    HALT switch to ENABLE, then CONT
 *   Halt   HALT switch down
 *   Step   HALT switch down (if it was not), S INST, then CONT
 *
 * The keyswitch: OFF is power off; RUN is normal; LOCK is the card's LOCK
 * (hw/design.md), so the panel's keys and switches do nothing. Turning it
 * from OFF to RUN is a cold start. While it is not at RUN the panel takes
 * no input, as on the machine.
 */

import type { MachinePort, PanelKey } from "../port/machine-port";

export type KeyPosition = "off" | "run" | "lock";
export const KEY_ORDER: KeyPosition[] = ["off", "run", "lock"];

export interface PanelSwitches {
  halt: boolean;
  busCycle: boolean;
  /** The switch register, A0-A19. */
  swr: number;
  key: KeyPosition;
}

export class PanelState {
  private sw: PanelSwitches = { halt: false, busCycle: false, swr: 0, key: "run" };
  private readonly listeners = new Set<(sw: PanelSwitches) => void>();

  constructor(private readonly port: MachinePort) {}

  get switches(): PanelSwitches {
    return { ...this.sw };
  }

  /** The panel takes input: the keyswitch is at RUN. */
  get live(): boolean {
    return this.sw.key === "run";
  }

  /** Move switches. The keyswitch always turns; the rest only at RUN. */
  set(change: Partial<PanelSwitches>): void {
    if (!this.live && change.key === undefined) return;
    const was = this.sw;
    const next = { ...was, ...change };
    next.swr &= 0xfffff;
    this.sw = next;
    if (next.halt !== was.halt || next.busCycle !== was.busCycle)
      this.port.setPanel({ halt: next.halt, busCycle: next.busCycle });
    if (next.swr !== was.swr) this.port.setSwitches(next.swr);
    if (next.key !== was.key) this.turnKey(was.key, next.key);
    this.notify();
  }

  /** Before connecting: where the switches were left (a remembered SWR). */
  restore(sw: Partial<PanelSwitches>): void {
    this.sw = { ...this.sw, ...sw, key: "run" };
    this.sw.swr &= 0xfffff;
    this.notify();
  }

  /** After connecting: tell the machine where everything already is. */
  applyAll(): void {
    this.port.setPanel({ halt: this.sw.halt, busCycle: this.sw.busCycle });
    this.port.setSwitches(this.sw.swr);
    this.port.setLock(this.sw.key === "lock");
  }

  private turnKey(from: KeyPosition, to: KeyPosition): void {
    if (from === "lock") this.port.setLock(false);
    if (to === "off") this.port.setPower(false);
    if (from === "off") this.port.setPower(true);
    if (to === "lock") this.port.setLock(true);
  }

  /** The next keyswitch position: OFF, RUN, LOCK, then OFF again. */
  turn(): void {
    this.set({ key: KEY_ORDER[(KEY_ORDER.indexOf(this.sw.key) + 1) % KEY_ORDER.length] });
  }

  press(key: PanelKey): void {
    if (this.live) this.port.press(key);
  }

  onChange(listener: (sw: PanelSwitches) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private notify(): void {
    for (const listener of this.listeners) listener(this.switches);
  }

  run(): void {
    if (!this.live) return;
    this.set({ halt: false });
    this.port.press("cont");
  }

  halt(): void {
    this.set({ halt: true });
  }

  step(): void {
    if (!this.live) return;
    if (!this.sw.halt || this.sw.busCycle) this.set({ halt: true, busCycle: false });
    this.port.press("cont");
  }
}
