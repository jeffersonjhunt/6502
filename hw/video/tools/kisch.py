"""Build KiCad 7 schematics from a parts-and-nets description.

Each part is placed once, and every pin gets a short wire stub ending in a
global label, a power symbol or a no-connect flag. The result is a correct,
netlist-style schematic meant to be tidied by hand in KiCad afterwards.
"""
import uuid, copy, os
from sexp import parse, dump, find, first, Q

KILIB = '/usr/share/kicad/symbols'
POWER_NETS = {'GND': 'GND', '+3V3': '+3V3', '+5V': '+5V'}
GRID = 1.27


def uid():
    return Q(str(uuid.uuid4()))


def snap(v):
    return round(round(v / GRID) * GRID, 4)


# ---------------------------------------------------------------- library

class Library:
    def __init__(self, name):
        self.name = name
        self.syms = {}          # name -> sexp (flattened, pins visible)
        self._cache = {}

    def _kilib(self, lib):
        if lib not in self._cache:
            L = parse(open(f'{KILIB}/{lib}.kicad_sym').read())
            self._cache[lib] = {s[1]: s for s in find(L, 'symbol')}
        return self._cache[lib]

    def take(self, lib, name, rename=None, unhide=True, props=None):
        S = self._kilib(lib)
        s = copy.deepcopy(S[name])
        new = rename or name
        ext = first(s, 'extends')
        if ext:
            parent = copy.deepcopy(S[ext[1]])
            body = [e for e in parent if isinstance(e, list) and e[0] not in ('property',)]
            for u in find(body, 'symbol'):
                u[1] = Q(u[1].replace(ext[1], name, 1))
            s = ['symbol', Q(name)] + [e for e in s[2:] if not (isinstance(e, list) and e[0] == 'extends')] + body
        s[1] = Q(new)
        for u in find(s, 'symbol'):
            if u[1].startswith(name + '_'):
                u[1] = Q(new + u[1][len(name):])
            if unhide:
                for p in find(u, 'pin'):
                    while 'hide' in p:
                        p.remove('hide')
        if props:
            for k, v in props.items():
                for p in find(s, 'property'):
                    if p[1] == k:
                        p[2] = Q(v)
        self.syms[new] = s
        return new

    def custom(self, name, left, right, ref='U', value=None, footprint='', datasheet='',
               desc='', top=(), bottom=(), width=None):
        """left/right/top/bottom: lists of (number, name, etype) or None for a gap."""
        pitch = 2.54
        n = max(len(left), len(right))
        if width is None:
            longest = max([len(p[1]) for p in list(left) + list(right) + list(top) + list(bottom) if p] + [4])
            width = max(15.24, snap(longest * 1.27 * 2 + 7.62))
            width = round(width / 2.54) * 2.54
        h = (n + 1) * pitch
        nt = max(len(top), len(bottom))
        width = max(width, (nt + 1) * pitch)
        x0, y0 = -width / 2, h / 2
        pins = []
        def add(lst, side):
            for i, p in enumerate(lst):
                if p is None:
                    continue
                num, nm, et = p
                if side == 'L':
                    at = (x0 - pitch, y0 - pitch * (i + 1), 0)
                elif side == 'R':
                    at = (-x0 + pitch, y0 - pitch * (i + 1), 180)
                elif side == 'T':
                    at = (x0 + pitch * (i + 1), y0 + pitch, 270)
                else:
                    at = (x0 + pitch * (i + 1), -y0 - pitch, 90)
                pins.append(['pin', et, 'line', ['at', round(at[0], 4), round(at[1], 4), at[2]], ['length', pitch],
                             ['name', Q(nm), ['effects', ['font', ['size', 1.27, 1.27]]]],
                             ['number', Q(num), ['effects', ['font', ['size', 1.27, 1.27]]]]])
        add(left, 'L'); add(right, 'R'); add(top, 'T'); add(bottom, 'B')
        def prop(k, v, y, hide=False):
            eff = ['effects', ['font', ['size', 1.27, 1.27]]]
            if hide:
                eff.append('hide')
            return ['property', Q(k), Q(v), ['at', 0, y, 0], eff]
        s = ['symbol', Q(name), ['pin_names', ['offset', 1.016]], ['in_bom', 'yes'], ['on_board', 'yes'],
             prop('Reference', ref, y0 + 3.81), prop('Value', value or name, -y0 - 3.81),
             prop('Footprint', footprint, -y0 - 6.35, True), prop('Datasheet', datasheet, -y0 - 8.89, True),
             prop('ki_description', desc, 0, True),
             ['symbol', Q(name + '_0_1'),
              ['rectangle', ['start', round(x0, 4), round(y0, 4)], ['end', round(-x0, 4), round(-y0, 4)],
               ['stroke', ['width', 0.254], ['type', 'default']], ['fill', ['type', 'background']]]],
             ['symbol', Q(name + '_1_1')] + pins]
        self.syms[name] = s
        return name

    def pins(self, name):
        out = []
        for u in find(self.syms[name], 'symbol'):
            if u[1].endswith('_0_1') or u[1].endswith('_1_1') or u[1].endswith('_0_0') or u[1].endswith('_1_0'):
                for p in find(u, 'pin'):
                    at = first(p, 'at')
                    out.append(dict(num=str(first(p, 'number')[1]), name=str(first(p, 'name')[1]),
                                    x=float(at[1]), y=float(at[2]), ang=int(float(at[3])), etype=p[1]))
        return out

    def prop(self, name, key):
        for p in find(self.syms[name], 'property'):
            if p[1] == key:
                return str(p[2])
        return ''

    def write(self, path):
        doc = ['kicad_symbol_lib', ['version', 20220914], ['generator', 'oo_video_gen']] + \
              [self.syms[k] for k in sorted(self.syms)]
        open(path, 'w').write(dump(doc) + '\n')


