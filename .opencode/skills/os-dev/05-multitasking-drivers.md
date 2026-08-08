# Fase 5 — Multitarea y Drivers

## Modelo de tareas

Cada tarea tiene: su propio conjunto de registros guardados, su propia pila (kernel stack, y user stack si corre en ring 3), y su propio espacio de direcciones (PML4) si es un proceso separado (para hilos dentro del mismo proceso, comparten PML4).

```c
typedef struct task {
    uint64_t rsp;          // stack pointer guardado al hacer switch
    uint64_t* pml4;
    struct task* next;      // lista circular simple para round-robin
    int state;               // RUNNING, READY, BLOCKED
} task_t;
```

## Context switch en ASM

El cambio de contexto guarda los registros de la tarea actual en su pila, cambia `rsp` a la pila de la siguiente tarea, y restaura sus registros. Es la única forma correcta de hacerlo porque el compilador de C no puede razonar sobre "saltar a mitad de la ejecución de otra función".

```nasm
; context_switch.asm
global context_switch
; void context_switch(uint64_t* old_rsp_ptr, uint64_t new_rsp)
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp      ; guardar rsp actual en *old_rsp_ptr
    mov rsp, rsi         ; cargar rsp de la siguiente tarea
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret                    ; "retorna" a donde esa tarea se había quedado
```

El truco: cuando una tarea nueva se crea, se prepara su pila artificialmente para que el primer `ret` de `context_switch` la lleve directo a su punto de entrada, como si ya hubiera sido interrumpida ahí antes.

## Scheduler round-robin simple

Disparado desde el handler del timer (IRQ0, Fase 3):

```c
void schedule(registers_t* regs) {
    task_t* prev = current_task;
    current_task = current_task->next;
    context_switch(&prev->rsp, current_task->rsp);
}
```

Mejoras posteriores razonables: colas de prioridad, quantum variable, tareas bloqueadas por I/O que no entran al round-robin hasta desbloquearse.

## Modelo de drivers

Cada driver expone una interfaz mínima (`init`, `read`, `write`, `handle_irq`) y se registra centralmente. Drivers imprescindibles para un OS mínimo funcional:

| Driver | Puerto/mecanismo | Notas |
|---|---|---|
| VGA texto | memoria mapeada `0xB8000` | ya usado en Fase 2 para el "hola mundo" |
| Framebuffer gráfico (opcional) | VBE/VESA o Multiboot2 framebuffer tag | requiere más trabajo, opcional si el objetivo es solo texto |
| Serial UART (COM1, `0x3F8`) | I/O ports | **extremadamente útil para debug**: imprime logs fuera de la pantalla de QEMU, visible con `-serial stdio` |
| PIT (timer) | puertos `0x40/0x43` | ya configurado en Fase 3, base del scheduler |
| Teclado PS/2 | puerto `0x60`, IRQ1 | scancodes → ASCII |
| Disco ATA PIO (simple) o AHCI (moderno) | puertos `0x1F0-0x1F7` (ATA) | necesario para el filesystem (Fase 6) |

### Driver serial (recomendado implementar temprano, antes que nada gráfico, para depuración)

```c
#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);   // 38400 baud
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20));
    outb(COM1, c);
}
```

## Checklist antes de avanzar a Fase 6
- [x] Dos o más tareas de prueba (p. ej. contadores infinitos que imprimen distinto texto) se alternan visiblemente por el scheduler
- [x] El context switch no corrompe registros (verificar con valores conocidos antes/despues del switch)
- [x] El driver serial funciona y se usa activamente como canal de log (`qemu ... -serial stdio`)
- [x] Teclado responde a pulsaciones y el texto aparece correctamente en pantalla
