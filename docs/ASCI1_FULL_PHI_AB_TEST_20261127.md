# Z180 ASCI1 full-PHI clock A/B test — 2026-11-27

## Purpose

Validate the suspected factor-of-two ASCI1 input-clock discrepancy using the existing writerdiag target and existing MAME branch. No new branch or independent build tree was created.

## Implementation

Core source: src/devices/cpu/z180/z180.cpp, in z8s180_device::device_clock_changed().

Default behavior remains the existing rate scale. When WRITER_TEST_ASCI1_FULL_PHI is set, ASCI1 receives DERIVED_CLOCK(1,1) (full Z180 input PHI) instead. WRITER_TEST_ASCI1_CLOCK_TRACE emits the resolved ASCI1 clock for evidence. Other channels and CPU clock behavior are unchanged.

MAME core commit: 7218da95.

## Same-binary A/B execution

Build target: make -j2 SUBTARGET=writerdiag in the existing /home/spc/projects/mame-as2k-stable-arm64 tree. The resulting binary was copied to /home/spc/projects/WRITER_INSTRUCTOR/diag/writerdiag.

Both runs used that same binary, ROM set, machine, and 4-second emulated duration:

| Case | Environment | Resolved ASCI1 clock | Exit |
|---|---|---:|---:|
| Baseline | WRITER_TEST_ASCI1_FULL_PHI explicitly unset | 1,536,000 Hz | 0 |
| Full PHI | WRITER_TEST_ASCI1_FULL_PHI=1 | 3,072,000 Hz | 0 |

ROM initialization sets CNTLB1=0x06 (PS=0, SS=6), CNTLA1=0x64 (8N1), and ASEXT1 remains at reset value (X1=0, DR=0). The official Zilog baud table/formula therefore yields 150 bps for the baseline clock and 300 bps for full PHI. The latter matches the PIC transmitter nominal ~300-bps timing estimate and the Zilog PHI=3.072-MHz table entry.

## Artifacts

- A/B logs: /home/spc/projects/WRITER_INSTRUCTOR/diag/asci1_ab_20261127/
  - baseline.error.log, baseline.stdout
  - fullphi.error.log, fullphi.stdout
- Updated binary: /home/spc/projects/WRITER_INSTRUCTOR/diag/writerdiag
- SHA-256: 9b549ca53b147293b4f8b83ee0560a8ee28f6e03bc023ee4cec7cc3e586642d8

## Interpretation and limits

The A/B confirms that the model default ASCI1 clock is PHI/2 (1.536 MHz) and that the opt-in override produces full PHI (3.072 MHz). With the ROM fixed divider values, this doubles the modeled baud from 150 to 300 bps.

Both short diagnostic executions exited normally. This test establishes the clock/divisor correction, but does not by itself establish successful four-byte PIC-to-Z180 handshake or prove that the startup reset loop is resolved. A longer behavioral run with RX/STAT1/PC evidence is still required before declaring the boot issue fixed.
