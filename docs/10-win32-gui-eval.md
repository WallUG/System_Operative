# MyOS — Evaluación: ejecutar aplicaciones Windows GUI reales

Objetivo: ejecutar un `.exe` Windows **totalmente gráfico** compilado para
la API Win32 real (no solo `MessageBoxA`), en modo GUI sobre el escritorio
de MyOS.

## 1. Qué exige hoy una app GUI Win32

Una aplicación "de verdad" (como un notepad típico) usa la pila completa:

```
main → RegisterClassEx → CreateWindowEx → GetMessage/PeekMessage
        → TranslateMessage/DispatchMessage → WndProc (WM_PAINT, WM_*)
        → GDI: GetDC/TextOut/CreateFont/GetStockObject/SelectObject...
        → controles: comctl32 (edit, listview, toolbar) y diálogos:
          comdlg32 (open/save)
```

Y presupone del sistema que nunca existió en MyOS:

| Pilar Windows | Estado en MyOS hoy |
|---------------|--------------------|
| **HWND + WNDPROC + mensajes WM_\*** | No existe: el kernel WM usa buffers + `SYS_EVENT` |
| **GDI** (DC, fuentes, pinceles, TextOut, BitBlt) | No existe: los shims dibujan directo al LFB |
| **Cola de mensajes por ventana** (GetMessage) | Remoto: cola de eventos por PD (`SYS_EVENT`) |
| **Controles comunes** (edit, listbox, combo, toolbar) | No existen (solo 4 widgets MyOS en user32) |
| **Diálogos estándar** (abrir/guardar) | No existen (`explorer.c` es lo más parecido) |

## 2. Notepad++ — dictamen: no es viable a corto plazo

Análisis del build real de Notepad++ (makefile oficial, 2026):

- **DLLs importadas**: comctl32, crypt32, dbghelp, ole32, sensapi,
  shlwapi, uuid, uxtheme, version, wininet, wintrust, dwmapi, imm32,
  msimg32, oleaut32, advapi32, gdi32, user32, kernel32 → ≈ **19 DLLs**,
  más de 300 imports con callbacks y estructuras complejas.
- **Scintilla + Lexilla** (editor y resaltado, enlazadas estáticamente):
  usan **GDI completo** (SurfaceGDI), Direct2D (SurfaceD2D), E/S de
  ventanas avanzada, fuentes Unicode/TTF, IME (imm32), selección de
  texto con caret, animaciones...
- **Manifest common controls v6** (temas) + DPI API dinámica
  (`GetDpiForWindow`), Boost.Regex, plug-ins que cargan DLLs arbitrarias.
- Es **C++20**: el CRT C++ (¿) y la STL en ring 3 hoy solo cubren el C
  de msvcrt (sin excepciones/iostreams/STL).

Conclusión: Notepad++ necesitaría esencialmente reescribir GDI en MyOS
durante meses. **Descartado como primer objetivo GUI**; queda como
"horizonte lejano" cuando exista GDI + comctl32 y un CRT C++.

## 3. Candidatos realistas (si el objetivo es "un exe Windows GUI real")

Requisitos del candidato ideal:
- **PE32 x86** (i686), no C++ (o C++ sin STL).
- **Imports mínimos**: kernel32 + user32 + gdi32, a lo sumo comctl32 /
  comdlg32 (parcial).
- API de ventanas básica: RegisterClass/CreateWindow/GetMessage/
  WndProc/DefWindowProc + GDI simple (texto, rects, líneas).
- Sin manifest comctl v6, sin red, sin registro, sin Unicode W (§si).

Candidatos por orden de dificultad:

| App | Tamaño | Imports clave | Veredicto |
|-----|--------|---------------|-----------|
| **Metapad** (notepad tiny, C) | ~150-200 KB | kernel32, user32, gdi32, comctl32, comdlg32, advapi32 | **Muy viable**: primer objetivo recomendado de exe "externo" |
| **Notepad2 / zufuliu** | ~1 MB | + RichEdit (riched20), comdlg32, comctl32 | Viable tras Metapad (requiere edit control + diálogos) |
| **Juegos retro mini** (mines, tetris Win32) | < 500 KB | user32/gdi32/winmm o DirectDraw | Viable si se añade winmm/ddraw básicos |
| **mTetris / "2048" Win32 C** | pequeño | user32/gdi32 | Viables pronto |
| **Nuestro propio notepad mingw** | controlado | solo lo que implementemos | **Primer hito de prueba** (blanco conocido) |

## 4. El puente: mapping de la API Win32 al WM de MyOS

La arquitectura actual ya está alineada para este salto (decisión de
diseño de las Fases 14-17). El plan es implementar **user32 completo
siendo el "adaptador" entre Windows y MyOS**:

### 4.1 user32.dll ampliado (mensajes y ventanas)

- `RegisterClassExA/WNDCLASS` → tabla de WNDPROC por clase (ring 3).
- `CreateWindowExA` → `SYS_WINCREATE` (id → **HWND** = id de ventana del
  kernel; el árbol de hijos se mantiene en user32 como id+padre).
