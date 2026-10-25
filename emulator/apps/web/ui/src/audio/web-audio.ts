/**
 * The speaker's AudioOut in a browser: an AudioContext at the device's
 * rate, the worklet (speaker-worklet.ts, bundled on its own as a worklet
 * must be), and a gain for the volume.
 */

import type { AudioOut, SpeakerStats } from "./speaker";
import workletUrl from "./speaker-worklet.ts?worker&url";

/** An AudioContext and the speaker's worklet, at the device's rate. */
export async function webAudioOut(url: string = workletUrl): Promise<AudioOut> {
  const context = new AudioContext({ latencyHint: "interactive" });
  await context.audioWorklet.addModule(url);
  const node = new AudioWorkletNode(context, "oo1-speaker", { numberOfInputs: 0, outputChannelCount: [2] });
  const gain = context.createGain();
  node.connect(gain).connect(context.destination);
  let statsListener: ((stats: SpeakerStats) => void) | null = null;
  node.port.onmessage = (e: MessageEvent<SpeakerStats>) => statsListener?.(e.data);
  return {
    rate: context.sampleRate,
    send: (block) => node.port.postMessage(block, [block.buffer]),
    clear: () => node.port.postMessage("clear"),
    resume: () => context.resume(),
    suspend: () => context.suspend(),
    setVolume: (v) => gain.gain.setTargetAtTime(Math.max(0, Math.min(1, v)), context.currentTime, 0.02),
    onStats: (listener) => (statsListener = listener),
  };
}
