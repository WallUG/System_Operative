#!/usr/bin/env python3
"""Fase 23-C9: modos binario/texto (_O_BINARY/_O_TEXT).

txtmode.exe:
- CreateFileA binario (default): "Hola\\nMundo\\n" guarda LF crudo (0 CRs)
- CreateFileA _O_TEXT: "Linea1\\nLinea2\\n" guarda CRLF (2 CRs al releer raw)
- Re-leer en _O_TEXT: CRLF->LF (0 CRs, len 13, match=1)
- _open/_write/_read con _O_TEXT (msvcrt): "X\\nY\\n" -> leido "X\\nY\\n"
  sin CRs (match=1)
- _open _O_BINARY sobre el mismo archivo: ve los CRLF crudos (2 CRs)
- metapad sigue guardando/leyendo bien (regresion 20d ya lo cubre)"""
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
            sys.stdout.write(ch.decode('utf-8', errors='replace'))
            sys.stdout.flush()
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

    sendcmd('run txtmode.exe')
    if not waitstr('t9: start', 20):
        print('FAIL - txtmode no arranca'); qemu.terminate(); return

    # binario por defecto: 0 CRs al releer t9_a.txt
    if not waitstr('t9: A bin w=11', 10):
        print('FAIL - A bin w'); qemu.terminate(); return
    if not waitstr(' cr=0', 10):
        print('FAIL - A bin cr'); qemu.terminate(); return
    check('binario (default): \\n guardado crudo, 0 CRs', True)

    # _O_TEXT al escribir: 2 CRs en el archivo crudo
    if not waitstr('t9: B txt escrito, raw cr=2 rd=16', 10):
        print('FAIL - B txt escrito cr'); qemu.terminate(); return
    check('_O_TEXT escribe LF->CRLF (2 CRs en crudo)', True)

    # _O_TEXT al leer: 0 CRs, len 13, match 1
    if not waitstr('t9: B txt leido cr=0', 10):
        print('FAIL - B txt leido cr'); qemu.terminate(); return
    if not waitstr(' len=14', 10):
        print('FAIL - B txt leido len'); qemu.terminate(); return
    if not waitstr(' match=1', 10):
        print('FAIL - B txt leido match'); qemu.terminate(); return
    check('_O_TEXT lee CRLF->LF (0 CRs, len 14)', True)

    # _open/_write/_read con _O_TEXT (msvcrt)
    if not waitstr('t9: C text read n=4', 10):
        print('FAIL - C text n'); qemu.terminate(); return
    if not waitstr(' cr=0', 10):
        print('FAIL - C text cr'); qemu.terminate(); return
    if not waitstr(' match=1', 10):
        print('FAIL - C text match'); qemu.terminate(); return
    check('_open/_write/_read _O_TEXT (msvcrt): X\\nY\\n limpio', True)

    # _open _O_BINARY: ve CRLF crudo (2 CRs)
    if not waitstr('t9: C bin n=6', 10):
        print('FAIL - C bin n'); qemu.terminate(); return
    if not waitstr(' cr=2', 10):
        print('FAIL - C bin cr'); qemu.terminate(); return
    check('_O_BINARY ve los CRLF crudos (2 CRs, 6 bytes)', True)

    if not waitstr('t9: fin', 10):
        print('FAIL - sin fin'); qemu.terminate(); return

    # cat t9_a.txt: sin ^M (texto limpio)
    time.sleep(1)
    sendcmd('cat t9_a.txt')
    if not waitstr('Mundo', 15):
        print('FAIL - cat t9_a'); qemu.terminate(); return
    check('cat t9_a.txt muestra el texto limpio', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)