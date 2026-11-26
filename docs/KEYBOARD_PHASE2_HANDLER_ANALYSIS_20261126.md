# Writer Instructor — keyboard translation and special-key analysis, phase 2

Date: 2026-11-26
ROM: `akt_writer_v3.70_m27c801.bin`
SHA-256: `f4086f4c792f72b7ee7a1a32334f8a3df9e8dedee3eaa20071915adee1540da9`
Ghidra: 12.1.3, Z80 little-endian 16-bit, persistent project `proj_keyboard_persist_20261126`.
Method: static ROM analysis only. No emulator or hardware validation in this phase.

## 1. Keyboard output is a two-layer interface

The scan/translation layer does not insert characters into the document. It returns a 16-bit result in HL:

- For alphabetic and symbol keys, L is the selected 7-bit ASCII byte; H may carry modifier-state bits.
- Handler `0x0D94` returns an opaque special/action code in L with H=0.
- Handler `0x0D9A` returns an opaque special/action code in L and the current five-bit modifier state in H.
- The editor then interprets selected results using inline callback tables. This is why an entry's `data0` must not automatically be treated as a printable character.

The common keyboard reader is `0x0E08` (API vector `0x01D0`). It invokes dispatcher `0x0D55`, returns the 16-bit result in HL, and may wait/sleep while no result is available. It is a result reader, not the document insertion routine.

## 2. Handler behavior recovered

### `0x0D68` — no output

Default/no-output handler. Many unused matrix positions and modifier-only positions point here.

### `0x0D8C` — state XOR

Reads the entry's first data byte and XORs it into RAM byte `D088`. The table currently has one entry using this handler: index `0x5A`, data `0x20`. This is not the printable Space key: the actual printable space is index `0x7B`, routed through the symbol handler with data `0x20/0x20`. Index `0x5A` toggles state bit `0x20` in `D088`; its physical legend/function is still unknown.

### `0x0D94` — special code, no modifier byte

Returns entry byte +2 in L and zero in H. The known table entry is index `0x27`, code `0xA7`.

### `0x0D9A` — special code with modifier state

Returns entry byte +2 in L and `D088 & 0x1F` in H. It is used by many non-printable keys. Their complete logical matrix positions and codes are in `KEYBOARD_SPECIAL_KEYS_20261126.tsv`.

### `0x0DA4` — symbol/punctuation

Starts with entry byte +2 (base). If any of modifier bits 2–4 are set, it retains the base byte. Otherwise, if either modifier bit 0 or bit 1 is set, it selects entry byte +3. Thus the alternate punctuation column is selected by bits 0/1 only when bits 2–4 are clear.

### `0x0DBC` — alphabetic

- If `D088=0`, returns entry byte +2.
- If any state is active, initially selects entry byte +3.
- It then masks modifier bits 2–4 into H.
- If mode byte `D085=0`, it accepts that selection.
- If `D085!=0`, and the low two modifier bits are both clear or both set, it accepts the selection.
- If `D085!=0` and exactly one of modifier bits 0/1 is set, it checks the selected alternate byte against one of two ROM membership sets. If absent, the handler suppresses output by taking the no-output path.

The membership sets are not accent maps:

| ROM range | Length | Contents (ASCII) |
|---|---:|---|
| `0x0D24–0x0D3E` | 27 | ` ^&*()_+YUIOP{}|HJKL:NM<>?"` |
| `0x0D3F–0x0D54` | 22 | ` ~!@#$%QWERTASDFGZXCVB` |

They are consulted as membership filters in the special alphabetic modifier/mode path. Their contents split into two recognizable groups: the first contains the right-hand letter group (YUIOP/HJKL/NM) and associated punctuation; the second contains the left-hand letter group (QWERT/ASDFG/ZXCVB) and associated punctuation. The handler selects the first set when modifier bit 0 is set and the second when bit 1 is set. This strongly suggests two side-specific shift channels, but the physical legends and user-facing mode name are not yet proven. We therefore record this as a structural hypothesis, not a final Shift/CapsLock assignment.

## 3. Keyboard mode configuration

Routine `0x0E77` (API vector `0x01B0`) takes a 16-bit value in HL and distributes its low three bits:

| Input bit | RAM destination | Transformation |
|---:|---|---|
| 0 | `D085` | stores 0 or 1; gates the alphabetic filter path |
| 1 | `D086` | stores 0x00 or 0xFF (sign extension); ORed into scan/index resolution at `0x0BD3` |
| 2 | `D087` | stores 0xE0 when clear, 0xC0 when set; used as a preservation mask in dispatcher `0x0D55` |

Keyboard reset routine `0x0E9B` clears FIFO/pending state and modifier state, then calls `0x0E77` with HL=0. Therefore the reset defaults are `D085=0`, `D086=0`, and `D087=0xE0`. This proves the initialization values, not the user-visible names of the three mode bits.

