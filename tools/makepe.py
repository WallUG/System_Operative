#!/usr/bin/env python3
"""MyOS - tools/makepe.py
Convierte un ELF32 ET_EXEC (los .elf de usuario, enlazados en
USER_VA 0x80000000 por tools/user.ld) a un PE32 real (formato Windows:
DOS header + 'PE\\0\\0' + COFF header + Optional Header PE32 + secciones).

El resultado es un .exe PE32 i386 (machine 0x14C) con la misma
disposicion virtual que el ELF (image base 0x80000000, entry = _start),
de modo que el loader PE del kernel (kernel/pe.c) construye el espacio
de usuario igual que elf_load: mapea secciones en un PD aislado.

Secciones generadas (una por segmento LOAD del ELF):
  - .text  (R X) : segmento 0   (codigo + rodata)
  - .data  (R W) : segmento 1   (datos inicializados)
  - .bss   (R W) : segmento 2   (sin bytes en el archivo)
Si hay mas de 3 LOAD, el resto se agrupa en .dta (RW).

Los .exe de prueba se producen por DOS vias (decision "Ambos"):
  1. makepe.py: convierte nuestros .elf de usuario a PE32.
  2. mingw (i686-w64-mingw32-gcc) si esta instalado: PE32 "real"
     generado por el cross-compiler (objetivo: probar el loader contra
     binarios ajenos). El Makefile elige makepe.py por defecto.

Uso: makepe.py <in.elf> -o <out.exe>
"""

import argparse
import struct

IMAGE_BASE = 0x80000000              # misma que tools/user.ld
SECTION_ALIGN = 0x1000
FILE_ALIGN = 0x200
OPT_HDR32_SIZE = 0xE0                # 224 bytes (PE32, no PE32+)
SHORT_NAME = 8

IMAGE_SCN_CNT_CODE = 0x00000020
IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_CNT_UNINITIALIZED_DATA = 0x00000080
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_READ = 0x40000000
IMAGE_SCN_MEM_WRITE = 0x80000000


def align_up(v, a):
    return (v + a - 1) & ~(a - 1)


def read_elf(path):
    d = open(path, "rb").read()
    if len(d) < 52 or d[:6] != b"\x7fELF\x01\x01":
        raise SystemExit(f"{path}: no es un ELF32 little-endian")
    e_type, e_machine = struct.unpack_from("<HH", d, 16)
    e_entry = struct.unpack_from("<I", d, 24)[0]
    e_phoff = struct.unpack_from("<I", d, 28)[0]
    e_phentsize, e_phnum = struct.unpack_from("<HH", d, 42)
    if e_type != 2 or e_machine != 3:      # ET_EXEC, EM_386
        raise SystemExit(f"{path}: solo ET_EXEC i386")
    loads = []
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_off, p_va, p_pa, p_filesz, p_memsz, \
            p_flags, p_align = struct.unpack_from("<IIIIIIII", d, off)
        if p_type == 1:                    # PT_LOAD
            loads.append((p_off, p_va, p_filesz, p_memsz))
    if not loads:
        raise SystemExit(f"{path}: sin segmentos PT_LOAD")
    return d, e_entry, loads


