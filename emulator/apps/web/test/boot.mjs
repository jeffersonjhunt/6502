// The WebAssembly build, headless under node, as the browser will use it:
// boot the embedded BIOS to the Woz Monitor in one-frame budgets, talk to
// it, use the panel and the trace, and time it.
//
//   node boot.mjs <path to oo1.mjs>
//
// Every check must see something: a missing module, ROM or output is a
// failure (exit 1), never a skip.

import { pathToFileURL } from "node:url";

const FRAME = 119318; // 7.15909 MHz / 60
const GREETING = "\rBACKPLANE BIOS 0.9\r\\\r";
const failures = [];
const check = (name, ok, detail = "") => {
  if (!ok) failures.push(detail ? `${name}: ${detail}` : name);
};
const show = (s) => JSON.stringify(s.slice(-200));
const hex2 = (n) => n.toString(16).toUpperCase().padStart(2, "0");

if (process.argv.length !== 3) {
  console.error("usage: node boot.mjs <oo1.mjs>");
  process.exit(2);
}
const { default: createOO1 } = await import(pathToFileURL(process.argv[2]).href);
const oo1 = await createOO1();
// The BIOS's labels, embedded beside it (ld65's "al 00C9BA .ECHO"): its code moves.
const labels = new Map(
  oo1.FS.readFile("/roms/bios.rom.lbl", { encoding: "utf8" })
    .split("\n")
    .map((l) => /^al ([0-9A-F]+) \.(\w+)$/i.exec(l.trim()))
    .filter(Boolean)
    .map((r) => [r[2], parseInt(r[1], 16)]),
);
const WOZMON = labels.get("WOZMON");
check("labels: WOZMON", WOZMON !== undefined);

// Opening what is not there says so.
const bad = new oo1.Machine();
check("open: a missing machine is refused", !bad.open("/machines/nope.machine", "", ""));
check("open: the error names it", bad.error().includes("nope.machine"), bad.error());
bad.delete();

const m = new oo1.Machine();
if (!m.open("/machines/backplane.machine", "/roms/bios.rom", "/roms/bios.rom.lbl")) {
  console.log(`NOT OK: cannot open the embedded machine: ${m.error()}`);
  process.exit(1);
}
check("the backplane has its Console card", m.hasCard());

function runUntil(want, frames = 60) {
  let out = "";
  for (let i = 0; i < frames; ++i) {
    m.run(FRAME);
    out += m.takeOutput();
    if (out.includes(want)) return out;
  }
  check(`never saw ${JSON.stringify(want)}`, false, show(out));
  return out;
}

// Boot, one frame at a time.
const first = m.run(FRAME);
check("run: a frame's budget", first === 0 && m.lastCycles() >= FRAME && m.lastCycles() < FRAME + 16,
      `why=${first} cycles=${m.lastCycles()}`);
const boot = m.takeOutput() + runUntil(GREETING);
check("boot: the banner and prompt", boot === GREETING, show(boot));

// The monitor reads the north bridge's registers back.
m.send("C680.C684\r");
check("serial round trip", runUntil("C680: 00 01 02 7F 7F").includes("C680: 00 01 02 7F 7F"));

// The video card: the BIOS's screen, as frames the page can draw.
check("video: the backplane has its card", m.hasVideo());
const pixels = m.videoFrame();
check("video: a 640x480 RGBA frame", pixels instanceof Uint8Array && pixels.length === 640 * 480 * 4, String(pixels.length));
const at = (x, y) => Array.from(pixels.subarray((y * 640 + x) * 4, (y * 640 + x) * 4 + 4)).join(",");
check("video: the B of BACKPLANE, lit in light grey", at(0, 18) === "170,170,170,255", at(0, 18)); // row 1, glyph row 2: 11111100
check("video: and dark beside it", at(6, 18) === "0,0,0,255", at(6, 18));
const before = m.videoFrames();
m.run(FRAME * 60);
const second = (n) => n === 59 || n === 60; // 59.94 Hz: where the beam starts decides
check("video: 60 frames a second at 7.16 MHz", second(m.videoFrames() - before), String(m.videoFrames() - before));
m.setClock(1_020_484);
const slow = m.videoFrames();
m.run(1_020_484);
check("video: still 60 a second at 1.02 MHz", second(m.videoFrames() - slow), String(m.videoFrames() - slow));
m.setClock(0);

// Bytes, not UTF-8: Woz echoes what it receives, so $E9 must go out and
// come back as one byte (UTF-8 would send C3 A9). ESC drops the line.
m.send("\u00e9\u001b");
const echoed = runUntil("\u001b");
check("bytes are Latin-1: one byte stored", m.peek(0x200) === 0xe9, m.peek(0x200).toString(16)); // Woz's input buffer
check("bytes are Latin-1: one byte back", echoed.includes("\u00e9\u001b"), show(echoed));

