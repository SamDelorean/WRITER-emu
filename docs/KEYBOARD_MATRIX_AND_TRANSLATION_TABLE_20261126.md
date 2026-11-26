# Writer Instructor — reverse engineering of keyboard matrix and key translation table

Date: 2026-11-26  
Target: `akt_writer_v3.70_m27c801.bin`  
ROM SHA-256: `f4086f4c792f72b7ee7a1a32334f8a3df9e8dedee3eaa20071915adee1540da9`  
Ghidra: 12.1.3, processor language `z80:LE:16:default`  
Scope: keyboard path in the low 64 KiB address window.

## Result

**The firmware contains an explicit 128-entry key dispatch/translation table at ROM offset `0x0964`.** It is not a flat ASCII lookup table. Each entry is four bytes:

| Offset within entry | Meaning |
|---:|---|
| +0, +1 | Little-endian handler address |
| +2 | Base/unmodified data byte |
| +3 | Alternate data byte (commonly Shift variant) |

The table occupies `0x0964–0x0B63` (512 bytes; 128 × 4). Full extraction: `KEYBOARD_TABLE_0964_20261126.tsv`.

Ghidra headless project and scripted outputs:
- Project: `/home/spc/projects/WRITER_INSTRUCTOR/willem/ghidra_writer/proj_keyboard_final_20261126`
- Script: `tools/ghidra/WriterKeyboardAnalysis.java` (working copy: `/home/spc/projects/WRITER_INSTRUCTOR/willem/ghidra_writer/WriterKeyboardAnalysis.java`)
- Output: `/home/spc/projects/WRITER_INSTRUCTOR/willem/ghidra_writer/analysis_keyboard_20261126/`; the routine listing is also versioned as `docs/KEYBOARD_ROUTINES_DISASSEMBLY_20261126.txt`.
- The 1 MiB ROM file was imported with the Z80 16-bit language; Ghidra reports clipping to its 64 KiB address space. All keyboard routines/table discussed here are below `0x10000`. This pass deliberately used `-noanalysis` and seeded disassembly/labels for the known routines; it is a focused analysis, not a claim that all banked ROM content has been analyzed.

## Reconstructed execution path

1. **Matrix scan — `0x0BE2`.** Writes `0xFF` to I/O port `0x40`, reads ports `0x80` and `0x82`, then asserts one row strobe at a time starting with `E=0x80`. For each row it samples both column banks and rotates the strobe right.
2. **Bit/index resolver — `0x0BD3`.** Uses the mask bytes at `0x0BC3` onward and increments the key index while testing the sampled input bits. The returned low seven bits index the 128-entry table; bit 7 is cleared by the dispatcher before lookup. Do not interpret bit 7 as make/break until separately proven.
3. **Modifier sampling — `0x0B64`.** Reads five selected strobe/input combinations through `0x40`, `0x80`, and `0x82`, assembling a five-bit modifier/state mask. The individual semantic names (Shift, Ctrl, etc.) still need to be assigned from controlled observations.
4. **Debounce/repeat — `0x0C40`.** Tracks current and previous matrix state in RAM around `D089–D08C`; calls the scanner and modifier reader, then passes a 16-bit event to the FIFO producer.
5. **FIFO producer — `0x0C22`.** Appends two-byte events at `D08D + 2×D0AD`; `D0AD` is the event count. The `0xFFF0` bound gives a 16-event/32-byte queue.
6. **Dispatcher — `0x0D55`.** If `D0AF` contains a pending result, it clears and returns that path. Otherwise it calls FIFO consumer `0x0CBE`, clears bit 7 of the event index, calculates `0x0964 + 4×index`, loads the handler pointer and jumps indirectly through it.
7. **Character/action handlers.**
   - `0x0D68`: no-op/default entry.
   - `0x0D8C`: XOR-toggles a state bit using the entry data.
   - `0x0D94`: control/action path.
   - `0x0D9A`: special-key path.
   - `0x0DA4`: symbol path; selects the alternate byte under modifier conditions.
   - `0x0DBC`: alphabetic path; selects the alternate letter under modifier conditions.

The second data byte is therefore not universally “Shift”; it is handler-dependent. For alphabetic and punctuation entries it commonly contains the upper/shifted character. Special entries use their data byte as an action/control code.

