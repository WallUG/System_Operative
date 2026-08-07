# Fase 7 — Pruebas, Depuración y CI

## Ejecutar en QEMU (comandos frecuentes)

```bash
# Ejecución básica con salida serial en la terminal (¡fundamental para debug!)
qemu-system-x86_64 -drive format=raw,file=os-image.bin -serial stdio

# Con más memoria y sin reinicio automático en triple fault (para ver el error)
qemu-system-x86_64 -drive format=raw,file=os-image.bin -m 256M -no-reboot -no-shutdown -d int,cpu_reset -D qemu.log

# Booteando una ISO con GRUB (si se usa Multiboot2)
qemu-system-x86_64 -cdrom myos.iso -serial stdio
```

`-d int,cpu_reset -D qemu.log` es la herramienta más valiosa cuando el sistema se reinicia solo: registra cada interrupción y el motivo exacto del triple fault en `qemu.log`, incluyendo el estado de todos los registros justo antes del fallo.

## Depuración con GDB remoto

```bash
qemu-system-x86_64 -s -S -drive format=raw,file=os-image.bin &
# -s = escucha GDB en localhost:1234 ; -S = pausa la CPU al arrancar

x86_64-elf-gdb -ex "target remote localhost:1234" -ex "symbol-file kernel.elf"
```

Comandos útiles dentro de GDB para OSDev:
- `break kmain` — poner breakpoint por símbolo, incluso antes de que el kernel exista físicamente en memoria (GDB lo activa cuando el código llega ahí)
- `info registers` — ver todos los registros en el punto exacto de fallo
- `x/10i $rip` — desensamblar las próximas instrucciones desde el instruction pointer actual
- `p/x *(uint64_t*)$rsp` — inspeccionar la pila directamente

## Causas comunes de fallos silenciosos (triple fault) y cómo diagnosticarlas

| Síntoma | Causas probables | Cómo confirmar |
|---|---|---|
| La VM se reinicia inmediatamente al bootear | GDT mal construida, far jump a segmento incorrecto | `-d int,cpu_reset`, revisar el log justo antes del reset |
| Se reinicia justo tras entrar al kernel en C | Pila mal configurada (`esp`/`rsp` apuntando a zona no válida) | Verificar en `entry.asm` que `stack_top` esté correctamente reservado y alineado a 16 bytes |
| Cuelgue tras habilitar interrupciones (`sti`) | IDT incompleta, PIC no remapeado, falta EOI | Comentar `sti` temporalmente para aislar si el problema es de la IDT o de otra parte |
| Page fault inesperado | Tabla de páginas mal construida, acceso a dirección no mapeada, off-by-one en cálculo de índices PML4/PDPT/PD/PT | Revisar el código de error de la excepción 14 (bit 0: presente/no presente, bit 1: lectura/escritura, bit 2: user/kernel) |
| Corrupción de datos aleatoria | Overflow de pila, uso de memoria antes de inicializar el heap, red zone no deshabilitada en interrupciones (x86_64) | Añadir "canary values" al final de la pila del kernel y verificarlos periódicamente |

## Checklist de pruebas por fase (repetir cada vez que se termina una fase)

1. Compilación limpia sin warnings relevantes (`-Wall -Wextra`).
2. Arranque en QEMU sin reinicios ni cuelgues inesperados.
3. Prueba funcional específica de la fase (ver el checklist al final de cada archivo de referencia 01–06).
4. Log serial revisado en busca de mensajes de error o advertencias no atendidas.
5. Commit en git con mensaje descriptivo del hito alcanzado.

## Automatización en CI (opcional, GitHub Actions)

```yaml
name: build-and-boot-test
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install deps
        run: sudo apt-get update && sudo apt-get install -y nasm qemu-system-x86 build-essential
      - name: Build
        run: make
      - name: Boot test (headless, con timeout)
        run: timeout 15 qemu-system-x86_64 -drive format=raw,file=os-image.bin -display none -serial file:serial.log || true
      - name: Check expected boot message
        run: grep "Kernel loaded" serial.log
```

Este patrón (arrancar en modo `-display none`, capturar el serial a un archivo, y hacer `grep` de un mensaje esperado) es la forma estándar de tener "tests automatizados" para un OS sin necesidad de un framework de testing especial.
