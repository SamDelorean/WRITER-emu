#!/usr/bin/env python3
"""Segunda pasada para localizar la rutina real de inicializacion del LCD
(puertos 00C0/00C2), siguiendo saltos/llamadas INDIRECTAS (JP (HL), CALL
tabla de vectores en 0150-018F, etc.) que la traza recursiva simple no
puede seguir.

Estrategia:
  1. Re-traza desde los puntos de entrada conocidos (reset, init, teclado,
     envio, impresion, recuperacion) como antes.
  2. Dentro de ese conjunto alcanzado, busca instrucciones de salto/llamada
     indirecto: JP (HL), JP (IX), JP (IY), CALL (HL) no existe en Z80 real
     pero el patron tipico es "LD HL,tabla; ADD HL,DE/A; JP (HL)" o una
     tabla de 2 bytes en 0150-018F con "LD HL,(0150h+n*2); ..." Vuelca
     TODAS las instrucciones JP (HL)/JP (IX)/JP (IY) encontradas junto con
     las ~6 instrucciones previas, para identificar manualmente la tabla
     base.
  3. Ademas, vuelca la tabla cruda de 0140-01A0 como pares de 16 bits (muy
     probable tabla de vectores, segun hallazgo previo) y, para cada
     puntero que caiga dentro del rango de codigo (0x0000-0x9FFF, evitando
     la zona de basura ya detectada en la pasada anterior), lo añade como
     nueva entrada de traza y vuelve a buscar accesos a C0h-C3h desde ahi.

Uso: python3 buscar_lcd2.py ROM.bin > salida.txt
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z180dis as Z

rom = open(sys.argv[1], 'rb').read()
mem = lambda a: rom[a & 0xFFFF]

base_entries = [0x0000, 0x0150, 0x046B, 0x0769, 0x078F, 0x0CF6, 0x1AC5,
                0x23EC, 0x2400, 0x24EB, 0x2570, 0x2682, 0x0E08]

traced = Z.trace(mem, base_entries, lo=0, hi=0x9FFF)

print("=== Instrucciones de salto/llamada indirecto (JP/CALL via registro) en el alcance conocido ===")
keys = sorted(traced)
indirect = []
for addr in keys:
    n, t, fl, _ = traced[addr]
    if re.match(r'^(JP|CALL) \((HL|IX|IY)\)$', t):
        indirect.append(addr)
        idx = keys.index(addr)
        print(f"--- {addr:04X}  {t} ---")
        for j in range(max(0, idx - 6), idx + 1):
            a2 = keys[j]
            n2, t2, fl2, _ = traced[a2]
            raw = ' '.join(f'{mem(a2+i):02X}' for i in range(n2))
            print(f"  {a2:04X}  {raw:<11} {t2}")
        print()
print(f"Total indirectos encontrados: {len(indirect)}")
print()

print("=== Tabla cruda 0140h-01A0h como punteros de 16 bits (little endian) ===")
candidates = []
for a in range(0x0140, 0x01A0, 2):
    lo, hi = mem(a), mem(a + 1)
    ptr = lo | (hi << 8)
    flag = ''
    if 0x0000 <= ptr <= 0x9FFF:
        candidates.append(ptr)
        flag = '  <- en rango de codigo, se añade como entrada'
    print(f"{a:04X}: {lo:02X} {hi:02X} -> {ptr:04X}{flag}")
print()

print("=== Re-traza incluyendo los punteros de la tabla como nuevas entradas ===")
all_entries = base_entries + candidates
traced2 = Z.trace(mem, all_entries, lo=0, hi=0x9FFF)
ports_of_interest = {0xC0, 0xC1, 0xC2, 0xC3}
hits = []
for addr in sorted(traced2):
    n, t, fl, _ = traced2[addr]
    m = re.search(r'\(([0-9A-Fa-f]{1,2})h\)', t)
    if m and ('OUT' in t or 'IN' in t) and 'OUT0' not in t and 'IN0' not in t:
        p = int(m.group(1), 16)
        if p in ports_of_interest:
            hits.append((addr, t))

print(f"Accesos a C0h-C3h encontrados tras incluir la tabla: {len(hits)}")
keys2 = sorted(traced2)
for addr, t in hits:
    print(f"\n--- contexto de {addr:04X}: {t} ---")
    idx = keys2.index(addr)
    for j in range(max(0, idx - 15), min(len(keys2), idx + 15)):
        a2 = keys2[j]
        n2, t2, fl2, _ = traced2[a2]
        raw = ' '.join(f'{mem(a2+i):02X}' for i in range(n2))
        mark = '>>' if a2 == addr else '  '
        print(f"{a2:04X} {mark} {raw:<11} {t2}")
