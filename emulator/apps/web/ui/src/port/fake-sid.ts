/**
 * A .sid file of our own, for tests: no one's tune, a few bytes of 6502.
 * Init (at $1000, the song less one in A) starts voice 1 on a triangle at
 * full envelope, its pitch from a table by song ($1000, $1400, ...: about
 * 245, 306, 368 Hz on PAL), and turns the volume up; play does nothing.
 */

export interface FakeSidOptions {
  songs?: number;
  name?: string;
  author?: string;
  released?: string;
  rsid?: boolean;
  /** The header's flags: 0x14 PAL and 6581 by default, 0x24 for an 8580. */
  flags?: number;
  /** A third SID's address byte (0x44: $D440), which makes it a version 4 file. */
  sid3?: number;
}

export function fakeSid(o: FakeSidOptions = {}): Uint8Array {
  const songs = o.songs ?? 3;
  const header = new Uint8Array(0x7c);
  const put16 = (at: number, v: number) => {
    header[at] = (v >> 8) & 0xff;
    header[at + 1] = v & 0xff;
  };
  const text = (at: number, s: string) => [...s.slice(0, 31)].forEach((c, i) => (header[at + i] = c.charCodeAt(0) & 0xff));
  text(0, o.rsid ? "RSID" : "PSID");
  const table = 0x1022;
  const code = [
    0xaa, // TAX
    0xbd, table & 0xff, table >> 8, // LDA table,X
    0x8d, 0x01, 0xd4, // STA $D401: the pitch
    0xa9, 0x00, 0x8d, 0x00, 0xd4,
    0xa9, 0x00, 0x8d, 0x05, 0xd4, // attack 0, decay 0
    0xa9, 0xf0, 0x8d, 0x06, 0xd4, // sustain F
    0xa9, 0x11, 0x8d, 0x04, 0xd4, // triangle, gate
    0xa9, 0x0f, 0x8d, 0x18, 0xd4, // volume 15
    0x60, // RTS
    0x60, // $1021, play: RTS
    ...Array.from({ length: songs }, (_, i) => 0x10 + 4 * i), // $1022: the pitches
  ];
  put16(0x04, o.sid3 ? 4 : 2);
  header[0x7b] = o.sid3 ?? 0;
  put16(0x06, 0x7c);
  put16(0x08, 0x1000);
  put16(0x0a, 0x1000);
  put16(0x0c, o.rsid ? 0 : 0x1021);
  put16(0x0e, songs);
  put16(0x10, 1);
  text(0x16, o.name ?? "Test Tone");
  text(0x36, o.author ?? "OO-1 tests");
  text(0x56, o.released ?? "2026");
  put16(0x76, o.flags ?? 0x14);
  const out = new Uint8Array(header.length + code.length);
  out.set(header);
  out.set(code, header.length);
  return out;
}
