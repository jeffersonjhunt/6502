/**
 * The Sound switch under the monitor: the speaker on or off, kept for next time,
 * with a level meter in the button while it plays. Its data-played and
 * data-peak are the speaker's, for tests.
 */

import type { Speaker } from "../audio/speaker";
import { h, prefs } from "./dom";

export function soundButton(speaker: Speaker, available: boolean) {
  const meter = h("i", { class: "meter", "aria-hidden": "true" });
  const button = h(
    "button",
    {
      class: "tbtn sound",
      type: "button",
      "aria-pressed": "false",
      "data-testid": "tb-sound",
      disabled: !available,
      title: available ? "The sound card through your speakers" : "This machine has no sound to play here",
    },
    meter,
    "Sound",
  );
  const show = () => {
    button.classList.toggle("on", speaker.on);
    button.setAttribute("aria-pressed", String(speaker.on));
    if (speaker.error) button.title = `No sound: ${speaker.error}`;
    button.dataset.played = String(speaker.stats.played);
  };
  speaker.onChange(show);
  button.addEventListener("click", () => {
    if (speaker.on) speaker.turnOff();
    else void speaker.turnOn();
    prefs.set("sound", speaker.on ? "on" : "off");
  });
  // A browser plays only after the user does something: the first click or key anywhere wakes a kept setting.
  for (const type of ["pointerdown", "keydown"] as const) document.addEventListener(type, () => speaker.wake(), { capture: true });

  /** Turn it on again if it was left on (it plays from the first click or key). */
  const restore = () => {
    if (available && prefs.get("sound") === "on") void speaker.turnOn();
  };
  /** The meter, once a frame. */
  const update = () => {
    const level = speaker.on ? Math.min(1, speaker.peak * 4) : 0; // a voice at full is about 1/8
    meter.style.setProperty("--level", level.toFixed(3));
    button.dataset.peak = speaker.peak.toFixed(4);
    button.dataset.played = String(speaker.stats.played);
  };
  show();
  return { element: button, update, restore };
}
