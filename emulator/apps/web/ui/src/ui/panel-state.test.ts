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
