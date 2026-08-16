# Análisis de compatibilidad Win32 (Fase 24-P4)

Estado del sistema al cierre de la Fase 24: metapad.exe (mingw real)
corre completo (menús, aceleradores, RichEdit, diálogos de Guardar/
Abrir de comdlg32, iconos .rsrc), y ahora con los 3 botones de
título (X/minimizar/maximizar) gestionados por el kernel.

## Superficie Win32 actual (exports por DLL)

| DLL       | Exp. | Cobertura |
|-----------|-----:|-----------|
| kernel32  | 108  | consola, archivos (CreateFileA/ReadFile/WriteFile/GetFileSize/CloseHandle), memoria (HeapAlloc/HeapFree/Global*), procesos (CreateProcessA → fork+exec, GetCommandLineA), entorno, threads (CreateThread/ExitThread, Fase 24-P2.2), registro (.ini, advapi32) |
| user32    | 103  | ventanas (CreateWindowExA/ShowWindow/DestroyWindow/SetWindowTextA), controles (RichEdit20A/EDIT/BUTTON/STATIC/SysListView32/ToolbarWindow32/statusbar/trackbar/treeview), menús (LoadMenuA/SetMenu/AppendMenuA + popup modal), diálogos (DialogBoxParamA/EndDialog, Fase 24-P1.2), messagebox, aceleradores (formato 8 B corregido en P4), clipboard (P2.3), timer, drag |
| gdi32     | 37   | TextOutA, Rectangle/FillRect/Ellipse/LineTo, BitBlt/StretchBlt (P2.3), pinceles/fuentes básicas, GetTextMetricsA (Fase 20-D) |
| msvcrt    | 61   | printf/sprintf/scanf, fopen/fread/fwrite, malloc/free/realloc (heap propio), atexit/exit, str*/mem*, time |
| comctl32  | 7    | InitCommonControls, CreateToolbarEx, listview/toolbar/statusbar/trackbar/treeview (user32) |
| comdlg32  | 10   | GetOpenFileNameA/GetSaveFileNameA (lista MEFS + confirmación overwrite), ChooseColor/Font/PrintDlg (stubs) |
| advapi32  | 8    | registro persistente a registry.ini |
| shell32   | 4    | ShellExecuteA (stub), SHGetSpecialFolderPath (stub) |
| ole32     | 9    | CoInitialize/CoUninitialize (no-op), COM stubs |
| winspool  | 8    | WritePrinter/OpenPrinterA (acumulan en buffer) |
| ntdll     | 5    | RtlZeroMemory/RtlMoveMemory |

## Qué .exe reales podrían correr YA

1. **Aplicaciones de consola simples** (ping.exe, ipconfig.exe de
   Windows clásico, busybox win32, wget.exe de mingw, archivos de
   texto): dependen casi solo de kernel32+msvcrt. Los ports mingw de
   utilidades CLI (p.ej. `cat.exe`, `ls.exe` de GnuWin32) corren si no
   usan IPC/red/threads avanzados. Limitantes: no hay stdin de
   archivo, la consola es básica, y SYS_EXEC solo carga ELF/PE desde
   el MEFS.
2. **Editores de texto tipo metapad** (Notepad2, metapad, win32pad):
   metapad YA corre. Notepad2 (más complejo: tabulación, syntax
   highlighting con controles propios) requeriría: WM_PAINT de
   ventana hijo con GDI avanzado (CreateCompatibleDC, PatBlt,
   SelectObject con fuentes reales) — parcialmente cubierto.
3. **Apps "helloworld" gráficas de mingw**: cualquier app generada
   con los mismos patrones que metapad (wndproc simple + controles
   básicos + menús) corre si sus imports están en la lista.
4. **GnuWin32/msys utils con GUI mínima**: p.ej. `gvim` (demasiado
   GDI), `notepad++` (no).

## Qué apps NO correrían aún (y por qué)

| App | Bloqueo principal |
|-----|-------------------|
| notepad.exe (XP) | Win32k avanzado: WM_* extenso, GDI fonts, Shell API de archivos, Unicode (APIs W) |
| wordpad.exe | RichEdit avanzado (COM/OLE embedding), toolbars comctl32 v6, menús contextuales, drag&drop OLE |
| mspaint.exe | GDI extenso (StretchBlt con DDBs, paletas, regiones), mensajes de dibujo, scroll |
| calc.exe | Botones propios + WM_COMMAND extenso (parcialmente: dlgtest2 cubre el patrón) |
| explorer.exe de Windows | shell32/COM/ListView virtual, drag&drop, contexto |
| Todo .NET / instaladores (NSIS) | kernel32 avanzado (VirtualAlloc/VirtualProtect, secciones con atributos), LoadLibrary dinámica de DLLs del sistema reales, importación de API no cubiertas |

## Brechas técnicas más probables (orden de impacto)

1. **APIs kernel32 no implementadas**: GetModuleFileNameA (parcial),
   GetProcAddress/GetModuleHandleA (sí), VirtualAlloc/VirtualProtect
   (los .exe con __declspec(align) o runtime con heap avanzado),
   GetSystemTimeAsFileTime (msvcrt _time64 lo usa), MultiByteToWideChar,
   CreateFileMapping (mapeo de archivos).
2. **Unicode (APIs W)**: los .exe de Windows reales importan las
   versiones W (CreateFileW, etc.) o usan thunking. Cubrir W→A por
   conversión UTF-16→ASCII (cp437) abriría mucho.
3. **GDI de 16/32-bit real**: GetDC/TextOutA funcionan; faltan
   CreateCompatibleDC (el DC de pantalla + búferes), PatBlt,
   GetSysColor, SetPixel/GetPixel con DDB, fuentes TrueType reales
   (solo 8x16).
4. **Shell32/COM**: ShellExecuteA, SHGetSpecialFolderPath, CLSID
   registry (CoCreateInstance) — básicamente todo el escritorio
   integrado.
5. **Sistema de archivos**: rutas con \ (solo / en MEFS), directorios
   de sistema, variables de entorno de PATH para LoadLibrary.

## Recomendaciones si se quiere un .exe "real de Windows"

- **mejor objetivo**: una app pequeña de consola de GnuWin32 (p.ej.
  `cat`/`ls` de GnuWin32, ~40 KB, imports: kernel32 KERNEL32.dll
  básicos + msvcrt) o un .exe gráfico simple de mingw hecho a medida
  con los mismos imports que metapad.
- **Windows real (no mingw)**: los binarios de Microsoft (notepad,
  cmd) requieren APIs W, secciones con atributos de ejecución, y
  la estructura Win32k completa. El salto más rentable es el
  thunking Unicode W→A.

## Conclusión

La fase actual corre **metapad.exe (mingw)** completo y es compatible
con aplicaciones de consola simples y GUI de patrón "metapad-like".
Para "una app de Windows 32 real" el siguiente paso con mayor retorno
es el **thunking Unicode (W→A) en kernel32/user32** + **VirtualAlloc/
VirtualProtect** + **GetModuleFileNameA/GetSystemTimeAsFileTime**, que
destraban la mayoría de los binarios de Windows clásico pequeños.