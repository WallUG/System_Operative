#!/usr/bin/env python3
"""Genera un ISO9660 booteable (El Torito, no-emulation) en Python puro.

El "boot image" es os-image.bin completo (boot + kernel + fs.bin): la
BIOS (SeaBIOS/QEMU) lo carga entero en 0x7C00 con load segment 0x07C0 y
boot.asm detecta modo CD (dl >= 0xE0), copia el kernel a 0x10000 y la
imagen MEFS a 0x50000 (mefs_init_mem en el kernel).

Estructura (sectores de 2048 B):
   0-15  area de sistema (ceros)
   16    PVD (Primary Volume Descriptor)
   17    Boot Record ("EL TORITO SPECIFICATION")
   18    Volume Descriptor Set Terminator
   19    Boot Catalog (validation + initial entry)
   20..  boot image (os-image.bin, pad a 2048)
   ..    archivos de usuario (hello.elf, fork.elf, exec.elf)
   ..    directorio raiz
   ..    path tables L + M

Uso: makeiso.py os-image.bin <archivos>... -o myos.iso
"""
import argparse
import os
import struct

SECTOR = 2048
CD_MAGIC = b"CD001"
LOAD_SEGMENT = 0x07C0              # segmento en parrafos: 0x07C0 -> 0x7C00


def pad(data, size=SECTOR):
    return data + b"\0" * ((-len(data)) % size)


def b16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def b32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def dir_record(lba, length, ident, is_dir=False):
    """Registro de directorio ISO9660 (33 B + identificador + pad par)."""
    rec = bytes([33 + len(ident)]) + b"\0" + \
          b32(lba) + b32(length) + \
          b"\0" * 7 + \
          bytes([0x02 if is_dir else 0x00]) + b"\0\0" + \
          b16(1) + \
          bytes([len(ident)]) + ident
    return rec + (b"\0" if len(rec) % 2 else b"")


def make_pvd(volume_size, root_lba, root_size, lt_lba, mt_lba, lt_size):
    b = bytearray(SECTOR)
    b[0] = 1
    b[1:6] = CD_MAGIC
    b[6] = 1
    b[7:39] = b"MYOS".ljust(32, b" ")
    b[39:71] = b"MYOS_CD".ljust(32, b" ")
    b[80:88] = b32(volume_size)
    b[120:124] = b16(1)                    # volumenes del conjunto
    b[124:128] = b16(1)                    # numero de volumen
    b[128:132] = b16(SECTOR)               # tamano de bloque logico
    b[132:140] = b32(lt_size)              # tamano path table
    b[140:144] = struct.pack("<I", lt_lba) # L path table (LE, 4 B)
    b[148:152] = struct.pack(">I", mt_lba) # M path table (BE, 4 B)
    b[156:190] = dir_record(root_lba, root_size, b"\0", is_dir=True)
    b[190:318] = b"MYOS".ljust(128, b" ")
    # fecha de creacion (el resto de campos de fecha a cero)
    import time
    t = time.localtime()
    b[446:463] = bytes([t.tm_year - 1900, t.tm_mon, t.tm_mday,
                        t.tm_hour, t.tm_min, t.tm_sec, 0]) + b"\0" * 10
    b[481] = 1                             # version del file structure
    return bytes(b)


def make_boot_record(catalog_lba):
    b = bytearray(SECTOR)
    b[0] = 0
    b[1:6] = CD_MAGIC
    b[6] = 1
    # SeaBIOS valida con strcmp("CD001\001EL TORITO SPECIFICATION"): el
    # campo debe terminar con NUL, NO con espacios.
    b[7:30] = b"EL TORITO SPECIFICATION"
    struct.pack_into("<I", b, 71, catalog_lba)
    return bytes(b)


def make_boot_catalog(img_lba, img_sectors_512):
    b = bytearray(SECTOR)
    # Validation entry (32 B): checksum = suma de palabras 16-bit = 0
    b[0] = 0x01                            # header id
    b[1] = 0x00                            # plataforma 80x86
    b[4:28] = b"MYOS".ljust(24, b"\0")
    b[30:32] = b"\x55\xAA"
    s = sum(struct.unpack_from("<H", b, i)[0] for i in range(0, 32, 2))
    struct.pack_into("<H", b, 28, (-s) & 0xFFFF)
    # Initial/default entry (32 B): no-emulation
    b[32] = 0x88                           # bootable
    b[33] = 0x00                           # media type: no emulation
    struct.pack_into("<H", b, 34, LOAD_SEGMENT)
    b[36] = 0x00                           # system type
    struct.pack_into("<H", b, 38, img_sectors_512)
    struct.pack_into("<I", b, 40, img_lba)
    b[64] = 0x00                           # section entry: no mas entradas
    return bytes(b)


def build(boot_image, files, out):
    img = pad(open(boot_image, "rb").read())
    img_secs = len(img) // SECTOR
    file_blobs = [pad(open(p, "rb").read()) for p in files]

    # --- Layout de sectores (LBA de 2048 B) ---
    catalog_lba = 19
    lba = 20
    img_lba = lba
    lba += img_secs
    file_entries = []                      # (lba, size, ident)
    for path, blob in zip(files, file_blobs):
        ident = os.path.basename(path).upper().encode()[:30]
        file_entries.append((lba, len(blob), ident))
        lba += len(blob) // SECTOR

    # Directorio raiz: "." y ".." referencian su propio lba/size.
    root_lba = lba
    rec_len = lambda ident: 33 + len(ident) + ((33 + len(ident)) % 2)
    root_size = 34 + 34 + sum(rec_len(ident) for _, _, ident in file_entries)
    root_dir = b"".join([
        dir_record(root_lba, root_size, b"\0", is_dir=True),
        dir_record(root_lba, root_size, b"\x01", is_dir=True),
    ] + [dir_record(l, s, ident) for l, s, ident in file_entries])
    root_size = len(root_dir)              # size real (datos sin pad)
    root_secs = (len(root_dir) + SECTOR - 1) // SECTOR
    lba += root_secs

    lt_lba = lba
    lba += 1
    mt_lba = lba
    lba += 1
    volume_size = lba

    # --- Path tables (root -> root, parent 1) ---
    lt = bytes([1, 0]) + struct.pack("<I", root_lba) + \
         struct.pack("<H", 1) + b"\0"
    lt = pad(lt)
    mt = bytes([1, 0]) + struct.pack(">I", root_lba) + \
         struct.pack(">H", 1) + b"\0"
    mt = pad(mt)

    # --- Volcado ---
    out_data = b"\0" * (16 * SECTOR)
    out_data += make_pvd(volume_size, root_lba, root_size,
                         lt_lba, mt_lba, 10)
    out_data += make_boot_record(catalog_lba)
    out_data += bytes(SECTOR)              # terminator
    out_data += make_boot_catalog(img_lba, len(img) // 512)
    out_data += img
    for blob in file_blobs:
        out_data += blob
    out_data += pad(root_dir)
    out_data += lt
    out_data += mt
    assert len(out_data) == volume_size * SECTOR, \
        "layout inconsistente: %d != %d" % (len(out_data), volume_size * SECTOR)

    with open(out, "wb") as f:
        f.write(out_data)
    print("ISO: %s (%d sectores, boot image %d sectores, %d archivos)" %
          (out, volume_size, img_secs, len(files)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("boot_image", help="os-image.bin (boot image El Torito)")
    ap.add_argument("files", nargs="*", help="archivos extra del ISO")
    ap.add_argument("-o", required=True)
    args = ap.parse_args()
    build(args.boot_image, args.files, args.o)


if __name__ == "__main__":
    main()
