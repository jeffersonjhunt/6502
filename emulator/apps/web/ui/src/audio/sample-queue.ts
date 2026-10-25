/**
 * The speaker's queue: blocks of stereo samples in from the frame loop,
 * out a render quantum at a time to the audio thread (speaker-worklet.ts).
 *
 * The two sides run on different clocks, the emulator's (paced by animation
 * frames) and the sound device's, so the queue is the slack between them.
 * It starts playing once it holds `start` frames, about 60 ms: one late
 * animation frame then doesn't run it dry. If it does run dry it plays
 * silence and waits to hold `start` again, rather than stutter a few
 * samples at a time. If it holds more than `most` (250 ms: the emulator ran
 * ahead, at "Max" or after a stall), it drops the oldest samples down to
 * `target`, so the sound never lags the machine by more than that.
 */

export interface QueueStats {
  /** Frames played from the queue (not silence). */
  played: number;
  /** Times it ran dry while playing. */
  underruns: number;
  /** Frames dropped to catch up. */
  dropped: number;
}

export class SampleQueue {
  private readonly blocks: Float32Array[] = [];
  private offset = 0; // frames already taken from blocks[0]
  private frames = 0; // frames queued
  private primed = false;
  readonly stats: QueueStats = { played: 0, underruns: 0, dropped: 0 };
  readonly start: number;
  readonly most: number;
  readonly target: number;

  constructor(rate: number) {
    this.start = Math.round(rate * 0.06);
    this.most = Math.round(rate * 0.25);
    this.target = Math.round(rate * 0.1);
  }

  get queued(): number {
    return this.frames;
  }

  /** Left and right interleaved. */
  push(block: Float32Array): void {
    const n = block.length >> 1;
    if (!n) return;
    this.blocks.push(block);
    this.frames += n;
    if (this.frames > this.most) this.drop(this.frames - this.target);
  }

  /** Empty it, as when the sound is turned off: not counted as dropped. */
  clear(): void {
    this.blocks.length = 0;
    this.offset = this.frames = 0;
    this.primed = false;
  }

  /** Fill one quantum of each channel: from the queue while it plays, else silence. @return frames from the queue */
  pull(left: Float32Array, right: Float32Array): number {
    const n = left.length;
    if (!this.primed && this.frames >= this.start) this.primed = true;
    let i = 0;
    if (this.primed) {
      while (i < n && this.blocks.length) {
        const b = this.blocks[0];
        const take = Math.min(n - i, (b.length >> 1) - this.offset);
        for (let k = 0; k < take; ++k) {
          left[i + k] = b[2 * (this.offset + k)];
          right[i + k] = b[2 * (this.offset + k) + 1];
        }
        i += take;
        this.advance(take);
      }
      if (i < n) {
        this.primed = false; // dry: wait to hold `start` again
        ++this.stats.underruns;
      }
    }
    left.fill(0, i);
    right.fill(0, i);
    this.stats.played += i;
    return i;
  }

  private drop(frames: number): void {
    this.stats.dropped += frames;
    while (frames > 0 && this.blocks.length) {
      const take = Math.min(frames, (this.blocks[0].length >> 1) - this.offset);
      this.advance(take);
      frames -= take;
    }
  }

  private advance(frames: number): void {
    this.offset += frames;
    this.frames -= frames;
    if (this.offset >= this.blocks[0].length >> 1) {
      this.blocks.shift();
      this.offset = 0;
    }
  }
}
