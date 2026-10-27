/**
 * The sound card's tune: Play .sid… in Settings takes a C64 tune of your
 * own (PSID or RSID), straight onto the card as a host program would start
 * it; then the status strip's now-playing pill has its name and author, the
 * song (n/m, with previous and next) and Stop. The file stays in the page:
 * it is not kept, and never uploaded.
 *
 * Play .sid… turns Sound on there and then, in its click, before the
 * picker opens (the gesture a browser waits for: Safari may not start
 * sound after an await, and the pick is one); a cancel, or a file refused,
 * turns it off again if it was off. A file given some other way turns it
 * on in the change. The note says what the card can't give a tune (a third
 * SID, an RSID's VIC-II), or why a file was refused. A tune stops when the
 * card does: Stop, a power cycle, START's /RESET, or the BIOS's QUIET on
 * Break, a BRK and the demo's end. It pauses in a background tab, as the
 * whole machine does (port/wasm-port.ts).
 */

import type { FrameState, MachinePort, SidTune } from "../port/machine-port";
import { h } from "./dom";
import { icon } from "./icons";

/** soundOn: Sound on if it is off. @returns whether it was off, so soundOff puts it back. */
export function sidBar(port: MachinePort, hooks: { soundOn: () => boolean; soundOff: () => void }) {
  const picker = h("input", { type: "file", accept: ".sid", hidden: true, "data-testid": "sid-file" });
  const play = h("button", { class: "tbtn", type: "button", title: "A C64 .sid tune of your own, played on the sound card: it stays in this page. It plays while this tab is in front", "data-testid": "sid-play" }, "Play .sid…");
  const note = h("p", { class: "row-note", role: "status", "data-testid": "sid-note" });
  const element = h(
    "div",
    { class: "row-wrap", hidden: !port.caps.sid, "data-testid": "sid-bar" },
    h("div", { class: "row" }, picker, h("div", { class: "what" }, h("div", { class: "name" }, "C64 tune"), h("div", { class: "meta" }, "a .sid file, played on the sound card")), play),
    note,
  );

  const title = h("span", { class: "sid-title", "data-testid": "sid-title" });
  const prev = h("button", { class: "tbtn sm", type: "button", title: "The song before", "aria-label": "Previous song", "data-testid": "sid-prev" }, icon("prev"));
  const song = h("span", { class: "sid-song", "data-testid": "sid-song" });
  const next = h("button", { class: "tbtn sm", type: "button", title: "The next song", "aria-label": "Next song", "data-testid": "sid-next" }, icon("next"));
  const stop = h("button", { class: "tbtn sm", type: "button", title: "Stop the tune", "aria-label": "Stop the tune", "data-testid": "sid-stop" }, icon("stop"));
  const pill = h("div", { class: "pill playing", role: "group", "aria-label": "Now playing", hidden: true, "data-testid": "sid-pill" }, icon("note"), title, song, prev, next, stop);

  let tune: SidTune | null = null;
  const say = (text: string) => (note.textContent = text);
  const show = () => {
    title.textContent = tune ? [tune.name || "(untitled)", tune.author].filter(Boolean).join(" — ") + (tune.released ? ` (${tune.released})` : "") : "";
    title.title = tune ? `${title.textContent} · ${tune.clock}, ${tune.model}${tune.rsid ? ", RSID" : ""}` : "";
    song.textContent = tune ? `${tune.song}/${tune.songs}` : "";
    song.title = tune ? `Song ${tune.song} of ${tune.songs}` : "";
    pill.hidden = !tune;
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

  let turnedOn = false; // by the click, for the pick it opened
  const giveBack = () => {
    if (turnedOn) hooks.soundOff();
    turnedOn = false;
  };
  play.addEventListener("click", () => {
    turnedOn = hooks.soundOn() || turnedOn;
    picker.click();
  });
  picker.addEventListener("cancel", giveBack);
  picker.addEventListener("change", async () => {
    const f = picker.files?.[0];
    picker.value = "";
    if (!f) return giveBack();
    const mine = turnedOn || hooks.soundOn(); // before any await
    turnedOn = false;
    try {
      started(await port.playSid(new Uint8Array(await f.arrayBuffer())));
    } catch (err) {
      if (mine) hooks.soundOff(); // not left on for a file that isn't a tune
      say(`${f.name}: ${err instanceof Error ? err.message : String(err)}`);
    }
  });
  const go = async (n: number) => {
    if (!tune) return; // previous and next are disabled at the ends, and the port refuses a song it hasn't
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

  /** The card stopped under the tune (a power cycle, START, QUIET): it's gone. */
  const update = (f: FrameState) => {
    if (tune && !f.soundRunning) {
      port.stopSid(); // forgotten, as the card has
      tune = null;
      say(f.powered ? "The machine stopped the card: the tune stopped" : "");
      show();
    }
  };
  return { element, pill, update, playing: () => !!tune };
}
