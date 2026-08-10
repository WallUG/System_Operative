#!/usr/bin/env python3
"""Convierte tools/font8x16_src.h (tabla VGA 8x16, fuente: dhepper/font8x16
MIT, https://github.com/hubenchang0515/font8x16) en la tabla C compacta
para user32.dll: solo los glifos ASCII 32..126 (95 x 16 bytes)."""
import re, sys

SRC = "tools/font8x16_src.h"
DSTS = ["user/win32/font8x16.h", "kernel/drivers/font8x16.h"]

data = open(SRC, "r", encoding="utf-8", errors="replace").read()
rows = re.findall(r"\{\s*(0x[0-9A-Fa-f]{2}(?:\s*,\s*0x[0-9A-Fa-f]{2}){15})\s*,?\s*\},\s*//\s*(0x[0-9A-Fa-f]{2})\b",
                  data)
if len(rows) != 128:
    sys.exit(f"error: esperaba 128 glifos, encontre {len(rows)}")
by_code = {int(code, 16): body for body, code in rows}

for dst in DSTS:
    with open(dst, "w", encoding="utf-8") as f:
        f.write(f"""/* MyOS - {dst} (GENERADO por tools/font2c.py, no editar)
 * Fuente VGA 8x16 (CP437), glifos ASCII 32..126 (95 x 16 bytes, 1 bit/px).
 * Origen: github.com/hubenchang0515/font8x16 (MIT). */
static const unsigned char font8x16_basic[95][16] = {{
""")
        for code in range(32, 127):
            f.write("    { " + by_code[code] + ", },\n")
        f.write("};\n")
    print(f"OK: {dst} generado ({95} glifos 32..126)")
