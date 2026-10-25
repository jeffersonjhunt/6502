/**
 * The sound card's tune under the monitor, beside Sound: Play .sid… takes a
 * C64 tune of your own (PSID or RSID), straight onto the card as a host
 * program would start it; then its name and author, the song (◀ n of N ▶)
 * and Stop. The file stays in the page: it is not kept, and never uploaded.
 *
 * Choosing a tune turns Sound on (the click is the gesture a browser waits
 * for). The note says what the card can't give a tune (a third SID, an
 * RSID's VIC-II), or why a file was refused. A tune stops when the card
 * does: Stop, a power cycle, or START's /RESET.
 */

import type { FrameState, MachinePort, SidTune } from "../port/machine-port";
import { h } from "./dom";

export function sidBar(port: MachinePort, hooks: { playing: () => void }) {
  const picker = h("input", { type: "file", accept: ".sid", hidden: true, "data-testid": "sid-file" });
  const play = h("button", { class: "tbtn", type: "button", title: "A C64 .sid tune of your own, played on the sound card: it stays in this page", "data-testid": "sid-play" }, "Play .sid…");
  const title = h("span", { class: "sid-title", "data-testid": "sid-title" });
  const prev = h("button", { class: "tbtn", type: "button", title: "The song before", "aria-label": "Previous song", "data-testid": "sid-prev" }, "◀");
  const song = h("span", { class: "sid-song", "data-testid": "sid-song" });
  const next = h("button", { class: "tbtn", type: "button", title: "The next song", "aria-label": "Next song", "data-testid": "sid-next" }, "▶");
  const stop = h("button", { class: "tbtn", type: "button", title: "Stop the tune", "data-testid": "sid-stop" }, "Stop");
  const note = h("span", { class: "apple-note", role: "status", "data-testid": "sid-note" });
  const element = h("div", { class: "apple-bar sid-bar", hidden: !port.caps.sid, "data-testid": "sid-bar" }, picker, play, title, prev, song, next, stop, note);

  let tune: SidTune | null = null;
  const say = (text: string) => (note.textContent = text);
  const show = () => {
    title.textContent = tune ? [tune.name || "(untitled)", tune.author].filter(Boolean).join(" — ") + (tune.released ? ` (${tune.released})` : "") : "";
    title.title = tune ? `${title.textContent} · ${tune.clock}, ${tune.model}${tune.rsid ? ", RSID" : ""}` : "";
    song.textContent = tune ? `song ${tune.song} of ${tune.songs}` : "";
    for (const b of [prev, next, stop]) b.hidden = !tune;
    title.hidden = song.hidden = !tune;
    prev.disabled = !tune || tune.song <= 1;
    next.disabled = !tune || tune.song >= tune.songs;
    play.textContent = tune ? "Another .sid…" : "Play .sid…";
  };
  const started = (t: SidTune) => {
    tune = t;
    const notes = [...t.warnings];
    if (t.rsid) notes.push("it may stay silent");
    say(notes.length ? `The card can't play all of it: ${notes.join("; ")}` : "");
    show();
  };

  play.addEventListener("click", () => picker.click());
  picker.addEventListener("change", async () => {
    const f = picker.files?.[0];
    picker.value = "";
    if (!f) return;
    hooks.playing();
    try {
      started(await port.playSid(new Uint8Array(await f.arrayBuffer())));
    } catch (err) {
      say(`${f.name}: ${err instanceof Error ? err.message : String(err)}`);
    }
  });
  const go = async (n: number) => {
    if (!tune) return; // ◀ and ▶ are disabled at the ends, and the port refuses a song it hasn't
    try {
      started(await port.playSong(n));
    } catch (err) {
      say(String(err instanceof Error ? err.message : err));
    }
  };
  prev.addEventListener("click", () => void go((tune?.song ?? 1) - 1));
  next.addEventListener("click", () => void go((tune?.song ?? 0) + 1));
  stop.addEventListener("click", () => {
    port.stopSid();
    tune = null;
    say("");
    show();
  });
  show();

  /** The card stopped under the tune (a power cycle, START): it's gone. */
  const update = (f: FrameState) => {
    if (tune && !f.soundRunning) {
      port.stopSid(); // forgotten, as the card has
      tune = null;
      say(f.powered ? "The card was reset: the tune stopped" : "");
      show();
    }
  };
  return { element, update };
}
