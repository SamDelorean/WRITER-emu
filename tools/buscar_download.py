#!/usr/bin/env python3
"""Locates the 'Please download' / 'Master Reset Password' messages in the
Writer ROM and the code that references them. Output only to stdout/files
(analysis notes, no ROM data is published by this tool).
Uso: python3 buscar_download.py ROM.bin carpeta_salida
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z180dis as Z

rom = open(sys.argv[1], 'rb').read()
out = sys.argv[2]; os.makedirs(out, exist_ok=True)
CLAVES = [b'download', b'corrupt', b'master reset', b'password', b'checksum', b'memory']
WINDOWS = {'phys00000': 0x00000, 'physC0000': 0xC0000}   # reset window / final MMU window
SEEDS = [0x0000, 0x0008, 0x0010, 0x0018, 0x0020, 0x0028, 0x0030, 0x0038, 0x0066, 0x23EC, 0x242A]
rep = open(os.path.join(out, 'reporte_download.txt'), 'w')
P = lambda *a: print(*a, file=rep)

def string_at(p):
    s = p
    while s > 0 and 0x20 <= rom[s - 1] < 0x7F: s -= 1
    e = p
    while e < len(rom) and 0x20 <= rom[e] < 0x7F: e += 1
    return s, rom[s:e].decode('ascii', 'replace')

# 1. strings
P('=== 1. Mensajes encontrados (direccion fisica) ===')
strings = {}
low = rom.lower()
for k in CLAVES:
    for m in re.finditer(re.escape(k), low):
        s, txt = string_at(m.start())
        strings[s] = txt
for s in sorted(strings):
    P(f'  {s:06X}: "{strings[s][:90]}"')

# 2. per window: trace + references
for wname, base in WINDOWS.items():
    mem = lambda a, b=base: rom[(b + (a & 0xFFFF)) % len(rom)]
    seen = Z.trace(mem, SEEDS)
    with open(os.path.join(out, f'listado_{wname}.txt'), 'w') as f:
        for a in sorted(seen):
            n, t, _, _ = seen[a]
            f.write(f'{a:04X}  {" ".join(f"{mem(a+i):02X}" for i in range(n)):<12} {t}\n')
    P(f'\n=== 2. Ventana {wname} (logica 0000-FFFF = fisica {base:05X}-{base+0xFFFF:05X}): {len(seen)} instrucciones trazadas ===')
    for s, txt in sorted(strings.items()):
        cands = {}
        if base <= s < base + 0x10000:
            cands[s - base] = 'directa'
        cands[0xF000 | (s & 0xFFF)] = f'ventana F000 con CBR={((s >> 12) - 0xF) & 0xFF:02X}h'
        for L, how in cands.items():
            lo, hi = L & 0xFF, L >> 8
            traced = [a for a, ins in seen.items() if L in ins[3]]
            raw = [m.start() for m in re.finditer(re.escape(bytes([lo, hi])), rom[base:base + 0x10000])]
            raw = [r - 1 for r in raw if r >= 1 and rom[base + r - 1] in (0x01, 0x11, 0x21, 0x31, 0x2A, 0x22, 0x3A, 0x32)]
            if not traced and not raw: continue
            P(f'\n  -- "{txt[:50]}" fis {s:06X} -> logica {L:04X} ({how})')
            for a in sorted(set(traced) | set(raw)):
                tag = 'TRAZADA' if a in seen else 'CRUDA'
                P(f'     ref {tag} en {a:04X}: {seen[a][1] if a in seen else Z.decode(mem, a)[1]}')
                # context: linear disassembly aligned to land on a
                start = a
                for st in range(max(0, a - 0x40), a):
                    x = st
                    while x < a: x += Z.decode(mem, x)[0]
                    if x == a: start = st; break
                x = start
                while x < a + 0x50:
                    n, t, _, _ = Z.decode(mem, x)
                    P(f'       {x:04X}{" >>" if x == a else "   "} {t}')
                    x += n
rep.close()
print(open(os.path.join(out, 'reporte_download.txt')).read()[:3000])
