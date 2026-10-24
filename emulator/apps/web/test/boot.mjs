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
const GREETING = "\rBACKPLANE BIOS 0.3\r\\\r";
const failures = [];
const check = (name, ok, detail = "") => {
  if (!ok) failures.push(detail ? `${name}: ${detail}` : name);
};
const show = (s) => JSON.stringify(s.slice(-200));

if (process.argv.length !== 3) {
  console.error("usage: node boot.mjs <oo1.mjs>");
  process.exit(2);
}
const { default: createOO1 } = await import(pathToFileURL(process.argv[2]).href);
const oo1 = await createOO1();

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

// Power cycle: RAM forgets, and it boots again.
m.setClock(1_020_484);
check("power cycle", m.powerCycle(), m.error());
m.run(1_020_484);
check("power cycle: the video card keeps the clock", second(m.videoFrames()), String(m.videoFrames())); // at 7.16 MHz time it would be 8 or 9
m.setClock(0);
check("power cycle: RAM cleared", m.peek(0x300) === 0);
check("power cycle: boots again", runUntil(GREETING).includes(GREETING));
m.delete();

for (const f of failures) console.log(`FAIL ${f}`);
console.log(`${failures.length ? "NOT OK" : "ok"}: ${failures.length} failure(s)`);
process.exit(failures.length ? 1 : 0);
