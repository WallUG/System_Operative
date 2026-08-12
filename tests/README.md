# Ejecutables Win32 reales de prueba (Hito B)

## metapad.exe

- Metapad 3.6 final (2011), de Alexander Davidson (liquidninja.com/metapad).
- Freeware del autor, descargado de su web oficial:
  https://liquidninja.com/metapad/downloads/metapad36.zip
- PE32 i386, subsistema GUI, ImageBase 0x00400000, con tabla .reloc.
- Entry point propio (parsea GetCommandLineA y llama a WinMain), sin
  CRT de arranque externo.

Imports (186 totales):
- msvcrt.dll 12 | kernel32.dll 47 | user32.dll 87 | gdi32.dll 18
- comctl32.dll 4 (2 por ordinal: ord#8 InitCommonControls, ord#17
  InitCommonControlsEx) | comdlg32.dll 9 | advapi32.dll 6 | shell32.dll 3

Estado de MyOS (agosto 2026, slice 1 del Hito B):
- Todas las DLLs salvo USER32 resueltas por el cargador pe.c (incluye
  rebasing .reloc de 0x00400000 a 0x81000000 y ordinales -> "ord#N").
- User32.dll actual solo exporta MessageBoxA; el slice 2 implementa el
  minimo WM (ventanas, bucle de mensajes, clase EDIT, menus).