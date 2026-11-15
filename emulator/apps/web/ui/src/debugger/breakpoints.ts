/**
 * Breakpoints, as the hardware has them: one comparator on the Console card
 * (BPADDR, BPCTRL; hw/design.md, "Console registers"). The pane keeps a
 * list of saved breakpoints, but at most one is armed, and arming is
 * writing the card's registers through the port, which is what the ESP32
 * would do on the board.
 *
 * A breakpoint is a physical address. Typed as five hex digits it is taken
 * as one ("FC951"); anything else is a CPU address or label ("ECHO",
 * "C951"), turned into a physical address through the windows as they are
 * when it is added (the CPU card's view: A0-A23), so "ECHO" shows as
 * "ECHO $C951 → FC951".
 *
 * BPADDR is 20 bits, and the card matches only a cycle with A20-A23 0
 * (hw/design.md, "B1: as built"). So a CPU address that resolves above
 * $FFFFF (a window on the flash, $F00000 up, or on the video aperture,
 * $E00000) is refused, and so is a bank 8 address ($80000-$8FFFF) while
 * VIDEO_ID reads $92: with a revision 2 card the CPU card sends the CPU's
 * and DMA's bank 8 to the aperture, so no cycle there reaches the bus to
 * match. A saved breakpoint that can't fire isn't restored, and
 * notRestored names it: above $FFFFF at once, and in bank 8 once
 * recheck() has read VIDEO_ID $92 (the pane calls it on connecting; a page
 * from before B1 could save one). Those entries stay in storage, kept
 * through every later save, so a machine they can fire on gets them back.
 *
 * The card loses its registers at power off, so a power cycle re-arms
 * whatever was armed. The list is remembered; nothing is armed at load.
 */

import { CARD, type MachinePort } from "../port/machine-port";
import { prefs } from "../ui/dom";

export const BPCTRL = { ENABLE: 0x01, FETCH: 0x02, READ: 0x04, WRITE: 0x08, NMI: 0x10 } as const;
export const STATUS_HIT = 0x02;

export interface Breakpoint {
  id: string;
  /** What was typed. */
  text: string;
  /** The CPU address it resolved to, or null if typed as physical. */
  cpu: number | null;
  physical: number;
  fetch: boolean;
  read: boolean;
  write: boolean;
  /** Pull /NMI instead of halting. */
  nmi: boolean;
}

export type Flag = "fetch" | "read" | "write" | "nmi";

export function bpctrl(b: Breakpoint): number {
  return (
    BPCTRL.ENABLE |
    (b.fetch ? BPCTRL.FETCH : 0) |
    (b.read ? BPCTRL.READ : 0) |
    (b.write ? BPCTRL.WRITE : 0) |
    (b.nmi ? BPCTRL.NMI : 0)
  );
}

export const hex = (v: number, n: number) => v.toString(16).toUpperCase().padStart(n, "0");

/** BPADDR's reach: A0-A19, with A20-A23 0. */
export const BPADDR_MAX = 0xfffff;
/** VIDEO_ID's address, and a revision 2 card's ID: its bank 8 is the aperture's. */
const VIDEO_ID = 0xc4ff;
const VIDEO_REV2 = 0x92;

/** "ECHO $C951 → FC951", "C951 → FC951", or "FC951" (six digits above FFFFF). */
export function describe(b: Breakpoint): string {
  if (b.cpu === null) return hex(b.physical, 5);
  const label = /^\$?[0-9a-f]{1,4}$/i.test(b.text) ? "" : `${b.text} $`;
  return `${label}${hex(b.cpu, 4)} → ${hex(b.physical, 5)}`;
}

export interface Hit {
  physical: number;
  data: number;
  /** LSTAT: bit 0 SYNC, bit 1 RWB (read), bit 2 VPB. */
  stat: number;
}

const STORE_KEY = "breakpoints";

/** A saved breakpoint as this page wrote it: anything else (an old page's, an edited one) is dropped, not drawn. */
function isBreakpoint(v: unknown, max = BPADDR_MAX): v is Breakpoint {
  if (typeof v !== "object" || v === null) return false;
  const b = v as Record<string, unknown>;
  const address = (a: unknown, max: number) => Number.isInteger(a) && (a as number) >= 0 && (a as number) <= max;
  return (
    typeof b.id === "string" &&
    typeof b.text === "string" &&
    (b.cpu === null || address(b.cpu, 0xffff)) &&
    address(b.physical, max) &&
    (["fetch", "read", "write", "nmi"] as const).every((f) => typeof b[f] === "boolean")
  );
}

export class Breakpoints {
  list: Breakpoint[] = [];
  armed: string | null = null;
  /** Saved breakpoints that can't fire here: not restored, named here for the pane to say so. */
  notRestored: string[] = [];
  /** Those entries as they were saved: kept in storage through the next save. */
  private held: unknown[] = [];
  private readonly listeners = new Set<() => void>();
  private next = 1;

