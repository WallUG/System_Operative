#!/usr/bin/env python3
"""Fase 24-P1.1: recursos PE .rsrc — LoadIconA (RT_GROUP_ICON/RT_ICON).

El parser .rsrc (find_resource + LoadMenuA/LoadStringA) ya existia;
este ítem completa LoadIconA, que antes devolvia siempre 0. iconres.exe
(mingw + icono embebido via windres) carga el icono y verifica:
- LoadIconA devuelve un handle != 0 y el parser extrae 32x32.
- LoadIconA de un id inexistente devuelve 0 (no se cuelga).
- fin con ok=1 (exit 0)."""
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

    sendcmd('run iconres.exe')
    if not waitstr('icon: start', 20):
        print('FAIL - iconres no arranca'); qemu.terminate(); return
    if not waitstr('icon: LoadIconA ok handle=', 10):
        print('FAIL - LoadIconA devuelve 0'); qemu.terminate(); return
    check('LoadIconA devuelve handle valido (antes siempre 0)', True)

    if not waitstr('[user32] LoadIconA: icono 100 slot=1 w=32 h=32', 10):
        print('FAIL - parser .rsrc no extrae 32x32'); qemu.terminate(); return
    check('RT_GROUP_ICON -> RT_ICON parsea el bitmap 32x32', True)

    if not waitstr('icon: id inexistente -> 0 ok', 10):
        print('FAIL - id inexistente no devuelve 0'); qemu.terminate(); return
    check('LoadIconA(id inexistente) = 0 sin colgarse', True)

    if not waitstr('icon: fin ok=1', 10):
        print('FAIL - sin fin ok'); qemu.terminate(); return
    if not waitstr('exit:0', 10):
        print('FAIL - exit != 0'); qemu.terminate(); return
    check('iconres termina con exit 0', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)