#!/usr/bin/env python3
"""Minimal Z80/Z180 disassembler (original code, BSD-3-Clause).
Library: decode(mem, addr) -> (length, text, targets, operand_words)
mem is a callable mem(addr)->byte for the 16-bit logical space.
"""
R = ['B', 'C', 'D', 'E', 'H', 'L', '(HL)', 'A']
RP = ['BC', 'DE', 'HL', 'SP']
RP2 = ['BC', 'DE', 'HL', 'AF']
CC = ['NZ', 'Z', 'NC', 'C', 'PO', 'PE', 'P', 'M']
ALU = ['ADD A,', 'ADC A,', 'SUB ', 'SBC A,', 'AND ', 'XOR ', 'OR ', 'CP ']
ROT = ['RLC', 'RRC', 'RL', 'RR', 'SLA', 'SRA', 'SLL', 'SRL']
X0Z7 = ['RLCA', 'RRCA', 'RLA', 'RRA', 'DAA', 'CPL', 'SCF', 'CCF']
BLI = {(4, 0): 'LDI', (4, 1): 'CPI', (4, 2): 'INI', (4, 3): 'OUTI',
       (5, 0): 'LDD', (5, 1): 'CPD', (5, 2): 'IND', (5, 3): 'OUTD',
       (6, 0): 'LDIR', (6, 1): 'CPIR', (6, 2): 'INIR', (6, 3): 'OTIR',
       (7, 0): 'LDDR', (7, 1): 'CPDR', (7, 2): 'INDR', (7, 3): 'OTDR'}
Z180_ED = {0x83: 'OTIM', 0x8B: 'OTDM', 0x93: 'OTIMR', 0x9B: 'OTDMR', 0x76: 'SLP'}


def s8(v):
    return v - 256 if v > 127 else v


def decode(mem, a):
    """Returns (len, text, flow_targets, words) ; flow: list of ('jp'|'call'|'ret'|'end', addr)"""
    b = mem(a)
    w = lambda o: mem(a + o) | (mem(a + o + 1) << 8)
    if b in (0xDD, 0xFD):
        return decode_idx(mem, a, 'IX' if b == 0xDD else 'IY')
    if b == 0xCB:
        o = mem(a + 1); x, y, z = o >> 6, (o >> 3) & 7, o & 7
        t = f'{ROT[y]} {R[z]}' if x == 0 else f"{['', 'BIT', 'RES', 'SET'][x]} {y},{R[z]}"
        return 2, t, [], []
    if b == 0xED:
        return decode_ed(mem, a)
    x, y, z = b >> 6, (b >> 3) & 7, b & 7
    p, q = y >> 1, y & 1
    if x == 0:
        if z == 0:
            if y == 0: return 1, 'NOP', [], []
            if y == 1: return 1, "EX AF,AF'", [], []
            t = (a + 2 + s8(mem(a + 1))) & 0xFFFF
            if y == 2: return 2, f'DJNZ {t:04X}h', [('jp', t)], []
            if y == 3: return 2, f'JR {t:04X}h', [('jp', t), ('end', 0)], []
            return 2, f'JR {CC[y-4]},{t:04X}h', [('jp', t)], []
        if z == 1:
            if q == 0: return 3, f'LD {RP[p]},{w(1):04X}h', [], [w(1)]
            return 1, f'ADD HL,{RP[p]}', [], []
        if z == 2:
            if q == 0:
                return [(1, 'LD (BC),A', [], []), (1, 'LD (DE),A', [], []),
                        (3, f'LD ({w(1):04X}h),HL', [], [w(1)]), (3, f'LD ({w(1):04X}h),A', [], [w(1)])][p]
            return [(1, 'LD A,(BC)', [], []), (1, 'LD A,(DE)', [], []),
                    (3, f'LD HL,({w(1):04X}h)', [], [w(1)]), (3, f'LD A,({w(1):04X}h)', [], [w(1)])][p]
        if z == 3: return 1, f"{'INC' if q == 0 else 'DEC'} {RP[p]}", [], []
        if z == 4: return 1, f'INC {R[y]}', [], []
        if z == 5: return 1, f'DEC {R[y]}', [], []
        if z == 6: return 2, f'LD {R[y]},{mem(a+1):02X}h', [], []
        return 1, X0Z7[y], [], []
    if x == 1:
        if b == 0x76: return 1, 'HALT', [], []
        return 1, f'LD {R[y]},{R[z]}', [], []
    if x == 2:
        return 1, f'{ALU[y]}{R[z]}', [], []
    if z == 0: return 1, f'RET {CC[y]}', [], []
    if z == 1:
        if q == 0: return 1, f'POP {RP2[p]}', [], []
        return [(1, 'RET', [('end', 0)], []), (1, 'EXX', [], []),
                (1, 'JP (HL)', [('end', 0)], []), (1, 'LD SP,HL', [], [])][p]
    if z == 2: return 3, f'JP {CC[y]},{w(1):04X}h', [('jp', w(1))], [w(1)]
    if z == 3:
        if y == 0: return 3, f'JP {w(1):04X}h', [('jp', w(1)), ('end', 0)], [w(1)]
        if y == 2: return 2, f'OUT ({mem(a+1):02X}h),A', [], []
        if y == 3: return 2, f'IN A,({mem(a+1):02X}h)', [], []
        return 1, ['', '', '', '', 'EX (SP),HL', 'EX DE,HL', 'DI', 'EI'][y], [], []
    if z == 4: return 3, f'CALL {CC[y]},{w(1):04X}h', [('call', w(1))], [w(1)]
    if z == 5:
        if q == 0: return 1, f'PUSH {RP2[p]}', [], []
        if p == 0: return 3, f'CALL {w(1):04X}h', [('call', w(1))], [w(1)]
    if z == 6: return 2, f'{ALU[y]}{mem(a+1):02X}h', [], []
    if z == 7: return 1, f'RST {y*8:02X}h', [('call', y * 8)], []
    return 1, f'DB {b:02X}h', [], []


