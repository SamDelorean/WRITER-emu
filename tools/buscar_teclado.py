#!/usr/bin/env python3
"""Decodes the Writer keyboard path: serial/PIC link (ASCI) vs matrix,
the compare routine for the master-reset key patterns, and any scancode table.
Uso: python3 buscar_teclado.py ROM.bin > salida.txt
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z180dis as Z

rom = open(sys.argv[1], 'rb').read()
mem = lambda a: rom[a & 0xFFFF]

Z180 = {0x00: 'CNTLA0', 0x01: 'CNTLA1', 0x02: 'CNTLB0', 0x03: 'CNTLB1',
        0x04: 'STAT0', 0x05: 'STAT1', 0x06: 'TDR0', 0x07: 'TDR1',
        0x08: 'RDR0', 0x09: 'RDR1', 0x0A: 'CNTR', 0x0B: 'TMDR0L',
        0x12: 'ASEXT0', 0x13: 'ASEXT1', 0x1E: 'ASTC0L', 0x1F: 'ASTC0H',
        0x32: 'DCNTL', 0x33: 'IL', 0x34: 'ITC', 0x36: 'RCR',
        0x38: 'CBR', 0x39: 'BBR', 0x3A: 'CBAR', 0x3F: 'ICR'}

def ann(t):
    m = re.search(r'\((0x[0-9A-Fa-f]{1,2})h?\)', t)
    if m:
        p = int(m.group(1), 16)
        if p in Z180 and ('IN0' in t or 'OUT0' in t):
            return t + f'   ; {Z180[p]}'
    return t

def dis(lo, hi, mark=()):
    x = lo
    while x < hi:
        n, t, _, _ = Z.decode(mem, x)
        raw = ' '.join(f'{mem(x+i):02X}' for i in range(n))
        print(f'{x:04X}{" >>" if x in mark else "   "} {raw:<11} {ann(t)}')
        x += n

for name, lo, hi in [
    ('046B init teclado/ASCI', 0x046B, 0x0530),
    ('0CF6 comparador de patron de teclas', 0x0CF6, 0x0D60),
    ('0CBE (chequeo en 2478)', 0x0CBE, 0x0CF6),
    ('214B (CALL en 243C)', 0x214B, 0x21C0),
    ('0400 (OUT (C) externo)', 0x0400, 0x0470),
    ('0680 lectura/traduccion de tecla', 0x0680, 0x0760),
    ('0812 imprime en LCD?', 0x0812, 0x0880),
]:
    print(f'\n=== {name} ===')
    dis(lo, hi)

# Patrones de teclas: 24 bytes en 23AC y 23BC
print('\n=== Patrones de combinacion (fisica) ===')
for a in (0x23AC, 0x23BC, 0x23A5):
    print(f'  {a:04X}: ' + ' '.join(f'{rom[a+i]:02X}' for i in range(24)))

# Busqueda de posible tabla de scancode->ASCII (cadenas largas imprimibles raras)
print('\n=== Posibles tablas de teclado (>=24 bytes imprimibles con letras) ===')
i = 0
while i < len(rom) - 24:
    j = i
    while j < len(rom) and 0x20 <= rom[j] < 0x7F:
        j += 1
    if j - i >= 24:
        chunk = rom[i:j]
        letters = sum(c.isalpha() for c in chunk.decode('ascii', 'ignore'))
        if letters >= 12 and b' ' not in chunk[:20]:
            print(f'  {i:06X} ({j-i}b): {chunk[:48].decode("ascii","replace")!r}')
    i = max(j, i + 1)
