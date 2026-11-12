"""Makes OOUI.BNK, OOUI's banked library (hw/ooui.md, "U3a: OOUI as a banked library").

    python3 -I mkbank.py --cfg cfg/ooui-bnk.cfg --iface ui/uiimp.inc --work build/bnk -o build/OOUI.BNK \\
        --src ui/*.s -- build/bnk-*.o
    python3 -I mkbank.py --show build/OOUI.BNK

The bank's objects are linked with cfg/ooui-bnk.cfg, in ld65's plain binary format (not o65, whose high-byte
relocations ld65 2.19 writes without their low byte), three times:

  L0  UISTATE (VARS) at $1000, OOUI's zero page at $10, import i at $4000 + i x $400, blocks 1, 2, 3
  L1  each kind moved by its own step: UISTATE +$0100, the zero page +5, an import +$0302, a block +$40
  L2  a check, at values unlike both: UISTATE at $8700, the zero page at $2A, the imports at irregular
      addresses, blocks 9, 13, 17

Every byte that differs between L0 and L1 must be one fixup, found in this order, or the build fails:
a word (two bytes, a 16-bit step of exactly $0302, L0's value in a real import's slot: the import and its
addend); a page (a byte one up: UISTATE's high byte); a zero page byte (five up); a block number (a byte
$40 up, 1-3 in L0). A lone byte moves by 1, 5 or $40, never a word's +2 or +3/+4, and two lone fixups side
by side make a 16-bit step whose high byte moves by 1, 5 or $40, never $0302's: so a lone #< or #> of an
import fits no kind and is refused. Then the table, applied to L0 with L2's values, must give L2 byte for
byte: the proof that it is complete.

Before that, L0's debug file (--dbgfile) is read for every reference: one from a block's segment to a
symbol of another block's, or from UICOM's to any block's, fails the build (the dispatchers' entries, the
only crossing points, excepted); so does a code segment that falls through into another (--src: the last
instruction before a segment changes must be RTS, RTI, JMP or BRA). The blocks have no zero page but
OOUI's own (zpuicore), each block's code and data fit $C800-$F7FF with UICOM, and UICOM's size is
measured by a first link and then fixed.

The file (format 1): record 0 a header, then each block's image (UICOM's bytes, then its own) from a
record, then each block's fixups (UICOM's and its own) from a record.
  +0 'OUB', +3 the format (1), +4 the interface version, +5 the blocks (3), +6-7 UISTATE's size, +8 the
  imports (the table's count, FARCALL the first), +9 the calls; +10 for each block 8 bytes: its image's
  first record and length, its fixups' first record and entries (the skips counted); +34 for each call its block (0 A, 1 B,
  2 C) and address.
A fixup is two bytes: the offset from the last one's (from the image's start, $C800, for the first),
then its kind: $00 a page (the byte + UISTATE's page - $10), $01 zero page (+ the zero page - $10),
$02 a block's number (its byte 1-3 names the block), $80 + i the word of import i (the word - $4000 -
i x $400 + the import's value), $FF nothing (a step of 255 more).

--show prints a file's header and fixups' counts. --lbl writes the labels of L0 as "area name address"
(area U, A, B, C, V the variables, Z the zero page), for the tests.
"""

import argparse
import importlib.util
import os
import re
import subprocess
import sys

RECORD = 128
MAGIC = b"OUB"
FORMAT = 1
CODE0, CODE_END = 0xC800, 0xF800
STEP = {"page": 0x0100, "zp": 5, "word": 0x0302, "block": 0x40}
L0 = {"uis": 0x1000, "uiz": 0x10, "blocks": (1, 2, 3)}
L1 = {"uis": 0x1100, "uiz": 0x15, "blocks": (0x41, 0x42, 0x43)}
L2 = {"uis": 0x8700, "uiz": 0x2A, "blocks": (9, 13, 17)}
SLOT = 0x400
BLOCKS = "ABC"
AREA_OF = {
    "ZEROPAGE": "Z", "UIHEAD": "V", "BSS": "V", "UCODE": "U", "URODATA": "U",
    "AENTRY": "A", "CODE": "A", "RODATA": "A", "BENTRY": "B", "BCODE": "B", "BRODATA": "B", "BBSS": "B",
    "CENTRY": "C", "CCODE": "C", "CRODATA": "C", "CBSS": "C",
}
CODESEGS = {"UCODE", "AENTRY", "CODE", "BENTRY", "BCODE", "CENTRY", "CCODE"}
ENTRIES_OK = {"AENT", "BENT", "CENT"}  # the dispatchers: the only symbols another block may name
KIND_PAGE, KIND_ZP, KIND_BLOCK, KIND_SKIP = 0x00, 0x01, 0x02, 0xFF
BLOCKCODE = {"A": ("UCODE", "AENTRY", "CODE"), "B": ("UCODE", "BENTRY", "BCODE"), "C": ("UCODE", "CENTRY", "CCODE")}

