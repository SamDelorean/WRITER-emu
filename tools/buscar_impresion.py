#!/usr/bin/env python3
"""Analiza impresion y baud de ASCI0 en The Writer:
- escrituras a CNTLB0/CNTLA0 (baud de ASCI0),
- primitiva TX (TDR0 en 0773) y sus llamadores,
- cadenas de impresora / modelos / secuencias de control.
Uso: python3 buscar_impresion.py ROM.bin > salida.txt
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z180dis as Z

rom = open(sys.argv[1], 'rb').read()
mem = lambda a: rom[a & 0xFFFF]

def dis(lo, hi, mark=()):
    x = lo
    while x < hi:
        n, t, _, _ = Z.decode(mem, x)
        raw = ' '.join(f'{mem(x+i):02X}' for i in range(n))
        print(f'{x:04X}{" >>" if x in mark else "   "} {raw:<11} {t}')
        x += n

def baud(cntlb, phi):
    ps = 30 if (cntlb >> 5) & 1 else 10
    dr = 64 if (cntlb >> 3) & 1 else 16
    ss = cntlb & 7
    if ss == 7: return "ext"
    return f"{phi/(ps*dr*(1<<ss)):.0f}"

# 1) CNTLB0 (port 02) y CNTLA0 (port 00): buscar OUT0 con inmediato previo
print("=== Init ASCI0: OUT0 a CNTLB0(02)/CNTLA0(00)/ASEXT0(12) con valor ===")
x = 0
while x < 0xFFF0:
    n, t, _, _ = Z.decode(mem, x)
    m = re.match(r'OUT0 \(([0-9A-Fa-f]{2})h\),A', t)
    if m and int(m.group(1), 16) in (0x00, 0x02, 0x12):
        val = None
        for b in (2, 3, 4, 5):
            if mem(x - b) == 0x3E: val = mem(x - b + 1); break
        reg = {0x00: 'CNTLA0', 0x02: 'CNTLB0', 0x12: 'ASEXT0'}[int(m.group(1), 16)]
        vs = f"{val:02X}h" if val is not None else "(compuesto)"
        line = f"  {x:04X}  {reg} = {vs}"
        if reg == 'CNTLB0' and val is not None:
            line += f"   baud: {baud(val,3_072_000)} (PHI/2) | {baud(val,6_144_000)} (PHI)"
        print(line)
    x += n

# 2) Primitiva TX (TDR0 en 0773) y su contexto + llamadores
print("\n=== Primitiva TX (region 0760-07E0) ===")
dis(0x0760, 0x07E0, mark=(0x0773,))
# hallar la rutina que contiene 0773 (primer CALL que apunte a su inicio)
print("\n=== CALL a rutinas de TX (0740-07E0) ===")
x = 0
while x < 0xFFF0:
    n, t, fl, _ = Z.decode(mem, x)
    for k, tg in fl:
        if k == 'call' and 0x0740 <= tg <= 0x07E0:
            print(f"  {x:04X} -> {t}")
    x += n

# 3) Cadenas de impresora / modelos / control
print("\n=== Cadenas de impresora y control ===")
claves = [b'rint', b'Epson', b'EPSON', b'Canon', b'HP ', b'LaserJet', b'DeskJet',
          b'IBM', b'Proprinter', b'ESC/P', b'draft', b'Draft', b'NLQ', b'baud',
          b'Baud', b'9600', b'19200', b'2400', b'1200', b'Send to', b'Computer',
          b'Printer', b'port', b'Port', b'parallel', b'Serial', b'IR', b'nfrared']
seen = set()
for k in claves:
    for mo in re.finditer(re.escape(k), rom):
        p = mo.start(); s = p
        while s > 0 and 0x20 <= rom[s-1] < 0x7f: s -= 1
        if s in seen: continue
        seen.add(s)
        e = s
        while e < len(rom) and 0x20 <= rom[e] < 0x7f: e += 1
        if e - s >= 4:
            print(f"  {s:06X}: {rom[s:e][:72].decode('ascii','replace')!r}")

# 4) Secuencias ESC (0x1B) seguidas de ASCII imprimible (posibles comandos de impresora)
print("\n=== Secuencias ESC (1B ..) en la ROM (primeras 40) ===")
cnt = 0
for i in range(len(rom) - 3):
    if rom[i] == 0x1B and 0x20 <= rom[i+1] < 0x7f and 0x20 <= rom[i+2] < 0x7f:
        print(f"  {i:06X}: 1B {rom[i+1]:02X} {rom[i+2]:02X}  ESC '{chr(rom[i+1])}{chr(rom[i+2])}'")
        cnt += 1
        if cnt >= 40: break
