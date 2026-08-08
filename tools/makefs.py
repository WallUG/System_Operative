#!/usr/bin/env python3
"""Genera el filesystem MEFS (MyOS Easy FS) como imagen de 512 B/sector.

Formato (relativo al inicio del FS):
  sector 0 : superbloque: 8 B magic 'MEFS01\\n', uint32 num_files,
             uint32 dir_lba (absoluto), uint32 dir_size (bytes)
  sector 1 : directorio: entradas de 32 B: name[16], size, lba, unused
  siguientes: datos de cada archivo en sectores contiguos (lba absoluto)

Uso: makefs.py <archivos>... -o <fs.bin>
Cada archivo se incluye con su nombre base. El FS empieza en el sector
LBA_FS_START del disco final (os-image.bin: boot 1 sector + kernel 63).
"""
import argparse
import os
import struct

LBA_FS_START = 129
# 8 bytes exactos (incluye el \0 final; el kernel compara 8 bytes).
MAGIC = b"MEFS01\n\0"
DIR_ENTRY = 32


def build(files, out):
    # 1. superbloque + directorio: reservamos 2 sectores, luego datos.
    entries = []
    lba = LBA_FS_START + 2
    for path in files:
        name = os.path.basename(path).encode()
        if len(name) > 15:
            name = name[:15]
        size = os.path.getsize(path)
        entries.append((name, size, lba))
        lba += (size + 511) // 512

    dir_bytes = b"".join(
        name.ljust(16, b"\0") + struct.pack("<IIII", size, start, 0, 0)
        for name, size, start in entries
    )
    dir_bytes = dir_bytes.ljust(512, b"\0")

    sb = MAGIC + struct.pack(
        "<III", len(entries), LBA_FS_START + 1, len(dir_bytes)
    )
    sb = sb.ljust(512, b"\0")

    data = b""
    for path in files:
        with open(path, "rb") as f:
            blob = f.read()
        data += blob.ljust(512, b"\0")

    with open(out, "wb") as f:
        f.write(sb)
        f.write(dir_bytes)
        f.write(data)
    print(f"MEFS: {len(entries)} archivo(s) -> {out} "
          f"({os.path.getsize(out)} bytes, {lba - LBA_FS_START} sectores)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", required=True)
    args = ap.parse_args()
    build(args.files, args.o)


if __name__ == "__main__":
    main()