sys.dont_write_bytecode = True  # no __pycache__ in tools/
_spec = importlib.util.spec_from_file_location("rules", os.path.join(os.path.dirname(os.path.abspath(__file__)), "rules.py"))
rules = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rules)


class Fail(Exception):
    pass


def interface(path):
    text = open(path).read()
    return re.findall(r"^\s*UIENT\s+(\w+)", text, re.M), re.findall(r"^\s*UIIMP\s+(\w+)", text, re.M)


def l0_import(i):
    return 0x4000 + i * SLOT


def l2_import(i):
    """Irregular addresses in $0300-$BFFC, no two a page or a common step apart."""
    return 0x0400 + (i * 0x1F3D + 0x02B7 + i * i * 0x61) % 0xB400


def values(v, imports):
    if v is L0:
        return {n: l0_import(i) for i, n in enumerate(imports)}
    if v is L1:
        return {n: l0_import(i) + STEP["word"] for i, n in enumerate(imports)}
    return {n: l2_import(i) for i, n in enumerate(imports)}


def link(cfg, objs, imports, v, ucomsz, work, name, debug=False, sizing=False):
    d = os.path.join(work, name)
    os.makedirs(d, exist_ok=True)
    blksz = 0x10000 - CODE0 - ucomsz if sizing else CODE_END - CODE0 - ucomsz
    defs = ["-D", "__UIS__=$%04X" % v["uis"], "-D", "__UIZ__=$%02X" % v["uiz"], "-D", "__UCOMSZ__=$%04X" % ucomsz,
            "-D", "__BLKSZ__=$%04X" % blksz]
    for n, val in values(v, imports).items():
        defs += ["-D", "%s=$%04X" % (n, val)]
    for b, val in zip(BLOCKS, v["blocks"]):
        defs += ["-D", "BLK_%s=$%02X" % (b, val)]
    cmd = ["ld65", "-C", os.path.abspath(cfg), "-o", "image.out", "-m", "map", "-Ln", "lbl"] + defs
    if debug:
        cmd += ["--dbgfile", "dbg"]
    cmd += [os.path.abspath(o) for o in objs]
    p = subprocess.run(cmd, cwd=d, capture_output=True, text=True)
    if p.returncode or p.stderr.strip():
        raise Fail("ld65 (%s): %s" % (name, (p.stderr or p.stdout).strip()))
    out = {}
    for a, f in (("U", "ucom.bin"), ("A", "a.bin"), ("B", "b.bin"), ("C", "c.bin")):
        path = os.path.join(d, f)
        out[a] = open(path, "rb").read() if os.path.exists(path) else b""
    return out, d


def segments(mapfile):
    text = open(mapfile).read()
    m = re.search(r'^Segment list:\n-+\n.*?\n-+\n(.*?)(?:\n\n|\Z)', text, re.M | re.S)
    return {n: (int(a, 16), int(b, 16), int(z, 16))
            for n, a, b, z in re.findall(r'^(\S+)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})', m.group(1), re.M)}


def parse_dbg(path):
    recs = {}
    for line in open(path):
        kind, _, rest = line.rstrip("\n").partition("\t")
        f = {}
        for part in re.findall(r'(\w+)=("(?:[^"]*)"|[^,]*)', rest):
            f[part[0]] = part[1].strip('"')
        recs.setdefault(kind, []).append(f)
    return recs


