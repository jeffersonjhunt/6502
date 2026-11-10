"""OOLIB's zero page, for overlays (the Programmer's Guide, chapter 8, "Linking the library into an
overlay").

    python3 -I libzp.py check build/lib-*.o
    python3 -I libzp.py names caller.map SID_ZP [NAME ...] -o callerzp.s

check: every library module's zero page is in a module of its own, lib-zpNAME.o, which has nothing
else; every other module has none. Then an overlay that is given its caller's addresses for those
names links no zero page of its own, and function 59 takes it. Fails on a module that breaks it, and
on no modules at all.

names: the caller's addresses for zero page names, from its ld65 map (-m), as a module to assemble
and link into the overlay before oolib.lib. Each name must be in the map's exports as zero page: a
caller links a library module's zero page by naming it (.importzp SID_ZP), even for a module only
its overlays call. Fails on a name the map doesn't have, or has but not as zero page.
"""
import os
import re
import subprocess
import sys


def segsizes(obj):
    """{segment: size} from od65, or None if od65 failed."""
    try:
        out = subprocess.run(['od65', '--dump-segsize', obj], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print(f'libzp: od65 on {obj}: {e}', file=sys.stderr)
        return None
    return {m.group(1): int(m.group(2)) for m in re.finditer(r'^\s+(\w+):\s+(\d+)$', out, re.M)}


def check(objs):
    if not objs:
        print('libzp: no library modules given', file=sys.stderr)
        return 1
    bad = 0
    for obj in objs:
        sizes = segsizes(obj)
        if sizes is None or 'ZEROPAGE' not in sizes:
            print(f'libzp: {obj}: no segment sizes', file=sys.stderr)
            bad += 1
            continue
        zp = sizes['ZEROPAGE']
        other = sum(v for k, v in sizes.items() if k != 'ZEROPAGE')
        if os.path.basename(obj).startswith('lib-zp'):
            if zp == 0 or other:
                print(f'libzp: {obj}: a zero page module holds zero page and nothing else ({zp} and {other})',
                      file=sys.stderr)
                bad += 1
        elif zp:
            print(f'libzp: {obj}: {zp} bytes of zero page of its own; move them into a lib/zp*.s module',
                  file=sys.stderr)
            bad += 1
    if bad:
        return 1
    print(f'libzp: {len(objs)} modules, zero page only in lib-zp*')
    return 0


def names(mapfile, wanted, out):
    if not wanted:
        print('libzp: no names given', file=sys.stderr)
        return 1
    text = open(mapfile).read()
    m = re.search(r'^Exports list by name:\n-+\n(.*?)(?:\n\n|\Z)', text, re.M | re.S)
    if not m:
        print(f'libzp: {mapfile}: no exports list', file=sys.stderr)
        return 1
    # Two to a line: NAME  VALUE  FLAGS (R referenced, L/E label or equate, Z zero page, A absolute)
    exports = {n: (int(v, 16), f) for n, v, f in re.findall(r'(\S+)\s+([0-9A-F]{6})\s+([A-Z]{3})', m.group(1))}
    lines, bad = [], 0
    for n in wanted:
        if n not in exports:
            print(f'libzp: {mapfile}: no {n}; the caller links it by naming it (.importzp {n})', file=sys.stderr)
            bad += 1
        elif exports[n][1][2] != 'Z' or exports[n][0] > 0xFF:
            print(f'libzp: {mapfile}: {n} is not zero page', file=sys.stderr)
            bad += 1
        else:
            lines.append(f'{n} = ${exports[n][0]:02X}\n        .exportzp {n}\n')
    if bad:
        return 1
    with open(out, 'w') as f:
        f.write(f'; The caller\'s zero page for an overlay, from {os.path.basename(mapfile)} (tools/libzp.py)\n\n')
        f.writelines(lines)
    return 0


def main(argv):
    if len(argv) >= 1 and argv[0] == 'check':
        return check(argv[1:])
    if len(argv) >= 4 and argv[0] == 'names' and argv[-2] == '-o':
        return names(argv[1], argv[2:-2], argv[-1])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
