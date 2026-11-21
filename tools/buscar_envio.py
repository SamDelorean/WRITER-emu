#!/usr/bin/env python3
"""Traza el despacho de envio/impresion (1B0E) y la ruta de recepcion
'Please download' en The Writer. Mecanismo ligero.
Uso: python3 buscar_envio.py ROM.bin > salida.txt
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
    if ss == 7: return "ext"
    return f"{phi/(ps*dr*(1<<ss)):.0f}"

# 1) Llamadores de rutinas serie clave, con L inmediato previo (parametro)
KEY = {0x0769: 'TX byte (TDR0)', 0x078F: 'cfg ASCI0 (CNTLB0=L baud)',
       0x0722: 'RX getc (ring D600)', 0x0713: 'RX disponible'}
print("=== Llamadores de rutinas serie ===")
x = 0
while x < 0xFFF0:
    n, t, fl, _ = Z.decode(mem, x)
    for k, tg in fl:
        if k in ('call', 'jp') and tg in KEY:
            extra = ""
            # LD L,n (2E nn) justo antes?
            if mem(x - 2) == 0x2E:
                lv = mem(x - 1)
                extra = f"  L={lv:02X}h"
                if tg == 0x078F:
                    extra += f"  baud~{baud(lv)} (PHI/2)"
            print(f"  {x:04X} -> {KEY[tg]}{extra}")
    x += n

# 2) Despacho de envio/impresion alrededor de 1B0E
print("\n=== Despacho de envio (1AE0-1B80) ===")
dis(0x1AE0, 0x1B80, mark=(0x1B0E,))

# 3) Ruta tras mostrar 'Please download' (2504-25C0) y sub-rutinas 0E08/2682
print("\n=== Ruta 'Please download' (24EB-2570) ===")
dis(0x24EB, 0x2570, mark=(0x2504, 0x2510, 0x2523))
print("\n=== Subrutina 2682 (post-download) ===")
dis(0x2682, 0x2700)
print("\n=== Subrutina 0E08 ===")
dis(0x0E08, 0x0E62)

# 4) Cadenas de menu de puerto / ajustes de comunicacion
print("\n=== Cadenas de ajustes de comunicacion ===")
claves = [b'Port', b'Baud', b'baud', b'Delay', b'IR ', b'Transfer', b'transfer',
          b'Receive', b'Send', b'Protocol', b'bps', b'Speed', b'speed', b'Flow']
seen = set()
for kk in claves:
    for mo in re.finditer(re.escape(kk), rom):
        s = mo.start()
        while s > 0 and 0x20 <= rom[s-1] < 0x7f: s -= 1
        if s in seen: continue
        seen.add(s)
        e = s
        while e < len(rom) and 0x20 <= rom[e] < 0x7f: e += 1
        if 4 <= e - s < 80:
            print(f"  {s:06X}: {rom[s:e].decode('ascii','replace')!r}")
