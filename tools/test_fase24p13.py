#!/usr/bin/env python3
"""Fase 24-P1.3: registro stubs -> .ini persistente (advapi32).

regtest.exe (advapi32): RegCreateKeyExA + RegSetValueExA +
RegQueryValueExA + RegOpenKeyExA sobre "registry.ini" del MEFS.
- Ejecucion 1 (sin registry.ini): escribe AppName/Count, los relee
  (round-trip en memoria) y persiste. Esperamos "run 1", "AppName=Hello
  Count=42", "open ok", exit 0.
- Ejecucion 2 (mismo disco, registry.ini ya persistido): lee Count=42
  desde el .ini (persistencia entre procesos). Esperamos "run 2
  (persistido) Count=42", "open ok", exit 0."""
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'
passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def boot(qemu):
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
                 '_':'sendkey shift-minus','\\':'sendkey backslash'}.get(
                 ch, f'sendkey {ch}'), 0.12)
    def waitstr(s, t=25):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False
    return acc, hmp, sendcmd, waitstr

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
    acc, hmp, sendcmd, waitstr = boot(qemu)

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    # --- ejecucion 1 (fresh) ---
    sendcmd('run regtest.exe')
    if not waitstr('reg: run 1 (sin registro previo)', 20):
        print('FAIL - regtest run1 no arranca'); qemu.terminate(); return
    if not waitstr('reg: AppName=Hello Count=42', 10):
        print('FAIL - round-trip en memoria'); qemu.terminate(); return
    check('RegCreateKeyExA+RegSetValueExA+RegQueryValueExA (round-trip)', True)
    if not waitstr('reg: open ok', 10):
        print('FAIL - RegOpenKeyExA falla'); qemu.terminate(); return
    check('RegOpenKeyExA abre la clave', True)
    if not waitstr('reg: fin ok=1', 10):
        print('FAIL - run1 fin != 1'); qemu.terminate(); return
    if not waitstr('exit:0', 10):
        print('FAIL - run1 exit != 0'); qemu.terminate(); return
    check('regtest run1 termina con exit 0 (persistio registry.ini)', True)

    # --- ejecucion 2 (mismo disco: registry.ini persistido) ---
    sendcmd('run regtest.exe')
    if not waitstr('reg: run 2 (persistido) Count=42', 25):
        print('FAIL - no leyo el valor persistido del .ini')
        print('=== SERIAL ==='); print(''.join(acc)[-2500:])
        qemu.terminate(); return
    check('persistencia: run2 lee Count=42 del registry.ini', True)
    if not waitstr('reg: open ok', 10):
        print('FAIL - run2 open falla'); qemu.terminate(); return
    if not waitstr('reg: fin ok=1', 10):
        print('FAIL - run2 fin != 1'); qemu.terminate(); return
    if not waitstr('exit:0', 10):
        print('FAIL - run2 exit != 0'); qemu.terminate(); return
    check('regtest run2 termina con exit 0 (persistencia entre procesos)', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)