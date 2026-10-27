/**
 * The Settings drawer's content, in sections: ROMs (the BIOS, and Apple II
 * mode's ROMs), Disk II (the two drives), SD card, Sound (a .sid tune),
 * Display (the phosphor) and About (the credits). The rows are the media's
 * own (ui/bios.ts, apple-bar.ts, disk-bar.ts, sd-bar.ts, sid-bar.ts); a
 * section with nothing to show on this machine (no SD card, say) hides.
 */

import { PHOSPHORS, type Phosphor } from "../crt/crt";
import { h, prefs, segmented } from "./dom";

export type SettingsSection = "roms" | "disk" | "sd" | "sound" | "display" | "about";

/** Colour, P1, P3 or P4, remembered; `onPick` hears the kept one at once. */
export function phosphorPicker(onPick: (p: Phosphor) => void): HTMLElement {
  const saved = prefs.get("phosphor") as Phosphor | null;
  const phosphor: Phosphor = saved && saved in PHOSPHORS ? saved : "p3";
  const seg = segmented<Phosphor>(
    "Phosphor",
    [
      { value: "colour", text: "Colour", swatch: "linear-gradient(90deg,#e33,#3c3,#36f)" },
      { value: "p1", text: "P1", swatch: PHOSPHORS.p1.fg },
      { value: "p3", text: "P3", swatch: PHOSPHORS.p3.fg },
      { value: "p4", text: "P4", swatch: PHOSPHORS.p4.fg },
    ],
    phosphor,
    (p) => {
      onPick(p);
      prefs.set("phosphor", p);
    },
  );
  onPick(phosphor);
  return seg.element;
}

const link = (href: string, text: string) => h("a", { href, target: "_blank", rel: "noopener" }, text);

/** The page's credits: the fonts it carries say whose they are. */
export function credits(): HTMLElement {
  const ofl = () => link("https://openfontlicense.org/", "SIL Open Font License 1.1");
  return h(
    "div",
    { class: "credits", "data-testid": "credits" },
    // The BIOS's font is CC BY-SA 4.0 (backplane/bios/font): the page carries it, so it says whose it is.
    h(
      "p",
      {},
      "Screen font: IBM VGA 8×16 from ",
      link("https://int10h.org/oldschool-pc-fonts/", "VileR's Ultimate Oldschool PC Font Pack"),
      ", ",
      link("https://creativecommons.org/licenses/by-sa/4.0/", "CC BY-SA 4.0"),
      ". Page fonts: ",
      link("https://rsms.me/inter/", "Inter"),
      " (The Inter Project Authors), ",
      link("https://www.jetbrains.com/lp/mono/", "JetBrains Mono"),
      " (The JetBrains Mono Project Authors) and ",
      link("https://fonts.google.com/specimen/VT323", "VT323"),
      " (The VT323 Project Authors), under the ",
      ofl(),
      ", bundled with the page.",
    ),
    h("p", {}, "Apple II mode runs your own Apple IIe ROM and disks: they stay in this browser and are never uploaded. So does a BIOS of your own; an SD card image stays in this page."),
  );
}

export interface SettingsParts {
  roms: HTMLElement[];
  drives: HTMLElement[];
  sd: HTMLElement;
  sound: HTMLElement;
  phosphor: HTMLElement;
}

export function settingsPanel(parts: SettingsParts) {
  const heads = {} as Record<SettingsSection, HTMLElement>;
  const section = (id: SettingsSection, title: string, ...body: HTMLElement[]) => {
    heads[id] = h("h3", { class: "lbl", id: `set-${id}`, tabindex: "-1" }, title);
    return h("section", { class: `set-sec set-${id}`, "aria-labelledby": `set-${id}`, "data-testid": `settings-${id}` }, heads[id], ...body);
  };
  const header = h("div", { class: "drawer-h" }, h("h2", {}, "Settings"));
  const element = h(
    "aside",
    { class: "settings", "aria-label": "Settings", "data-testid": "settings" },
    header,
    h(
      "div",
      { class: "settings-b" },
      section("roms", "ROMs", ...parts.roms),
      section("disk", "Disk II", ...parts.drives),
      section("sd", "SD card", parts.sd),
      section("sound", "Sound", parts.sound),
      section("display", "Display", h("div", { class: "row plain" }, h("span", { class: "name" }, "Phosphor"), parts.phosphor)),
      section("about", "About", credits()),
    ),
  );
  /** The heading to show and focus for a section. */
  const at = (s: SettingsSection) => heads[s];
  return { element, header, at };
}
