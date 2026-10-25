import { describe, expect, test } from "vitest";
import { SampleQueue } from "./sample-queue";

const RATE = 1000; // start 60 frames, most 250, target 100

/** `frames` stereo frames, left = n, right = -n, counting on from `from`. */
function block(frames: number, from = 0): Float32Array {
  const b = new Float32Array(2 * frames);
  for (let i = 0; i < frames; ++i) {
    b[2 * i] = from + i;
    b[2 * i + 1] = -(from + i);
  }
  return b;
}

function pull(q: SampleQueue, n = 10) {
  const l = new Float32Array(n).fill(99);
  const r = new Float32Array(n).fill(99);
  const got = q.pull(l, r);
  return { got, l: [...l], r: [...r] };
}

describe("SampleQueue", () => {
  test("silence until it holds 60 ms, then the samples in order, left and right", () => {
    const q = new SampleQueue(RATE);
    q.push(block(59));
    expect(pull(q)).toEqual({ got: 0, l: Array(10).fill(0), r: Array(10).fill(0) });
    q.push(block(41, 59));
    const p = pull(q);
    expect(p.got).toBe(10);
    expect(p.l).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
    expect(p.r).toEqual([-0, -1, -2, -3, -4, -5, -6, -7, -8, -9]);
    expect(pull(q).l[0]).toBe(10); // on across the block boundary at 59
    for (let i = 0; i < 4; ++i) pull(q);
    expect(pull(q).l).toEqual([60, 61, 62, 63, 64, 65, 66, 67, 68, 69]);
    expect(q.queued).toBe(30);
    expect(q.stats.played).toBe(70);
  });

  test("running dry: the rest is silence, counted, and it waits to hold 60 ms again", () => {
    const q = new SampleQueue(RATE);
    q.push(block(64));
    for (let i = 0; i < 6; ++i) pull(q);
    const p = pull(q);
    expect(p.got).toBe(4);
    expect(p.l).toEqual([60, 61, 62, 63, 0, 0, 0, 0, 0, 0]);
    expect(q.stats.underruns).toBe(1);
    q.push(block(30, 64));
    expect(pull(q).got).toBe(0); // 30 < 60: still waiting
    q.push(block(30, 94));
    expect(pull(q).l[0]).toBe(64);
  });

  test("past 250 ms it drops the oldest, down to 100 ms", () => {
    const q = new SampleQueue(RATE);
    for (let i = 0; i < 5; ++i) q.push(block(50, i * 50));
    expect(q.queued).toBe(250);
    q.push(block(50, 250)); // 300 queued: drop 200
    expect(q.queued).toBe(100);
    expect(q.stats.dropped).toBe(200);
    expect(pull(q).l[0]).toBe(200); // the newest 100 stay
  });

  test("clear empties it without counting a drop, and it waits to fill again", () => {
    const q = new SampleQueue(RATE);
    q.push(block(100));
    pull(q);
    q.clear();
    expect(q.queued).toBe(0);
    expect(q.stats.dropped).toBe(0);
    q.push(block(10, 500));
    expect(pull(q).got).toBe(0);
  });

  test("an empty block is nothing", () => {
    const q = new SampleQueue(RATE);
    q.push(new Float32Array(0));
    expect(q.queued).toBe(0);
  });
});
