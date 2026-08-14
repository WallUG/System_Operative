#!/usr/bin/env python3
"""Genera el filesystem MEFS v2 (MyOS Easy FS) como imagen de 512 B/sector.

Formato (relativo al inicio del FS, LBA absoluto MEFS_FS_START):
  sector 0        : superbloque (512 B)
  sector 1..+dir  : directorio: MEFS_MAX_FILES entradas de 32 B
  +bitmap         : bitmap de bloques libres (1 bit por sector de datos)
  +data           : datos de los archivos (bloques asignados via bitmap)

Superbloque:
  0  magic "MEFS02\\n\\0"
  8  uint32 num_files
  12 uint32 dir_lba
  16 uint32 dir_size
  20 uint32 bitmap_lba
  24 uint32 bitmap_sectors
  28 uint32 data_start
  32 uint32 fs_capacity

Entrada de directorio (32 B): name[16], size, lba, flags, parent.
  flags bit0 = IS_DIR; parent = indice del padre (0xFFFFFFFF = raiz).

Uso: makefs.py <archivos>... -o <fs.bin>
Cada archivo se incluye con su nombre base, en la raiz. Capacidad del FS =
fs_capacity (por defecto FS_SECTORS si se pasa -c).
"""
import argparse
import os
import struct

LBA_FS_START = 129
MAGIC = b"MEFS02\n\0"
DIR_ENTRY = 32
MAX_FILES = 64
MEFS_ROOT = 0xFFFFFFFF
FLAG_DIR = 1


def build(files, out, capacity, boot_gui=True):
    # layout: superbloque, directorio (fijo MAX_FILES entradas), bitmap, datos
    dir_size = MAX_FILES * 32
    dir_sectors = (dir_size + 511) // 512
    bitmap_sectors = (capacity + 4095) // 4096
    if bitmap_sectors == 0:
        bitmap_sectors = 1
    bitmap_lba = LBA_FS_START + 1 + dir_sectors
    data_start = bitmap_lba + bitmap_sectors

    # asigna bloques de datos (contiguos, uno por archivo)
    n = len(files)
    entries = []          # (name, size, lba, flags, parent)
    block = 0
    for path in files:
        name = os.path.basename(path).encode()
        if len(name) > 15:
            name = name[:15]
        size = os.path.getsize(path)
        nb = (size + 511) // 512
        entries.append((name, size, data_start + block, 0, MEFS_ROOT))
        block += nb

    # bitmap: marca usados los bloques de los archivos
    bitmap = bytearray((capacity + 7) // 8)
    for name, size, lba, flags, parent in entries:
        nb = (size + 511) // 512
        base = lba - data_start
        for i in range(base, base + nb):
            bitmap[i // 8] |= 1 << (i % 8)

    dir_bytes = b"".join(
        name.ljust(16, b"\0") + struct.pack("<IIII", size, start, fl, par)
        for name, size, start, fl, par in entries
    ).ljust(dir_size, b"\0")

    sb = (MAGIC +
          struct.pack("<IIIIIII",
                      n,                       # num_files
                      LBA_FS_START + 1,        # dir_lba
                      dir_size,                # dir_size
                      bitmap_lba,              # bitmap_lba
                      bitmap_sectors,          # bitmap_sectors
                      data_start,              # data_start
                      capacity))               # fs_capacity
    sb = sb.ljust(512, b"\0")
    sb = bytearray(sb)
    sb[36] = 1 if boot_gui else 0          # Fase 22: autoboot del escritorio
    sb = bytes(sb)

    bitmap_bytes = bytes(bitmap).ljust(bitmap_sectors * 512, b"\0")

    data = b""
    for path in files:
        with open(path, "rb") as f:
            blob = f.read()
        data += blob.ljust(((len(blob) + 511) // 512) * 512, b"\0")

    with open(out, "wb") as f:
        f.write(sb)
        f.write(dir_bytes)
        f.write(bitmap_bytes)
        f.write(data)
    total = len(sb) + len(dir_bytes) + len(bitmap_bytes) + len(data)
    print(f"MEFS: {n} archivo(s) -> {out} "
          f"(fs_capacity={capacity} sectores, data_start={data_start}, "
          f"bitmap {bitmap_sectors} sectores, {total} bytes)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", required=True)
    ap.add_argument("-c", type=int, default=1400, help="fs_capacity (sectores)")
    ap.add_argument("-b", type=int, default=1,
                    help="boot_gui (1 = autoboot escritorio, 0 = consola)")
    args = ap.parse_args()
    build(args.files, args.o, args.c, bool(args.b))


if __name__ == "__main__":
    main()
