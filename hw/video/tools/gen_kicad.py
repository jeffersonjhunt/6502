#!/usr/bin/env python3
"""Generate the OO-1 video card's KiCad schematics and FPGA pin constraints.

    python3 hw/video/tools/gen_kicad.py

Writes, under hw/video/:
  kicad/lib/oo_video.kicad_sym           the project's symbols
  kicad/proto-carrier/*                  the prototype carrier (iCESugar-Pro module on the OO carrier)
  kicad/direct/*                         the direct board's skeleton (shared sheets; FPGA and SDRAM to come)
  pinmap/carrier-assign.csv              signal per SODIMM pin, ball, bank
  fpga/icesugar_carrier.lpf              nextpnr constraints for the carrier

This is a bootstrap. Once the schematics are edited by hand in KiCad, KiCad's
files are the source and this script is not run again over them.
"""
import csv, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from kisch import Library, Sheet, load_power, root_sheet, write_project
from sexp import dump

VIDEO = os.path.dirname(HERE)
LIBNAME = 'oo_video'

# ------------------------------------------------------------------ signals

BUS_IN = [f'A{i}' for i in range(24)] + [f'PS{i}' for i in range(4)] + \
         ['RWB', '~{OE}', '~{WORD}', '~{MEM}', 'PHI2', '~{RESET}', '~{IACK}', '~{DMA}', 'AMODE', 'TC']
OD_OUT = [('~{IRQ}', 'F_IRQ_N'), ('~{DREQ}', 'F_DREQ_N'), ('~{WAIT}', 'F_WAIT_N'),
          ('D1', 'F_POLL1_N'), ('D9', 'F_POLL9_N')]


def fname(bus):
    """FPGA-side net for a bus signal."""
    s = bus.replace('~{', '').replace('}', '_N') if bus.startswith('~{') else bus
    return 'F_' + s


VIDEO_SIGS = [f'F_R{i}' for i in range(8)] + [f'F_G{i}' for i in range(8)] + [f'F_B{i}' for i in range(8)] + \
             ['F_DE', 'F_HS', 'F_VS', 'F_PCLK_DVI', 'F_PCLK_VGA']
MISC_SIGS = ['F_SCL', 'F_SDA', 'F_LED0', 'F_LED1', 'F_HPD']
BUS_SIGS = [fname(b) for b in BUS_IN] + [f'F_D{i}' for i in range(16)] + [f for _, f in OD_OUT] + \
           ['F_DDIR', 'F_DOE0_N', 'F_DOE1_N']
CLOCK_PINS = {51: 'F_CLK_PIX', 118: 'F_CLK_SYN'}    # PT29A PCLKT0_0, PT27A PCLKT0_1


def rtl_name(net):
    """Port name for the RTL top level and the LPF."""
    import re
    n = net[2:].lower()
    m = re.fullmatch(r'([a-z]+)(\d+)', n)
    if m and m.group(1) in ('a', 'd', 'ps', 'r', 'g', 'b', 'spare'):
        return f'{m.group(1)}[{m.group(2)}]'
    m = re.fullmatch(r'led(\d)', n)
    if m:
        return f'led[{m.group(1)}]'
    return n


def assign():
    rows = list(csv.DictReader(open(f'{VIDEO}/pinmap/icesugar-pro-sodimm.csv')))
    sig = [r for r in rows if r['kind'] == 'signal']
    free = [r for r in sig if int(r['sodimm_pin']) not in CLOCK_PINS and r['bank'] != '8']
    bank8 = [r for r in sig if r['bank'] == '8']
    order = VIDEO_SIGS + MISC_SIGS + BUS_SIGS
    assert len(order) <= len(free), (len(order), len(free))
    amap = {}
    for r, s in zip(free, order):
        amap[int(r['sodimm_pin'])] = s
    for p, s in CLOCK_PINS.items():
        amap[p] = s
    spares = [r for r in free[len(order):]] + bank8
    for i, r in enumerate(spares):
        amap[int(r['sodimm_pin'])] = f'F_SPARE{i}'
    out = []
    for r in rows:
        p = int(r['sodimm_pin'])
        if r['kind'] == 'signal':
            out.append(dict(pin=p, net=amap[p], pad=r['pad'], ball=r['ball'], bank=r['bank'], clk=r['clock_function']))
        else:
            out.append(dict(pin=p, net={'GND': 'GND', '5V': '+5V_MOD', 'NC': 'NC'}[r['kind']], pad='', ball='', bank='', clk=''))
    return out


# ------------------------------------------------------------------ library

