#!/usr/bin/env python3
"""Enlace Z180<->PIC (ASCI1): lectura de 4 bytes al arranque y uso de D011/D013;
tabla de velocidades (llamadores de 078F). Mecanismo ligero.
Uso: python3 buscar_pic.py ROM.bin > salida.txt
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

def baud(cntlb, phi=3_072_000):
    ps = 30 if (cntlb >> 5) & 1 else 10
    dr = 64 if (cntlb >> 3) & 1 else 16
    ss = cntlb & 7
    return "ext" if ss == 7 else f"{phi/(ps*dr*(1<<ss)):.0f}"

print("=== ASCI1 lectura (062F-0664) ===")
dis(0x062F, 0x0664)
print("\n=== Lectura de 4 bytes al arranque (243C-2460) ===")
dis(0x243C, 0x2460)
print("\n=== Vectores serie bajos (0150-0190) ===")
dis(0x0150, 0x0190)
print("\n=== Region 1F80-1FB0 (cfg ASCI0 / baud) ===")
dis(0x1F80, 0x1FB0)

# Referencias a D011 / D013 (que significan los 4 bytes del PIC)
print("\n=== Referencias a D011h / D013h ===")
x = 0
while x < 0xFFF0:
    n, t, _, _ = Z.decode(mem, x)
    if 'D011h' in t or 'D013h' in t:
        print(f"  {x:04X}  {t}")
    x += n

# Llamadores de 078F (cfg ASCI0) con L inmediato = codigo de baud
print("\n=== Llamadores de 078F (cfg baud ASCI0) con valor L ===")
x = 0
while x < 0xFFF0:
    n, t, fl, _ = Z.decode(mem, x)
    for k, tg in fl:
        if k in ('call', 'jp') and tg == 0x078F:
            lv = None
            for b in (2, 3, 4, 5):
                if mem(x - b) == 0x2E: lv = mem(x - b + 1); break
            if lv is not None:
                print(f"  {x:04X} -> 078F  L={lv:02X}h  baud~{baud(lv)} (PHI/2) | {baud(lv,6_144_000)} (PHI)")
            else:
                print(f"  {x:04X} -> 078F  (L calculado)")
    x += n

# Cadenas de velocidades/numeros cerca de 'Speed'
print("\n=== Cadenas con numeros de baud ===")
for kk in [b'300', b'1200', b'2400', b'4800', b'9600', b'19200', b'38400', b'57600', b'115200']:
    for mo in re.finditer(re.escape(kk), rom):
        s = mo.start()
        pre = rom[max(0,s-1)]
        post = rom[s+len(kk)] if s+len(kk) < len(rom) else 0
        if not (0x30 <= pre <= 0x39) and not (0x30 <= post <= 0x39):  # numero aislado
            ss = s
            while ss > 0 and 0x20 <= rom[ss-1] < 0x7f: ss -= 1
            e = s
            while e < len(rom) and 0x20 <= rom[e] < 0x7f: e += 1
            if 3 <= e-ss < 40:
                print(f"  {s:06X}: {rom[ss:e].decode('ascii','replace')!r}")
                break
