# Writer Instructor — Ghidra keyboard investigation, phase 1

Date: 2026-11-26
Target ROM: `akt_writer_v3.70_m27c801.bin`
SHA-256: `f4086f4c792f72b7ee7a1a32334f8a3df9e8dedee3eaa20071915adee1540da9`
Ghidra: 12.1.3, language `z80:LE:16:default`
Analysis mode: headless, existing persistent project, `-noanalysis`; labels, comments, seeded disassembly and extraction script only.

## 1. Five modifier/state bits — statically resolved to matrix positions

Routine `0x0B64` samples five selected row/column intersections. For each test it writes a row strobe to I/O port `0x40`, reads one of the input ports, tests a bit and ORs the corresponding flag into A.

| Result bit | Strobe to port 0x40 | Input port | Tested input bit | Derived scan-table index | Table handler |
|---:|---:|---:|---:|---:|---:|
| 0 | `0x20` | `0x82` | 1 | `0x26` | `0x0D68` |
| 1 | `0x04` | `0x82` | 1 | `0x56` | `0x0D68` |
| 2 | `0x02` | `0x80` | 1 | `0x6E` | `0x0D68` |
| 3 | `0x10` | `0x80` | 7 | `0x38` | `0x0D68` |
| 4 | `0x08` | `0x82` | 7 | `0x40` | `0x0D68` |

All five corresponding key-table slots point to the no-op/default handler `0x0D68` and contain `00 00`. This is consistent with dedicated modifier/state keys which are sampled into the event's modifier byte instead of being translated as printable characters. **Their legends and semantics (Shift/Ctrl/Alt/Caps/etc.) are not assigned yet.**

### How the scan index was derived

Routine `0x0BE2` starts row strobe E at `0x80`, rotates it right once per row, and reads port `0x82` followed by `0x80`. Routine `0x0BD3` examines the sampled byte from bit 7 down to bit 0 using `RLA`; D starts at `0x80` and increments once per candidate position. The dispatcher later clears bit 7 of the returned index before calculating `0x0964 + 4×index`.

For a row number 0–7, port bank 0 = `0x82`, bank 1 = `0x80`, and input bit number 0–7, the table index is:

`index = row × 16 + bank × 8 + (7 − input_bit)`

The five derived indices above therefore agree with the scanner's row/bank/bit order. The port bit is considered asserted by this routine when the tested bit reads as 1; physical electrical polarity still needs hardware/MAME confirmation.

### Event construction

At `0x0C69–0x0C73`, the debounce routine loads the stable matrix index from `D089`, calls `0x0B64`, moves the returned five-bit mask into H, and stores the resulting word at `D08A`. It then calls `0x0C22` to append that two-byte event to the FIFO. Thus the event carries the key index in L and the modifier/state mask in H.

## 2. Master Reset matrix patterns — raw patterns decoded

Routine `0x0CF6` is a full matrix-snapshot comparator, not a key translation routine:

1. If HL is zero, it substitutes the 16 zero bytes at `0x0CE6`.
2. It selects each of eight rows using strobe E = `0x80, 0x40, …, 0x01`.
3. For each row it reads port `0x82`, compares one byte at [HL], then reads port `0x80` and compares the next byte.
4. It returns with Z set only when all 16 bytes match.

The three patterns referenced by the Master Reset input loop at `0x2460–0x24D1` are:

| Pattern | Matrix positions asserted | Derived table entries |
|---|---|---|
| A, `0x23AC` | Row 2 / port 0x80 / bit 6; Row 6 / port 0x80 / bit 6 | `0x29` = z/Z; `0x69` = special code `0x1B` (ESC) |
| B, `0x23BC` | Pattern A plus Row 2 / port 0x82 / bit 0 | `0x27` = handler `0x0D94`, data `0xA7`; plus `0x29`, `0x69` |
| C, `0x23DC` | Row 2 / port 0x82 / bit 0 only | `0x27` = handler `0x0D94`, data `0xA7` |

This resolves the earlier coordinate concern: the raw pattern bytes really do map to table indices `0x29` and `0x69` (Z and ESC), with pattern B/C additionally including the special entry `0x27`. The earlier F1/F2-style naming was not supported and should be discarded. The physical legend/meaning of the `0xA7` special entry remains unknown.

The surrounding code is in the Master Reset password flow: nearby ROM strings include `Enter Master Reset Password`, `Password :`, and the expected string `ACE135`. The three snapshots are compared as raw matrix states before the normal password buffer check. Do not yet assign user-facing names to patterns A/B/C; the current evidence establishes their electrical contents and branch points only.

## 3. Static keyboard-event path and password success condition

The firmware contains a complete static path from matrix sampling to password comparison:

- `0x0C40`: debounce/repeat; after a stable key is accepted, calls `0x0B64` and `0x0C22`.
- `0x0C22`: appends a two-byte event at `D08D + 2×D0AD`; `D0AD` is the event count. The `0xFFF0` bound limits the queue to 16 events.
- `0x0CBE`: dequeues the first two-byte event, decrements `D0AD`, compacts the remaining queue, and returns the low event byte in A.
- `0x0D55`: if `D0AF` is nonzero, clears that pending-result word and returns without consuming FIFO. Otherwise calls `0x0CBE`; if an event exists, merges its modifier byte into `D088`, clears bit 7 from the key index, and dispatches through `0x0964 + 4×index`.
- `0x0DA4` and `0x0DBC`: symbol and alphabetic handlers inspect the modifier/state mask in `D088` to select base or alternate data. This confirms the five bits are not merely diagnostic; they influence translation.
- Master Reset input loop `0x2460–0x24A7`: after matrix-pattern checks and a FIFO-drain loop at `0x2478`, initializes the password buffer at `D03C`, uppercases it with `0x27C1`, calls the event dispatcher `0x0D55`, and invokes callback/dispatch code at `0x2682`.
- `0x278B` compares the buffer at `D03C` with the ROM string `ACE135` at `0x23A5`. At `0x24BE`, equality branches directly to `0x2523`.

Therefore `0x2523` is confirmed as the password-accepted path in this Master Reset routine. Its next actions include calling `0x22F3`, resetting SP to `D000`, clearing selected RAM state, and continuing through initialization routines. This is a static control-flow conclusion; no forced jump or password bypass was applied.

### What remains unresolved for point 3

Static analysis proves that the queue consumer and dispatcher are called in the password loop, but it does not yet prove that the running emulator reaches them at the expected time. The remaining question is dynamic: whether the FIFO receives the key event, whether `D0AF` diverts the dispatcher, whether the event handler updates the password buffer, and whether `0x2682` completes its callback path. That requires a controlled MAME trace after the Ghidra pass.

## Ghidra artifacts

- Persistent project: `willem/ghidra_writer/proj_keyboard_persist_20261126/`
- Script: `tools/ghidra/WriterKeyboardPhase1.java`
- Script output: `analysis_phase1_20261126/`
- Disassembly: `KEYBOARD_PHASE1_DISASSEMBLY_20261126.txt`
- Modifier mapping: `KEYBOARD_MODIFIER_SLOTS_20261126.tsv`
- Raw reset patterns: `KEYBOARD_RESET_PATTERNS_20261126.tsv`

The script completed successfully in Ghidra headless (`WRITER_KEYBOARD_PHASE1_OK`). It added labels/comments and seeded disassembly to the persistent Ghidra project. No ROM bytes, MAME source, emulator state, or hardware were modified.
