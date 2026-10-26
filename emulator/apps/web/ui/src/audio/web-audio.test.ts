import { afterEach, expect, test, vi } from "vitest";
import { webAudioOut } from "./web-audio";

afterEach(() => vi.unstubAllGlobals());

test("a worklet that will not load: the AudioContext is closed, not left open for each retry", async () => {
  const contexts: { closed: boolean }[] = [];
  vi.stubGlobal(
    "AudioContext",
    class {
      closed = false;
      sampleRate = 48_000;
      audioWorklet = { addModule: async () => Promise.reject(new Error("no worklet")) };
      constructor() {
        contexts.push(this);
      }
      async close() {
        this.closed = true;
      }
    },
  );
  await expect(webAudioOut("x.js")).rejects.toThrow("no worklet");
  await expect(webAudioOut("x.js")).rejects.toThrow("no worklet");
  expect(contexts.map((c) => c.closed)).toEqual([true, true]);
});
