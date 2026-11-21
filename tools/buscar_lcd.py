#!/usr/bin/env python3
"""Localiza la rutina real de inicializacion/escritura del LCD (puertos 00C0/00C2)
y el contexto de codigo alrededor, para determinar como se selecciona cada uno
de los dos controladores KS0066 (CS) frente a la señal RS (instruccion/dato).

Busca:
  - Todas las referencias directas a los puertos C0h/C2h/C1h/C3h (OUT (n),A /
    IN A,(n) / OUT0 / IN0) en el espacio completo de la ROM alcanzable desde
    el reset (0000) y desde el punto de inicializacion de pantalla conocido.
  - Para cada acceso, vuelca una ventana de codigo alrededor (contexto) para
    ver si hay una escritura a OTRO puerto justo antes/despues que pudiera
    ser la seleccion de controlador (CS), y si A080-style loops tipicos de
    "espera busy flag" aparecen.
  - Tambien busca accesos via "OUT (C),r" con el valor de C fijado cerca
    (LD C,n) para no perder selecciones dinamicas de puerto.

Uso: python3 buscar_lcd.py ROM.bin > salida.txt
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z180dis as Z

rom = open(sys.argv[1], 'rb').read()
mem = lambda a: rom[a & 0xFFFF]

def dis_range(lo, hi):
    out = {}
    x = lo
    while x < hi:
        ins = Z.decode(mem, x)
        n = ins[0]
        out[x] = ins
        x += n
    return out

# 1) Disassemble linearly from reset and from a generous window around the
#    known MMU-setup/reset code (0x23EC area) and the whole low ROM, to get
#    a full instruction list covering most of the firmware (same approach
#    as previous scripts: linear sweep + flow-based trace merged).
entries = [0x0000, 0x0150, 0x046B, 0x0769, 0x078F, 0x0CF6, 0x1AC5,
           0x23EC, 0x2400, 0x24EB, 0x2570, 0x2682, 0x0E08]
traced = Z.trace(mem, entries, lo=0, hi=0xFFFF)

# Also do a plain linear sweep over the whole addressable ROM window actually
# used by code (0000-3FFF covers all routines referenced in prior findings)
# to catch anything the recursive trace missed (data embedded as code refs).
linear = dis_range(0x0000, 0xFFFF)

all_instr = {}
all_instr.update(linear)
all_instr.update(traced)

ports_of_interest = {0xC0, 0xC1, 0xC2, 0xC3}

hits = []
for addr in sorted(all_instr):
    n, t, fl, _ = all_instr[addr]
    m = re.search(r'\(([0-9A-Fa-f]{1,2})h\)', t)
    if m and ('OUT' in t or 'IN' in t) and 'OUT0' not in t and 'IN0' not in t:
        p = int(m.group(1), 16)
        if p in ports_of_interest:
            hits.append((addr, n, t))

print(f"=== Accesos directos a puertos C0-C3h (OUT (n),A / IN A,(n)) ===")
for addr, n, t in hits:
    print(f"{addr:04X}  {t}")
print(f"Total: {len(hits)}")
print()

def context(addr, before=24, after=24):
    # find window of instruction addresses around 'addr' using linear map
    keys = sorted(linear)
    idx = keys.index(addr) if addr in keys else None
    if idx is None:
        return
    lo = max(0, idx - before)
    hi = min(len(keys), idx + after)
    for k in keys[lo:hi]:
        n, t, fl, _ = linear[k]
        raw = ' '.join(f'{mem(k+i):02X}' for i in range(n))
        mark = '>>' if k == addr else '  '
        print(f"{k:04X} {mark} {raw:<11} {t}")
    print()

print("=== Contexto (24 instrucciones antes/despues) de cada acceso ===")
for addr, n, t in hits:
    print(f"--- contexto de {addr:04X}: {t} ---")
    context(addr)

# 2) Also look for "OUT (C),r" near "LD C,n" with n in C0-C3, within the
#    linear sweep, in case the firmware uses dynamic port addressing for LCD.
print("=== Busqueda de OUT (C),r precedido de LD C,<C0..C3> cerca ===")
keys = sorted(linear)
for i, addr in enumerate(keys):
    n, t, fl, _ = linear[addr]
    if t.startswith('OUT (C),'):
        # look back up to 6 instructions for LD C,n
        for j in range(max(0, i - 6), i):
            a2 = keys[j]
            n2, t2, fl2, _ = linear[a2]
            mm = re.match(r'LD C,([0-9A-Fa-f]{2})h', t2)
            if mm and int(mm.group(1), 16) in ports_of_interest:
                print(f"{a2:04X} {t2}  ...  {addr:04X} {t}")

print()
print("=== Busqueda de posibles bits de chip-select cerca de 00C0/00C2 ===")
print("(escrituras a OTROS puertos de salida dentro de +/- 10 instrucciones")
print(" de cada acceso C0/C2, candidatas a ser la linea de seleccion CS)")
for addr, n, t in hits:
    if addr not in keys:
        continue
    idx = keys.index(addr)
    for j in range(max(0, idx - 10), min(len(keys), idx + 10)):
        if j == idx:
            continue
        a2 = keys[j]
        n2, t2, fl2, _ = linear[a2]
        m2 = re.search(r'OUT \(([0-9A-Fa-f]{1,2})h\),A', t2)
        if m2 and int(m2.group(1), 16) not in ports_of_interest:
            print(f"cerca de {addr:04X} ({t}): {a2:04X} {t2}")
