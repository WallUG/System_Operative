#!/usr/bin/env python3
"""Fase 24-P1.2: DialogBoxParamA real con EDIT control + GetDlgItemTextA.

B5 ya cubria el caso basico (STATIC + DEFPUSHBUTTON OK). Este ítem
completa los controles EDIT: el dialogo enfoca el primer EDIT, recibe
el teclado (se escriben 'A','B','C'), y el DlgProc lee GetDlgItemTextA
y EndDialog(IDOK) solo si el texto coincide con 'ABC' (exit 0)."""
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

    sendcmd('run dlgtest2.exe')
    if not waitstr('dlg2: abriendo dialogo con edit', 20):
        print('FAIL - dlgtest2 no arranca'); qemu.terminate(); return
    if not waitstr('dlg2: WM_INITDIALOG', 10):
        print('FAIL - sin WM_INITDIALOG'); qemu.terminate(); return
    check('DialogBoxParamA envia WM_INITDIALOG (dialogo con EDIT)', True)

    # escribir ABC en el EDIT enfocado, luego Enter (DEFPUSHBUTTON)
    for c in 'abc':
        hmp(f'sendkey {c}', 0.15)
    hmp('sendkey ret', 0.4)

    if not waitstr('dlg2: leido 3 = abc', 10):
        print('FAIL - el EDIT no recibio/guardo el texto o GetDlgItemTextA falla')
        print('=== SERIAL ===')
        print(''.join(acc)[-3000:])
        qemu.terminate(); return
    check('teclado enruta al EDIT y GetDlgItemTextA devuelve ABC', True)

    if not waitstr('dlg2: devolvio 1', 10):
        print('FAIL - EndDialog no devolvio IDOK'); qemu.terminate(); return
    check('EndDialog(IDOK) devuelve 1', True)

    if not waitstr('exit:0', 10):
        print('FAIL - exit != 0 (texto no coincidia)'); qemu.terminate(); return
    check('dlgtest2 termina con exit 0', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)