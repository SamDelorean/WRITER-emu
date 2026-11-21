#!/usr/bin/env python3
"""Mapea los dos canales serie ASCI del Z180 en el firmware de The Writer:
init (CNTLA/CNTLB/STAT), candidatos de baud, rutinas TX/RX y cadenas relacionadas
(impresora, IR, descarga). Mecanismo ligero, sin Ghidra.
Uso: python3 buscar_serial.py ROM.bin > salida.txt
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import z180dis as Z

rom = open(sys.argv[1], 'rb').read()
mem = lambda a: rom[a & 0xFFFF]

ASCI = {0x00: 'CNTLA0', 0x01: 'CNTLA1', 0x02: 'CNTLB0', 0x03: 'CNTLB1',
        0x04: 'STAT0', 0x05: 'STAT1', 0x06: 'TDR0', 0x07: 'TDR1',
        0x08: 'RDR0', 0x09: 'RDR1', 0x12: 'ASEXT0', 0x13: 'ASEXT1',
        0x1A: 'ASTC0L', 0x1B: 'ASTC0H', 0x1C: 'ASTC1L', 0x1D: 'ASTC1H'}

def dis(lo, hi):
    x = lo
    while x < hi:
        n, t, _, _ = Z.decode(mem, x)
        print(f'{x:04X}  {t}')
        x += n

def baud_candidates(cntlb):
    ps = 30 if (cntlb >> 5) & 1 else 10
    dr = 64 if (cntlb >> 3) & 1 else 16
    ss = cntlb & 7
    if ss == 7:
        return "SS=ext (reloj externo CKA)"
    ssf = 1 << ss
    out = []
    for phi, tag in ((3_072_000, 'PHI=XTAL/2'), (6_144_000, 'PHI=XTAL')):
        out.append(f"{phi/(ps*dr*ssf):.0f} bps ({tag})")
    return f"PS/{ps} DR/{dr} SS/{ssf} -> " + " | ".join(out)

# 1) Todas las escrituras a registros ASCI con su valor inmediato previo (LD A,n)
print("=== Escrituras OUT0 a registros ASCI (ventana 00000) ===")
inits = {}
x = 0
while x < 0xFFF0:
    n, t, _, _ = Z.decode(mem, x)
    m = re.match(r'OUT0 \(0x([0-9a-f]+)h?\),A', t)
    if m:
        p = int(m.group(1), 16)
        if p in ASCI:
            val = None
            # busca LD A,n inmediatamente antes (3E nn) o a 2-5 bytes atras
            for back in (2, 3, 4, 5, 6, 7):
                if mem(x - back) == 0x3E:
                    val = mem(x - back + 1); break
            vs = f"{val:02X}h" if val is not None else "??"
            print(f"  {x:04X}  OUT0 {ASCI[p]:7} = {vs}")
            if ASCI[p] in ('CNTLB0', 'CNTLB1') and val is not None:
                print(f"           baud: {baud_candidates(val)}")
            inits.setdefault(ASCI[p], []).append((x, val))
    x += n

# 2) Sitios que tocan TDR/RDR (envio/recepcion)
print("\n=== Sitios que acceden TDR/RDR (TX/RX) ===")
x = 0
while x < 0xFFF0:
    n, t, _, _ = Z.decode(mem, x)
    for reg in ('TDR0', 'TDR1', 'RDR0', 'RDR1'):
        pnum = [k for k, v in ASCI.items() if v == reg][0]
        if re.search(rf'\(0x0?{pnum:x}h?\)', t) and ('IN0' in t or 'OUT0' in t):
            print(f"  {x:04X}  {t}   ; {reg}")
    x += n

# 3) Cadenas relacionadas (impresora / IR / descarga / baud / modem)
print("\n=== Cadenas relacionadas ===")
claves = [b'print', b'Print', b'baud', b'9600', b'19200', b'serial', b'Serial',
          b'IrDA', b'infrared', b'Infrared', b'modem', b'Modem', b'download',
          b'Download', b'XON', b'XOFF', b'Epson', b'PCL', b'HP ', b'LPT',
          b'Send to', b'Printer', b'Computer', b'baud rate', b'parity']
seen = set()
for k in claves:
    for mo in re.finditer(re.escape(k), rom):
        p = mo.start()
        s = p
        while s > 0 and 0x20 <= rom[s-1] < 0x7f: s -= 1
        if s in seen: continue
        seen.add(s)
        e = s
        while e < len(rom) and 0x20 <= rom[e] < 0x7f: e += 1
        if e - s >= 4:
            print(f"  {s:06X}: {rom[s:e][:70].decode('ascii','replace')!r}")
