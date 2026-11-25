# The Writer — boot blockage investigation and resolution

**Date:** 2026-11-25  
**Target:** AKT Writer Instructor v3.70, Z8S180-based unit  
**Repository:** `SamDelorean/writer-emu`  
**Working branch:** `fix/irq1-ra2-wakeup`  
**Base commit:** `fb69945` — `writer: disable ANALYSIS_FORCE_VALID_RAM_SIGNATURE for RE of boot stall`

## Executive result

The original boot stall was caused by an incomplete PIC16C54C ↔ Z180 interconnection in the MAME driver. The real PIC firmware was loaded, but its RA2 interrupt output and RB0 serial transmitter were not connected to the Z180. Connecting RA2 alone was insufficient: with RB2 held low, the PIC remained in its initial wait path with RA2 low, causing repeated IRQ1 service.

After modeling the PIC input state and connecting RA2→/INT1, RB0→RXA1, RB1←/HALT and RB7←TXA1, the emulator reaches the firmware's **MEMORY ERROR WARNING** and enters SLP at PC `0x0E27`. This is a valid firmware wait state, not the former interrupt storm.

The remaining stop is caused by the cold/invalid SRAM signature. The physical 256 KiB SRAM map and persistence are not implemented yet. No master reset was executed and no destructive RAM operation was performed.

## Evidence reviewed

- Git history and local driver: `writer-emu`, base `fb69945`, branch `fix/irq1-ra2-wakeup`.
- Drive hardware continuity record: `WRITER_RE_06_PIC_mapa_conexiones_U11`.
- Drive/Ghidra analysis notes: `WRITER_RE_05`, `WRITER_RE_07`, and `WRITER_RE_08_IRQ1_vector_Ghidra`.
- Local Ghidra workspace: `/home/spc/projects/WRITER_INSTRUCTOR/willem/ghidra_writer/`.
- Main ROM: `roms/akwriter/akt_writer_v3.70_m27c801.bin`.
- PIC firmware: `roms/akwriter/writer_instructor_PIC16C54C.bin` and `writer_instructor_pic16c54c_u11.bin`.

### Corrected IRQ1 vector analysis

The previous RE08 conclusion that D700 had no confirmed vector was a false negative. The ROM installs the vector indirectly:

```text
0x214B: LD HL,0
       LD DE,0x182A
       CALL 0x040C
```

Routine `0x040C` executes `LD A,I`, copies I into D, clears E, adds DE to HL and writes E/D through HL. Since boot set `I=0xD7`, the result is the pointer `0x182A` at `D700–D701`. The ISR at `0x182A` updates D015, D017, D00F, D011 and D013, then returns with `EI/RET`.

The new live RAM capture independently confirms `D700 = 2A 18`. This dynamic setup was missed by searching only for literal writes to D700 or the IL register.

### Physical PIC connections confirmed by WRITER_RE_06

| PIC pin | Confirmed board connection |
|---|---|
| RA1 (18) | Z180 `/RESET` |
| RA2 (1) | Z180 `/INT1` |
| RA3 (2) | SRAM U16/U17 `CS2` |
| RA4/T0CKI (3) | U19 pin 12, 74HC138 `/Y3` |
| RB0 (6) | Z180 ASCI1 `RXA1` |
| RB1 (7) | Z180 `/HALT` |
| RB2 (8) | J1 pin 3 through R28 (22 kΩ) |
| RB3 (9) | U9 pin 11, 74VHC244 input `2A1` |
| RB4 (10) | Keyboard pad KB1 and D3/U10 Q1 path |
| RB5 (11) | D14 / supercapacitor path |
| RB6 (12) | Z180 EXTAL connection |
| RB7 (13) | Z180 ASCI1 `TXA1` |

RA2 and T0CKI are distinct PIC pins. The exact U19 address decode and the idle voltage on RB2 still require electrical verification.

## Driver changes

The current driver implements these links:

- PIC RA2 drives MAME `Z180_INPUT_LINE_IRQ1`, active low.
- PIC RB0 drives Z180 `rxa1_w()`; this feeds the existing MAME Z180 ASCI1 receiver.
- Z180 TXA1 callback updates the PIC's RB7 input.
- PIC RB1 reads the Z180's internal HALT state. MAME stores HALT=1 and SLP=2, while the public `Z180_HALT` state is masked to bit 0; the driver reads the registered raw state byte so both conditions assert the external `/HALT` signal.
- RB2 defaults high as the provisional idle level for J1. `WRITER_PIC_RB2=0` allows comparison with the alternate level.
- RB3 currently uses aggregate active-key detection from the 8×16 matrix as a temporary approximation of U9/2A1. The exact matrix column is not yet known.
- The synthetic 1 kHz D017 timer is disabled by default. It is available only with `WRITER_ENABLE_D017_TICK=1`.