## Printable entries by logical matrix row

Coordinates below use the current logical scan convention: `row = index >> 4`; scan slot is `index & 0x0F`; logical column label is provisionally `C = 15 - scan_slot`. The complete TSV is authoritative for all 128 entries, including special/no-op slots.

| Row | Printable key pairs (base / alternate) |
|---:|---|
| R0 | C14 r/R; C13 u/U; C12 i/I; C11 o/O; C10 e/E; C06 q/Q; C05 w/W; C03 p/P |
| R1 | C14 4/$; C13 7/&; C12 8/*; C11 9/(; C10 3/#; C06 1/!; C05 2/@; C03 0/) |
| R2 | C14 v/V; C13 m/M; C12 ,/<; C11 ./>; C10 c/C; C06 z/Z; C05 x/X |
| R3 | C14 f/F; C13 j/J; C12 k/K; C11 l/L; C10 d/D; C06 a/A; C05 s/S; C04 backslash/pipe; C03 semicolon/colon |
| R4 | C14 5/%; C13 6/^; C12 =/+; C06 `/~; C03 -/_ |
| R5 | C14 t/T; C13 y/Y; C12 ]/}; C03 [/{ |
| R6 | C14 g/G; C13 h/H; C03 apostrophe/quote |
| R7 | C14 b/B; C13 n/N; C04 space; C03 slash/? |

Other table entries route to special/control handlers or no-op. They are preserved verbatim in the TSV rather than assigned speculative key names.

## Password-key cross-check

The already instrumented event trace identifies these six characters:

| Character | Matrix position (existing trace convention) | Table index | Event low byte | Entry data |
|---|---|---:|---:|---|
| A | R3C6 | `0x39` | `0xB9` | `aA` |
| C | R2C10 | `0x25` | `0xA5` | `cC` |
| E | R0C10 | `0x05` | `0x85` | `eE` |
| 1 | R1C6 | `0x19` | `0x99` | `1!` |
| 3 | R1C10 | `0x15` | `0x95` | `3#` |
| 5 | R4C14 | `0x41` | `0xC1` | `5%` |

The matrix scan/FIFO production and the character dispatch table are now independently supported by static ROM analysis and the earlier MAME input trace. Authentic entry of `ACE135` and ROM password acceptance remain unproven because the foreground consumer was not observed consuming the post-prompt FIFO event in the last controlled run.

## Master Reset cross-check (resolved statically)

**Resolved by the Ghidra phase-1 analysis (2026-11-26):** routine `0x0CF6` compares full 16-byte matrix snapshots, and the raw patterns at `0x23AC`, `0x23BC`, and `0x23DC` map to table entries `0x29` (z/Z), `0x69` (special code `0x1B`, ESC), and `0x27` (special handler `0x0D94`, data `0xA7`). The physical legend/meaning of entry `0x27` remains unresolved. Earlier F1/F2-style naming was unsupported and is withdrawn. See `KEYBOARD_PHASE1_GHIDRA_20261126.md` for the raw pattern bytes and branch analysis.

## Next steps — static work before emulator access

1. Continue tracing the opaque special codes (`0x8E`, `0x97`, `0x98`, `0x9B`, `0x9D–0x9F`, `0xAD`, `0xC4`…`0xFF`) through application/editor callback tables.
2. Trace character-set conversion and glyph/font paths to determine whether the Writer application has an alternate accent-entry mechanism. The direct keyboard-to-editor path currently resolves to 7-bit ASCII (`0x20–0x7F`); application-level accent support remains unknown.
3. Preserve the two side-specific modifier-channel inference as provisional until their physical legends are identified. Bits 2–4 also remain unnamed.
4. When emulator access returns, validate physical legends, modifier semantics, special keys, and any accent-entry sequence. Do not alter ROM or force FIFO consumption before that.
5. Keep keyboard translation separate from the outstanding `Missing Application BIOS, 0x203B` boot blocker.

See `KEYBOARD_PHASE2_HANDLER_ANALYSIS_20261126.md` for the handler-level findings and `KEYBOARD_SPECIAL_KEYS_20261126.tsv` for the special-key scan positions/codes.

No ROM bytes, MAME driver behavior, or hardware state were modified by this analysis.
