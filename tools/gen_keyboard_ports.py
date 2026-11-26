#!/usr/bin/env python3
"""Generate the confirmed INPUT_PORTS_START("writer") block from the
Ghidra-confirmed 128-entry key table (docs/KEYBOARD_TABLE_0964_20261126.tsv).

Index formula (derived and cross-checked against the printable-key table and
the Master Reset chord in docs/REVERSE_ENGINEERING_STATUS_20261126.md):
    index = row*16 + (15 - col)
where row/col are the MAME ROW{row}/C{col} matrix coordinates already used
in src/mame/akt/writer.cpp (ROW0..ROW7, bit 0..15 = C0..C15).
"""
import csv

TSV = "docs/KEYBOARD_TABLE_0964_20261126.tsv"

rows = {}
with open(TSV, newline="") as f:
    r = csv.DictReader(f, delimiter="\t")
    for row in r:
        idx = int(row["index"], 16)
        rows[idx] = row

# KEYCODE candidates for printable chars (only used when legend is confirmed printable)
KEYCODE = {
    'a':'KEYCODE_A','b':'KEYCODE_B','c':'KEYCODE_C','d':'KEYCODE_D','e':'KEYCODE_E',
    'f':'KEYCODE_F','g':'KEYCODE_G','h':'KEYCODE_H','i':'KEYCODE_I','j':'KEYCODE_J',
    'k':'KEYCODE_K','l':'KEYCODE_L','m':'KEYCODE_M','n':'KEYCODE_N','o':'KEYCODE_O',
    'p':'KEYCODE_P','q':'KEYCODE_Q','r':'KEYCODE_R','s':'KEYCODE_S','t':'KEYCODE_T',
    'u':'KEYCODE_U','v':'KEYCODE_V','w':'KEYCODE_W','x':'KEYCODE_X','y':'KEYCODE_Y',
    'z':'KEYCODE_Z',
    '0':'KEYCODE_0','1':'KEYCODE_1','2':'KEYCODE_2','3':'KEYCODE_3','4':'KEYCODE_4',
    '5':'KEYCODE_5','6':'KEYCODE_6','7':'KEYCODE_7','8':'KEYCODE_8','9':'KEYCODE_9',
    ',':'KEYCODE_COMMA','.':'KEYCODE_STOP','/':'KEYCODE_SLASH',';':'KEYCODE_COLON',
    "'":'KEYCODE_QUOTE','[':'KEYCODE_OPENBRACE',']':'KEYCODE_CLOSEBRACE',
    '-':'KEYCODE_MINUS','=':'KEYCODE_EQUALS','`':'KEYCODE_TILDE','\\':'KEYCODE_BACKSLASH',
    ' ':'KEYCODE_SPACE',
}

# Known special-code semantics, confirmed by data byte value from the table.
SPECIAL_LEGEND = {
    0x08: ("Backspace (codigo 0x08, fisico sin confirmar)", 'KEYCODE_BACKSPACE'),
    0x09: ("Tab (codigo 0x09, fisico sin confirmar)", 'KEYCODE_TAB'),
    0x1B: ("ESC (codigo 0x1B, Master Reset key 2)", 'KEYCODE_ESC'),
}

# The 5 modifier-bit scan positions (0x0B64), statically confirmed, legend unknown.
MODIFIER_INDEX = {0x26: 0, 0x56: 1, 0x6E: 2, 0x38: 3, 0x40: 4}

MASTER_RESET_Z_INDEX = 0x29  # R2C6, same as the 'z' key
HANDLER_NOOP = "0D68"

def classify(idx):
    row = rows.get(idx)
    if row is None:
        return ("(sin dato en tabla)", None)
    handler = row["handler"]
    base = row["base"]
    shifted = row["shifted"]
    data0 = int(row["data0_hex"], 16)

    if idx in MODIFIER_INDEX:
        return (f"modificador bit{MODIFIER_INDEX[idx]} (muestreado en 0x0B64, sin leyenda fisica)", None)

    if handler == HANDLER_NOOP and base == '.' and shifted == '.':
        return ("(sin tecla asignada)", None)

    if base not in ('.', ' ') and shifted not in ('.',) and len(base) == 1:
        legend = f"{base}/{shifted}" if shifted not in ('.', base) else base
        code = KEYCODE.get(base.lower())
        extra = ""
        if idx == MASTER_RESET_Z_INDEX:
            extra = " (Master Reset key 1)"
        return (legend + extra, code)

    if base == ' ' and shifted == ' ':
        return ("space", 'KEYCODE_SPACE')

    if data0 in SPECIAL_LEGEND:
        legend, code = SPECIAL_LEGEND[data0]
        return (legend, code)

    if handler == "0D8C":
        return (f"toggle de estado (handler 0D8C, dato 0x{data0:02X}, sin leyenda)", None)

    # Generic opaque/special code, handler-dependent, legend not established.
    return (f"codigo especial 0x{data0:02X} (handler {handler}, sin leyenda fisica)", None)


lines = []
lines.append("// Matrix is active-high (pressed = 1). Undefined bits read 0.")
lines.append("// Full 8x16 matrix populated from the Ghidra-confirmed 128-entry key")
lines.append("// dispatch/translation table at ROM 0x0964 (docs/KEYBOARD_TABLE_0964_20261126.tsv,")
lines.append("// docs/REVERSE_ENGINEERING_STATUS_20261126.md). Index formula, derived and")
lines.append("// cross-checked against the printable-key table and the Master Reset chord:")
lines.append("//     index = row*16 + (15 - col)")
lines.append("// This is static-ROM-confirmed legend data, NOT yet validated against physical")
lines.append("// keycap legends or electrical polarity -- that follow-up still needs multimeter")
lines.append("// continuity / controlled single-key hardware traces (pendiente, no asumir).")
lines.append("// The 5 modifier-bit positions and several opaque special codes are confirmed")
lines.append("// scan positions with NO assigned physical legend; do not invent one.")
lines.append("static INPUT_PORTS_START( writer )")
for row in range(8):
    lines.append(f'\tPORT_START("ROW{row}")')
    for col in range(16):
        idx = row * 16 + (15 - col)
        legend, code = classify(idx)
        mask = 1 << col
        name = f"R{row}C{col} {legend}"
        if code:
            lines.append(f'\tPORT_BIT(0x{mask:04x}, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("{name}") PORT_CODE({code})')
        else:
            lines.append(f'\tPORT_BIT(0x{mask:04x}, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("{name}")')
    lines.append("")
lines[-1] = "INPUT_PORTS_END"

with open("/tmp/new_input_ports.txt", "w") as f:
    f.write("\n".join(lines) + "\n")

print("OK, wrote", len(lines), "lines")