def crossrefs(dbgfile):
    """Each reference from one block's segment to another's symbol: [(what, where)]."""
    d = parse_dbg(dbgfile)
    seg = {int(s["id"]): s["name"] for s in d.get("seg", [])}
    span = {int(s["id"]): seg[int(s["seg"])] for s in d.get("span", []) if "seg" in s}
    files = {int(f["id"]): f["name"] for f in d.get("file", [])}
    lines = {}
    for l in d.get("line", []):
        spans = [span[int(x)] for x in l.get("span", "").split("+") if x and int(x) in span]
        lines[int(l["id"])] = (files.get(int(l["file"]), "?"), l["line"], spans)
    syms = {int(s["id"]): s for s in d.get("sym", [])}
    bad = []
    for s in syms.values():
        target = s
        if s.get("type") == "imp":
            if "exp" not in s:
                continue
            target = syms[int(s["exp"])]
        if "seg" not in target:
            continue  # an equate, or a -D value
        tarea = AREA_OF.get(seg[int(target["seg"])])
        if tarea not in ("U", "A", "B", "C") or target["name"] in ENTRIES_OK:
            continue
        for r in s.get("ref", "").split("+"):
            if not r:
                continue
            f, ln, spans = lines[int(r)]
            for sp in spans:
                rarea = AREA_OF.get(sp)
                if rarea in ("A", "B", "C") and tarea in ("A", "B", "C") and rarea != tarea or \
                        rarea == "U" and tarea != "U":
                    bad.append("%s:%s: %s's %s names %s's %s" % (os.path.basename(f), ln, rarea, sp, tarea, target["name"]))
    return sorted(set(bad))


TERMINAL = re.compile(r"^\s*(?:[A-Za-z_@][\w]*:)?\s*(rts|rti|jmp|bra)\b", re.I)
SEGDIR = re.compile(r"^\s*(?:\.segment\s+\"(\w+)\"|\.(code|rodata|bss|data|zeropage)\b)", re.I)
INSTR = re.compile(r"^\s*(?:[A-Za-z_@][\w]*:)?\s*([a-z]{3})\b(?!\s*=)", re.I)


def fallthroughs(paths):
    """A code segment left by a segment directive after an instruction that falls through."""
    bad = []
    for p in paths:
        seg, last, lastline = "CODE", None, 0
        for n, raw in enumerate(open(p), 1):
            code = raw.split(";")[0].rstrip()
            if not code.strip():
                continue
            m = SEGDIR.match(code)
            if m:
                new = (m.group(1) or {"code": "CODE", "rodata": "RODATA", "bss": "BSS", "data": "DATA",
                                      "zeropage": "ZEROPAGE"}[m.group(2).lower()]).upper()
                if new != seg and seg in CODESEGS and last is not None and not TERMINAL.match(last):
                    bad.append("%s:%d: %s ends in '%s', which falls through" % (os.path.basename(p), lastline, seg,
                                                                                last.strip()))
                seg, last = new, None
                continue
            if code.lstrip().startswith(".") or re.match(r"^\s*\w+\s*(=|:=)", code):
                continue
            if INSTR.match(code):
                last, lastline = code, n
    return bad


def classify(l0, l1, imports, where):
    """The fixups that make L1 of L0: [(offset, kind)]; Fail on any byte that fits no kind."""
    if len(l0) != len(l1):
        raise Fail("%s: L0 and L1 differ in size (%d, %d)" % (where, len(l0), len(l1)))
    fix, i, n = [], 0, len(l0)
    while i < n:
        if l0[i] == l1[i]:
            i += 1
            continue
        if i + 1 < n:
            w0 = l0[i] | l0[i + 1] << 8
            w1 = l1[i] | l1[i + 1] << 8
            if (w1 - w0) & 0xFFFF == STEP["word"]:
                k = (w0 - 0x4000) // SLOT
                if w0 >= 0x4000 and 0 <= k < len(imports):
                    fix.append((i, 0x80 + k))
                    i += 2
                    continue
                raise Fail("%s+$%04X: a word $%04X that moves as an import's but names none" % (where, i, w0))
        d = (l1[i] - l0[i]) & 0xFF
        if d == 1:
            fix.append((i, KIND_PAGE))
        elif d == 5:
            fix.append((i, KIND_ZP))
        elif d == 0x40 and l0[i] in L0["blocks"]:
            fix.append((i, KIND_BLOCK))
        else:
            raise Fail("%s+$%04X: a byte that moves by $%02X ($%02X to $%02X), which no kind of fixup does: a #< or "
                       "#> of an import, or an addend past $3FF?" % (where, i, d, l0[i], l1[i]))
        i += 1
    return fix