def build_pe(d, e_entry, loads):
    """Construye la imagen PE32: (.text, .data, .bss[, .dta])"""
    sections = []
    for i, (p_off, p_va, p_filesz, p_memsz) in enumerate(loads):
        if i == 0:
            name = b".text"
            ch = (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE
                  | IMAGE_SCN_MEM_READ)
        elif i == 1:
            name = b".data"
            ch = (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ
                  | IMAGE_SCN_MEM_WRITE)
        elif i == 2:
            name = b".bss"
            ch = (IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ
                  | IMAGE_SCN_MEM_WRITE)
        else:
            name = b".dta"
            ch = (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ
                  | IMAGE_SCN_MEM_WRITE)
        raw = d[p_off:p_off + p_filesz]
        vsize = align_up(p_memsz, SECTION_ALIGN)
        if name == b".bss":
            raw = b""
        sections.append((name, p_va, raw, vsize, ch))

    entry_rva = e_entry - IMAGE_BASE

    # ---- tamanos y alineaciones de archivo ----
    num_sec = len(sections)
    peak = 4 + 20 + OPT_HDR32_SIZE + num_sec * 40        # desde PE sig
    headers_size = align_up(0x40 + peak, FILE_ALIGN)

    rova = min(s[1] - IMAGE_BASE for s in sections)      # lowest RVA
    size_of_image = max(s[1] + s[3] for s in sections) - IMAGE_BASE
    code_base = base_of_code = sections[0][1] - IMAGE_BASE

    # ---- DOS header (64 B) ----
    pe_off = 0x40
    out = bytearray(0x40)                    # DOS stub de 64 B
    out[0] = ord('M'); out[1] = ord('Z')
    struct.pack_into("<I", out, 0x3C, pe_off)    # e_lfanew

    # ---- PE signature + COFF file header (20 B) ----
    out += b"PE\x00\x00"
    out += struct.pack("<HHIIIHH",
                       0x14C,          # machine: i386
                       num_sec,
                       0, 0, 0,        # timestamp, symtab, nsyms
                       OPT_HDR32_SIZE,
                       0x0102)         # executable | 32-bit

    # ---- Optional header PE32 (224 B) ----
    opt = b""
    opt += struct.pack("<HBB", 0x10B, 0, 0)          # magic, linker ver
    opt += struct.pack("<I", len(sections[0][2]))    # SizeOfCode
    opt += struct.pack("<II", 0, 0)                  # init/uninit data
    opt += struct.pack("<I", entry_rva)              # AddressOfEntryPoint
    opt += struct.pack("<I", base_of_code)           # BaseOfCode
    opt += struct.pack("<I", base_of_code)           # BaseOfData
    opt += struct.pack("<II", IMAGE_BASE, SECTION_ALIGN)   # ImageBase
    opt += struct.pack("<I", FILE_ALIGN)             # FileAlignment
    opt += struct.pack("<HHHHHH",
                       4, 0, 0, 0, 4, 0)             # vers OS/Image/Subsys
    opt += struct.pack("<I", 0)                      # Win32VersionValue
    opt += struct.pack("<I", size_of_image)          # SizeOfImage
    opt += struct.pack("<I", headers_size)           # SizeOfHeaders
    opt += struct.pack("<I", 0)                      # CheckSum
    opt += struct.pack("<HH", 3, 0)                  # Subsystem console
    opt += struct.pack("<IIII",
                       0x100000, 0x1000,             # stack rsv/commit
                       0x10000, 0x1000)              # heap rsv/commit
    opt += struct.pack("<II", 0, 1)                  # LoaderFlags,
    opt += struct.pack("<32I", *([0] * 32))          # DataDirectory[16]
    if len(opt) != OPT_HDR32_SIZE:
        raise SystemExit(f"BUG: optional header {len(opt)} != 224")
    out += opt

    # ---- section headers (pegados al optional header) ----
    raw_ptr = headers_size
    for name, rva, raw, vsize, ch in sections:
        out += name.ljust(SHORT_NAME, b"\x00")
        out += struct.pack("<IIIIIIHHI",
                           vsize, rva - IMAGE_BASE, len(raw), raw_ptr,
                           0, 0, 0, 0, ch)
        raw_ptr += align_up(len(raw), FILE_ALIGN)
    out += b"\x00" * (headers_size - len(out))

    # ---- payload ----
    for _, _, raw, _, _ in sections:
        if not raw:
            continue
        out += raw
        out += b"\x00" * (align_up(len(raw), FILE_ALIGN) - len(raw))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[2])
    ap.add_argument("elf", help="ELF32 ET_EXEC de entrada")
    ap.add_argument("-o", required=True, help=".exe de salida")
    a = ap.parse_args()

    d, entry, loads = read_elf(a.elf)
    pe = build_pe(d, entry, loads)
    with open(a.o, "wb") as f:
        f.write(pe)
    print(f"PE32 ok: {a.o} ({len(pe)} bytes, "
          f"{len(loads)} LOAD -> {len(loads)} secciones)")


if __name__ == "__main__":
    main()