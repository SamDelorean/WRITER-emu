# WRITER-emu

MAME-based experimental emulator for **AKT Inc. The Writer / Writer Instructor**, firmware v3.70.

## Current status — 2026-11-26

**Keyboard dispatch table found.** Static ROM analysis located a 128-entry key dispatch/translation table at `0x0964` (4 bytes/entry: handler pointer, base byte, Shift/alternate byte). The matrix scan (`0x0BE2`), modifier sampler (`0x0B64`), FIFO producer/consumer and dispatcher (`0x0D55`) are traced end to end, and most printable keys are mapped by logical row/column. The Master Reset chord's exact (row,col)↔table-index correlation is still an open discrepancy — do not hard-code F1/F2 legends yet. See [keyboard matrix and translation table](docs/KEYBOARD_MATRIX_AND_TRANSLATION_TABLE_20261126.md).

**U19 (74HC138) port decoder closed.** Multimeter continuity on U14 (Z8S180), converted to real PLCC-68 pin numbers and cross-checked against an independent public copy of the Zilog pinout, confirms select A/B/C = Z180 A1/A6/A7 and enables /G2B/G1 = Z180 /IORQ/E. This exactly reproduces the already-documented firmware port map (`0x40/0x80/0x82/0xC0/0xC2`); no emulator behavior changed, only trace comments. See [U19 74HC138 pin map](docs/U19_74138_PIN_MAP_20261125.md).

The previous PIC/Z180 boot deadlock has been resolved at the interconnect level. The real PIC16C54C dump executes in MAME and is linked to the Z8S180:

- PIC RA2 → Z180 `/INT1`.
- PIC RB0 → Z180 ASCI1 `RXA1`.
- PIC RB7 samples Z180 `TXA1`.
- PIC RB1 samples Z180 `/HALT`, including the Z180's SLP state.
- PIC RB2 defaults high as the inactive J1 input level in the emulator; the physical idle voltage still needs measurement.
- PIC RB3 is physically connected to U9 pin 11 (2A1). The emulator still uses aggregate keyboard activity as a provisional approximation; the exact matrix column represented by this U9 channel and the remaining U8/U9/U10 wiring are not yet traced.

The firmware recognizes the Master Reset chord previously identified as F1+F2 (R2C6/R6C6) and reaches the password prompt. The exact correlation between the raw chord pattern and the new 128-entry translation table is still being reconciled; do not treat those matrix labels as closed. A diagnostic IRQ1-vector redirect lets the next real PIC pulse wake the Z180 from `SLP` into the original ROM routine at `0x2523`. The routine executes its initialization and writes the expected signatures `D000=916A` and `D002=5A3C`; the original IRQ1 vector `0x182A` is restored afterward. This is a real ROM-generated result, not an injected signature.

This test bypasses keyboard entry and password comparison. It does **not** validate `ACE135`. After the reset routine, the firmware reaches the `Powering Down...` path with `Missing Application BIOS, 0x203B`; application BIOS availability/loading remains unresolved.

After that initialization, the next failure is:

```text
Aborting App - Fatal Error
Missing Application BIOS, 0x203B
Powering Down...
```

The Application BIOS failure has now been traced one level deeper: ROM routine `0x20FF` searches index `0x12` for `SHELL`, but the corresponding table entry `D0DF` is zero. The ROM scan routine `0x2206` maps logical `F000` to physical `0x40000` (`CBR=31`, `CBAR=F0`) and expects a `3C 5A` block header there. The supplied EPROM has `FF 00` at that signature position and no `3C 5A` markers in `0x40000–0x7FFFF`, a 256 KiB window matching the documented total SRAM capacity. The current flat 1 MiB writable ROM shadow does not model separate SRAM or persistence, so the leading explanation is a missing SRAM overlay/image; exact chip-select decoding and real SRAM contents remain to be verified. See [Application BIOS storage scan analysis](docs/APPLICATION_BIOS_STORAGE_SCAN_20261125.md).

A dynamic PIC trace confirms RA3→CS2 changes state: PORTA `00` drives RA3 low and PORTA `0C` drives it high. Drive photographs identify both SRAMs as Samsung K6X1008C2D-GB55 (128K×8, 32-SOP); the project datasheet assigns CS2 to pin 30, active high, and /CS1 to pin 22, active low. Therefore RA3 high asserts the shared CS2 input during ROM scan `0x2206`. Full chip selection remains unresolved because /CS1 must also be low and its per-chip routing is not yet traced. See [SRAM hardware evidence](docs/SRAM_K6X1008C2D_HARDWARE_EVIDENCE_20261125.md) and [PIC RA3 / SRAM CS2 trace](docs/RA3_SRAM_CS2_TRACE_20261125.md).

Password-key capture remains a separate unresolved path. The 64 KiB RAM capture is only the logical `0000–FFFF` window, not the complete 256 KiB physical SRAM image.