def apply(image, fix, uis, uiz, blocks, imp):
    out = bytearray(image)
    for off, kind in fix:
        if kind == KIND_PAGE:
            out[off] = (out[off] + (uis >> 8) - (L0["uis"] >> 8)) & 0xFF
        elif kind == KIND_ZP:
            out[off] = (out[off] + uiz - L0["uiz"]) & 0xFF
        elif kind == KIND_BLOCK:
            out[off] = blocks[out[off] - 1]
        else:
            k = kind - 0x80
            w = out[off] | out[off + 1] << 8
            w = (w - l0_import(k) + imp[k]) & 0xFFFF
            out[off], out[off + 1] = w & 0xFF, w >> 8
    return bytes(out)


def encode(fix):
    out, last = bytearray(), 0
    for off, kind in fix:
        d = off - last
        while d > 255:
            out += bytes((255, KIND_SKIP))
            d -= 255
        out += bytes((d, kind))
        last = off
    return bytes(out)


def pad(b):
    return b + bytes(-len(b) % RECORD)


def build(a):
    entries, imports = interface(a.iface)
    if not entries or not imports or imports[0] != "FARCALL":
        raise Fail("%s: no UIENT or UIIMP lines, or FARCALL isn't the first import" % a.iface)
    bad = fallthroughs(a.src)
    if bad:
        raise Fail("a code segment falls through into another:\n  " + "\n  ".join(bad))
    # UICOM's size: a first link with room, then fixed at what it used.
    first, d = link(a.cfg, a.objs, imports, L0, 0x1000, a.work, "L0size", sizing=True)
    ucomsz = len(first["U"])
    imgs = {}
    for name, v in (("L0", L0), ("L1", L1), ("L2", L2)):
        imgs[name], dirs = link(a.cfg, a.objs, imports, v, ucomsz, a.work, name, debug=name == "L0")
        if name == "L0":
            d0 = dirs
    segs = segments(os.path.join(d0, "map"))
    bad = crossrefs(os.path.join(d0, "dbg"))
    if bad:
        raise Fail("a reference across blocks (a crossing goes through a dispatcher):\n  " + "\n  ".join(bad))
    zp = segs.get("ZEROPAGE", (0, 0, 0))[2]
    if zp != 8:
        raise Fail("the bank's zero page is %d bytes: only OOUI's own 8 (zpuicore)" % zp)
    uisize = sum(segs.get(s, (0, 0, 0))[2] for s in ("UIHEAD", "BSS"))
    blocks = []
    for b in BLOCKS:
        bss = segs.get(b + "BSS", (0, 0, 0))[2]
        size = ucomsz + len(imgs["L0"][b]) + bss
        if size > CODE_END - CODE0:
            raise Fail("block %s: %d bytes with UICOM, past the code window's %d" % (b, size, CODE_END - CODE0))
        l0 = imgs["L0"]["U"] + imgs["L0"][b]
        l1 = imgs["L1"]["U"] + imgs["L1"][b]
        l2 = imgs["L2"]["U"] + imgs["L2"][b]
        fix = classify(l0, l1, imports, "block " + b)
        proof = apply(l0, fix, L2["uis"], L2["uiz"], L2["blocks"], [l2_import(i) for i in range(len(imports))])
        if proof != l2:
            at = next(i for i in range(len(l2)) if proof[i] != l2[i])
            raise Fail("block %s: the fixups applied to L0 aren't L2 at +$%04X ($%02X, not $%02X)" %
                       (b, at, proof[at], l2[at]))
        blocks.append((l0, fix, size, l2))
    # the calls' blocks and addresses, from L0's debug file
    dbg = parse_dbg(os.path.join(d0, "dbg"))
    seg = {int(s["id"]): s["name"] for s in dbg.get("seg", [])}
    where = {}
    for s in dbg.get("sym", []):
        if s.get("type") == "lab" and "seg" in s and s["name"] in entries:
            where[s["name"]] = (AREA_OF[seg[int(s["seg"])]], int(s["val"], 16))
    missing = [e for e in entries if e not in where or where[e][0] not in BLOCKS]
    if missing:
        raise Fail("calls not found in a block: %s" % ", ".join(missing))
    head = bytearray(RECORD)
    head[0:3] = MAGIC
    head[3], head[4], head[5] = FORMAT, a.version, len(BLOCKS)
    head[6:8] = uisize.to_bytes(2, "little")
    head[8], head[9] = len(imports), len(entries)
    body, rec = b"", 1
    images = [pad(l0) for l0, _, _, _ in blocks]
    encoded = [encode(fix) for _, fix, _, _ in blocks]
    fixes = [pad(e) for e in encoded]
    for i, img in enumerate(images):
        head[10 + 8 * i:12 + 8 * i] = rec.to_bytes(2, "little")
        head[12 + 8 * i:14 + 8 * i] = len(blocks[i][0]).to_bytes(2, "little")
        rec += len(img) // RECORD
    for i, fx in enumerate(fixes):
        head[14 + 8 * i:16 + 8 * i] = rec.to_bytes(2, "little")
        head[16 + 8 * i:18 + 8 * i] = (len(encoded[i]) // 2).to_bytes(2, "little")  # the entries, skips too
        rec += len(fx) // RECORD
    for i, e in enumerate(entries):
        area, addr = where[e]
        head[34 + 3 * i] = BLOCKS.index(area)
        head[35 + 3 * i:37 + 3 * i] = addr.to_bytes(2, "little")
    if 34 + 3 * len(entries) > RECORD:
        raise Fail("the header is past a record")
    open(a.out, "wb").write(bytes(head) + b"".join(images) + b"".join(fixes))
    if a.lbl:
        with open(a.lbl, "w") as f:
            for s in dbg.get("sym", []):
                if s.get("type") == "lab" and "seg" in s and s.get("parent") is None:
                    f.write("%s %s %s\n" % (AREA_OF.get(seg[int(s["seg"])], "?"), s["name"], s["val"][2:].upper()))
    # The 65C816 rules on L2's images, which are relocated (UISTATE at $8700, the zero page at $2A, the
    # imports where L2 has them): rule 4 lets a JSR or JMP go to an import, and FARCALL's three bytes
    # are passed over (hw/ooui.md, U3a, round 1's D7).
    l2segs = segments(os.path.join(a.work, "L2", "map"))
    l2segs = {n: v for n, v in l2segs.items()}
    imps = tuple(l2_import(i) for i in range(len(imports)))
    problems = []
    for b, (_, _, _, l2) in zip(BLOCKS, blocks):
        count, probs = rules.check(l2, CODE0, l2segs, [c for c in BLOCKCODE[b] if c in l2segs], (), imps,
                                   l2_import(imports.index("FARCALL")))
        problems += ["block %s: %s" % (b, p) for p in probs]
        if a.rules:
            open(os.path.join(a.work, "rules-%s.bin" % b), "wb").write(l2)
    if problems:
        raise Fail("the 65C816 rules (tools/rules.py, on L2's relocated images):\n  " + "\n  ".join(problems))
    print("mkbank: %s: UICOM %d bytes; %s; UISTATE %d; %s" % (
        a.out, ucomsz, ", ".join("%s %d (%d free)" % (b, s, CODE_END - CODE0 - s)
                                 for b, (_, _, s, _) in zip(BLOCKS, blocks)), uisize,
        ", ".join("%s %d fixups" % (b, len(f)) for b, (_, f, _, _) in zip(BLOCKS, blocks))))


def show(path):
    d = open(path, "rb").read()
    if d[:3] != MAGIC:
        raise Fail("%s: not an OOUI.BNK" % path)
    print("format %d, interface %d, %d blocks, UISTATE %d, %d imports, %d calls" % (
        d[3], d[4], d[5], int.from_bytes(d[6:8], "little"), d[8], d[9]))
    for i in range(d[5]):
        o = 10 + 8 * i
        print("  block %s: image record %d, %d bytes; fixups record %d, %d" % (
            BLOCKS[i], int.from_bytes(d[o:o + 2], "little"), int.from_bytes(d[o + 2:o + 4], "little"),
            int.from_bytes(d[o + 4:o + 6], "little"), int.from_bytes(d[o + 6:o + 8], "little")))


def main(argv):
    ap = argparse.ArgumentParser(description="Make OOUI.BNK from the bank's objects.")
    ap.add_argument("--cfg")
    ap.add_argument("--iface", help="ui/uiimp.inc: the calls and the imports")
    ap.add_argument("--work")
    ap.add_argument("-o", "--out")
    ap.add_argument("--version", type=int, default=1)
    ap.add_argument("--src", nargs="*", default=[], help="the bank's sources, for the fall-through check")
    ap.add_argument("--lbl", help="write L0's labels here")
    ap.add_argument("--rules", action="store_true", help="write L2's images for rules.py")
    ap.add_argument("--show")
    ap.add_argument("objs", nargs="*")
    a = ap.parse_args(argv)
    try:
        if a.show:
            show(a.show)
        else:
            build(a)
    except Fail as e:
        print("mkbank: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
