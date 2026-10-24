// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { PanelState } from "../ui/panel-state";
import { frontPanel, glow } from "./panel";

function setup() {
  const port = new FakePort();
  const state = new PanelState(port);
  const fp = frontPanel(state);
  document.body.replaceChildren(fp.element);
  const q = <T extends Element>(sel: string) => fp.element.querySelector<T>(sel)!;
  const lamp = (name: string) => Number(q<HTMLElement>(`.lamp[data-lamp="${name}"]`).style.getPropertyValue("--b") || 0);
  return { port, state, fp, q, lamp };
}

const lights = (addr: number[], data: number[], extra: Partial<{ sync: number; rwb: number; vpb: number; rdy: number }> = {}) => ({
  addr: Float32Array.from(addr),
  data: Float32Array.from(data),
  sync: 0,
  rwb: 0,
  vpb: 0,
  rdy: 0,
  ...extra,
});

describe("front panel", () => {
  beforeEach(() => document.body.replaceChildren());

  test("glow: dark at 0, full at 1, a little duty still shows", () => {
    expect(glow(0)).toBe(0);
    expect(glow(1)).toBe(1);
    expect(glow(0.01)).toBeGreaterThan(0.12);
  });

  test("a switch register switch flips its bit and tells the port", () => {
    const { port, q } = setup();
    q<HTMLButtonElement>('.sr[data-bit="17"]').click();
    q<HTMLButtonElement>('.sr[data-bit="0"]').click();
    expect(port.callsTo("setSwitches")).toEqual([[0x20000], [0x20001]]);
    expect(q('.sr[data-bit="17"]').getAttribute("aria-pressed")).toBe("true");
    expect(q('[data-testid="readout-swr"]').textContent).toBe("20001");
    q<HTMLButtonElement>('.sr[data-bit="17"]').click();
    expect(port.callsTo("setSwitches").at(-1)).toEqual([0x00001]);
  });

  test("hex digits alternate orange and gold, as in the mockup", () => {
    const { q } = setup();
    expect(q('.sr[data-bit="19"]').classList.contains("orange")).toBe(true);
    expect(q('.sr[data-bit="15"]').classList.contains("gold")).toBe(true);
    expect(q('.sr[data-bit="11"]').classList.contains("orange")).toBe(true);
    expect(q('.sr[data-bit="4"]').classList.contains("gold")).toBe(true);
    expect(q('.sr[data-bit="3"]').classList.contains("orange")).toBe(true);
  });

  test("momentary keys press; the toggles move HALT and S INST", () => {
    const { port, q } = setup();
    for (const k of ["load", "exam", "dep", "cont", "start"]) q<HTMLButtonElement>(`[data-ctl="${k}"]`).click();
    expect(port.callsTo("press")).toEqual([["load"], ["exam"], ["dep"], ["cont"], ["start"]]);
    q<HTMLButtonElement>('[data-ctl="halt"]').click();
    q<HTMLButtonElement>('[data-ctl="step"]').click();
    expect(port.callsTo("setPanel")).toEqual([[{ halt: true, busCycle: false }], [{ halt: true, busCycle: true }]]);
    expect(q('[data-ctl="halt"]').classList.contains("up")).toBe(false); // down = HALT
  });

  test("the toolbar's Halt moves the panel's toggle too", () => {
    const { state, q } = setup();
    state.halt();
    expect(q('[data-ctl="halt"]').getAttribute("aria-pressed")).toBe("true");
  });

  test("keyswitch: RUN, LOCK (switches dead), OFF (power off), RUN (cold start)", () => {
    const { port, q } = setup();
    const key = q<HTMLButtonElement>('[data-testid="keyswitch"]');
    key.click(); // RUN -> LOCK
    expect(port.callsTo("setLock")).toEqual([[true]]);
    q<HTMLButtonElement>('.sr[data-bit="3"]').click();
    q<HTMLButtonElement>('[data-ctl="cont"]').click();
    q<HTMLButtonElement>('[data-ctl="halt"]').click();
    expect(port.callsTo("setSwitches")).toEqual([]);
    expect(port.callsTo("press")).toEqual([]);
    expect(port.callsTo("setPanel")).toEqual([]);
    key.click(); // LOCK -> OFF
    expect(port.callsTo("setLock").at(-1)).toEqual([false]);
    expect(port.callsTo("setPower")).toEqual([[false]]);
    expect(document.body.dataset.key).toBe("off");
    key.click(); // OFF -> RUN
    expect(port.callsTo("setPower")).toEqual([[false], [true]]);
    q<HTMLButtonElement>('[data-ctl="cont"]').click();
    expect(port.callsTo("press")).toEqual([["cont"]]);
  });

  test("lamps show duty while running, the latched cycle when halted", () => {
    const { fp, lamp, q } = setup();
    const addr = Array(20).fill(0);
    addr[19] = 0.6;
    addr[0] = 1;
    fp.update({ ...base(), lights: lights(addr, [0.25, 0, 0, 0, 0, 0, 0, 0], { sync: 0.33, rwb: 1 }) });
    expect(lamp("A19")).toBeCloseTo(glow(0.6), 2);
    expect(lamp("A0")).toBe(1);
    expect(lamp("A1")).toBe(0);
    expect(lamp("D0")).toBeCloseTo(glow(0.25), 2);
    expect(lamp("SYNC")).toBeCloseTo(glow(0.33), 2);
    expect(lamp("RUN")).toBe(1);
    expect(lamp("HALT")).toBe(0);
    expect(q('[data-testid="readout-addr"]').textContent).toBe("-----");

    const latched = Array(20).fill(0);
    for (const bit of [19, 18, 17, 16, 15, 14, 11, 0]) latched[bit] = 1; // $FC801
    fp.update({ ...base(), halted: true, lights: lights(latched, [1, 0, 1, 1, 0, 1, 0, 1]) }); // $AD
    expect(lamp("HALT")).toBe(1);
    expect(lamp("RUN")).toBe(0);
    expect(q('[data-testid="readout-addr"]').textContent).toBe("FC801");
    expect(q('[data-testid="readout-data"]').textContent).toBe("AD");
  });

  test("IRQ and POWER lamps; power off darkens everything", () => {
    const { fp, lamp } = setup();
    fp.update({ ...base(), irq: true, lights: lights(Array(20).fill(1), Array(8).fill(1)) });
    expect(lamp("IRQ")).toBe(1);
    expect(lamp("POWER")).toBe(1);
    fp.update({ ...base(), powered: false, irq: true, lights: lights(Array(20).fill(1), Array(8).fill(1)) });
    for (const l of ["IRQ", "POWER", "RUN", "A5", "D3"]) expect(lamp(l)).toBe(0);
  });
});

function base() {
  return {
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
    video: null,
  };
}