A diagnostic input pulse can be requested with `WRITER_TEST_PIC_WAKE=1`; it does not alter normal runs.

## Test results

All tests used the real ROM and PIC dumps on Vontar/Armbian.

| Run | PIC input model | D017 synthetic timer | Result |
|---|---|---:|---|
| Initial RA2-only connection | RB2=0, RB3=0 | Enabled | RA2 stayed low; repeated IRQ1; final PC `0x1869` |
| Corrected PIC inputs | RB2=1, no key | Enabled | PIC released RA2, RB0 bitstream appeared, LCD memory warning reached |
| Fidelity check | RB2=1, no key | **Disabled** | Same memory warning reached; final PC `0x0E27` |

The no-D017 run ended with:

```text
FINAL PC=0E27
D000=FD66
D002=CDE5
D011=0019
D013=0000
```

The log shows PIC RB0 transitions, RA2 assertion/deassertion, Z180 I/O activity and LCD writes. This establishes that the interrupt path and serial connection are active. It does not yet certify every received byte, baud rate or UART framing.

## Ghidra live snapshot

The new 64 KiB snapshot and its MAME log were preserved here:

```text
/home/spc/projects/WRITER_INSTRUCTOR/willem/ghidra_writer/analysis_20261125/
```

Files:

- `out/ram_c0000_cffff_boot_warning_no_d017.bin`
- `out/mame_boot_warning_no_d017.log`

Snapshot SHA-256:

```text
41d595d28144f5488edcb1367855ccb5fe6d7ae36dfa6992d3bee52ce0ccf454
```

At file offsets corresponding to logical addresses: D000=`66 FD`, D002=`E5 CD`, D011=`19 00`, D013=`00 00`, D700=`2A 18`. The snapshot covers the active 64 KiB window C0000–CFFFF; it is **not** a complete image of the 256 KiB SRAM.

## Interpretation and remaining work

The current stop at `0x0E27` follows the firmware's MEMORY ERROR WARNING. The cold signature at D000/D002 is invalid. The emulator's artificial signature injection remains disabled. The warning is therefore consistent with uninitialized or incorrectly modeled SRAM; it is not evidence that the Z180 core is still deadlocked.

No master reset was performed. The documented F1+F2-at-power-on sequence and password `ACE135` remain to be tested from the first emulated CPU cycle. A Lua test that set F1+F2 after startup was too late to establish the power-on reset path.

Priority order:

1. Measure RB2's actual idle voltage at J1 pin 3.
2. Identify the exact keyboard column connected to U9/2A1 (RB3); replace the any-key approximation.
3. Model PIC RA1→Z180 `/RESET`, accounting for TRISA and pull-up R19.
4. Model PIC RA3→SRAM CS2 and separate the 256 KiB SRAM from the 1 MiB EPROM.
5. Trace U19's 74HC138 inputs and generate the correct T0CKI pulses from `/Y3`.
6. Model RB4/RB5/RB6 and RA0 power control.
7. Validate all four RB0→RXA1 bytes, baud rate and framing against the PIC bit timing and ASCI1 status.
8. Implement persistent SRAM, then validate master reset and a normal editor boot.

The code and documentation are committed locally on `fix/irq1-ra2-wakeup`, based on `fb69945`; the branch has not been pushed. Generated `cfg/`, `snap/`, `error.log` and the existing `tools/dis_driver.py` were preserved and are not part of the source commit.

## Additional 30-second no-D017 run

The revised `build_writerdiag.sh` completed an end-to-end build, ROM validation and 30-second emulated run. The run exited with code 0; D017 synthetic timing remained disabled.

- PIC RB0 transitions: 28.
- PIC RA2 edges: 28.
- LCD writes: 148.
- Final sample: `PC=16A4`, `D011=0050`, `D013=0000`.
- D700 still contains `2A 18`.

PC `0x16A4` is a helper routine ending at `0x16C0`; the final PC is simply where the 30-second stop occurred, not evidence of a loop. The RAM snapshot and trace are preserved as `ram_c0000_cffff_boot_warning_30s_no_d017.bin` and `mame_boot_warning_30s_no_d017.log` under `analysis_20261125/out/`.


## Subsequent update — ROM reset entry validated (2026-11-25)

The statement above that no Master Reset had been executed describes the state at the time of that boot-blocker investigation. A later controlled run is recorded in `RECOVERY_STATUS_AND_NEXT_START_20261125.md` and `PASSWORD_INPUT_TRACE_20261125.md`: the driver temporarily redirected IRQ1 vector `D700` from `0x182A` to `0x2523`; the real PIC RA2 pulse woke the Z180 from `SLP`; the ROM routine initialized RAM, wrote `D000=916A` and `D002=5A3C`, and the driver restored vector `0x182A`. This bypass did not validate `ACE135`. The subsequent `Missing Application BIOS, 0x203B` / `Powering Down...` condition remains unresolved.