def decode_ed(mem, a):
    o = mem(a + 1)
    w = lambda k: mem(a + k) | (mem(a + k + 1) << 8)
    x, y, z = o >> 6, (o >> 3) & 7, o & 7
    p, q = y >> 1, y & 1
    if o in Z180_ED: return 2, Z180_ED[o], [], []
    if x == 0:  # Z180: IN0 / OUT0 / TST
        if z == 0: return 3, f'IN0 {R[y] if y != 6 else "F"},({mem(a+2):02X}h)', [], []
        if z == 1: return 3, f'OUT0 ({mem(a+2):02X}h),{R[y]}', [], []
        if z == 4: return 2, f'TST {R[y]}', [], []
    if o == 0x64: return 3, f'TST {mem(a+2):02X}h', [], []
    if o == 0x74: return 3, f'TSTIO {mem(a+2):02X}h', [], []
    if x == 1:
        if z == 0: return 2, f'IN {R[y] if y != 6 else "F"},(C)', [], []
        if z == 1: return 2, f'OUT (C),{R[y] if y != 6 else "0"}', [], []
        if z == 2: return 2, f"{'SBC' if q == 0 else 'ADC'} HL,{RP[p]}", [], []
        if z == 3:
            if q == 0: return 4, f'LD ({w(2):04X}h),{RP[p]}', [], [w(2)]
            return 4, f'LD {RP[p]},({w(2):04X}h)', [], [w(2)]
        if z == 4:
            if q == 1: return 2, f'MLT {RP[p]}', [], []
            return 2, 'NEG', [], []
        if z == 5: return 2, 'RETI' if y == 1 else 'RETN', [('end', 0)], []
        if z == 6: return 2, f'IM {[0, 0, 1, 2, 0, 0, 1, 2][y]}', [], []
        return 2, ['LD I,A', 'LD R,A', 'LD A,I', 'LD A,R', 'RRD', 'RLD', 'NOP', 'NOP'][y], [], []
    if x == 2 and (y, z) in BLI: return 2, BLI[(y, z)], [], []
    return 2, f'DB EDh,{o:02X}h', [], []


def decode_idx(mem, a, ix):
    o = mem(a + 1)
    d = s8(mem(a + 2))
    ds = f'({ix}{d:+d})'
    w = lambda k: mem(a + k) | (mem(a + k + 1) << 8)
    if o == 0xCB:
        op = mem(a + 3); x, y, z = op >> 6, (op >> 3) & 7, op & 7
        t = f'{ROT[y]} {ds}' if x == 0 else f"{['', 'BIT', 'RES', 'SET'][x]} {y},{ds}"
        return 4, t, [], []
    if o == 0x21: return 4, f'LD {ix},{w(2):04X}h', [], [w(2)]
    if o == 0x22: return 4, f'LD ({w(2):04X}h),{ix}', [], [w(2)]
    if o == 0x2A: return 4, f'LD {ix},({w(2):04X}h)', [], [w(2)]
    if o == 0x23: return 2, f'INC {ix}', [], []
    if o == 0x2B: return 2, f'DEC {ix}', [], []
    if o in (0x09, 0x19, 0x29, 0x39): return 2, f"ADD {ix},{['BC', 'DE', ix, 'SP'][o >> 4]}", [], []
    if o == 0x34: return 3, f'INC {ds}', [], []
    if o == 0x35: return 3, f'DEC {ds}', [], []
    if o == 0x36: return 4, f'LD {ds},{mem(a+3):02X}h', [], []
    if o == 0xE1: return 2, f'POP {ix}', [], []
    if o == 0xE5: return 2, f'PUSH {ix}', [], []
    if o == 0xE9: return 2, f'JP ({ix})', [('end', 0)], []
    if o == 0xE3: return 2, f'EX (SP),{ix}', [], []
    if o == 0xF9: return 2, f'LD SP,{ix}', [], []
    x, y, z = o >> 6, (o >> 3) & 7, o & 7
    if x == 1 and o != 0x76 and (y == 6 or z == 6):
        return 3, f"LD {ds if y == 6 else R[y]},{ds if z == 6 else R[z]}", [], []
    if x == 2 and z == 6:
        return 3, f'{ALU[y]}{ds}', [], []
    n, t, f, ws = decode(mem, a + 1)  # prefix ignored
    return 1 + n, t, f, ws


def trace(mem, entries, lo=0, hi=0xFFFF):
    """Recursive-descent: returns dict addr -> (len, text, targets, words)"""
    seen, todo = {}, list(entries)
    while todo:
        a = todo.pop()
        while lo <= a <= hi and a not in seen:
            ins = decode(mem, a)
            seen[a] = ins
            n, t, fl, _ = ins
            stop = False
            for kind, tg in fl:
                if kind in ('jp', 'call') and lo <= tg <= hi:
                    todo.append(tg)
                if kind == 'end':
                    stop = True
            if stop:
                break
            a = (a + n) & 0xFFFF
    return seen
