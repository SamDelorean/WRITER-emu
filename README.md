# WRITER-emu

Minimal MAME-based emulator (driver) for **AKT Inc. "The Writer"** (Writer Instructor), firmware v3.70.

- CPU: Zilog Z8S180
- ROM: M27C801 EPROM, 1 MiB
- LCD: 40x4 characters (two KS0066 / HD44780-compatible controllers), same module as AlphaSmart Pro/2000/3000

## Status

Skeleton driver: `src/mame/akt/writer.cpp` (`MACHINE_NOT_WORKING`).
Memory map, I/O ports, keyboard and clock are pending firmware analysis.

Code is based on existing MAME drivers (BSD-3-Clause), credited in the source header:
`src/mame/brother/ax145.cpp` and `src/mame/skeleton/alphasma.cpp`.

## ROM

**The ROM is NOT included in this repository** and will never be: the firmware is copyrighted by AKT Inc.
Use a dump of your own device. Expected file:

| File | Size | CRC32 | SHA1 |
|---|---|---|---|
| `akt_writer_v3.70_m27c801.bin` | 1048576 | `c454875c` | `6bd0691f96c7987b2e266e220834bf4d7e3e22b4` |

## Legal

This repository contains only original code and hardware documentation written by the author.
No firmware, ROM data or disassembled firmware code is distributed here.

## License

BSD-3-Clause, same as the MAME drivers it is based on.