- `GetMessage/PeekMessage` → `SYS_EVENT` (cola del proceso) +
  traducción a mensajes `WM_*` dirigidos al HWND correspondiente
  (EV_KEY → WM_KEYDOWN/CHAR; EV_BUTTON_* → WM_LBUTTONDOWN/UP con
  coordenadas de ventana; EV_WINCLOSE → WM_CLOSE).
- `DispatchMessage` → llama al WNDPROC; `DefWindowProc` pinta el fondo
  gris por defecto y responde a WM_PAINT/WM_ERASEBKGND.
- `SendMessage/PostMessage` → directo a WNDPROC (mismo proceso; el árbol
  de ventanas vive en user32).
- `ShowWindow/UpdateWindow/InvalidateRect` → marcas + `SYS_WINUPDATE`
  (recomposición del kernel).
- `SetTimer` → contador interno en GetMessage con `GetTickCount`.

### 4.2 gdi32.dll nuevo (dibujo en el buffer de la ventana)

- Un **DC** = tupla {ventana destino, font actual, fg/bg, brush, pen,
  clip rect = cliente, posición}. Se crea con `GetDC(hwnd)`, apunta al
  buffer USER de la ventana (buf_va de SYS_WINCREATE: las apps escriben
  directo a su buffer, el kernel ya lo compone).
- `TextOutA/DrawTextA` → glifos 8×16 de la fuente VGA (existente);
  `SetBkMode/SetTextColor/GetTextMetrics/GetTextExtentPoint32`.
- `GetStockObject/SelectObject/DeleteObject/CreateSolidBrush/
  CreatePen/CreateFontIndirectA`.
- `FillRect` (rect check + fill), `Rectangle`, `MoveToEx/LineTo`,
  `PatBlt` → operaciones directas sobre el buffer (con clip al cliente).
- `BitBlt/StretchBlt` (blits) → se pueden dejar para después.
- `SetDIBitsToDevice` → solo si necesario (bitmaps; después).

### 4.3 comctl32/comdlg32 (después)

- **Edit** (caja de 1 línea) y **Button** (ya existe el widget de la
  Fase 15): suficiente para un diálogo simple.
- **comdlg32.GetOpenFileName** → diálogo propio que reutiliza
  `SYS_DLIST`/`SYS_DREAD` (explorer.c como base).

## 5. Hoja de ruta propuesta (orden de la skill win32-exe-dll-planning)

1. **Hito A — notepad propio (blanco conocido)**: compilar con
   `i686-w64-mingw32-gcc` un mini-notepad en C puro (win32 API: una
   ventana, un edit 1-línea o dibujo directo, menú no; imports
   controlados). Implementar **solo** lo que importe: ~10-15 exports de
   user32 + ~10 de gdi32. Validar en QEMU con QMP (screendump de la
   ventana). → **Escalera GUI 1/1**.
2. **Hito B — Metapad (exe externo real)**: **CONCLUIDO** (Fases 18/A-D):
   el `metapad.exe` real (3.6, mingw) carga al 100 % y funciona de forma
   interactiva: ventana con **barra de menú** (File Edit Favourites Options
   Help) y **desplegables** (Fase D, `LoadMenuA`/`TrackPopupMenuEx` +
   `WM_COMMAND`), edición de texto real (Fase A), apertura por línea de
   comandos (Fase B), diálogo **Abrir** real sobre MEFS (Fase C,
   `GetOpenFileNameA`) y diálogos de pregunta `MessageBoxA`. La capa
   user32/gdi32/comctl32/comdlg32 implementa texto/rects/pinseles y menús;
   queda sin registro ni comctl v6.
3. **Hito C — Notepad2**: añadir RichEdit (control excl. = un edit
   multilínea propio mapeado a nuestro dibujo) — evaluar costo real.
4. **Fase E (pendiente)**: FS de escritura (MEFS read-only hoy) para
   que **Guardar** (`GetSaveFileNameA` + `WriteFile`) funcione de verdad.
5. Horizontes lejanos: GDI+ / D2D, imm32, uxtheme, wininet, C++ CRT.

## 6. Riesgos y límites aceptados (decisión de diseño)

- **Un solo DC "por ventana"**, sin compat DC ni regiones complejas.
- Mensajes **síncronos** dentro del proceso; sin cross-process
  WM_*/SendMessage (el WM del kernel ya enruta por PD).
- Sin foco global Windows: el foco lo gestiona nuestro WM (por PD).
- Sin Unicode completo: shims A/W mapeando a la fuente 8×16 ASCII.
- Las apps que necesiten comctl v6 manifest, DWM, temas o DPI quedarán
  fuera **por decisión**: se declaran no soportadas (valor de fallo
  plausible, nunca crash).

## 7. Validación

- Escalera de compatibilidad **GUI** nueva (`ladder_gui_test.py`):
  screendumps QMP por hito + asserts de píxeles (como la Fase 12-15).
- Regresión completa: ladder 14/14 + desktop 9/9 + f17 5/5 tras cada
  cambio de los shims.

**Veredicto**: con este plan, el primer `.exe` Windows GUI real (nuestro
notepad mingw primero, Metapad después) es un objetivo alcanzable en 2-3
iteraciones de la fase. Notepad++ es inviable a corto plazo y queda como
proyecto de largo recorrido.