# ---------------------------------------------------------------- schematic

class Sheet:
    def __init__(self, lib, title, filename, paper='A2', comment=''):
        self.lib, self.title, self.filename, self.paper = lib, title, filename, paper
        self.items, self.used = [], set()
        self.uuid = uid()
        self.parts = []    # (sym, ref, value, at, nets, props)
        self.texts = []
        self.comment = comment

    def part(self, sym, ref, nets, at, value=None, footprint=None, fields=None):
        """nets: dict pin-number-or-name -> net name, 'NC', or None (unconnected)."""
        self.parts.append(dict(sym=sym, ref=ref, nets=nets, at=at, value=value,
                               footprint=footprint, fields=fields or {}))

    def text(self, s, at, size=2.0):
        self.texts.append((s, at, size))

    def _label(self, net, x, y, ang):
        if net in POWER_NETS:
            sym = 'power:' + net
            rot = {'GND': 0, '+3V3': 0, '+5V': 0}[net]
            if net == 'GND':
                rot = 0
            return ('power', net, x, y)
        just = {0: 'left', 180: 'right', 90: 'left', 270: 'right'}[ang]
        return ['global_label', Q(net), ['shape', 'passive'], ['at', x, y, ang], ['fields_autoplaced'],
                ['effects', ['font', ['size', 1.27, 1.27]], ['justify', just]], ['uuid', uid()],
                ['property', Q('Intersheetrefs'), Q('${INTERSHEET_REFS}'), ['at', x, y, 0],
                 ['effects', ['font', ['size', 1.27, 1.27]], 'hide']]]

    def build(self, project, root_uuid, sheet_path, power_refs):
        items = []
        libsyms = {}
        for P in self.parts:
            sym = P['sym']
            libsyms[sym] = True
            sx, sy = snap(P['at'][0]), snap(P['at'][1])
            pins = self.lib.pins(sym)
            nets = P['nets']
            ref = P['ref']
            props = []
            def pr(k, v, dx, dy, hide=False):
                eff = ['effects', ['font', ['size', 1.27, 1.27]]]
                if hide:
                    eff.append('hide')
                props.append(['property', Q(k), Q(v), ['at', round(sx + dx, 4), round(sy + dy, 4), 0], eff])
            ys = [-p['y'] for p in pins] or [0]
            pr('Reference', ref, 0, min(ys) - 3.81)
            pr('Value', P['value'] or self.lib.prop(sym, 'Value') or sym, 0, max(ys) + 3.81)
            pr('Footprint', P['footprint'] if P['footprint'] is not None else self.lib.prop(sym, 'Footprint'), 0, max(ys) + 6.35, True)
            pr('Datasheet', self.lib.prop(sym, 'Datasheet'), 0, max(ys) + 8.89, True)
            for k, v in P['fields'].items():
                pr(k, v, 0, max(ys) + 11.43, True)
            pinlist = []
            done_pts = {}
            for p in pins:
                pinlist.append(['pin', Q(p['num']), ['uuid', uid()]])
                key = p['num'] if p['num'] in nets else (p['name'] if p['name'] in nets else None)
                net = nets.get(key) if key else None
                px, py = round(sx + p['x'], 4), round(sy - p['y'], 4)
                if (px, py) in done_pts:
                    if net and done_pts[(px, py)] not in (net,):
                        raise ValueError(f'{ref} stacked pin {p["num"]} net clash {net} vs {done_pts[(px,py)]}')
                    continue
                if key is None:
                    raise ValueError(f'{ref} ({sym}): pin {p["num"]} {p["name"]} has no net assigned')
                done_pts[(px, py)] = net
                if net in (None, 'NC'):
                    items.append(['no_connect', ['at', px, py], ['uuid', uid()]])
                    continue
                d = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[p['ang']]
                L = 2.54
                ex, ey = round(px + d[0] * L, 4), round(py + d[1] * L, 4)
                items.append(['wire', ['pts', ['xy', px, py], ['xy', ex, ey]],
                              ['stroke', ['width', 0], ['type', 'default']], ['uuid', uid()]])
                lang = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}[d]
                lab = self._label(net, ex, ey, lang)
                if isinstance(lab, tuple):
                    _, pn, lx, ly = lab
                    libsyms['power:' + pn] = True
                    pref = '#PWR%03d' % power_refs[0]
                    power_refs[0] += 1
                    rot = 0
                    if pn == 'GND' and d == (0, -1):
                        rot = 180
                    if pn != 'GND' and d == (0, 1):
                        rot = 180
                    items.append(['symbol', ['lib_id', Q('power:' + pn)], ['at', lx, ly, rot], ['unit', 1],
                                  ['in_bom', 'yes'], ['on_board', 'yes'], ['dnp', 'no'], ['uuid', uid()],
                                  ['property', Q('Reference'), Q(pref), ['at', lx, ly, 0],
                                   ['effects', ['font', ['size', 1.27, 1.27]], 'hide']],
                                  ['property', Q('Value'), Q(pn), ['at', lx, round(ly + (3.81 if pn == 'GND' else -3.81), 4), 0],
                                   ['effects', ['font', ['size', 1.27, 1.27]]]],
                                  ['property', Q('Footprint'), Q(''), ['at', lx, ly, 0], ['effects', ['font', ['size', 1.27, 1.27]], 'hide']],
                                  ['property', Q('Datasheet'), Q(''), ['at', lx, ly, 0], ['effects', ['font', ['size', 1.27, 1.27]], 'hide']],
                                  ['pin', Q('1'), ['uuid', uid()]],
                                  ['instances', ['project', Q(project), ['path', Q(sheet_path), ['reference', Q(pref)], ['unit', 1]]]]])
                else:
                    items.append(lab)
            items.append(['symbol', ['lib_id', Q(f'{self.lib.name}:{sym}')], ['at', sx, sy, 0], ['unit', 1],
                          ['in_bom', 'yes'], ['on_board', 'yes'], ['dnp', 'no'], ['uuid', uid()]] + props + pinlist +
                         [['instances', ['project', Q(project), ['path', Q(sheet_path), ['reference', Q(ref)], ['unit', 1]]]]])
        for s, at, size in self.texts:
            items.append(['text', Q(s), ['at', at[0], at[1], 0],
                          ['effects', ['font', ['size', size, size]], ['justify', 'left', 'top']], ['uuid', uid()]])
        lib_symbols = []
        for k in sorted(libsyms):
            if k.startswith('power:'):
                s = copy.deepcopy(self.lib.power[k.split(':')[1]])
                s[1] = Q(k)
            else:
                s = copy.deepcopy(self.lib.syms[k])
                s[1] = Q(f'{self.lib.name}:{k}')
            lib_symbols.append(s)
        doc = ['kicad_sch', ['version', 20230121], ['generator', 'eeschema'], ['uuid', self.uuid],
               ['paper', Q(self.paper)],
               ['title_block', ['title', Q(self.title)], ['date', Q('2026-11-20')], ['rev', Q('0.1')],
                ['company', Q('One Off Endeavors')], ['comment', 1, Q(self.comment)],
                ['comment', 2, Q('Generated by hw/video/tools/gen_kicad.py; tidy by hand in KiCad')]],
               ['lib_symbols'] + lib_symbols] + items
        return doc


