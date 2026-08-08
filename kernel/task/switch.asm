; MyOS - kernel/task/switch.asm
; Cambio de contexto entre tareas (i386).
;
; La clave del diseño: el epilogo de irq_common_stub (pop ds, pop es,
; popad, add esp 8, iret) ya restaura TODO el estado de la tarea
; interrumpida. El scheduler solo necesita:
;   1) guardar el puntero al marco registers_t de la tarea actual
;      (task->esp), y
;   2) cargar esp con el marco de la siguiente tarea y ejecutar ese
;      mismo epilogo: los pops + iret saltan al punto exacto donde la
;      otra tarea fue interrumpida.
;
; task_create fabrica un marco falso (eip = entrada de la tarea,
; cs = 0x8, eflags = 0x202) para que el primer iret arranque la tarea.
;
; void sched_switch(uint32_t *old_esp, uint32_t new_esp, void *frame,
;                   uint32_t new_cr3)
;   old_esp : &task->esp de la tarea que sale (donde guardar el marco)
;   new_esp : task->esp de la tarea que entra (marco registers_t)
;   frame   : puntero al marco registers_t de la tarea que sale
;   new_cr3 : PD de la tarea que entra (cambio de espacio de usuario)
; No retorna: termina saltando a la tarea entrante.

[bits 32]

section .text

global sched_switch

sched_switch:
    mov eax, [esp + 4]          ; old_esp: puntero al campo esp
    mov ecx, [esp + 12]         ; frame: marco de la tarea que sale
    mov [eax], ecx              ; guardar esp (puntero al marco)
    mov edx, [esp + 16]         ; new_cr3: PD de la tarea que entra
    mov esp, [esp + 8]          ; esp = marco de la tarea que entra
    mov cr3, edx                ; cambiar el espacio de direcciones

    ; --- Epilogo identico a irq_common_stub ---
    pop ds
    pop es
    popad
    add esp, 8                  ; descartar int_no + err_code
    iret                        ; salta a eip/cs/eflags de la tarea entrante

; --- Inicio de una tarea nueva (primer iret) ---
; La tarea de kernel normal NO retorna: si el entry retorna, entramos
; aqui y morimos (sti;hlt) en vez de saltar a basura de la pila.
; sti;hlt (no cli;hlt): el IRQ0 del PIT debe poder despertar la tarea
; para que el scheduler la salte (esta fuera de la lista, no vuelve).
global task_stub_exit
task_stub_exit:
    sti
    hlt
    jmp task_stub_exit