def build_library(rows):
    L = Library(LIBNAME)
    load_power(L)
    fpR = 'Resistor_SMD:R_0603_1608Metric'
    L.take('Device', 'R', props={'Footprint': fpR})
    L.take('Device', 'C', props={'Footprint': 'Capacitor_SMD:C_0603_1608Metric'})
    L.take('Device', 'LED', props={'Footprint': 'LED_SMD:LED_0603_1608Metric'})
    L.take('Device', 'D_Schottky', props={'Footprint': 'Diode_SMD:D_SMA'})
    L.take('Device', 'Crystal_GND24', props={'Footprint': 'Crystal:Crystal_SMD_3225-4Pin_3.2x2.5mm'})
    L.take('Regulator_Linear', 'AMS1117-3.3')
    L.take('Logic_LevelTranslator', 'SN74LVC245APW')
    L.take('Video', 'TFP410PAP')
    L.take('Oscillator', 'Si5351A-B-GT')
    L.take('Oscillator', 'ASE-xxxMHz')
    L.take('Connector', 'HDMI_A', props={'Footprint': 'Connector_Video:HDMI_A_Molex_208658-1001_Horizontal'})
    L.take('Connector', 'DE15_Receptacle_HighDensity', props={
        'Footprint': 'Connector_Dsub:DSUB-15-HD_Female_Horizontal_P2.29x1.98mm_EdgePinOffset3.03mm_Housed_MountingHolesOffset4.94mm'})
    L.take('Connector', 'TestPoint', props={'Footprint': 'TestPoint:TestPoint_Pad_D1.0mm'})
    L.take('Connector_Generic', 'Conn_01x10', props={'Footprint': 'Connector_PinHeader_2.54mm:PinHeader_1x10_P2.54mm_Vertical'})
    L.take('Memory_RAM', 'MT48LC16M16A2TG')

    # iCESugar-Pro module on its DDR2 SODIMM-200 edge. Pin names: pad/ball, so the
    # schematic shows where each pin lands on the ECP5.
    def mname(r):
        if r['net'] == 'GND':
            return 'GND', 'power_in'
        if r['net'] == '+5V_MOD':
            return '5V', 'power_in'
        if r['net'] == 'NC':
            return 'NC', 'no_connect'
        return f"{r['pad']}/{r['ball']}", 'bidirectional'
    left = [(str(r['pin']),) + mname(r) for r in rows if r['pin'] % 2 == 1]
    right = [(str(r['pin']),) + mname(r) for r in rows if r['pin'] % 2 == 0]
    L.custom('ICESUGAR_PRO', left, right, ref='M', value='iCESugar-Pro (LFE5U-25F-6BG256C)',
             footprint='', datasheet='https://github.com/wuxx/icesugar-pro',
             desc='Muse Lab iCESugar-Pro on a DDR2 SODIMM-200 socket (fit TE 1473005-1 or equivalent). Pins: pad/ball.')

    # The OO-1 backplane: the 98-contact keyed card edge (hw/modules.md, B6 draft).
    A = ['GND', '+5V', '+5V', '+5V', 'GND', '~{RESET}', '~{NMI}', '~{IRQ}', 'AMODE', 'GND', '~{BOOTSEL}', 'GND',
         'D0', 'D1', 'D2', 'D3', 'GND', 'D4', 'D5', 'D6', 'D7', 'GND', 'D8', 'D9', 'D10', 'D11', 'GND',
         'D12', 'D13', 'D14', 'D15', 'GND', 'RWB', '~{OE}', '~{WORD}', '~{MEM}', 'GND', 'PHI2', 'GND', 'RDY', 'BE',
         '~{WAIT}', 'SYNC', 'GND', 'OSC', 'GND', 'VPB', 'TC', 'RSV0']
    B = ['GND', '+5V', '+5V', '+5V', 'GND', 'BR0', 'BR1', 'BR2', '~{DREQ}', 'GND', 'RSV1', 'GND',
         'A0', 'A1', 'A2', 'A3', 'GND', 'A4', 'A5', 'A6', 'A7', 'GND', 'A8', 'A9', 'A10', 'A11', 'GND',
         'A12', 'A13', 'A14', 'A15', 'GND', 'A16', 'A17', 'A18', 'A19', 'GND', 'A20', 'A21', 'A22', 'A23', 'GND',
         'PS0', 'PS1', 'PS2', 'PS3', 'GND', '~{IACK}', '~{DMA}']
    assert len(A) == 49 and len(B) == 49
    et = lambda n: 'power_in' if n in ('GND', '+5V') else 'bidirectional'
    L.custom('OO_BUS_EDGE98', [(f'A{i+1}', n, et(n)) for i, n in enumerate(A)],
             [(f'B{i+1}', n, et(n)) for i, n in enumerate(B)], ref='J', value='OO-1 bus, 98-contact edge',
             footprint='Connector_PCBEdge:BUS_PCIexpress_x8',
             desc='OO-1 backplane card edge (PCIe x8 form, keyed and shrouded, 5 V only). Pin positions: hw/modules.md B6 draft, owner to confirm.')

    # SN74LVC16T245, TSSOP-48 (DGG): TI SCES636B pin table.
    L16 = [('1', '1DIR', 'input'), ('2', '1B1', 'bidirectional'), ('3', '1B2', 'bidirectional'), ('4', 'GND', 'power_in'),
           ('5', '1B3', 'bidirectional'), ('6', '1B4', 'bidirectional'), ('7', 'VCCB', 'power_in'), ('8', '1B5', 'bidirectional'),
           ('9', '1B6', 'bidirectional'), ('10', 'GND', 'power_in'), ('11', '1B7', 'bidirectional'), ('12', '1B8', 'bidirectional'),
           ('13', '2B1', 'bidirectional'), ('14', '2B2', 'bidirectional'), ('15', 'GND', 'power_in'), ('16', '2B3', 'bidirectional'),
           ('17', '2B4', 'bidirectional'), ('18', 'VCCB', 'power_in'), ('19', '2B5', 'bidirectional'), ('20', '2B6', 'bidirectional'),
           ('21', 'GND', 'power_in'), ('22', '2B7', 'bidirectional'), ('23', '2B8', 'bidirectional'), ('24', '2DIR', 'input')]
    R16 = [('48', '~{1OE}', 'input'), ('47', '1A1', 'bidirectional'), ('46', '1A2', 'bidirectional'), ('45', 'GND', 'power_in'),
           ('44', '1A3', 'bidirectional'), ('43', '1A4', 'bidirectional'), ('42', 'VCCA', 'power_in'), ('41', '1A5', 'bidirectional'),
           ('40', '1A6', 'bidirectional'), ('39', 'GND', 'power_in'), ('38', '1A7', 'bidirectional'), ('37', '1A8', 'bidirectional'),
           ('36', '2A1', 'bidirectional'), ('35', '2A2', 'bidirectional'), ('34', 'GND', 'power_in'), ('33', '2A3', 'bidirectional'),
           ('32', '2A4', 'bidirectional'), ('31', 'VCCA', 'power_in'), ('30', '2A5', 'bidirectional'), ('29', '2A6', 'bidirectional'),
           ('28', 'GND', 'power_in'), ('27', '2A7', 'bidirectional'), ('26', '2A8', 'bidirectional'), ('25', '~{2OE}', 'input')]
    L.custom('SN74LVC16T245DGG', L16, R16, ref='U', value='SN74LVC16T245DGG',
             footprint='Package_SO:TSSOP-48_6.1x12.5mm_P0.5mm', datasheet='https://www.ti.com/lit/gpn/sn74lvc16t245',
             desc='16-bit dual-supply transceiver; DIR and OE referenced to VCCA; DIR high: A to B')

    # 74LVC07A, SOIC-14: hex buffer, open-drain outputs, 5 V-tolerant.
    L.custom('74LVC07A_SOIC14',
             [('1', '1A', 'input'), ('3', '2A', 'input'), ('5', '3A', 'input'), ('9', '4A', 'input'), ('11', '5A', 'input'), ('13', '6A', 'input'),
              None, ('14', 'VCC', 'power_in')],
             [('2', '1Y', 'open_collector'), ('4', '2Y', 'open_collector'), ('6', '3Y', 'open_collector'), ('8', '4Y', 'open_collector'),
              ('10', '5Y', 'open_collector'), ('12', '6Y', 'open_collector'), None, ('7', 'GND', 'power_in')],
             ref='U', value='74LVC07A', footprint='Package_SO:SOIC-14_3.9x8.7mm_P1.27mm',
             datasheet='https://www.ti.com/lit/gpn/sn74lvc07a', desc='Hex buffer, open-drain outputs, 5 V-tolerant')

    # ADV7123, LQFP-48: ADI datasheet Rev. D, table 8.
    Lp = [(str(1 + i), f'G{i}', 'input') for i in range(10)] + [('11', '~{BLANK}', 'input'), ('12', '~{SYNC}', 'input'),
          None] + [(str(39 + i), f'R{i}', 'input') for i in range(10)] + [None, ('24', 'CLOCK', 'input'), ('38', '~{PSAVE}', 'input')]
    Rp = [(str(14 + i), f'B{i}', 'input') for i in range(10)] + [None, ('34', 'IOR', 'output'), ('33', '~{IOR}', 'output'),
          ('32', 'IOG', 'output'), ('31', '~{IOG}', 'output'), ('28', 'IOB', 'output'), ('27', '~{IOB}', 'output'), None,
          ('37', 'RSET', 'passive'), ('36', 'VREF', 'passive'), ('35', 'COMP', 'passive'), None,
          ('13', 'VAA', 'power_in'), ('29', 'VAA', 'power_in'), ('30', 'VAA', 'power_in'), ('25', 'GND', 'power_in'), ('26', 'GND', 'power_in')]
    L.custom('ADV7123_LQFP48', Lp, Rp, ref='U', value='ADV7123KSTZ140', footprint='Package_QFP:LQFP-48_7x7mm_P0.5mm',
             datasheet='https://www.analog.com/ADV7123', desc='Triple 10-bit video DAC, 3.3 V')
    return L