The keyboard findings and broader reverse-engineering state (including the corrected Z180 Ghidra processor selection, handler semantics, editor callbacks, LCD glyph path, and remaining validation gaps) are consolidated in [Reverse-engineering status — 2026-11-26](docs/REVERSE_ENGINEERING_STATUS_20261126.md).

`MACHINE_NOT_WORKING` remains set. This is not yet a complete hardware emulator.

See:
- [Boot interconnect and IRQ1 resolution](docs/BOOT_BLOCKER_RESOLUTION_20261125.md)
- [Recovery status, RAM initialization evidence and next starting point](docs/RECOVERY_STATUS_AND_NEXT_START_20261125.md)
- [Samsung K6X1008C2D-GB55 — SRAM hardware evidence and corrected pinout](docs/SRAM_K6X1008C2D_HARDWARE_EVIDENCE_20261125.md)
- [PIC RA3 / SRAM CS2 dynamic trace](docs/RA3_SRAM_CS2_TRACE_20261125.md)
- [SRAM bus continuity test matrix — U16/U17, U21 and Z180](docs/SRAM_BUS_CONTINUITY_TEST_MATRIX_20261125.md)
- [PIC/Z180 IRQ, T0CKI bus clock and keyboard hardware findings](docs/PIC_Z180_IRQ_AND_IO_CLOCK_HARDWARE_20261126.md)
- [Keyboard physical net-trace plan — U10/U8/U9 to matrix rows and columns](docs/KEYBOARD_PHYSICAL_NET_TRACE_PLAN_20261126.md)

## Hardware model

- CPU: Zilog Z8S180, PHI=3.072 MHz (XTAL=6.144 MHz; PHI is XTAL/2).
- Firmware: M27C801 EPROM, 1 MiB.
- RAM: two Samsung K6X1008C2D-GB55 SRAMs, each 128K×8 (256 KiB total); shared CS2 is U11 RA3 to pin 30 and active high. Per-chip /CS1 routing and bank map remain pending.
- LCD: 40×4 characters, two KS0066 controllers.
- Keyboard: 8×16 matrix, 74HC374 row latch, two 74VHC244 column buffers and 74HC138 decoder.
- PIC: Microchip PIC16C54C U11, 32.768 kHz, real 1 KiB dump.

The current 1 MiB `bankrw` memory shadow is diagnostic, not a faithful EPROM/SRAM overlay. PIC RA1→Z180 `/RESET`, the per-chip SRAM `/CS1` decoding, SRAM `/OE`/`/WE` and physical address/data buses, T0CKI←U19 `/Y3`, RB4/RB5/RB6, exact RB3 column mapping, and persistent SRAM remain to be modeled.

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
- `WRITER_TEST_BYPASS_PASSWORD_TO_RESET=1` waits for the password editor to initialize, temporarily redirects the actual IRQ1 vector at `D700` from `0x182A` to ROM reset entry `0x2523`, then lets the next real PIC pulse wake the Z180 from `SLP`. ROM executes the initial `CALL 0x22F3`, installs its own `SP=D000`, and performs initialization. When ROM writes `D000=916A` and `D002=5A3C`, the diagnostic restores the original IRQ1 vector `0x182A`. This bypasses keyboard entry and password comparison but preserves the real interrupt wake path. Combine with `WRITER_TEST_MASTER_RESET_KEYS=1` to reach the password screen.
- `WRITER_TEST_VALID_RAM_SIGNATURE=1` injects only the two signature words as an experimental control; it is not a reset and must not be used as a normal boot workaround.
- `WRITER_TRACE_INPUT_WRITES=1` traces writes to the password buffer and event FIFO in the physical `CD000-CDFFF` RAM window.
- `WRITER_TRACE_INPUT_READS=1` traces reads of the FIFO count (`D0AD`) and pending result (`D0AF`).
- `WRITER_TRACE_PASSWORD_FLOW=1` samples Z180 PC/SP, interrupt flags, timer and password-editor/FIFO state once per frame (diagnostic only; disabled by default).
- `WRITER_TRACE_KEYBOARD=1` traces matrix strobes and reads; verbose and capped at 4096 records.

## Ghidra and evidence

Local analysis workspace:

```text
/home/spc/projects/WRITER_INSTRUCTOR/willem/ghidra_writer/
```

The 2026-11-21 analysis outputs are under `analysis_20261121/out/`. The 2026-11-25 reports record the IRQ1 vector correction, Master Reset path, Application BIOS failure, and the keyboard/FIFO/password-input trace in `docs/PASSWORD_INPUT_TRACE_20261125.md`. Diagnostic RAM captures and the run log are preserved under the project's `snap/` and `diag/` folders and in the project Drive archive.

## Legal and license

This repository contains original driver code and hardware-analysis documentation only. Firmware ROM data and full disassemblies are not distributed. The driver is BSD-3-Clause, based on MAME drivers including `src/mame/brother/ax145.cpp` and `src/mame/skeleton/alphasma.cpp`.
