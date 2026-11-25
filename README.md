# WRITER-emu

MAME-based experimental emulator for **AKT Inc. The Writer / Writer Instructor**, firmware v3.70.

## Current status — 2026-11-25

The previous PIC/Z180 boot deadlock has been resolved at the interconnect level. The real PIC16C54C dump executes in MAME and is linked to the Z8S180:

- PIC RA2 → Z180 `/INT1`.
- PIC RB0 → Z180 ASCI1 `RXA1`.
- PIC RB7 samples Z180 `TXA1`.
- PIC RB1 samples Z180 `/HALT`, including the Z180's SLP state.
- PIC RB2 defaults high as the inactive J1 input level in the emulator; the physical idle voltage still needs measurement.
- PIC RB3 currently models aggregate keyboard activity; its exact connection to U9 2A1 and the corresponding matrix column remain to be identified.

The firmware recognizes the F1+F2 Master Reset chord (F1=R2C6, F2=R6C6) and reaches the password prompt. The original ROM Master Reset routine was then executed in the emulator. It generated the expected RAM signature at D000–D003 and initialized the observed D800–EFFF areas. This is a real ROM-generated result, not an injected signature.

After that initialization, the next failure is:

```text
Aborting App - Fatal Error
Missing Application BIOS, 0x203B
Powering Down...
```

The current investigation therefore moves to password-key capture, the application storage/directory format, SRAM bank mapping/persistence, and locating/loading the Application BIOS. The RAM capture is only a 64 KiB logical window, not the full 256 KiB physical SRAM image.

`MACHINE_NOT_WORKING` remains set. This is not yet a complete hardware emulator.

See:
- [Boot interconnect and IRQ1 resolution](docs/BOOT_BLOCKER_RESOLUTION_20261125.md)
- [Recovery status, RAM initialization evidence and next starting point](docs/RECOVERY_STATUS_AND_NEXT_START_20261125.md)

## Hardware model

- CPU: Zilog Z8S180, 6.144 MHz.
- Firmware: M27C801 EPROM, 1 MiB.
- RAM: two 128 KiB SRAM devices, 256 KiB total (physical chip-select and bank mapping pending).
- LCD: 40×4 characters, two KS0066 controllers.
- Keyboard: 8×16 matrix, 74HC374 row latch, two 74VHC244 column buffers and 74HC138 decoder.
- PIC: Microchip PIC16C54C U11, 32.768 kHz, real 1 KiB dump.

The current 1 MiB `bankrw` memory shadow is diagnostic, not a faithful EPROM/SRAM overlay. PIC RA1→Z180 `/RESET`, RA3→SRAM CS2, T0CKI←U19 `/Y3`, RB4/RB5/RB6, exact RB3 column mapping, and persistent SRAM remain to be modeled.

## Build and run

From `/home/spc/projects/WRITER_INSTRUCTOR`:

```bash
./build_writerdiag.sh
```

The script builds the local `writer-emu` driver into the installed MAME source tree. The expected ROM is already held locally at:

```text
roms/akwriter/akt_writer_v3.70_m27c801.bin
```

The ROM and PIC dumps are not distributed in this repository. Use dumps from the owner's device.

The synthetic 1 kHz D017 timer is **disabled by default**. It can be enabled only for comparison runs with:

```bash
WRITER_ENABLE_D017_TICK=1
```

Diagnostic overrides:

- `WRITER_PIC_RB2=0` tests the alternate RB2 input level; default is high.
- `WRITER_TEST_PIC_WAKE=1` injects a short RB3 keyboard-wake pulse.
- `WRITER_MEM_DUMP_FILE=/path/to/snapshot.bin` saves the logical `0000–FFFF` window (currently physical `C0000–CFFFF`, 64 KiB), not the complete 256 KiB SRAM.
- `WRITER_UI_FRAME_FILE=/path/to/frame.ppm` saves the rendered LCD frame.
- `WRITER_TEST_MASTER_RESET_KEYS=1` injects the F1+F2 chord for one diagnostic matrix scan.
- `WRITER_TEST_EXECUTE_MASTER_RESET=1` redirects the Z180 PC to the original ROM reset routine after two emulated seconds; test-only.
- `WRITER_TEST_VALID_RAM_SIGNATURE=1` injects only the two signature words as an experimental control; it is not a reset and must not be used as a normal boot workaround.

## Ghidra and evidence

Local analysis workspace:

```text
/home/spc/projects/WRITER_INSTRUCTOR/willem/ghidra_writer/
```

The 2026-11-21 analysis outputs are under `analysis_20261121/out/`. The 2026-11-25 reports record the IRQ1 vector correction, Master Reset path and post-reset Application BIOS failure. Diagnostic RAM captures and the run log are preserved under the project's `snap/` and `diag/` folders and in the project Drive archive.

## Legal and license

This repository contains original driver code and hardware-analysis documentation only. Firmware ROM data and full disassemblies are not distributed. The driver is BSD-3-Clause, based on MAME drivers including `src/mame/brother/ax145.cpp` and `src/mame/skeleton/alphasma.cpp`.
