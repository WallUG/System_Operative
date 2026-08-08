MyOS v0.9 - prueba de filesystem desde Windows API real.
Este archivo vive en el FS MEFS del ISO (solo lectura).
dir.exe lo ha leido con CreateFileA + ReadFile (kernel32.dll).
Si ves esto: la capa Win32 y el FS funcionan a la vez!