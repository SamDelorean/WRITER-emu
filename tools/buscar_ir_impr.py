#!/usr/bin/env python3
"""IR (conmutacion 0777 / 'IR Delay') e impresion (Send-to-Computer vs Print,
secuencias de control, impresoras) en The Writer. Mecanismo ligero.
Uso: python3 buscar_ir_impr.py ROM.bin > salida.txt
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

def callers(targets, label):
    print(f"\n=== Llamadores de {label} ===")
    x = 0
    while x < 0xFFF0:
        n, t, fl, _ = Z.decode(mem, x)
        for k, tg in fl:
            if k in ('call', 'jp') and tg in targets:
                print(f"  {x:04X}  {t}")
        x += n

# IR: rutina 0777 (conmuta ASEXT0) y sus llamadores (directo y via vector 0170)
print("=== IR: rutina de conmutacion 0777-07B7 ===")
dis(0x0777, 0x07B8, mark=(0x0777,))
callers({0x0777, 0x0170}, "conmutacion IR (0777 / vector 0170)")

# Envio por paquetes: rutinas 1AC5/1ACD/1AE0 y su cabecera; llamadores
print("\n=== Envio por paquetes (1AC0-1B40) ===")
dis(0x1AC0, 0x1B40, mark=(0x1AE0, 0x1B0A))
callers({0x1AC5, 0x1ACD, 0x1AE0}, "envio de paquete (1AC5/1ACD/1AE0)")

# Secuencias de control: regiones con varios 0x1B cercanos (posible tabla de impresora)
print("\n=== Regiones con multiples ESC (posible tabla de control/impresora) ===")
i = 0
while i < len(rom) - 64:
    window = rom[i:i+64]
    if window.count(0x1B) >= 2:
        # volcar 48 bytes legibles
        txt = ''.join(chr(c) if 0x20 <= c < 0x7f else ('.' if c != 0x1B else '[ESC]') for c in rom[i:i+48])
        print(f"  {i:06X}: {txt}")
        i += 64
    else:
        i += 1

# Cadenas de impresora / envio
print("\n=== Cadenas de impresion/envio/modelos ===")
claves = [b'Send to', b'Computer', b'Printer', b'Print ', b'Epson', b'Canon', b'HP',
          b'IBM', b'Star', b'Okidata', b'Brother', b'Panasonic', b'Citizen',
          b'LaserJet', b'DeskJet', b'Stylus', b'PostScript', b'PCL', b'Draft',
          b'Letter', b'Pitch', b'Font', b'margin', b'Margin', b'Receive', b'Infrared', b'IR']
seen = set()
for kk in claves:
    for mo in re.finditer(re.escape(kk), rom):
        s = mo.start()
        while s > 0 and 0x20 <= rom[s-1] < 0x7f: s -= 1
        if s in seen: continue
        seen.add(s)
        e = s
        while e < len(rom) and 0x20 <= rom[e] < 0x7f: e += 1
        if 4 <= e - s < 70:
            print(f"  {s:06X}: {rom[s:e].decode('ascii','replace')!r}")
