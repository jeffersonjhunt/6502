/**
 * The sound card through the computer's speakers: Web Audio, with the
 * samples from FrameState.audio queued on the audio thread
 * (speaker-worklet.ts) to smooth over the frame loop's jitter.
 *
 * Off, the port makes no samples at all (listen(0)). On, it makes them at
 * the AudioContext's own rate, so nothing here resamples. A browser lets a
 * page start sound only after the user has done something on it: turned on
 * by a click, it plays at once; turned on at load (the setting kept from
 * last time), it waits for the first click or key, through wake().
 *
 * The audio is behind an AudioOut (web-audio.ts's, loaded when first
 * wanted), so tests can run the logic without a browser's audio.
 */

import type { FrameState, MachinePort } from "../port/machine-port";
import type { QueueStats } from "./sample-queue";

export interface SpeakerStats extends QueueStats {
  /** Frames waiting on the audio thread. */
  queued: number;
}

/** Where the samples go. */
export interface AudioOut {
  readonly rate: number;
  /** Left and right interleaved; transferred, so not to be used after. */
  send(block: Float32Array): void;
  clear(): void;
  resume(): Promise<void>;
  suspend(): Promise<void>;
  /** 0 to 1. */
  setVolume(volume: number): void;
  onStats(listener: (stats: SpeakerStats) => void): void;
}

export class Speaker {
  private out: AudioOut | null = null;
  private opening: Promise<AudioOut> | null = null;
  private wanted = false;
  private volume = 1;
  private readonly listeners = new Set<() => void>();
  /** The loudest sample in the last block, 0 to 1. */
  peak = 0;
  stats: SpeakerStats = { played: 0, underruns: 0, dropped: 0, queued: 0 };
  /** Why it could not start, if it could not. */
  error = "";

  constructor(
    private readonly port: MachinePort,
    private readonly open: () => Promise<AudioOut> = () => import("./web-audio").then((m) => m.webAudioOut()),
  ) {}

  get on(): boolean {
    return this.wanted;
  }

  /** Start: from a click it plays at once; otherwise at the next wake(). */
  async turnOn(): Promise<void> {
    this.wanted = true;
    this.error = "";
    this.changed();
    try {
      this.opening ??= this.open();
      const out = await this.opening;
      if (!this.out) {
        this.out = out;
        out.setVolume(this.volume);
        out.onStats((s) => {
          this.stats = s;
          this.changed();
        });
      }
      if (!this.wanted) return; // turned off while it opened
      this.port.listen(out.rate);
      void out.resume().catch(() => {}); // pending until the user does something, if they haven't
    } catch (err) {
      this.wanted = false;
      this.opening = null;
      this.error = err instanceof Error ? err.message : String(err);
      this.changed();
    }
  }

  turnOff(): void {
    this.wanted = false;
    this.port.listen(0);
    this.peak = 0;
    if (this.out) {
      this.out.clear();
      void this.out.suspend().catch(() => {});
    }
    this.changed();
  }

  /** A click or a key: what a browser waits for before it plays. */
  wake(): void {
    if (this.wanted && this.out) void this.out.resume().catch(() => {});
  }

  setVolume(volume: number): void {
    this.volume = volume;
    this.out?.setVolume(volume);
  }

  /** The frame's samples to the speaker. */
  feed(frame: FrameState): void {
    const block = frame.audio;
    if (!block || !this.wanted || !this.out) return;
    let peak = 0;
    for (const v of block) peak = Math.max(peak, Math.abs(v));
    this.peak = peak;
    this.out.send(block);
  }

  onChange(listener: () => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private changed(): void {
    for (const l of this.listeners) l();
  }
}
