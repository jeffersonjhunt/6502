// @vitest-environment happy-dom
import { beforeEach, describe, expect, test } from "vitest";
import { CARD } from "../port/machine-port";
import { FakePort } from "../port/fake-port";
import { BPCTRL, Breakpoints, bpctrl, describe as describeBp } from "./breakpoints";

function port() {
  const p = new FakePort();
  p.labels.set("ECHO", 0xc951);
  return p;
}
const writes = (p: FakePort) => p.callsTo("cardWrite") as [number, number][];

describe("Breakpoints", () => {
  beforeEach(() => localStorage.clear());

  test("a label resolves through the windows to a physical address", async () => {
    const p = port();
    const bps = new Breakpoints(p);
    const b = await bps.add("ECHO");
    expect(typeof b).toBe("object");
    if (typeof b === "string") return;
    expect(b.cpu).toBe(0xc951);
    expect(b.physical).toBe(0xfc951); // FakePort's windows: $C800+ is $Fxxxx
    expect(describeBp(b)).toBe("ECHO $C951 → FC951");
  });

  test("five hex digits are a physical address as typed; four are a CPU address", async () => {
    const bps = new Breakpoints(port());
    const phys = (await bps.add("0C301")) as { cpu: number | null; physical: number };
    expect(phys.cpu).toBeNull();
    expect(phys.physical).toBe(0x0c301);
    const cpu = (await bps.add("$C951")) as { cpu: number | null; physical: number; text: string };
    expect(cpu.cpu).toBe(0xc951);
    expect(describeBp(cpu as never)).toBe("C951 → FC951");
  });

  test("nonsense is an error, not a breakpoint", async () => {
    const bps = new Breakpoints(port());
    expect(await bps.add("NOWHERE")).toMatch(/NOWHERE: not a label/);
    expect(await bps.add("  ")).toMatch(/type an address/);
    expect(bps.list).toEqual([]);
  });

  test("arming writes the address before enabling, and disarming clears BPCTRL", async () => {
    const p = port();
    const bps = new Breakpoints(p);
    const b = (await bps.add("ECHO", { fetch: true })) as { id: string };
    bps.arm(b.id);
    expect(writes(p)).toEqual([
      [CARD.BPCTRL, 0],
      [CARD.BPADDR, 0x51],
      [CARD.BPADDR + 1, 0xc9],
      [CARD.BPADDR + 2, 0x0f],
      [CARD.BPCTRL, BPCTRL.ENABLE | BPCTRL.FETCH],
    ]);
    bps.arm(null);
    expect(writes(p).at(-1)).toEqual([CARD.BPCTRL, 0]);
  });

  test("one armed at a time; changing the armed one's flags rewrites BPCTRL", async () => {
    const p = port();
    const bps = new Breakpoints(p);
    const a = (await bps.add("ECHO")) as { id: string };
    const b = (await bps.add("0C301", { read: true })) as { id: string };
    bps.arm(a.id);
    bps.arm(b.id);
    expect(bps.armed).toBe(b.id);
    expect(p.card.get(CARD.BPADDR + 2)).toBe(0x00);
    bps.setFlag(b.id, "write", true);
    bps.setFlag(b.id, "nmi", true);
    expect(p.card.get(CARD.BPCTRL)).toBe(BPCTRL.ENABLE | BPCTRL.READ | BPCTRL.WRITE | BPCTRL.NMI);
    bps.setFlag(b.id, "read", false);
    bps.setFlag(b.id, "write", false); // nothing to match: fetch, rather than a dead comparator
    expect(bps.get(b.id)!.fetch).toBe(true);
    const n = writes(p).length;
    bps.setFlag(a.id, "read", true); // not armed: the card is not touched
    expect(writes(p).length).toBe(n);
  });

  test("removing the armed one disarms the card", async () => {
    const p = port();
    const bps = new Breakpoints(p);
    const a = (await bps.add("ECHO")) as { id: string };
    bps.arm(a.id);
    bps.remove(a.id);
    expect(bps.armed).toBeNull();
    expect(p.card.get(CARD.BPCTRL)).toBe(0);
  });

  test("the list is remembered; nothing comes back armed", async () => {
    const bps = new Breakpoints(port());
    const a = (await bps.add("ECHO")) as { id: string };
    bps.arm(a.id);
    const again = new Breakpoints(port());
    expect(again.list.map((b) => b.text)).toEqual(["ECHO"]);
    expect(again.armed).toBeNull();
    const b = (await again.add("C800")) as { id: string };
    expect(b.id).not.toBe(a.id); // ids keep counting
  });

  test("hit: reads the latched cycle only when STATUS says so; clear writes 1 to the bit", async () => {
    const p = port();
    const bps = new Breakpoints(p);
    p.card.set(CARD.STATUS, 0x01); // halted, no hit
    expect(await bps.hit()).toBeNull();
    p.card.set(CARD.STATUS, 0x03);
    p.card.set(CARD.LADDR, 0x51);
    p.card.set(CARD.LADDR + 1, 0xc9);
    p.card.set(CARD.LADDR + 2, 0x0f);
    p.card.set(CARD.LDATA, 0x48);
    p.card.set(CARD.LSTAT, 0x03);
    expect(await bps.hit()).toEqual({ physical: 0xfc951, data: 0x48, stat: 0x03 });
    bps.clearHit();
    expect(writes(p).at(-1)).toEqual([CARD.STATUS, 0x02]);
  });

  test("bpctrl bits", () => {
    const b = { id: "1", text: "x", cpu: null, physical: 0, fetch: false, read: true, write: true, nmi: true };
    expect(bpctrl(b)).toBe(0x1d);
  });
});