RAM roles now supported by cross-references:
- `D088`: modifier/state byte merged by the event dispatcher and consumed by symbol/alphabetic handlers.
- `D0AF`: pending 16-bit result; dispatcher clears/returns it before reading the FIFO.
- `D085–D087`: keyboard mode/configuration bytes described above.
- `D0B5`: saved cursor-context pointer used by the result path; it is not the keycode.

## 4. Editor callback layer and special codes

Routine `0x2682` is a generic inline-table switch. Each call is followed by pairs of 16-bit result code and 16-bit handler address, terminated by a zero result code.

The editor input loop at `0x25DE` calls keyboard reader `0x01D0`, then dispatches through the callback table at `0x25E8`. The table has five explicit special-code entries and a zero terminator; unmatched results fall through to the inline default insertion handler at `0x25FD`:

| Result code | Callback | Static behavior |
|---:|---:|---|
| `0x00F8` | `0x2673` | Stores the returned result as pending, then restores editor cursor context |
| `0x00A7` | `0x2673` | Same callback as code `0x00F8` |
| `0x001B` | `0x266E` | Writes zero at the editor's current character pointer |
| `0x000D` | `0x2677` | Restores/updates editor cursor context |
| `0x0008` | `0x2636` | Backspace: moves cursor backward and clears the removed character |

The Master Reset password loop has a separate callback table at `0x24A7`: `0x00F8 -> 0x2557`, `0x00A7 -> 0x2563`, and `0x001B -> 0x2557`, followed by a zero terminator. These are context-specific branches; they do not establish physical key legends.

If no special-code entry matches, the inline default handler beginning at `0x25FD` accepts only result bytes in the range `0x20–0x7F`, rejects results with modifier bits 2–4 set, and inserts the accepted low byte into the editor buffer at `D1E9`. The special-code table therefore handles non-printable actions before the default ASCII insertion path.

The table's familiar control codes provide useful cross-checks: `0x09` (Tab), `0x0D` (CR/Enter), `0x08` (Backspace), and `0x1B` (ESC). Other codes from `0x8E` through `0xFF` are preserved as opaque special codes until their callers and physical legends are independently identified.

## 5. Accented characters — current evidence and boundary

**There is no direct accented-character entry in the extracted 128-entry keyboard translation table.** Every printable base/alternate byte in the alphabetic and symbol handlers is in the 7-bit ASCII range. In addition, the generic editor callback accepts only `0x20–0x7F`; high-bit bytes are not inserted as ordinary text by this path. The two extra tables at `0x0D24` and `0x0D3F` are modifier-selection membership sets, not mappings such as `a -> á` or dead-key composition tables.

The recovered keyboard handlers do not implement a visible dead-key composition sequence. They return one ASCII byte or one opaque action code, with modifier state carried separately. This establishes that the normal keyboard-to-editor text path is 7-bit ASCII, not an 8-bit Latin-1 input path. It is still not sufficient to conclude that the complete Writer application cannot produce accents: an application/editor layer could interpret a special code, invoke a character-set routine, or provide a separate composition mechanism. The meaning of `0xA7` and several other special codes remains unresolved.

To close the accent question without an emulator, the next static-analysis targets are:
1. Follow all references to special result `0xA7` beyond the two callback tables.
2. Identify the editor's character insertion and character-set conversion routines called by the editor callbacks.
3. Search for ROM tables containing high-bit character codes and determine whether any are reached from keyboard/editor paths.
4. Determine whether the Writer firmware exposes a compose/dead-key or alternate-character API through its ROM jump table.

Until those steps are complete, classify accent support as **not present in the direct keymap; application-level support unknown**.

## 6. Special-key index inventory

`KEYBOARD_SPECIAL_KEYS_20261126.tsv` records every keymap entry handled by `0x0D94`, `0x0D9A`, or `0x0D8C`, with:
- 7-bit table index;
- logical row;
- input bank and I/O port;
- row strobe written to port `0x40`;
- input bit;
- handler and raw data byte.

Coordinates are logical scan coordinates derived from the ROM algorithm. They are not yet physical key legends or hardware-verified labels.

## 7. Ghidra artifacts and integrity

- Persistent Ghidra project: `willem/ghidra_writer/proj_keyboard_persist_20261126/`
- Phase-2 script: `tools/ghidra/WriterKeyboardPhase2.java`
- Phase-2 disassembly: `KEYBOARD_PHASE2_HANDLERS_20261126.txt`
- Complete 128-entry re-extraction: `KEYBOARD_PHASE2_TABLE_20261126.tsv`
- Special-key position/code map: `KEYBOARD_SPECIAL_KEYS_20261126.tsv`

The phase-2 headless script completed successfully and saved labels/comments to the persistent project. The canonical human-readable table was re-exported with correct TSV quoting and validated at 128 rows; its bytes match the phase-2 extraction in all 128 entries. No ROM, emulator, or hardware state was modified.
