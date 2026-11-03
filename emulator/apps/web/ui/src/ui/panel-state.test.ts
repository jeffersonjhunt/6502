import { expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { PanelState } from "./panel-state";

test("Run is HALT to ENABLE then CONT", () => {
  const port = new FakePort();
  const panel = new PanelState(port);
  panel.halt();
  panel.run();
  expect(port.calls.slice(-2)).toEqual([
    ["setPanel", { halt: false, busCycle: false }],
    ["press", "cont"],
  ]);
});

test("Step halts first if running, selects S INST, then CONT", () => {
  const port = new FakePort();
  const panel = new PanelState(port);
  panel.set({ busCycle: true });
  panel.step();
  expect(port.calls.slice(-2)).toEqual([
    ["setPanel", { halt: true, busCycle: false }],
    ["press", "cont"],
  ]);
  panel.step(); // already halted on S INST: just CONT
  expect(port.calls.slice(-1)).toEqual([["press", "cont"]]);
});

test("listeners see each change", () => {
  const panel = new PanelState(new FakePort());
  const seen: boolean[] = [];
  panel.onChange((sw) => seen.push(sw.halt));
  panel.halt();
  panel.run();
  expect(seen).toEqual([true, false]);
});

test("restore and applyAll: the switches as they were left reach the machine once connected", () => {
  const port = new FakePort();
  const panel = new PanelState(port);
  panel.restore({ swr: 0x1234567 }); // more than 20 bits: only A0-A19 exist
  expect(port.calls).toEqual([]); // nothing sent before connecting
  panel.applyAll();
  expect(port.calls).toEqual([
    ["setPanel", { halt: false, busCycle: false }],
    ["setSwitches", 0x34567],
    ["setLock", false],
  ]);
});

test("toolbar actions do nothing unless the keyswitch is at RUN", () => {
  const port = new FakePort();
  const panel = new PanelState(port);
  panel.turn(); // LOCK
  const before = port.calls.length;
  panel.run();
  panel.step();
  panel.halt();
  expect(port.calls.length).toBe(before);
});

test("the page starts the key at OFF; a remembered LOCK is never restored; OFF to RUN powers on", () => {
  const port = new FakePort();
  const panel = new PanelState(port);
  panel.restore({ key: "off" });
  expect(panel.switches.key).toBe("off");
  expect(panel.live).toBe(false);
  panel.turn(); // RUN
  expect(panel.switches.key).toBe("run");
  expect(port.calls).toContainEqual(["setPower", true]);
  panel.restore({ key: "lock" });
  expect(panel.switches.key).toBe("run");
});