m.send("300: E9\r300\r");
runUntil("0300: E9");
check("peek sees the store", m.peek(0x300) === 0xe9, String(m.peek(0x300)));

// The panel: HALT, one step with CONT, then run on.
m.setPanel(true, false);
check("halt: run stops at once", m.run(FRAME) === 1 && m.lastCycles() === 0 && m.halted());
const pc = m.regs().pc;
m.press(8); // CONT
check("cont: one instruction", m.run(FRAME) === 1 && m.lastCycles() >= 2 && m.lastCycles() <= 7,
      `cycles=${m.lastCycles()}`);
check("cont: the PC moved", m.regs().pc !== pc);
m.setPanel(false, false);
m.press(8);
check("enable + cont: runs on", m.run(FRAME) === 0 && !m.halted());

// A breakpoint on a label, through the card's SPI registers.
const echo = m.resolve("ECHO");
check("resolve: ECHO is a label", echo > 0, String(echo));
check("resolve: nonsense is -1", m.resolve("NOWHERE") === -1);
const phys = m.physical(echo);
[0, 1, 2].forEach((i) => m.cardWrite(0x08 + i, (phys >> (8 * i)) & 0xff));
m.cardWrite(0x0b, 0x03); // enable, fetch
m.send("A");
let why = 0;
for (let i = 0; i < 10 && why !== 1; ++i) why = m.run(FRAME);
check("breakpoint: halted at ECHO", why === 1 && m.regs().pc === echo, `why=${why} pc=${m.regs().pc}`);
check("breakpoint: STATUS halted + hit", (m.cardRead(0x0c) & 0x03) === 0x03, String(m.cardRead(0x0c))); // bit 2: CONT unread
m.cardWrite(0x0b, 0x00);
m.press(8);

// The IRQ line: quiet in the monitor, raised by a panel key with IEN set.
check("irq: quiet", !m.irq());
m.cardWrite(0x18, 0x01); // IEN
m.press(1); // LOAD ADRS
check("irq: a key press raises it", m.irq());
m.cardWrite(0x19, 0x01); // ISR: write 1 to clear
check("irq: cleared", !m.irq());
m.cardWrite(0x18, 0x00);

// Lamps and the trace ring, as typed arrays.
m.setLights(true);
m.run(FRAME);
const lights = m.lights();
check("lights: 32 floats", lights instanceof Float32Array && lights.length === 32);
check("lights: A19 glows, not solid", lights[19] > 0.5 && lights[19] < 1.0, String(lights[19]));
m.setTrace(true, 256);
m.run(2000);
const rows = m.trace(100);
check("trace: 100 rows of 4 words", rows instanceof Uint32Array && rows.length === 400, String(rows.length));
const newest = rows[4 * 99] + rows[4 * 99 + 1] * 2 ** 32;
check("trace: the newest is now", newest <= m.cycles() && newest > m.cycles() - 16, `${newest} vs ${m.cycles()}`);

// Speed: the native clock is 7.16 MHz, one frame's budget 1/60 s of it.
const t0 = performance.now();
let ran = 0;
for (let i = 0; i < 60; ++i) {
  m.run(FRAME);
  ran += m.lastCycles();
}
const secs = (performance.now() - t0) / 1000;
const mhz = ran / secs / 1e6;
console.log(`speed: ${mhz.toFixed(1)} MHz emulated; real time at 7.16 MHz takes ${(100 * secs).toFixed(0)}% of a core`);
check("speed: at least real time", mhz > 7.16, `${mhz.toFixed(2)} MHz`);

// The USB keyboard: HID usages in, the monitor answers on the serial line.
check("usb: the backplane has its card", m.hasUsb());
const USAGE = { C: 0x06, 6: 0x23, 8: 0x25, 0: 0x27, 4: 0x21, ".": 0x37, "\r": 0x28 };
let typed = "";
for (const ch of "C680.C684\r") {
  m.usbKey(USAGE[ch], true);
  m.run(FRAME / 4);
  m.usbKey(USAGE[ch], false);
  m.run(FRAME / 4);
  typed += m.takeOutput();
}
for (let i = 0; i < 30 && !typed.includes("C680: 00 01 02 7F 7F"); ++i) { // the rest of the answer
  m.run(FRAME);
  typed += m.takeOutput();
}
check("usb: typed into the monitor", typed.includes("C680.C684\r\rC680: 00 01 02 7F 7F"), show(typed));

