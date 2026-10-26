// @vitest-environment happy-dom
import { describe, expect, test } from "vitest";
import { FakePort } from "../port/fake-port";
import { sidBar } from "./sid-bar";

const flush = async () => {
  for (let i = 0; i < 6; ++i) await new Promise((r) => setTimeout(r, 0));
};
const $ = (el: HTMLElement, id: string) => el.querySelector<HTMLElement>(`[data-testid="${id}"]`)!;
const visible = (el: HTMLElement) => ["sid-title", "sid-prev", "sid-song", "sid-next", "sid-stop"].filter((id) => !$(el, id).hidden);

async function pick(bar: HTMLElement, name: string, bytes = new Uint8Array([1, 2, 3, 4])) {
  const input = $(bar, "sid-file") as HTMLInputElement;
  Object.defineProperty(input, "files", { configurable: true, value: [new File([bytes], name)] });
  input.dispatchEvent(new Event("change"));
  await flush();
}

function make(caps = { sid: true }) {
  const port = new FakePort(caps);
  const sound = { on: false, ons: 0 };
  const bar = sidBar(port, {
    soundOn: () => !sound.on && ((sound.on = true), ++sound.ons, true),
    soundOff: () => void (sound.on = false),
  });
  port.onFrame(bar.update);
  return { port, bar, el: bar.element, sound, playing: () => sound.ons };
}

describe("the tune", () => {
  test("hidden where the port can't play one", () => {
    expect(make({ sid: false }).el.hidden).toBe(true);
    expect(make().el.hidden).toBe(false);
  });

  test("a file chosen: Sound on, onto the card, its name, author and song; ◀ ▶ step the songs", async () => {
    const { port, el, playing } = make();
    expect(visible(el)).toEqual([]);
    await pick(el, "tone.sid", new Uint8Array(10));
    expect(playing()).toBe(1);
    expect(port.callsTo("playSid")).toEqual([[10, 0]]);
    expect($(el, "sid-title").textContent).toBe("Fake Tune — Nobody (2026)");
    expect($(el, "sid-song").textContent).toBe("song 1 of 3");
    expect(visible(el)).toEqual(["sid-title", "sid-prev", "sid-song", "sid-next", "sid-stop"]);
    expect(($(el, "sid-prev") as HTMLButtonElement).disabled).toBe(true);
    $(el, "sid-next").click();
    await flush();
    $(el, "sid-next").click();
    await flush();
    expect(port.callsTo("playSong")).toEqual([[2], [3]]);
    expect($(el, "sid-song").textContent).toBe("song 3 of 3");
    expect(($(el, "sid-next") as HTMLButtonElement).disabled).toBe(true);
    $(el, "sid-next").click(); // past the last: nothing
    await flush();
    expect(port.callsTo("playSong").length).toBe(2);
    $(el, "sid-prev").click();
    await flush();
    expect(port.callsTo("playSong").at(-1)).toEqual([2]);
  });

  test("Stop stops it; a card that stopped by itself (START, power) forgets it too", async () => {
    const { port, el } = make();
    await pick(el, "a.sid");
    port.emit({ soundRunning: true });
    expect(visible(el).length).toBe(5);
    $(el, "sid-stop").click();
    expect(port.callsTo("stopSid").length).toBe(1);
    expect(visible(el)).toEqual([]);
    await pick(el, "b.sid");
    port.emit({ soundRunning: false }); // START's /RESET
    expect(port.callsTo("stopSid").length).toBe(2);
    expect(visible(el)).toEqual([]);
    expect($(el, "sid-note").textContent).toBe("The machine stopped the card: the tune stopped");
  });

  test("what the card can't give it, and a refusal, in the note", async () => {
    const { port, el, playing } = make();
    port.tune = { ...(port.tune as object), rsid: true, warnings: ["it needs a third SID, at $D440"] } as typeof port.tune;
    await pick(el, "big.sid");
    expect($(el, "sid-note").textContent).toBe("The card can't play all of it: it needs a third SID, at $D440; it may stay silent");
    port.tune = "not a .sid file (no PSID or RSID header)";
    await pick(el, "x.txt");
    expect($(el, "sid-note").textContent).toBe("x.txt: not a .sid file (no PSID or RSID header)");
    expect(playing()).toBe(1); // Sound on for the tune that played, not for the refusal
    expect(visible(el).length).toBe(5); // the tune before plays on
  });

  test("a file given without the click turns Sound on in the change, before any await, and off again if refused", async () => {
    const { port, el, sound } = make();
    port.tune = "not a .sid file (no PSID or RSID header)";
    const input = $(el, "sid-file") as HTMLInputElement;
    Object.defineProperty(input, "files", { configurable: true, value: [new File([new Uint8Array(4)], "x.txt")] });
    input.dispatchEvent(new Event("change"));
    expect(sound.on).toBe(true); // before any await
    await flush();
    expect(sound.on).toBe(false); // refused: not left on
    sound.on = true; // on already, by the user: a refusal leaves it on
    await pick(el, "y.txt");
    expect(sound.on).toBe(true);
  });

  test("Play .sid… turns Sound on in its click, before the picker; a cancel turns it off again if it was off", async () => {
    const { el, sound, playing } = make();
    const input = $(el, "sid-file") as HTMLInputElement;
    let onAtPick = false;
    input.click = () => void (onAtPick = sound.on);
    $(el, "sid-play").click();
    expect(onAtPick).toBe(true); // the AudioContext starts inside the click, not after the file's await
    input.dispatchEvent(new Event("cancel"));
    expect(sound.on).toBe(false);
    $(el, "sid-play").click();
    await pick(el, "a.sid");
    expect(sound.on).toBe(true);
    expect(playing()).toBe(2); // the pick did not turn it on a second time
    $(el, "sid-play").click(); // on already, by the tune: a cancel leaves it on
    input.dispatchEvent(new Event("cancel"));
    expect(sound.on).toBe(true);
  });
});
