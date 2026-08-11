# MyOS — GUI: VBE, eventos, gestor de ventanas y escritorio

Archivos: `kernel/drivers/vbe.c|h`, `kernel/drivers/vgafx.c|h`,
`kernel/drivers/mouse.c|h`, `kernel/winmgr.c|h`, `kernel/syscall.c`
(SYS_GFXINFO/SYS_MOUSEINFO/SYS_EVENT/SYS_WIN*), `user/winlib.h`,
`user/desktop.c`, `user/explorer.c`, `user/win_two.c`,
`user/win_demo.c`, `user/mouseinfo.c`.

## 1. Modo gráfico VBE (`vbe.c`)

- Modo 800x600x32 (formato 0x00RRGGBB) vía interfaz **dispi** (puertos
  0x01CE/0x01CF). No-op silencioso si no hay VGA.
- LFB leído del **BAR0 PCI** (`0xFD000000` en QEMU 10.x; no 0xE0000000):
  config space PCI (0xCF8/0xCFC), bus 0, dev 2, fn 0.
- Mapeado: identity supervisor en el PD del kernel + usuario en
  `VBE_LFB_USER_VA` (0xA8000000). `paging_is_lfb_frame` impide que el
  PMM regale frames del LFB.

## 2. Consola gráfica (`vgafx.c`)

- Texto del kernel sobre el LFB con la **fuente VGA 8x16** (compartida con
  user32 vía `font8x16.h` generado por `tools/font2c.py`).
- `vgafx_suppressed()`: **si el WM tiene ventanas (escritorio), la
  consola no pinta** (el texto sigue yéndose por serial). Al cerrar la
  última ventana reanuda.
- Cursor de texto row/col + scroll por bloques de 16 px.

## 3. Eventos de ratón/teclado

- `mouse_event_t {type, x, y, buttons, key}`; tipos `EV_MOVE` 1,
  `EV_BUTTON_DOWN` 2, `EV_BUTTON_UP` 3, `EV_KEY` 4, `EV_WINCLOSE` 5.
- Generación en los IRQs (ver 07-drivers.md); consumo por polling
  (`SYS_MOUSEINFO` 16) y colas (`SYS_EVENT` 17).

## 4. Gestor de ventanas (`winmgr.c`) — modelo

Ventanas con **backing buffer en ring 3** (la app pinta su cliente con
`SYS_MALLOC` y lo registra) y **composición centralizada en el kernel**:

1. Snapshot del LFB (la consola) como fondo — `wm_ensure_bg()` (kmalloc
   800×600×4 ≈ 1.9 MB; se libera con la última ventana).
2. Se dibujan las ventanas en orden z: marco 2px `0xC0C0C0`, título 20px
   `0x000088` con texto, botón X 16×16 `0xCC3333`, y el **cliente** se
   blitea desde el buffer del usuario **validado página a página** con el
   PD del dueño (`paging_user_frame`).
3. Las fijas (taskbar) se dibujan al final (siempre arriba; z efectivo
   +0x1000 en hit-testing). El cursor se redibuja al final de cada
   composición (el blit lo pisa).

Estructura de ventana (`win_t`): id, título, rect total (x/y/w/h),
cliente (cx/cy/cw/ch), `buf_va/buf_sz`, PD dueño, flags, z, visible,
drag (dx/dy).

Flags: `WM_FLAG_FIXED` 0x1 (taskbar), `WM_FLAG_NOFRAME` 0x2,
`WM_FLAG_BG` 0x4 (fondo del escritorio).

Límites aceptados (diseño): recomposición completa (fondo + todas las
ventanas) en cada cambio; sin clipping por ventana (aún).

### Z-order y foco

- `wm_raise(w)`: sube a `top + 1` **excluyendo FIXED y BG** (bug #4: el
  fast-path antiguo impedía crecer el z y el teclado iba al fondo).
- `wm_topmost_app()`: la más alta **no FIXED no BG** → destino de
  `EV_KEY`. Si no hay app: `wm_topmost()`; si no: `WM_ROUTE_RAW`.
- `wm_focus_pd`: destino de clics en el fondo y del teclado sin ventana
  bajo el cursor.

### Enrutado (Fase 17)

`wm_route(ev)`:

| Evento | Decisión |
|--------|----------|
| `EV_BUTTON_DOWN` | si hay drag: consumir; si no, hit-testing top-down; título → drag; X → `EV_WINCLOSE`; cliente → raise (si no es X) + entregar; fondo → foco |
| `EV_MOVE` | drag: mover ventana (`cx/cy` recomputados); si no, hover → dueño |
| `EV_BUTTON_UP` | fin de drag (consumido) o dueño bajo el cursor/foco |
| `EV_KEY` | `wm_topmost_app()` → foco → PD |
| sin ventanas | `WM_ROUTE_RAW` |

### Syscalls de ventanas

`SYS_WINCREATE` 18 (`ebx=&{title*,x,y,w,h,buf_va,buf_sz,flags}` → id, con
`user_memcpy_in`), `SYS_WINCLOSE` 19 (validando dueño), `SYS_WINMOVE` 20,
`SYS_WINUPDATE` 21, `SYS_WININFO` 22 (x/y/w/h + cliente).

### Limpieza al morir una tarea

`sched_kill_current` → `wm_cleanup_pd(pd)`: elimina las ventanas del PD,
libera su cola de eventos, `wm_recompute()` (libera el snapshot si no
quedan ventanas, reasigna foco). Sin esto, los blits usarían buffers de
PDs liberados (basura).

## 5. Escritorio (Fase 17)

- **`desktop.c`**: crea dos ventanas:
  - `"Fondo"` (`WM_FLAG_BG|NOFRAME`, pantalla completa): wallpaper con
    franjas y título; el WM lo compone en z=0 sobre el snapshot de la
    consola.
  - `"Taskbar"` (`WM_FLAG_FIXED|NOFRAME`, y=572, h=28): botón
    "EXPLORADOR" (clic → `fork`+`exec explorer.elf` en hijo con PD
    propio) y hint "q:salir".
  - `q` → `sys_winclose` de ambas + exit (el WM restaura la consola).
- **`explorer.c`**: lista el FS con `SYS_DLIST` + `SYS_DREAD`; clic
  selecciona la fila; **Enter abre el visor** (`win_two.c`) con el
  contenido; `q` cierra.
- **`win_two.c`**: visor de texto con scroll básico.
- **`winlib.h`**: wrappers de los syscalls `SYS_WIN*` + `EV_*` +
  helpers (`wl_point_in`, `wl_drawtext`, `wl_dec`...).

## 6. Blit del cliente (bug #5, Fase 17)

El blit del buffer de usuario al LFB copia por página:
`frame = paging_user_frame(pd, va & ~0xFFF)` + `memcpy(dst, frame+off,
chunk)` **avanzando dst linealmente** — antes sumaba `row` (offset del
buffer) a `dst` (LFB), corrompiendo las coordenadas y dejando el snapshot
de la consola visible.