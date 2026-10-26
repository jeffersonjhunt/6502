import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import type { FrameState } from "../port/machine-port";
import { Speaker, type AudioOut, type SpeakerStats } from "./speaker";

class FakeOut implements AudioOut {
  readonly rate = 44_100;
  sent: Float32Array[] = [];
  calls: string[] = [];
  volume = 1;
  stats: ((s: SpeakerStats) => void) | null = null;
  send(block: Float32Array) {
    this.sent.push(block);
  }
  clear() {
    this.calls.push("clear");
  }
  async resume() {
    this.calls.push("resume");
  }
  async suspend() {
    this.calls.push("suspend");
  }
  setVolume(v: number) {
    this.volume = v;
  }
  onStats(listener: (s: SpeakerStats) => void) {
    this.stats = listener;
  }
}

/** An idle frame carrying `audio`. */
function frame(audio: Float32Array | null): FrameState {
  const port = new FakePort();
  let f: FrameState | undefined;
  port.onFrame((x) => (f = x));
  port.emit({ audio });
  return f!;
}

describe("Speaker", () => {
  test("on: the port makes samples at the device's rate, and they go out", async () => {
    const port = new FakePort();
    const out = new FakeOut();
    let opened = 0;
    const s = new Speaker(port, async () => (++opened, out));
    await s.turnOn();
    expect(port.calls).toContainEqual(["listen", 44_100]);
    expect(out.calls).toContain("resume");
    const block = Float32Array.of(0.1, -0.2, 0.05, 0);
    s.feed(frame(block));
    expect(out.sent).toEqual([block]);
    expect(s.peak).toBeCloseTo(0.2);
    s.turnOff();
    await s.turnOn(); // opened once only
    expect(opened).toBe(1);
  });

  test("on again once opened: resumed in the same call, inside the user's gesture (Safari's rule)", async () => {
    const out = new FakeOut();
    const s = new Speaker(new FakePort(), async () => out);
    await s.turnOn();
    s.turnOff();
    out.calls = [];
    void s.turnOn();
    expect(out.calls).toEqual(["resume"]); // before any await
  });

  test("off: no samples made, the queue emptied, the device suspended, nothing fed", async () => {
    const port = new FakePort();
    const out = new FakeOut();
    const s = new Speaker(port, async () => out);
    await s.turnOn();
    s.turnOff();
    expect(port.calls.at(-1)).toEqual(["listen", 0]);
    expect(out.calls).toEqual(["resume", "clear", "suspend"]);
    s.feed(frame(Float32Array.of(1, 1)));
    expect(out.sent).toEqual([]);
    expect(s.peak).toBe(0);
  });

  test("turned off while the device opens: it never listens", async () => {
    const port = new FakePort();
    let open!: (o: AudioOut) => void;
    const s = new Speaker(port, () => new Promise((r) => (open = r)));
    const on = s.turnOn();
    s.turnOff();
    open(new FakeOut());
    await on;
    expect(port.calls.filter(([c]) => c === "listen")).toEqual([["listen", 0]]);
  });

  test("a device that won't open: off again, with the reason", async () => {
    const port = new FakePort();
    const s = new Speaker(port, async () => {
      throw new Error("no audio device");
    });
    let changes = 0;
    s.onChange(() => ++changes);
    await s.turnOn();
    expect(s.on).toBe(false);
    expect(s.error).toBe("no audio device");
    expect(changes).toBeGreaterThan(0);
  });

  test("wake resumes a device left waiting for the user; volume and stats pass through", async () => {
    const out = new FakeOut();
    const s = new Speaker(new FakePort(), async () => out);
    s.setVolume(0.5);
    await s.turnOn();
    expect(out.volume).toBe(0.5);
    s.wake();
    expect(out.calls.filter((c) => c === "resume").length).toBe(2);
    out.stats!({ played: 480, underruns: 1, dropped: 0, queued: 1200 });
    expect(s.stats.played).toBe(480);
  });
});
