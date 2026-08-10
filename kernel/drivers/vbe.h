/* MyOS - kernel/drivers/vbe.h
 * Video VBE via la interfaz "dispi" de Bochs/QEMU (puertos 0x01CE/0x01CF),
 * usable desde modo protegido (sin int 0x10). Fase 12. */
#ifndef MYOS_VBE_H
#define MYOS_VBE_H

#include <stdint.h>

#define VBE_SCREEN_W    800
#define VBE_SCREEN_H    600
#define VBE_SCREEN_BPP  32

/* El LFB del VGA (QEMU std/bochs-disp) lo lee vbe_init() del BAR0 por
 * PCI (en QEMU 10.x esta en 0xFD000000, no en la base clasica 0xE00000
 * 00). El kernel lo mapea identity (superpage supervisor) y cada PD de
 * usuario en VBE_LFB_USER_VA (dentro de la region de usuario). */
extern uint32_t vbe_lfb_phys;
#define VBE_LFB_USER_VA 0xA8000000u
#define VBE_LFB_PAGES   512u        /* 2 MiB */

/* Inicializa el modo grafico (800x600x32) por la interfaz dispi y
 * localiza el LFB por PCI. No-op silencioso si no hay VGA. */
void vbe_init(void);

/* 1 si el modo grafico quedo activo (hay LFB); el kernel usa entonces
 * la consola grafica vgafx en vez del buffer de texto 0xB8000. */
extern int vbe_graphics_active;

#endif
