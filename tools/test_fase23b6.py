#!/usr/bin/env python3
"""Fase 23-B6: message loop real (GetMessageA/DispatchMessageA) con
routing por ventana.

Flujo (patron del test A3): shell -> explorer.elf; el explorer lanza
wintwo.exe (end+Enter, ultima entrada) y b6inj.elf (home + 8 downs +
Enter). b6inj inyecta clics sinteticos exactos (SYS_MOUSE_INJECT):
- clic en la ventana A (260,222) -> debe imprimir SOLO "A: click"
- clic en la ventana B (540,302) -> debe imprimir SOLO "B: click"
- tras 2 clics PostQuitMessage -> GetMessageA=0 -> "loop terminado"."""
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
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0, text=True)
    acc = []
    def reader():
        while True:
            ch = qemu.stdout.read(1)
            if ch == '': return
            acc.append(ch); sys.stdout.write(ch); sys.stdout.flush()
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
    def key(k, dt=0.15):
        hmp(f'sendkey {k}', dt)
    def waitstr(s, t=30):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False
    def have(s):
        return s in ''.join(acc)

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    key('x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    # --- 1) explorer ---
    for ch in 'run explorer.elf\n':
        key({' ':'spc','\n':'ret','.':'dot'}.get(ch, ch), 0.12)
    if not waitstr('exp: explorador iniciando', 15):
        print('FAIL - explorer no arranca'); qemu.terminate(); return
    time.sleep(1.5)

    # --- 2) lanzar b6inj.elf primero (home + 8 downs + Enter; no crea
    #        ventana -> el explorer conserva el foco del teclado) ---
    key('home', 0.4)
    for _ in range(8):
        key('down', 0.15)
    key('ret', 0.4)
    if not waitstr('b6i: start', 15):
        print('FAIL - b6inj no corre'); qemu.terminate(); return

    # --- 3) lanzar wintwo.exe (end = metapad, up=readme, up=wintwo, Enter) ---
    key('end', 0.4)
    key('up', 0.3)
    key('up', 0.3)
    key('ret', 0.4)
    if not waitstr('wintwo: ventanas creadas', 25):
        print('FAIL - wintwo no crea ventanas'); qemu.terminate(); return
    check('GetMessageA/DispatchMessageA: ventanas creadas y WM_CREATE ok', True)
    time.sleep(1)

    # --- 4) clic1 en la ventana A (inyectado en 260,222) ---
    if not waitstr('b6i: clickA', 45):
        print('FAIL - sin clickA'); qemu.terminate(); return
    if not waitstr('A: click', 15):
        print('FAIL - sin A: click'); qemu.terminate(); return
    check('Clic en ventana A llega al wndproc de A', True)
    check('Clic en A NO llega al wndproc de B',
          not have('B: click'))
    time.sleep(0.5)

    # --- 5) clic2 en la ventana B (inyectado en 540,302) ---
    if not waitstr('b6i: clickB', 30):
        print('FAIL - sin clickB'); qemu.terminate(); return
    if not waitstr('B: click', 15):
        print('FAIL - sin B: click'); qemu.terminate(); return
    check('Clic en ventana B llega al wndproc de B', True)

    # --- 6) tras 2 clics: WM_QUIT y loop terminado ---
    if not waitstr('wintwo: loop terminado (WM_QUIT)', 15):
        print('FAIL - sin WM_QUIT'); qemu.terminate(); return
    check('PostQuitMessage/GetMessageA=0: el loop termina con WM_QUIT', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)