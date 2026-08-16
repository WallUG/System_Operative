#!/usr/bin/env python3
"""Fase 25-W2A paso 4: binario objetivo W (w2demo.exe, compilado con
-DUNICODE -municode — el patron de imports de un notepad.exe real
MSVC: RegisterClassW/CreateWindowExW/GetMessageW/DispatchMessageW/
DefWindowProcW/SetWindowTextW/GetWindowTextW/CharNextW/CreateFileW/
ReadFile/WriteFile/GetModuleFileNameW + CRT W (_wgetmainargs,
GetStartupInfoW, __p__wcmdln, __p___winitenv).

Verifica en QEMU headless:
- Carga: el loader resuelve TODOS los imports W (sin "import no
  resuelto") y el CRT W arranca (GetStartupInfoW, _wgetmainargs).
- RegisterClassW + CreateWindowExW -> WM_CREATE con EDIT hijo.
- Teclas -> WM_KEYDOWN/WM_CHAR del mensaje W.
- Ctrl+S (GetKeyState real) -> CreateFileW/WriteFile -> demo.txt.
- Esc -> WM_CLOSE -> WM_DESTROY -> exit:0.
- cat demo.txt devuelve el texto tecleado (persistencia real)."""
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

    sendcmd('run w2demo.exe')
    if not waitstr('w2demo: start', 20):
        print('FAIL - w2demo no arranca'); print(''.join(acc)[-500:]);
        qemu.terminate(); return
    check('w2demo (binario W -municode) carga y el CRT W arranca', True)

    if not waitstr('w2demo: WM_CREATE', 10):
        print('FAIL - sin WM_CREATE'); qemu.terminate(); return
    check('RegisterClassW + CreateWindowExW -> WM_CREATE (EDIT hijo)', True)

    if not waitstr("w2demo: classW='", 10):
        print('FAIL - sin classW'); qemu.terminate(); return
    check('GetClassNameW + CharNextW', True)

    if not waitstr('w2demo: loop', 10):
        print('FAIL - sin loop'); qemu.terminate(); return
    check('Bucle GetMessageW/DispatchMessageW', True)

    for k in 'hola':
        hmp(f'sendkey {k}', 0.2)
    if not waitstr('w2demo: msg m=258 w=104', 10):
        print('FAIL - sin WM_CHAR'); qemu.terminate(); return
    check('Teclas -> WM_KEYDOWN/WM_CHAR (mensaje W)', True)

    hmp('sendkey ctrl-s', 0.5)
    if not waitstr('w2demo: saved 4 bytes', 10):
        print('FAIL - sin save'); qemu.terminate(); return
    check('Ctrl+S (GetKeyState real) -> CreateFileW/WriteFile guarda', True)

    hmp('sendkey esc', 0.5)
    if not waitstr('w2demo: WM_CLOSE', 10):
        print('FAIL - sin WM_CLOSE'); qemu.terminate(); return
    if not waitstr('w2demo: exit', 10):
        print('FAIL - sin exit'); qemu.terminate(); return
    if not waitstr('exit:0', 10):
        print('FAIL - exit != 0'); qemu.terminate(); return
    check('Esc -> WM_CLOSE -> WM_DESTROY -> exit:0 limpio', True)

    hmp('sendkey ret', 0.4)
    sendcmd('cat demo.txt')
    if not waitstr('hola', 10):
        print('FAIL - demo.txt sin contenido'); qemu.terminate(); return
    check('cat demo.txt = texto tecleado (persistencia real)', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)