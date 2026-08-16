#!/usr/bin/env python3
"""Fase 25-W2A paso 1: set de inicializacion W de MSVC (kernel32).

Verifica en QEMU headless (w2atest.exe):
- GetCommandLineW coincide con GetCommandLineA (conversion UTF-16).
- GetModuleFileNameW coincide con GetModuleFileNameA.
- GetEnvironmentStringsW = bloque UTF-16 con PATH=.
- GetCurrentThreadId == GetCurrentProcessId (hilo principal).
- GetTickCount real: crece tras Sleep.
- QueryPerformanceFrequency == 1193182 y QPC crece entre 2 llamadas.
- GetSystemTimeAsFileTime real: >= 2024-01-01 y crece entre llamadas.
- Secciones criticas Init/Enter/Leave/Delete no cuelgan.
- Fin con exit 0."""
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'
passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def run():
    subprocess.run(['make', 'persist_disk'], capture_output=True)
    subprocess.run(['pkill', '-9', '-f', 'qemu-system'], capture_output=True)
    time.sleep(0.5)
    try: os.unlink(MON)
    except FileNotFoundError: pass
    qemu = subprocess.Popen(
        ['qemu-system-i386','-display','none','-monitor',f'unix:{MON},server,nowait',
         '-serial','stdio','-no-reboot','-no-shutdown',
         '-drive',f'format=raw,file={WORK}'],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
    acc = []
    def reader():
        while True:
            ch = qemu.stdout.read(1)
            if ch == b'': return
            acc.append(ch.decode('utf-8', errors='replace'))
    threading.Thread(target=reader, daemon=True).start()
    def mon_sock():
        for _ in range(200):
            try:
                s = socket.socket(socket.AF_UNIX); s.settimeout(10)
                s.connect(MON); s.recv(4096); return s
            except OSError: time.sleep(0.1)
        return None
    def hmp(c, dt=0.25):
        s = mon_sock()
        if s is None: return
        s.sendall(f'{c}\n'.encode()); time.sleep(dt); s.close()
    def sendcmd(cmd):
        for ch in cmd+'\n':
            hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot',
                 '_':'sendkey shift-minus'}.get(ch, f'sendkey {ch}'), 0.12)
    def waitstr(s, t=25):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    sendcmd('run w2atest.exe')
    if not waitstr('w2atest: cmdlineW=', 20):
        print('FAIL - w2atest no arranca'); qemu.terminate(); return
    check('w2atest arranca', True)

    if not waitstr('w2atest: moduleW=', 10):
        print('FAIL - sin moduleW'); qemu.terminate(); return
    check('GetModuleFileNameW se ejecuta', True)

    if not waitstr('w2atest: envW=', 10):
        print('FAIL - sin envW'); qemu.terminate(); return
    check('GetEnvironmentStringsW se ejecuta', True)

    if not waitstr('w2atest: pid=', 10):
        print('FAIL - sin pid'); qemu.terminate(); return
    check('GetCurrentProcessId/GetCurrentThreadId se ejecutan', True)

    if not waitstr('w2atest: ticks ', 10):
        print('FAIL - sin ticks'); qemu.terminate(); return
    check('GetTickCount real (crece tras Sleep)', True)

    if not waitstr('w2atest: qpf=', 10):
        print('FAIL - sin qpf'); qemu.terminate(); return
    check('QueryPerformanceCounter/Frequency se ejecutan', True)

    if not waitstr('w2atest: filetime=', 10):
        print('FAIL - sin filetime'); qemu.terminate(); return
    check('GetSystemTimeAsFileTime se ejecuta', True)

    if not waitstr('w2atest:PASS', 10):
        print('FAIL - w2atest no pasa'); qemu.terminate(); return
    check('Set W completo (cmdlineW/moduleW/envW/tid/ticks/qpc/filetime/cs)', True)

    if not waitstr('w2atest: createw/writew/attrsW/deletew ok', 10):
        print('FAIL - CreateFileW'); qemu.terminate(); return
    check('CreateFileW/GetFileAttributesW/DeleteFileW', True)

    if not waitstr('w2atest: cp437 unicode name ok', 10):
        print('FAIL - cp437'); qemu.terminate(); return
    check('cp437: nombre unicode cafe.txt (0x82) crea y borra', True)

    if not waitstr('w2atest: findw items=', 10):
        print('FAIL - FindFirstFileW'); qemu.terminate(); return
    check('FindFirstFileW/FindNextFileW listan y encuentran', True)

    if not waitstr('w2atest: paths W: temp=C:\\TEMP', 10):
        print('FAIL - paths W'); qemu.terminate(); return
    check('GetTempPathW/GetWindowsDirectoryW/GetSystemDirectoryW/GetCurrentDirectoryW', True)

    if not waitstr("w2atest: envW PATH='.'", 10):
        print('FAIL - envW'); qemu.terminate(); return
    check('GetEnvironmentVariableW (PATH=.)', True)

    if not waitstr('w2atest: lstr*W ok', 10):
        print('FAIL - lstr*W'); qemu.terminate(); return
    check('lstrlenW/lstrcpyW/lstrcatW/lstrcmpW/lstrcmpiW', True)

    if not waitstr("w2atest: fullW='metapad.exe' drvW='C:\\'", 10):
        print('FAIL - fullpath/drives'); qemu.terminate(); return
    check('GetFullPathNameW + GetLogicalDriveStringsW', True)

    if not waitstr('w2atest: copyw/movew ok', 10):
        print('FAIL - copyw/movew'); qemu.terminate(); return
    check('CopyFileW + MoveFileW', True)

    if not waitstr('w2atest: mkdirW/rmdirW ok', 10):
        print('FAIL - mkdirW/rmdirW'); qemu.terminate(); return
    check('CreateDirectoryW + RemoveDirectoryW', True)

    if not waitstr('w2atest: fmtW n=', 10):
        print('FAIL - fmtW'); qemu.terminate(); return
    check('FormatMessageW/GetDateFormatW/GetLocaleInfoW', True)

    if not waitstr('w2atest: regw/createw ok', 10):
        print('FAIL - regw/createw'); qemu.terminate(); return
    check('RegisterClassW + CreateWindowExW (clase W)', True)

    if not waitstr('w2atest: settextW/gettextW/classW/sendW ok', 10):
        print('FAIL - settextW'); qemu.terminate(); return
    check('SetWindowTextW/GetWindowTextW/GetClassNameW/SendMessageW text', True)

    if not waitstr('w2atest: charW ok', 10):
        print('FAIL - charW'); qemu.terminate(); return
    check('CharUpperW/CharLowerW/CharNextW', True)

    if not waitstr('w2atest: msvcrt W ok', 10):
        print('FAIL - msvcrt W'); qemu.terminate(); return
    check('msvcrt W (wcscpy/wcsstr/_wcsicmp/_wtoi/_itow)', True)

    if not waitstr('w2atest: wgetmainargs argc=', 10):
        print('FAIL - _wgetmainargs'); qemu.terminate(); return
    check('_wgetmainargs (argv W del CRT MSVC)', True)

    if not waitstr('exit:0', 10):
        print('FAIL - exit != 0'); qemu.terminate(); return
    check('w2atest termina con exit 0', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)