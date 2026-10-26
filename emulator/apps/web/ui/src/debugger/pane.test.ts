// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { debuggerPane } from "./pane";

const tick = () => new Promise((r) => setTimeout(r, 0));

function setup() {
  const port = new FakePort();
  port.labels.set("ECHO", 0xc951);
  const pane = debuggerPane(port);
  document.body.replaceChildren(pane.element, pane.toggle);
  pane.connect();
  const q = <T extends Element>(sel: string) => pane.element.querySelector<T>(sel)!;
  return { port, pane, q };
}

describe("debugger pane", () => {
  beforeEach(() => {
    localStorage.clear();
    document.body.className = "";
  });

  test("registers and flags from the frame", () => {
    const { pane, q } = setup();
    pane.update({ ...frame(), cycles: 1234567, regs: { pc: 0xc888, a: 0x41, x: 0, y: 1, s: 0xfd, p: 0xe1 } });
    expect(q('[data-testid="reg-pc"]').textContent).toBe("C888");
    expect(q('[data-testid="reg-a"]').textContent).toBe("41");
    expect(q('[data-testid="reg-s"]').textContent).toBe("FD");
    const on = [...q('[data-testid="flags"]').children].filter((e) => e.classList.contains("on")).map((e) => e.textContent);
    expect(on).toEqual(["N", "V", "-", "C"]); // $E1: not the same bits reversed
    expect(q('[data-testid="cycles"]').textContent).toBe("cycle 1 234 567");
  });

  test("watches only what is showing: registers on CPU, the trace on Bus trace, nothing when closed", () => {
    const { port, pane, q } = setup();
    expect(port.callsTo("watchRegisters").at(-1)).toEqual([true]);
    expect(port.callsTo("setTrace").at(-1)).toEqual([false, 1024]);
    q<HTMLButtonElement>('[data-tab="trace"]').click();
    expect(port.callsTo("watchRegisters").at(-1)).toEqual([false]);
    expect(port.callsTo("setTrace").at(-1)).toEqual([true, 1024]);
    pane.toggle.click(); // close
    expect(document.body.classList.contains("dbg-closed")).toBe(true);
    expect(port.callsTo("setTrace").at(-1)).toEqual([false, 1024]);
    expect(port.callsTo("watchRegisters").at(-1)).toEqual([false]);
  });

  test("the trace tab shows rows: fetches bright, I/O tinted with its module", async () => {
    const { port, pane, q } = setup();
    q<HTMLButtonElement>('[data-tab="trace"]').click();
    port.traceRows = [
      { cycle: 10, physical: 0xfc888, cpu: 0xc888, data: 0xad, module: 3, write: false, sync: true, vpb: false },
      { cycle: 11, physical: 0x0c301, cpu: 0xc301, data: 0x10, module: 6, write: false, sync: false, vpb: false },
      { cycle: 12, physical: 0x001fd, cpu: 0x01fd, data: 0x41, module: 0, write: true, sync: false, vpb: false },
    ];
    pane.update({ ...frame(), cycles: 12 });
    await tick();
    const rows = [...q('[data-testid="trace-rows"]').children] as HTMLElement[];
    expect(rows.map((r) => [...r.children].map((c) => c.textContent))).toEqual([
      ["10", "FC888", "C888", "AD", "R", "SYNC"],
      ["11", "0C301", "C301", "10", "R", "I/O m6"],
      ["12", "001FD", "01FD", "41", "W", ""],
    ]);
    expect(rows[0].classList.contains("f")).toBe(true);
    expect(rows[1].classList.contains("io")).toBe(true);
  });

  test("add a breakpoint by label: the first is armed; a hit shows, and Clear clears it", async () => {
    const { port, pane, q } = setup();
    q<HTMLButtonElement>('[data-tab="bps"]').click();
    q<HTMLInputElement>('[data-testid="bp-input"]').value = "ECHO";
    q<HTMLFormElement>("form").dispatchEvent(new Event("submit", { cancelable: true }));
    await tick();
    await tick();
    expect(q('[data-testid="bp-row"]').getAttribute("data-armed")).toBe("true");
    expect(q('[data-testid="bp-row"]').textContent).toContain("ECHO $C951 → FC951");
    expect(port.card.get(0x0b)).toBe(0x03);

    for (const [o, v] of [[0x0c, 0x03], [0x10, 0x51], [0x11, 0xc9], [0x12, 0x0f], [0x13, 0x48], [0x14, 0x03]]) port.card.set(o, v);
    pane.update({ ...frame(), halted: true, cycles: 99 });
    await tick();
    await tick();
    expect(q<HTMLElement>('[data-testid="bp-hit"]').hidden).toBe(false);
    expect(q('[data-testid="bp-hit-text"]').textContent).toBe("hit: FC951  data 48  fetch");
    q<HTMLButtonElement>('[data-testid="bp-clear"]').click();
    expect(port.callsTo("cardWrite").at(-1)).toEqual([0x0c, 0x02]);
    expect(q<HTMLElement>('[data-testid="bp-hit"]').hidden).toBe(true);
  });

  test("halted, each step refreshes the trace at once; running, a few times a second", async () => {
    const { port, pane, q } = setup();
    q<HTMLButtonElement>('[data-tab="trace"]').click();
    let reads = 0;
    const read = port.readTrace.bind(port);
    port.readTrace = (max: number) => {
      reads++;
      return read(max);
    };
    pane.update({ ...frame(), halted: true, cycles: 100 });
    await tick();
    pane.update({ ...frame(), halted: true, cycles: 100 }); // nothing ran: no read
    await tick();
    pane.update({ ...frame(), halted: true, cycles: 104 }); // a step, straight after
    await tick();
    expect(reads).toBe(2);
    pane.update({ ...frame(), cycles: 120_000 }); // running, inside the 250 ms: wait
    await tick();
    expect(reads).toBe(2);
  });

  test("a bad address shows an error", async () => {
    const { q } = setup();
    q<HTMLButtonElement>('[data-tab="bps"]').click();
    q<HTMLInputElement>('[data-testid="bp-input"]').value = "NOWHERE";
    q<HTMLFormElement>("form").dispatchEvent(new Event("submit", { cancelable: true }));
    await tick();
    expect(q('[data-testid="bp-error"]').textContent).toMatch(/NOWHERE/);
  });

  test("a power cycle re-arms the card (it forgot)", async () => {
    const { port, pane, q } = setup();
    q<HTMLButtonElement>('[data-tab="bps"]').click();
    q<HTMLInputElement>('[data-testid="bp-input"]').value = "ECHO";
    q<HTMLFormElement>("form").dispatchEvent(new Event("submit", { cancelable: true }));
    await tick();
    await tick();
    pane.update({ ...frame(), powered: false });
    const before = port.callsTo("cardWrite").length;
    pane.update({ ...frame(), powered: true });
    expect(port.callsTo("cardWrite").length).toBe(before + 5);
    expect(port.callsTo("cardWrite").at(-1)).toEqual([0x0b, 0x03]);
  });

  test("open or closed is remembered", () => {
    const { pane } = setup();
    pane.toggle.click();
    document.body.className = "";
    const again = debuggerPane(new FakePort());
    void again;
    expect(document.body.classList.contains("dbg-closed")).toBe(true);
  });
});

function frame() {
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
    apple: false,
    disk: null,
    sd: null,
    video: null,
    audio: null,
    soundRunning: false,
    keysQueued: 0,
  };
}
