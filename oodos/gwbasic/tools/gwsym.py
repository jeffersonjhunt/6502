"""The resident core's exports, for the blocks (hw/oodos.md, "The plan, phase 7").

    python3 -I gwsym.py gwbasic.map -o ressym.s

Reads ld65's map of the resident link (its "Exports list") and writes an assembler module that exports
each as an absolute symbol, so a block's link takes the resident core's routines and variables at their
addresses, as OOUI's blocks take UICOM's (hw/ooui.md, U3a). Zero page names are gwbasic.inc's equates,
not exports.
"""
import argparse
import re

ap = argparse.ArgumentParser()
ap.add_argument("map")
ap.add_argument("-o", required=True)
a = ap.parse_args()
text = open(a.map).read()
part = text.split("Exports list by name:", 1)[1].split("Exports list by value:", 1)[0]
names = {}
for m in re.finditer(r"(\S+)\s+([0-9A-F]{6})\s+([A-Z]+)", part):
    name = m.group(1)
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name) and not name.startswith("__") and "Z" not in m.group(3):
        names[name] = int(m.group(2), 16)
out = ["; Made by tools/gwsym.py from the resident core's map: don't edit."]
for n in sorted(names):
    out.append("%s = $%04X" % (n, names[n]))
    out.append(".export %s" % n)
open(a.o, "w").write("\n".join(out) + "\n")
