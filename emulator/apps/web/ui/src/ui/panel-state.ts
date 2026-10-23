/**
 * Where the front panel's physical switches are, shared by everything that
 * moves them: the toolbar's Run/Halt/Step now, the panel itself next. Every
 * change goes to the port, so there is one path to the machine.
 *
 * Toolbar actions are the panel's own moves:
 *   Run    HALT switch to ENABLE, then CONT
 *   Halt   HALT switch down
 *   Step   HALT switch down (if it was not), S INST, then CONT
 */

import type { MachinePort } from "../port/machine-port";

export interface PanelSwitches {
  halt: boolean;
  busCycle: boolean;
}

export class PanelState {
  private sw: PanelSwitches = { halt: false, busCycle: false };
  private readonly listeners = new Set<(sw: PanelSwitches) => void>();

  constructor(private readonly port: MachinePort) {}

  get switches(): PanelSwitches {
    return { ...this.sw };
  }

  set(change: Partial<PanelSwitches>): void {
    this.sw = { ...this.sw, ...change };
    this.port.setPanel(this.sw);
    for (const listener of this.listeners) listener(this.switches);
  }

  onChange(listener: (sw: PanelSwitches) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  run(): void {
    this.set({ halt: false });
    this.port.press("cont");
  }

  halt(): void {
    this.set({ halt: true });
  }

  step(): void {
    if (!this.sw.halt || this.sw.busCycle) this.set({ halt: true, busCycle: false });
    this.port.press("cont");
  }
}
