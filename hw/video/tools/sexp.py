"""Tiny S-expression reader/writer for KiCad files."""
import re
TOK = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))')

class Q(str):
    """A quoted string atom."""

def parse(text):
    pos, stack, cur = 0, [], []
    while True:
        m = TOK.match(text, pos)
        if not m or m.end() == pos:
            break
        pos = m.end()
        if m.group(1):
            stack.append(cur); cur = []
        elif m.group(2):
            done = cur; cur = stack.pop(); cur.append(done)
        elif m.group(3) is not None:
            cur.append(Q(m.group(3).replace('\\n', '\n').replace('\\"', '"').replace('\\\\', '\\')))
        else:
            cur.append(m.group(4))
    return cur[0] if len(cur) == 1 else cur

def dump(x, ind=0):
    if isinstance(x, list):
        if not x:
            return '()'
        flat = all(not isinstance(e, list) for e in x)
        if flat or (len(x) <= 4 and sum(len(dump(e)) for e in x) < 70 and
                    all(not isinstance(e, list) or all(not isinstance(f, list) for f in e) for e in x)):
            return '(' + ' '.join(dump(e) for e in x) + ')'
        head = []
        rest = list(x)
        while rest and not isinstance(rest[0], list):
            head.append(dump(rest.pop(0)))
        pad = '  ' * (ind + 1)
        return '(' + ' '.join(head) + ''.join('\n' + pad + dump(e, ind + 1) for e in rest) + ')'
    if isinstance(x, Q):
        return '"' + x.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
    if isinstance(x, float):
        s = ('%.4f' % x).rstrip('0').rstrip('.')
        return '0' if s in ('-0', '') else s
    return str(x)

def find(x, key):
    return [e for e in x if isinstance(e, list) and e and e[0] == key]

def first(x, key):
    r = find(x, key)
    return r[0] if r else None