  constructor(private readonly port: MachinePort) {
    try {
      const saved: unknown = JSON.parse(prefs.get(STORE_KEY) ?? "[]");
      if (Array.isArray(saved)) {
        this.list = saved.filter((v) => isBreakpoint(v));
        this.held = saved.filter((v) => !isBreakpoint(v) && isBreakpoint(v, 0xffffff));
        this.notRestored = this.held.map((b) => describe(b as Breakpoint));
      }
    } catch {
      this.list = [];
    }
    for (const b of [...this.list, ...(this.held as Breakpoint[])]) this.next = Math.max(this.next, Number(b.id) + 1 || 0);
  }

  /**
   * Once the machine is there: with a revision 2 video card (VIDEO_ID $92) a restored bank 8 breakpoint can never
   * fire, so it joins notRestored (and stays saved). Disarms it if it was armed.
   */
  async recheck(): Promise<void> {
    if ((await this.port.readMemory(VIDEO_ID, 1, "io"))[0] !== VIDEO_REV2) return;
    const dead = this.list.filter((b) => b.physical >> 16 === 0x8);
    if (!dead.length) return;
    if (dead.some((b) => b.id === this.armed)) this.arm(null);
    this.list = this.list.filter((b) => !dead.includes(b));
    this.held.push(...dead);
    this.notRestored.push(...dead.map(describe));
    this.changed();
  }

  onChange(listener: () => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private changed(): void {
    prefs.set(STORE_KEY, JSON.stringify([...this.list, ...this.held]));
    for (const listener of this.listeners) listener();
  }

  /** @returns the new breakpoint, or an error to show */
  async add(text: string, flags: Partial<Record<Flag, boolean>> = { fetch: true }): Promise<Breakpoint | string> {
    const t = text.trim();
    if (!t) return "type an address or a label";
    let cpu: number | null = null;
    let physical: number;
    if (/^\$?[0-9a-f]{5}$/i.test(t)) {
      physical = parseInt(t.replace("$", ""), 16);
    } else {
      cpu = await this.port.resolve(t);
      if (cpu === null) return `${t}: not a label or a hex address`;
      physical = await this.port.physical(cpu);
    }
    if (physical > BPADDR_MAX)
      return `${t}: $${hex(cpu ?? 0, 4)} is ${hex(physical, 6)}, above FFFFF: the Console card's breakpoint is 20 bits and never matches the flash or the video aperture`;
    if (physical >> 16 === 0x8 && (await this.port.readMemory(VIDEO_ID, 1, "io"))[0] === VIDEO_REV2)
      return `${t}: bank 8 is the video aperture with a revision 2 card, so the CPU and DMA never put ${hex(physical, 5)} on the bus: this breakpoint could never fire`;
    const b: Breakpoint = {
      id: String(this.next++),
      text: t,
      cpu,
      physical,
      fetch: !!flags.fetch,
      read: !!flags.read,
      write: !!flags.write,
      nmi: !!flags.nmi,
    };
    if (!b.fetch && !b.read && !b.write) b.fetch = true;
    this.list.push(b);
    this.changed();
    return b;
  }

  remove(id: string): void {
    if (this.armed === id) this.arm(null);
    this.list = this.list.filter((b) => b.id !== id);
    this.changed();
  }

  get(id: string | null): Breakpoint | undefined {
    return this.list.find((b) => b.id === id);
  }

  /** Arm one breakpoint (null: none). */
  arm(id: string | null): void {
    this.armed = this.get(id) ? id : null;
    this.write();
    this.changed();
  }

  setFlag(id: string, flag: Flag, on: boolean): void {
    const b = this.get(id);
    if (!b) return;
    b[flag] = on;
    if (!b.fetch && !b.read && !b.write) b.fetch = true; // a comparator that matches nothing is no breakpoint
    if (id === this.armed) this.write();
    this.changed();
  }

  /** Write the armed breakpoint (or none) to the card: after arming, and after a power cycle. */
  write(): void {
    const b = this.get(this.armed);
    if (!b) {
      this.port.cardWrite(CARD.BPCTRL, 0);
      return;
    }
    this.port.cardWrite(CARD.BPCTRL, 0); // never armed on a half-written address
    for (let i = 0; i < 3; ++i) this.port.cardWrite(CARD.BPADDR + i, (b.physical >> (8 * i)) & 0xff);
    this.port.cardWrite(CARD.BPCTRL, bpctrl(b));
  }

  /** STATUS says a breakpoint fired: the cycle it latched. null if not. */
  async hit(): Promise<Hit | null> {
    if (!((await this.port.cardRead(CARD.STATUS)) & STATUS_HIT)) return null;
    const r = (o: number) => this.port.cardRead(o);
    const physical = (await r(CARD.LADDR)) | ((await r(CARD.LADDR + 1)) << 8) | ((await r(CARD.LADDR + 2)) << 16);
    return { physical, data: await r(CARD.LDATA), stat: await r(CARD.LSTAT) };
  }

  /** The STATUS hit bit stays until a 1 is written to it. */
  clearHit(): void {
    this.port.cardWrite(CARD.STATUS, STATUS_HIT);
  }
}