def load_power(lib):
    L = parse(open(f'{KILIB}/power.kicad_sym').read())
    S = {s[1]: s for s in find(L, 'symbol')}
    lib.power = {n: copy.deepcopy(S[n]) for n in ('GND', '+3V3', '+5V', 'PWR_FLAG')}


def root_sheet(project, title, sheets, paper='A3', notes=()):
    ru = uid()
    items = []
    x, y = 30.48, 40.64
    for i, (name, fname) in enumerate(sheets):
        su = uid()
        items.append(['sheet', ['at', x, y], ['size', 50.8, 20.32], ['fields_autoplaced'],
                      ['stroke', ['width', 0.1524], ['type', 'solid']], ['fill', ['color', 0, 0, 0, 0.0]],
                      ['uuid', su],
                      ['property', Q('Sheetname'), Q(name), ['at', x, round(y - 0.7, 4), 0],
                       ['effects', ['font', ['size', 1.27, 1.27]], ['justify', 'left', 'bottom']]],
                      ['property', Q('Sheetfile'), Q(fname), ['at', x, round(y + 20.32 + 0.6, 4), 0],
                       ['effects', ['font', ['size', 1.27, 1.27]], ['justify', 'left', 'top']]],
                      ['instances', ['project', Q(project), ['path', Q('/' + ru), ['page', Q(str(i + 2))]]]]])
        x += 63.5
        if x > 260:
            x = 30.48; y += 38.1
        sheets[i] = (name, fname, su)
    ty = y + 45
    for n in notes:
        items.append(['text', Q(n), ['at', 30.48, ty, 0], ['effects', ['font', ['size', 1.8, 1.8]], ['justify', 'left', 'top']], ['uuid', uid()]])
        ty += 6 + 3.5 * n.count('\n')
    doc = ['kicad_sch', ['version', 20230121], ['generator', 'eeschema'], ['uuid', ru], ['paper', Q(paper)],
           ['title_block', ['title', Q(title)], ['date', Q('2026-11-20')], ['rev', Q('0.1')], ['company', Q('One Off Endeavors')]],
           ['lib_symbols']] + items + [['sheet_instances', ['path', Q('/'), ['page', Q('1')]]]]
    return doc, ru


def write_project(dirpath, project, libname, librel):
    os.makedirs(dirpath, exist_ok=True)
    pro = {
        "board": {"design_settings": {}},
        "meta": {"filename": f"{project}.kicad_pro", "version": 1},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "sheets": [], "text_variables": {}
    }
    import json
    open(f'{dirpath}/{project}.kicad_pro', 'w').write(json.dumps(pro, indent=2) + '\n')
    open(f'{dirpath}/sym-lib-table', 'w').write(
        f'(sym_lib_table\n  (lib (name "{libname}")(type "KiCad")(uri "${{KIPRJMOD}}/{librel}")(options "")(descr "OO-1 video card symbols"))\n)\n')
