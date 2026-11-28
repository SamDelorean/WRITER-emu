#!/usr/bin/env python3
import sys
sys.path.insert(0, "/home/spc/projects/WRITER_INSTRUCTOR/writer-emu/tools")
from z180dis import decode

ROM = "/home/spc/projects/WRITER_INSTRUCTOR/writer-emu/roms/akwriter/akt_writer_v3.70_m27c801.bin"
with open(ROM, "rb") as f:
    rom = f.read()

def mem(addr):
    # logical 0000-FFFF -> physical 0xC0000 + addr (per writer.cpp header: CBAR=F0/CBR=C0/BBR=C0)
    return rom[(0xC0000 + (addr & 0xFFFF)) & 0xFFFFF]

def dump(start, count, label):
    print(f"\n--- {label} (from {start:04X}h) ---")
    a = start
    for _ in range(count):
        n, t, fl, _ = decode(mem, a)
        print(f"{a:04X}  {t}")
        if any(k == 'end' for k, _ in fl):
            print("  (end of flow here)")
        a = (a + n) & 0xFFFF

if __name__ == "__main__":
    for arg in sys.argv[1:]:
        addr = int(arg, 16)
        dump(addr, 40, f"PC={arg}")