# ------------------------------------------------------------------ sheets

class Refs:
    def __init__(self):
        self.n = {}

    def __call__(self, p):
        self.n[p] = self.n.get(p, 0) + 1
        return f'{p}{self.n[p]}'


def grid(cols, x0, y0, dx, dy):
    i = 0
    while True:
        yield (x0 + (i % cols) * dx, y0 + (i // cols) * dy)
        i += 1


def module_sheet(L, ref, rows):
    S = Sheet(L, 'iCESugar-Pro module', 'module.kicad_sch', paper='A1',
              comment='DDR2 SODIMM-200 socket for the iCESugar-Pro; pin names are pad/ball on the LFE5U-25F')
    nets = {str(r['pin']): r['net'] for r in rows}
    S.part('ICESUGAR_PRO', ref('M'), nets, (300, 290), footprint='')
    S.part('D_Schottky', ref('D'), {'1': '+5V_MOD', '2': '+5V'}, (120, 120), value='SS34',
           fields={'Note': 'Keeps the module USB 5 V off the bus'})
    S.part('C', ref('C'), {'1': '+5V_MOD', '2': 'GND'}, (120, 150), value='22u', footprint='Capacitor_SMD:C_1206_3216Metric')
    # Spare pins to a header, for a logic analyser.
    hdr = {str(i + 1): f'F_SPARE{i}' for i in range(8)}
    hdr.update({'9': 'GND', '10': 'GND'})
    S.part('Conn_01x10', ref('J'), hdr, (120, 200), value='SPARE')
    S.part('R', ref('R'), {'1': 'F_LED0', '2': 'LED0_A'}, (480, 120), value='1k')
    S.part('LED', ref('D'), {'1': 'GND', '2': 'LED0_A'}, (480, 145), value='GREEN')
    S.part('R', ref('R'), {'1': 'F_LED1', '2': 'LED1_A'}, (520, 120), value='1k')
    S.part('LED', ref('D'), {'1': 'GND', '2': 'LED1_A'}, (520, 145), value='YELLOW')
    S.text('Module 5 V: pins 2-12 (+5V_MOD). GND: 1-11, 39-40, 55-56, 105-108, 157-158, 199-200.\n'
           'SODIMM pin to ball: pinmap/icesugar-pro-sodimm.csv. Signal per pin: pinmap/carrier-assign.csv.\n'
           'Two schematic labels on the module are misnamed (PT62D is ball C13 = PT60B; PT58D is C12 = PT56B);\n'
           'the balls are what count, and they are used here.', (40, 40))
    return S


def bus_sheet(L, ref):
    S = Sheet(L, 'Bus interface', 'bus.kicad_sch', paper='A2',
              comment='OO-1 backplane (5 V) to the FPGA (3.3 V)')
    edge = {}
    used_in = set(BUS_IN) | {f'D{i}' for i in range(16)} | {b for b, _ in OD_OUT}
    for side, pins in (('A', L.pins('OO_BUS_EDGE98')), ):
        pass
    for p in L.pins('OO_BUS_EDGE98'):
        n = p['name']
        if n in ('GND', '+5V'):
            edge[p['num']] = n
        elif n in used_in:
            edge[p['num']] = n
        else:
            edge[p['num']] = 'NC'
    S.part('OO_BUS_EDGE98', ref('J'), edge, (70, 180))
    # Inputs: five 74LVC245 at 3.3 V, A side on the bus (5 V-tolerant inputs), DIR high (A to B), OE low.
    ins = BUS_IN + [None, None]
    g = grid(3, 200, 70, 95, 80)
    for k in range(5):
        chunk = ins[k * 8:(k + 1) * 8]
        nets = {'1': '+3V3', '19': 'GND', '10': 'GND', '20': '+3V3'}
        for i, b in enumerate(chunk):
            nets[str(2 + i)] = b if b else 'GND'
            nets[str(18 - i)] = fname(b) if b else 'NC'
        u = ref('U')
        x, y = next(g)
        S.part('SN74LVC245APW', u, nets, (x, y))
        S.part('C', ref('C'), {'1': '+3V3', '2': 'GND'}, (x + 30, y - 25), value='100n')
    # Data: SN74LVC16T245, A side (VCCA 3.3 V) to the FPGA, B side (VCCB 5 V) to the bus.
    nets = {'1': 'F_DDIR', '24': 'F_DDIR', '48': 'F_DOE0_N', '25': 'F_DOE1_N'}
    for p in L.pins('SN74LVC16T245DGG'):
        n = p['name']
        if n == 'GND': nets[p['num']] = 'GND'
        elif n == 'VCCA': nets[p['num']] = '+3V3'
        elif n == 'VCCB': nets[p['num']] = '+5V'
        elif n[1:2] in 'AB' and n[0] in '12':
            bit = (int(n[0]) - 1) * 8 + int(n[2]) - 1
            nets[p['num']] = f'F_D{bit}' if n[1] == 'A' else f'D{bit}'
    x, y = next(g)
    S.part('SN74LVC16T245DGG', ref('U'), nets, (x, y + 20))
    for k, net in enumerate(('+3V3', '+3V3', '+5V', '+5V')):
        S.part('C', ref('C'), {'1': net, '2': 'GND'}, (x + 40 + 15 * k, y - 30), value='100n')
    # Pull-ups so the bus stays released while the FPGA configures.
    for net in ('F_DOE0_N', 'F_DOE1_N'):
        S.part('R', ref('R'), {'1': '+3V3', '2': net}, next(g), value='10k')
    S.part('R', ref('R'), {'1': 'F_DDIR', '2': 'GND'}, next(g), value='10k')
    # Open-drain outputs: 74LVC07A.
    nets = {'14': '+3V3', '7': 'GND'}
    for i, (bus, f) in enumerate(OD_OUT + [(None, None)]):
        a, y_ = {0: ('1', '2'), 1: ('3', '4'), 2: ('5', '6'), 3: ('9', '8'), 4: ('11', '10'), 5: ('13', '12')}[i]
        nets[a] = f if f else '+3V3'
        nets[y_] = bus if bus else 'NC'
    x, y = next(g)
    S.part('74LVC07A_SOIC14', ref('U'), nets, (x, y))
    S.part('C', ref('C'), {'1': '+3V3', '2': 'GND'}, (x + 30, y - 25), value='100n')
    for _, f in OD_OUT:
        S.part('R', ref('R'), {'1': '+3V3', '2': f}, next(g), value='10k')
    S.text('Polls: the card pulls D1 (interrupt poll, ID 9 mod 8) and D9 (DMA request poll) through the 74LVC07A,\n'
           'with the data transceiver off. Unused edge contacts carry no-connect flags.\n'
           'Edge pin positions follow the B6 draft in hw/modules.md and are the owner\'s to confirm.', (40, 20))
    return S


def video_sheet(L, ref):
    S = Sheet(L, 'Video outputs', 'video.kicad_sch', paper='A2', comment='DVI (TFP410) and VGA (ADV7123) from one 24-bit pixel bus')
    t = {'DE': 'F_DE', 'HSYNC': 'F_HS', 'VSYNC': 'F_VS', 'IDCK+': 'F_PCLK_DVI', 'IDCK-': 'GND',
         'VREF': '+3V3', 'CTL3/A3/DK3': 'GND', 'CTL2/A2/DK2': 'GND', 'CTL1/A1/DK1': 'GND',
         'EDGE/HTPLG': '+3V3', '~PD': '+3V3', 'MSEN/PO1': 'NC', 'ISEL/~RST': 'GND', 'DSEL/SDA': 'GND',
         'BSEL/SCL': '+3V3', 'TFADJ': 'TFADJ', 'RESERVED': 'GND', 'DKEN': 'GND', 'NC': 'NC', 'EP': 'GND',
         'DVDD': '+3V3', 'PVDD': '+3V3', 'TVDD': '+3V3', 'DGND': 'GND', 'PGND': 'GND', 'TGND': 'GND',
         'TXC-': 'TMDS_CK-', 'TXC+': 'TMDS_CK+', 'TX0-': 'TMDS_D0-', 'TX0+': 'TMDS_D0+',
         'TX1-': 'TMDS_D1-', 'TX1+': 'TMDS_D1+', 'TX2-': 'TMDS_D2-', 'TX2+': 'TMDS_D2+'}
    for i in range(24):
        t[f'DATA{i}'] = (f'F_B{i}' if i < 8 else f'F_G{i-8}' if i < 16 else f'F_R{i-16}')
    nets = {}
    for p in L.pins('TFP410PAP'):
        nets[p['num']] = t[p['name']]
    S.part('TFP410PAP', ref('U'), nets, (120, 140))
    S.part('R', ref('R'), {'1': 'TFADJ', '2': 'GND'}, (60, 260), value='510 1%')
    for k in range(5):
        S.part('C', ref('C'), {'1': '+3V3', '2': 'GND'}, (60 + 18 * k, 290), value='100n')
    h = {'1': 'TMDS_D2+', '2': 'GND', '3': 'TMDS_D2-', '4': 'TMDS_D1+', '5': 'GND', '6': 'TMDS_D1-',
         '7': 'TMDS_D0+', '8': 'GND', '9': 'TMDS_D0-', '10': 'TMDS_CK+', '11': 'GND', '12': 'TMDS_CK-',
         '13': 'NC', '14': 'NC', '15': 'DDC_SCL', '16': 'DDC_SDA', '17': 'GND', '18': 'HDMI_5V', '19': 'HDMI_HPD', 'SH': 'GND'}
    S.part('HDMI_A', ref('J'), h, (260, 120), value='HDMI (DVI-D)')
    S.part('D_Schottky', ref('D'), {'1': 'HDMI_5V', '2': '+5V'}, (260, 210), value='BAT54')
    S.part('R', ref('R'), {'1': 'HDMI_5V', '2': 'DDC_SCL'}, (290, 210), value='2k2')
    S.part('R', ref('R'), {'1': 'HDMI_5V', '2': 'DDC_SDA'}, (310, 210), value='2k2')
    S.part('R', ref('R'), {'1': 'HDMI_HPD', '2': 'F_HPD'}, (290, 240), value='10k')
    S.part('R', ref('R'), {'1': 'F_HPD', '2': 'GND'}, (310, 240), value='20k')
    a = {'~{BLANK}': 'F_DE', '~{SYNC}': 'GND', 'CLOCK': 'F_PCLK_VGA', '~{PSAVE}': '+3V3',
         'IOR': 'VGA_R', 'IOG': 'VGA_G', 'IOB': 'VGA_B', '~{IOR}': 'GND', '~{IOG}': 'GND', '~{IOB}': 'GND',
         'RSET': 'DAC_RSET', 'VREF': 'DAC_VREF', 'COMP': 'DAC_COMP', 'VAA': '+3V3', 'GND': 'GND'}
    for c in 'RGB':
        a[f'{c}0'] = 'GND'; a[f'{c}1'] = 'GND'
        for i in range(8):
            a[f'{c}{i+2}'] = f'F_{c}{i}'
    nets = {p['num']: a[p['name']] for p in L.pins('ADV7123_LQFP48')}
    S.part('ADV7123_LQFP48', ref('U'), nets, (420, 140))
    S.part('R', ref('R'), {'1': 'DAC_RSET', '2': 'GND'}, (380, 250), value='536 1%')
    S.part('C', ref('C'), {'1': 'DAC_VREF', '2': 'GND'}, (400, 250), value='100n')
    S.part('C', ref('C'), {'1': 'DAC_COMP', '2': '+3V3'}, (420, 250), value='100n')
    for k in range(3):
        S.part('C', ref('C'), {'1': '+3V3', '2': 'GND'}, (440 + 18 * k, 250), value='100n')
    for c in 'RGB':
        S.part('R', ref('R'), {'1': f'VGA_{c}', '2': 'GND'}, (380 + 20 * 'RGB'.index(c), 290), value='75 1%')
    S.part('R', ref('R'), {'1': 'F_HS', '2': 'VGA_HS'}, (450, 290), value='33')
    S.part('R', ref('R'), {'1': 'F_VS', '2': 'VGA_VS'}, (470, 290), value='33')
    d = {'1': 'VGA_R', '2': 'VGA_G', '3': 'VGA_B', '4': 'NC', '5': 'GND', '6': 'GND', '7': 'GND', '8': 'GND',
         '9': 'NC', '10': 'GND', '11': 'NC', '12': 'NC', '13': 'VGA_HS', '14': 'VGA_VS', '15': 'NC'}
    S.part('DE15_Receptacle_HighDensity', ref('J'), d, (530, 140), value='VGA')
    S.text('TFP410 straps: ISEL=0 (no I2C), BSEL=1 (24-bit), DSEL=0, EDGE=1 (rising), DKEN=0, VREF=3.3 V.\n'
           'DATA[23:16] red, [15:8] green, [7:0] blue. TX0 blue, TX1 green, TX2 red.\n'
           'ADV7123: top 8 bits of each channel driven, bottom 2 tied low; RSET 536 for 0.7 V into 37.5 ohm.\n'
           'Prototype has no TPD12S016 (ESD); the final card adds it between the TFP410 and the socket.', (40, 20))
    return S


def clocks_sheet(L, ref):
    S = Sheet(L, 'Clocks', 'clocks.kicad_sch', paper='A3', comment='25.175 MHz pixel oscillator and the Si5351A')
    S.part('ASE-xxxMHz', ref('X'), {'1': '+3V3', '2': 'GND', '3': 'OSC_OUT', '4': '+3V3'}, (60, 80), value='25.175MHz')
    S.part('R', ref('R'), {'1': 'OSC_OUT', '2': 'F_CLK_PIX'}, (110, 80), value='33')
    S.part('C', ref('C'), {'1': '+3V3', '2': 'GND'}, (60, 120), value='100n')
    S.part('Si5351A-B-GT', ref('U'), {'1': '+3V3', '7': '+3V3', '8': 'GND', '2': 'XTAL_A', '3': 'XTAL_B',
                                      '4': 'F_SCL', '5': 'F_SDA', '10': 'SYN_CLK0', '9': 'SYN_CLK1', '6': 'SYN_CLK2'}, (220, 90))
    S.part('Crystal_GND24', ref('Y'), {'1': 'XTAL_A', '3': 'XTAL_B', '2': 'GND', '4': 'GND'}, (160, 150), value='25MHz 8pF')
    S.part('R', ref('R'), {'1': 'SYN_CLK0', '2': 'F_CLK_SYN'}, (290, 80), value='33')
    S.part('TestPoint', ref('TP'), {'1': 'SYN_CLK1'}, (290, 110), value='CLK1')
    S.part('TestPoint', ref('TP'), {'1': 'SYN_CLK2'}, (310, 110), value='CLK2')
    S.part('R', ref('R'), {'1': '+3V3', '2': 'F_SCL'}, (220, 150), value='4k7')
    S.part('R', ref('R'), {'1': '+3V3', '2': 'F_SDA'}, (240, 150), value='4k7')
    S.part('C', ref('C'), {'1': '+3V3', '2': 'GND'}, (260, 150), value='100n')
    S.part('C', ref('C'), {'1': '+3V3', '2': 'GND'}, (280, 150), value='100n')
    S.text('The card boots on the 25.175 MHz oscillator. The Si5351A (I2C 0x60) is left unprogrammed on OO-1;\n'
           'a later bitstream programs it and switches to CLK0 during vertical blank. CLK1 and CLK2 go to test points.', (30, 20))
    return S


def power_sheet(L, ref):
    S = Sheet(L, 'Power', 'power.kicad_sch', paper='A4', comment='3.3 V from the bus 5 V')
    S.part('AMS1117-3.3', ref('U'), {'3': '+5V', '2': '+3V3', '1': 'GND'}, (100, 80))
    S.part('C', ref('C'), {'1': '+5V', '2': 'GND'}, (70, 110), value='22u', footprint='Capacitor_SMD:C_1206_3216Metric')
    S.part('C', ref('C'), {'1': '+3V3', '2': 'GND'}, (130, 110), value='22u', footprint='Capacitor_SMD:C_1206_3216Metric')
    S.text('Prototype: a linear regulator (about 0.4 A, 0.7 W). The final card uses a buck converter.\n'
           'The module takes 5 V on its own pins (module sheet) through a Schottky diode.', (30, 20))
    return S


def emit(project, outdir, sheets, title, notes):
    write_project(outdir, project, LIBNAME, f'../lib/{LIBNAME}.kicad_sym')
    names = [(s.title, s.filename) for s in sheets]
    root, ru = root_sheet(project, title, names, notes=notes)
    open(f'{outdir}/{project}.kicad_sch', 'w').write(dump(root) + '\n')
    pref = [1]
    for s, (_, _, su) in zip(sheets, names):
        doc = s.build(project, ru, f'/{ru}/{su}', pref)
        open(f'{outdir}/{s.filename}', 'w').write(dump(doc) + '\n')


def main():
    rows = assign()
    os.makedirs(f'{VIDEO}/kicad/lib', exist_ok=True)
    os.makedirs(f'{VIDEO}/fpga', exist_ok=True)
    L = build_library(rows)
    L.write(f'{VIDEO}/kicad/lib/{LIBNAME}.kicad_sym')

    with open(f'{VIDEO}/pinmap/carrier-assign.csv', 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['sodimm_pin', 'net', 'rtl_port', 'pad', 'ball', 'bank', 'clock_function'])
        for r in rows:
            if r['ball']:
                w.writerow([r['pin'], r['net'], rtl_name(r['net']), r['pad'], r['ball'], r['bank'], r['clk']])
    with open(f'{VIDEO}/fpga/icesugar_carrier.lpf', 'w') as f:
        f.write('# OO-1 video card: iCESugar-Pro module on the OO carrier.\n'
                '# Generated by hw/video/tools/gen_kicad.py from pinmap/carrier-assign.csv.\n'
                '# The module\'s SDRAM, flash and serial port are on fixed balls: see the iCESugar-Pro repo\'s LPF files.\n\n'
                'BLOCK RESETPATHS;\nBLOCK ASYNCPATHS;\n\n')
        for r in sorted([r for r in rows if r['ball']], key=lambda r: r['net']):
            port = rtl_name(r['net'])
            f.write(f'LOCATE COMP "{port}" SITE "{r["ball"]}";  # SODIMM {r["pin"]}, {r["pad"]}, bank {r["bank"]}'
                    f'{", " + r["clk"] if r["clk"] else ""}\n')
            f.write(f'IOBUF PORT "{port}" IO_TYPE=LVCMOS33;\n')
        f.write('\nFREQUENCY PORT "clk_pix" 25.175 MHZ;\nFREQUENCY PORT "clk_syn" 25.175 MHZ;\n')

    ref = Refs()
    sheets = [module_sheet(L, ref, rows), bus_sheet(L, ref), video_sheet(L, ref), clocks_sheet(L, ref), power_sheet(L, ref)]
    emit('proto-carrier', f'{VIDEO}/kicad/proto-carrier', sheets, 'OO-1 Video Card: prototype carrier',
         ['OO-1 video card, prototype carrier: an iCESugar-Pro module (LFE5U-25F, 32 MB SDRAM) in a DDR2 SODIMM-200\n'
          'socket, with the OO-1 bus interface, DVI and VGA outputs and clocks. See hw/video-card-hw.md.',
          'Generated by hw/video/tools/gen_kicad.py as a starting point: every pin has a stub and a label.\n'
          'Arrange and wire it by hand from here; KiCad then owns these files.'])

    ref2 = Refs()
    d_sheets = [bus_sheet(L, ref2), video_sheet(L, ref2), clocks_sheet(L, ref2)]
    fpga = Sheet(L, 'FPGA', 'fpga.kicad_sch', paper='A2', comment='LFE5U-25F-6BG256C, chip-down: to come')
    fpga.text('To come: the LFE5U-25F-6BG256C (or 45F), its banks at 3.3 V, 1.1 V core and 2.5 V VCCAUX,\n'
              'configuration (SPI flash W25Q128, JTAG), and the 25 MHz system clock.\n'
              'KiCad 7 has no LFE5U-25F caBGA-256 symbol: build it from the Lattice pinout (caBGA-256 migration CSV).', (30, 30))
    sd = Sheet(L, 'SDRAM', 'sdram.kicad_sch', paper='A3', comment='Two 16-bit SDRAMs on a 32-bit bus')
    sd.text('To come: two MT48LC16M16A2TG / IS42S16160 / W9825G6KH (32 MB, x16, TSOP-54) sharing address,\n'
            'bank, clock and command lines, with separate DQ0-15 and DQ16-31: a 32-bit bus, about 300 MB/s or more.', (30, 30))
    pw = Sheet(L, 'Power', 'power.kicad_sch', paper='A4', comment='Buck 3.3 V, 2.5 V and 1.1 V LDOs: to come')
    pw.text('To come: 5 V from the bus; a 3.3 V buck; 2.5 V (VCCAUX) and 1.1 V (core) regulators; sequencing per\n'
            'the ECP5 hardware checklist (FPGA-TN-02038).', (30, 30))
    emit('direct', f'{VIDEO}/kicad/direct', d_sheets + [fpga, sd, pw], 'OO-1 Video Card: direct board',
         ['OO-1 video card, direct board: the ECP5 and two SDRAMs (32-bit) soldered down. Skeleton: the bus, video\n'
          'and clock sheets are the carrier\'s; the FPGA, SDRAM and power sheets are to come.'])
    print('ok')


if __name__ == '__main__':
    main()
