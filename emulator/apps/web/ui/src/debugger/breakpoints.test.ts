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

  test("a saved list that is not what this page wrote: the bad ones are dropped, the good one kept", () => {
    const good = { id: "3", text: "ECHO", cpu: 0xc951, physical: 0xfc951, fetch: true, read: false, write: false, nmi: false };
    const bad = [
      null,
      7,
      { physical: 0x1234 }, // no cpu: describe() would throw
      { ...good, cpu: "C951" },
      { ...good, physical: Number.NaN },
      { ...good, physical: 0x100000 },
      { ...good, text: 5 },
      { ...good, fetch: "yes" },
      { ...good, id: 9 },
    ];
    localStorage.setItem("oo1.breakpoints", JSON.stringify([...bad, good]));
    const bps = new Breakpoints(port());
    expect(bps.list).toEqual([good]);
    expect(bps.list.map(describeBp)).toEqual(["ECHO $C951 → FC951"]);
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

  // The 24-bit bus (hw/design.md, "B1: as built"): BPADDR is 20 bits and matches only with A20-A23 0.
  test("a label in W1 on block 33, the aperture at $E04123, is refused: BPADDR can't hold it", async () => {
    const p = port();
    p.labels.set("SLOT1", 0x4123);
    p.physical = async (cpu: number) => (cpu >= 0x4000 && cpu < 0x8000 ? 0xe00000 | cpu : cpu); // W1 on block 33
    const bps = new Breakpoints(p);
    const r = await bps.add("SLOT1");
    expect(typeof r).toBe("string");
    expect(r).toMatch(/SLOT1: \$4123 is E04123, above FFFFF/);
    expect(bps.list).toEqual([]);
  });

  test("a typed 84123 is refused while AP8 is set, bank 8 being the aperture; kept with it clear", async () => {
    const p = port();
    p.io.set(0xc6ff, 0xd3); // NB_ID: the bridge
    p.io.set(0xc6a5, 0x01); // MAP: AP8 set
    const bps = new Breakpoints(p);
    const r = await bps.add("84123");
    expect(typeof r).toBe("string");
    expect(r).toMatch(/84123: bank 8 is the video aperture/);
    expect(bps.list).toEqual([]);
    p.io.set(0xc6a5, 0x00); // AP8 clear: bank 8 is RAM
    const kept = await bps.add("84123");
    expect(typeof kept).toBe("object");
    expect(bps.list.length).toBe(1);
  });

  test("a saved E0xxxx breakpoint isn't restored, and says so", () => {
    const good = { id: "3", text: "ECHO", cpu: 0xc951, physical: 0xfc951, fetch: true, read: false, write: false, nmi: false };
    const high = { ...good, id: "4", text: "SLOT1", cpu: 0x4123, physical: 0xe04123 };
    localStorage.setItem("oo1.breakpoints", JSON.stringify([good, high]));
    const bps = new Breakpoints(port());
    expect(bps.list).toEqual([good]);
    expect(bps.notRestored).toEqual(["SLOT1 $4123 → E04123"]);
  });

  test("a saved bank 8 breakpoint, from before B1, isn't restored with AP8 set once checked", async () => {
    const good = { id: "3", text: "ECHO", cpu: 0xc951, physical: 0xfc951, fetch: true, read: false, write: false, nmi: false };
    const bank8 = { ...good, id: "5", text: "84123", cpu: null, physical: 0x84123 };
    localStorage.setItem("oo1.breakpoints", JSON.stringify([good, bank8]));
    const set = port();
    set.io.set(0xc6ff, 0xd3); // NB_ID: the bridge
    set.io.set(0xc6a5, 0x01); // MAP: AP8 set
    const bps = new Breakpoints(set);
    await bps.recheck();
    expect(bps.list).toEqual([good]);
    expect(bps.notRestored).toEqual(["84123"]);
    const clear = port();
    clear.io.set(0xc6ff, 0xd3);
    clear.io.set(0xc6a5, 0x00); // AP8 clear: bank 8 is RAM, it can fire
    const kept = new Breakpoints(clear);
    await kept.recheck();
    expect(kept.list).toEqual([good, bank8]);
    expect(kept.notRestored).toEqual([]);
  });

  // B7 (question 12, C12): with the bridge MAP's AP8 decides; with nothing answering at NB_ID, VIDEO_ID does.
  test("with AP8 clear, a bank 8 breakpoint is kept though the video card is fitted", async () => {
    const p = port();
    p.io.set(0xc6ff, 0xd3); // NB_ID: the bridge
    p.io.set(0xc6a5, 0x00); // MAP: AP8 clear
    p.io.set(0xc4ff, 0x92); // VIDEO_ID: the video card
    const bps = new Breakpoints(p);
    const kept = await bps.add("84123");
    expect(typeof kept).toBe("object");
    expect(bps.list.length).toBe(1);
    await bps.recheck();
    expect(bps.list.length).toBe(1);
    expect(bps.notRestored).toEqual([]);
  });

  test("with no bridge answering bank 8 reaches the bus, whatever MAP and VIDEO_ID read", async () => {
    // Only the bridge redirects bank 8: with none, a bank 8 breakpoint is kept, the video card fitted or not.
    const p = port();
    p.io.set(0xc6ff, 0xff);
    p.io.set(0xc6a5, 0xff); // nothing answering reads $FF, AP8's bit too
    p.io.set(0xc4ff, 0xff); // VIDEO_ID: no video card
    const bps = new Breakpoints(p);
    const kept = await bps.add("84123");
    expect(typeof kept).toBe("object");
    p.io.set(0xc4ff, 0x92); // the video card
    const also = await bps.add("84124");
    expect(typeof also).toBe("object");
    expect(bps.list.length).toBe(2);
    await bps.recheck();
    expect(bps.list.length).toBe(2);
    expect(bps.notRestored).toEqual([]);
  });

  test("a bank 8 breakpoint follows MAP's AP8 on a $D3 bridge", async () => {
    // Whatever card is fitted: AP8 clear with the video card keeps it; AP8 set with none answering refuses it.
    const p = port();
    p.io.set(0xc6ff, 0xd3); // NB_ID: the bridge, revision 3
    p.io.set(0xc6a5, 0x00); // MAP: AP8 clear
    p.io.set(0xc4ff, 0x92); // VIDEO_ID: the video card
    const bps = new Breakpoints(p);
    const kept = await bps.add("84123");
    expect(typeof kept).toBe("object");
    await bps.recheck();
    expect(bps.list.length).toBe(1);
    expect(bps.notRestored).toEqual([]);
    p.io.set(0xc6a5, 0x01); // AP8 set
    p.io.set(0xc4ff, 0xff); // and no video card answering
    const r = await bps.add("84124");
    expect(r).toMatch(/84124: bank 8 is the video aperture while AP8 \(MAP, \$C6A5 bit 0\) is set/);
    await bps.recheck();
    expect(bps.list).toEqual([]);
    expect(bps.notRestored).toEqual(["84123"]);
  });

  test("breakpoints not restored are kept through the next save, and their ids aren't reused", async () => {
    const good = { id: "3", text: "ECHO", cpu: 0xc951, physical: 0xfc951, fetch: true, read: false, write: false, nmi: false };
    const high = { ...good, id: "4", text: "SLOT1", cpu: 0x4123, physical: 0xe04123 };
    localStorage.setItem("oo1.breakpoints", JSON.stringify([good, high]));
    const bps = new Breakpoints(port());
    const added = (await bps.add("C800")) as { id: string };
    expect(added.id).toBe("5");
    const saved = JSON.parse(localStorage.getItem("oo1.breakpoints")!) as { id: string }[];
    expect(saved.map((b) => b.id).sort()).toEqual(["3", "4", "5"]);
    const again = new Breakpoints(port());
    expect(again.list.map((b) => b.id)).toEqual(["3", "5"]);
    expect(again.notRestored).toEqual(["SLOT1 $4123 → E04123"]);
  });

  test("bpctrl bits", () => {
    const b = { id: "1", text: "x", cpu: null, physical: 0, fetch: false, read: true, write: true, nmi: true };
    expect(bpctrl(b)).toBe(0x1d);
  });
});
