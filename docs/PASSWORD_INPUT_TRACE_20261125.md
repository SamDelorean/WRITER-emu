# The Writer Instructor — password input trace (2026-11-25)

## Scope and method

Continued from commit `76f0c8e` (`Trace Writer event queue consumer`). No ROM bytes were patched and no RAM signatures were injected. Added optional, passive MAME taps to the Z180 physical program-space window. The taps are enabled only with environment variables:

- `WRITER_TRACE_INPUT_WRITES=1`: writes to physical `0xCD03C-0xCD041` and `0xCD08D-0xCD0AF`.
- `WRITER_TRACE_INPUT_READS=1`: reads of physical `0xCD0AD-0xCD0B0` (FIFO count and pending result).
- `WRITER_TRACE_KEYBOARD=1`: keyboard strobe and matrix reads.

The physical address correction is important: logical `D000-DFFF` is in the Z180 RAM window at physical `CD000-CDFFF`. Earlier taps at `0x0Dxxx` were on the wrong physical range.

The logged `PC_NEXT` is the Z180 PC after the writing/reading instruction has executed. The instruction address is recovered from the disassembly; e.g. `PC_NEXT=0C35` is the write at `0C34`.

## Confirmed keyboard-to-FIFO path

The dynamic interrupt path is:

1. PIC U11 RA2 asserts Z180 `/INT1`.
2. Z180 ISR at `0x182A` updates time counters and calls `0x0C40` at `0x1879`.
3. `0x0C40` implements key debounce/repeat using `D089`, `D08A`, and `D08C`.
4. `0x0BE2` scans the keyboard matrix. It writes `0xFF` to port `0x40`, reads ports `0x80` and `0x82`, then scans row strobes from `0x80` down to `0x01`.
5. `0x0BD3` maps the matrix position to a key index. `0x0B64` reads modifier keys and returns the modifier mask.
6. `0x0C22` appends a 16-bit event to the FIFO at `D08D` and increments `D0AD`.

`0x0C22` computes the destination as `D08D + 2 * D0AD`, writes the low/high event bytes, then increments the event count. Its `0xFFF0 + count` check indicates a 16-event capacity (32 bytes).

The runtime write trace confirms the producer instructions:

| Logical target | Physical write | Instruction | Result |
|---|---:|---:|---|
| `D08D` | `CD08D` | `0C34: LD (HL),E` | low event byte |
| `D08E` | `CD08E` | `0C36: LD (HL),D` | high event byte |
| `D0AD` | `CD0AD` | `0C38: LD (D0AD),BC` | incremented event count |

`0x0CBE` is the FIFO consumer: it reads `D0AD`, decrements it, extracts the first two-byte event, and compacts the remaining entries with `LDIR`.

## Event dispatch and character map

`0x0D55` first reads the 16-bit pending value at `D0AF`. If nonzero, it clears `D0AF` and returns through `0x0E05`; it does **not** consume the FIFO on that path. If `D0AF` is zero, it calls `0x0CBE` and dispatches the low seven bits through the 128-entry, four-byte-stride table beginning at `0x0964`.

For printable keys, table entries point to `0x0DBC` or `0x0DA4`; the two-byte data field contains lower/upper-case or unshifted/shifted character pairs. The scan order maps event index `i` to row `i // 16`, column `15 - (i % 16)`. Thus the password characters map as follows:

| Character | Matrix position | Table index | Event byte | Table handler/data |
|---|---:|---:|---:|---|
| A | `R3C6` | `0x39` | `0xB9` | `0DBC / 'aA'` |
| C | `R2C10` | `0x25` | `0xA5` | `0DBC / 'cC'` |
| E | `R0C10` | `0x05` | `0x85` | `0DBC / 'eE'` |
| 1 | `R1C6` | `0x19` | `0x99` | `0DA4 / '1!'` |
| 3 | `R1C10` | `0x15` | `0x95` | `0DA4 / '3#'` |
| 5 | `R4C14` | `0x41` | `0xC1` | `0DA4 / '5%'` |

The high byte of the queued event carries the modifier mask. A simulated `R0C0` key generated `0x008F`; table index `0x0F` points to the default/no-op path, so that arbitrary test key is not a useful character test.

## Runtime experiments

- A Lua-controlled `R3C6` press reached the PIC input model (`PORTB` changed from `0x84` to `0x8C`, setting RB3) and generated event `0x00B9` in the FIFO.
- A Lua-controlled `R2C4` press generated event `0x00AB` (Enter) in the next FIFO slot.
- An early `R3C6` press (frame 100) generated `0x00B9`; the read tap then showed `0x0CBE` consuming it and decrementing `D0AD` back to zero **before** the password buffer was initialized. The later initialization writes zero to the first byte at `D03C` (`0x25D5: LD (HL),0`; logged `PC_NEXT=25D7`). The early character is therefore discarded for the password.
- Later `A`/Enter presses, after the prompt was displayed, were successfully queued, but the queue count remained nonzero. The read trace showed `0x0D55` reading `D0AF=0x00`, later receiving `0x00F8`, and taking the pending-result path that clears `D0AF` without calling `0x0CBE`. No subsequent FIFO-count read/consume or character write to `D03C-D041` was observed in that run.
- LCD writes in the same run explicitly displayed `Enter Master Reset Password: [ ]`.

This isolates the remaining issue to the timing/control-flow boundary between password-buffer initialization, the `D0AF` pending-result path, and FIFO dispatch. It does not yet prove that the queued character is being rejected by the character table; the queue was not consumed in the late-key test.

## Current status and next step

Confirmed: physical keyboard matrix -> Z180 ISR -> debounce/repeat -> FIFO producer -> FIFO consumer -> 128-entry character/action dispatch table. The six password key positions and event codes are now known.

Open: arrange a test key so its event is consumed by `0x0D55` after `D03C` is initialized, then verify the corresponding character write into `D03C-D041`. After that, inject `ACE135` through the same matrix path and verify the ROM comparison at `0x24B5-0x24BB`.

Not yet achieved: successful password entry, Master Reset completion, or resolution of `Missing Application BIOS, 0x203B`.

All key presses in these tests were MAME Lua input-field simulations, not physical keyboard measurements. The source ROM remained unchanged.
