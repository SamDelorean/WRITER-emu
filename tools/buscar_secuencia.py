#!/usr/bin/env python3
"""Finds how the firmware reaches the 'Enter Master Reset Password' routine:
callers of that routine and the key-read primitive it uses.
Uso: python3 buscar_secuencia.py ROM.bin > salida.txt
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z180dis as Z

rom = open(sys.argv[1], 'rb').read()
mem = lambda a: rom[a & 0xFFFF]            # ventana fisica 00000

def dis_range(lo, hi, mark=()):
    x = lo
    while x < hi:
        n, t, _, _ = Z.decode(mem, x)
        print(f'{x:04X}{" >>" if x in mark else "   "} {t}')
        x += n

# 1. The master-reset routine prints "Enter Master Reset Password" at 2481.
#    Find the routine entry (first byte reached by a CALL) and its callers.
MR_LO, MR_HI = 0x2440, 0x2490
seen = Z.trace(mem, [0x0000, 0x2557, 0x2563])   # boot + the two post-check targets
callers = {}
for a, (n, t, fl, words) in seen.items():
    for kind, tg in fl:
        if kind in ('call', 'jp') and MR_LO <= tg <= MR_HI:
            callers.setdefault(tg, []).append((a, kind, t))

print('=== Callers de la rutina de contrasena maestra (2440-2490) ===')
if not callers:
    print('  (ninguno en el trazado de arranque; se llega por el bucle principal)')
for tg in sorted(callers):
    print(f'  -> {tg:04X}:')
    for a, kind, t in callers[tg]:
        print(f'     {a:04X} {t}')

# 2. Linear sweep of whole ROM physical 00000 window for CALL/JP into that routine
print('\n=== Barrido lineal: CALL/JP hacia 2440-2490 en ventana 0000-FFFF ===')
x = 0
while x < 0xFFF0:
    n, t, fl, _ = Z.decode(mem, x)
    for kind, tg in fl:
        if kind in ('call', 'jp') and MR_LO <= tg <= MR_HI:
            print(f'  {x:04X} {t}')
    x += 1  # byte-by-byte to catch all alignments

# 3. Key-read primitive: 062F is called repeatedly to read password chars.
print('\n=== Primitiva de lectura de tecla (062F) ===')
dis_range(0x062F, 0x06A0)

# 4. Dispatcher around boot: after MMU setup (2438) where does it go?
print('\n=== Flujo tras configurar MMU (2438..24A8) ===')
dis_range(0x2438, 0x24A8, mark=(0x2460, 0x2481, 0x24B5, 0x2523))

# 5. The compare used for the master key (278B) and reinit (22F3)
print('\n=== Comparador de buffer 278B ===')
dis_range(0x278B, 0x27C1)
print('\n=== Reinit RAM 22F3 ===')
dis_range(0x22F3, 0x2360)
