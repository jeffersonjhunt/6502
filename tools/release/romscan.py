"""Does any 64-byte run of a protected file appear in a site build?

usage: python3 -I tools/release/romscan.py LIST ROOT
LIST is a file of NUL-separated paths (the protected files, the control last);
ROOT is the build. Every 64-byte window of ROOT is indexed (ROOT is small), then
each protected file's windows at a 16-byte stride are looked up, so any shared
run of 79 bytes or more is found. Windows of four byte values or fewer (fill,
zeros) are skipped, as romscan.py does. Prints one HIT line per (build file,
protected file) pair, and the counts; nothing from the protected files.
"""
import os
import sys

lst, root = sys.argv[1], sys.argv[2]
paths = [p for p in open(lst, "rb").read().decode().split("\0") if p]

index = {}
for d, _, fs in os.walk(root):
    for f in fs:
        p = os.path.join(d, f)
        data = open(p, "rb").read()
        for i in range(len(data) - 63):
            index.setdefault(hash(data[i:i + 64]), p)
print("build windows", len(index))

hits = set()
for r in paths:
    b = open(r, "rb").read()
    for i in range(0, len(b) - 63, 16):
        c = b[i:i + 64]
        p = index.get(hash(c))
        if p is None or len(set(c)) <= 4:
            continue
        data = open(p, "rb").read()
        if c in data and (p, r) not in hits:  # a hash match, confirmed
            hits.add((p, r))
            print("HIT", os.path.relpath(p, root), os.path.basename(r))
print("protected files", len(paths), "hits", len(hits))
