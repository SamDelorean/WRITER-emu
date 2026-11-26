# Writer Instructor — display path and accent support, phase 3

Date: 2026-11-26
ROM: `akt_writer_v3.70_m27c801.bin`
Ghidra: 12.1.3, persistent project `proj_keyboard_persist_20261126`.
Method: static ROM analysis only; no emulator or hardware access used.

## 1. Character output to the display

Routine `0x0812` is the low-level display output primitive used by the editor and UI:

- If bit 7 of L is clear, it treats L as character data, ORs bit 7 (`L | 0x80`), and sends it through the display data port `0xC0`.
- It then performs the controller handshake using port `0xC2` and the control value at `D07D`.
- If bit 7 of L is set, it branches to `0x0837` and uses the value as a display-controller command instead of text data.

Therefore, the normal display text interface is a 7-bit character interface. A byte with bit 7 set is interpreted as a command/control value, not as a Latin-1 accented character. This is consistent with the editor input path at `0x25FD`, which accepts ordinary text only in the range `0x20–0x7F`.

## 2. Built-in custom glyphs

The ROM also has a custom glyph loader:

- `0x0874` accepts a 9-byte record: glyph slot followed by eight bitmap rows.
- `0x0890` writes the same glyph definition to both display-controller ports (`0xC0` and `0xC2`).
- `0x08C5` initializes four built-in glyph slots from records at `0x08A1`, `0x08AA`, `0x08B3`, and `0x08BC`.

The four slots are `0x0C–0x0F`. Their bitmaps are:

| Slot | Eight bitmap rows |
|---:|---|
| `0x0C` | `00 00 00 00 00 00 00 00` |
| `0x0D` | `1F 1F 1F 1F 00 00 00 00` |
| `0x0E` | `00 00 00 00 1F 1F 1F 1F` |
| `0x0F` | `1F 1F 1F 1F 1F 1F 1F 1F` |

These are blank/solid-block style glyphs, not accented-letter glyphs. The static references found in this path initialize these four fixed display glyphs; no connection from the keyboard translation table to an accent glyph was found.

## 3. Conclusion on accents

The evidence now covers both ends of the normal low-ROM text path:

1. Keyboard/editor input filters ordinary inserted characters to `0x20–0x7F`.
2. The display output routine treats bit-7-set values as controller commands, not ordinary text bytes.
3. The four custom glyphs initialized by this ROM path are blank/block patterns, not á/é/í/ó/ú/ñ glyphs.

**Conclusion:** the normal keyboard-to-editor-to-display path is 7-bit ASCII. It does not provide direct Latin-1/8-bit accented-character entry or display through the ordinary character path.

This does not rule out every possible application-specific method. An application could draw a character as a bitmap, use a different banked-ROM routine, or invoke another display/controller path. No such route has yet been connected to the keyboard input path. The opaque keyboard code `0xA7` is dispatched as an action/result callback, not inserted as an accented character.

## 4. Remaining static scope

The Ghidra program currently models the low 64 KiB address window of the 1 MiB ROM image. The next no-emulator task, if we need to establish application-wide accent capability, is to identify ROM banking and analyze the other ROM banks for alternate text/editor routines or character-generation tables. Until that mapping is established, the conclusion above is specifically for the normal keyboard/display path recovered in the low 64 KiB window.

## Ghidra artifacts

- Script: `tools/ghidra/WriterKeyboardPhase3.java`
- Disassembly: `KEYBOARD_PHASE3_DISPLAY_DISASSEMBLY_20261126.txt`
- Built-in glyph records: `KEYBOARD_PHASE3_BUILTIN_GLYPHS_20261126.tsv`

The headless script completed successfully and saved labels/comments to the persistent Ghidra project. No ROM bytes, emulator state, or hardware state were modified.
