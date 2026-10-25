/**
 * The speaker's audio-thread side: an AudioWorklet processor playing the
 * sound card's samples from a SampleQueue. Blocks arrive as messages (a
 * Float32Array, left and right interleaved, or "clear"); about every 250 ms
 * it posts the queue's stats back, which the page shows and tests read.
 *
 * Bundled on its own (`?worker&url` in speaker.ts), as a worklet must be.
 */

import { SampleQueue } from "./sample-queue";

// The AudioWorkletGlobalScope, which TypeScript's DOM library leaves out.
declare const sampleRate: number;
declare function registerProcessor(name: string, processor: new () => object): void;
declare class AudioWorkletProcessor {
  readonly port: MessagePort;
}

class SpeakerProcessor extends AudioWorkletProcessor {
  private readonly queue = new SampleQueue(sampleRate);
  private sinceReport = 0;

  constructor() {
    super();
    this.port.onmessage = (e: MessageEvent<Float32Array | "clear">) => {
      if (e.data === "clear") this.queue.clear();
      else this.queue.push(e.data);
    };
  }

  process(_inputs: Float32Array[][], outputs: Float32Array[][]): boolean {
    const [left, right] = outputs[0];
    this.queue.pull(left, right ?? new Float32Array(left.length));
    this.sinceReport += left.length;
    if (this.sinceReport >= sampleRate / 4) {
      this.sinceReport = 0;
      this.port.postMessage({ ...this.queue.stats, queued: this.queue.queued });
    }
    return true;
  }
}

registerProcessor("oo1-speaker", SpeakerProcessor);