// Power cycle: RAM forgets, and it boots again.
m.setClock(1_020_484);
check("power cycle", m.powerCycle(), m.error());
m.run(1_020_484);
check("power cycle: the video card keeps the clock", second(m.videoFrames()), String(m.videoFrames())); // at 7.16 MHz time it would be 8 or 9
m.setClock(0);
check("power cycle: RAM cleared", m.peek(0x300) === 0);
check("power cycle: boots again", runUntil(GREETING).includes(GREETING));
m.delete();

// The real-time clock: set from the host's wall clock, it counts in the CPU's time and
// keeps it through a power cycle (the DS3231's battery).
const clock = new oo1.Machine();
check("rtc: open", clock.open("/machines/backplane.machine", "/roms/bios.rom", "/roms/bios.rom.lbl"), clock.error());
const wall = Date.UTC(2026, 9, 26, 13, 45, 7, 950); // the local clock in ms, as the page passes it
clock.setRtc(wall);
const rtc = () => Array.from(clock.rtc());
check("rtc: 35 bytes", rtc().length === 35, String(rtc().length));
check("rtc: SEC-YEAR", rtc().slice(0, 7).join() === [0x07, 0x45, 0x13, 0x02, 0x26, 0x10, 0x26].join(), rtc().slice(0, 7).join());
check("rtc: the DS3231's temperature", rtc()[16 + 0x11] === 0x19);
check("rtc: RTCCTL clear", rtc()[7] === 0, String(rtc()[7]));
for (let i = 0; i < 6; ++i) clock.run(FRAME); // 100 ms at 7.16 MHz, and the clock was 950 ms into its second
check("rtc: the next second", rtc()[0] === 0x08, String(rtc()[0]));
check("rtc: TICK", (rtc()[7] & 0x80) !== 0, String(rtc()[7]));
check("rtc: power cycle", clock.powerCycle(), clock.error());
check("rtc: kept through it", rtc()[0] === 0x08 && rtc()[6] === 0x26, rtc().slice(0, 7).join());
check("rtc: $C690 reads it", clock.peekIo(0x690) === 0x08, String(clock.peekIo(0x690)));
clock.delete();

// The SD card's save: a copy that fails leaves the writes unsaved, so the page still warns.
const sd = new oo1.Machine();
check("sd: open", sd.open("/machines/backplane.machine", "/roms/bios.rom", "/roms/bios.rom.lbl"), sd.error());
check("sd: a card goes in", sd.insertSd(new Uint8Array(64 * 512), "card.img"), sd.error());
let sdOut = ""; // booted first: keys sent before the monitor runs are lost
for (let i = 0; i < 60 && !sdOut.includes(GREETING); ++i) {
  sd.run(FRAME);
  sdOut += sd.takeOutput();
}
check("sd: boots", sdOut.includes(GREETING), show(sdOut));
// WRITE sector 0, $AA throughout, by a CPU loop typed at the monitor (as e2e/sd.spec.ts does),
// then JMP back to the monitor.
sd.send("1000: A9 01 8D 8A C3 9C 8B C3 9C 84 C3 9C 86 C3 A9 30 8D 83 C3\r");
sd.send(`1013: A0 00 A9 AA 8D 8C C3 C8 D0 FA 8D 8C C3 C8 D0 FA 4C ${hex2(WOZMON & 0xff)} ${hex2(WOZMON >> 8)}\r`);
sd.send("1000R\r");
sdOut = "";
for (let i = 0; i < 120 && !(sd.sdInfo() & 1 && sdOut.endsWith("\\\r")); ++i) {
  sd.run(FRAME);
  sdOut += sd.takeOutput();
}
check("sd: written to", (sd.sdInfo() & 1) === 1, String(sd.sdInfo()));
check("sd: back at the monitor, not started again", sdOut.includes("1000R") && sdOut.endsWith("\\\r") && !sdOut.includes(GREETING), show(sdOut));
const slice = Uint8Array.prototype.slice;
Uint8Array.prototype.slice = () => { throw new RangeError("out of memory"); };
let threw = false;
try { sd.sdSave(); } catch { threw = true; }
Uint8Array.prototype.slice = slice;
check("sd: the failed copy threw", threw);
check("sd: and the writes still count as unsaved", (sd.sdInfo() & 1) === 1, String(sd.sdInfo()));
const saved = sd.sdSave();
check("sd: then a save copies them", saved && saved.length === 64 * 512 && saved[0] === 0xaa && saved[512] === 0);
check("sd: and counts as saved", (sd.sdInfo() & 1) === 0, String(sd.sdInfo()));
sd.delete();

for (const f of failures) console.log(`FAIL ${f}`);
console.log(`${failures.length ? "NOT OK" : "ok"}: ${failures.length} failure(s)`);
process.exit(failures.length ? 1 : 0);
