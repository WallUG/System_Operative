#!/usr/bin/env python3
"""Fase 23-B8: DLLs OLE32/SHLWAPI/WINSPOOL.

dlltest.exe (importa ole32.dll, shlwapi.dll, winspool.dll):
- CoInitialize/CoUninitialize (0 / ok)
- StrStrI("Hello World","WORLD") -> offset 6
- StrCmpI("HELLO","hello") -> 0
- PathFileExists("metapad.exe") -> 1, ("noexiste.bin") -> 0
- PathFindFileName("C:\\dirs\\sub\\archivo.txt") -> "archivo.txt"
- PathRemoveFileSpec -> 1, dir="C:\\dirs\\sub"
- WINSPOOL: OpenPrinter/StartDoc/WritePrinter/EndDoc/Close -> print.txt
- cat print.txt desde la shell -> "Hola impresora!" en el serial"""
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
    def sendcmd(cmd):
        for ch in cmd+'\n':
            hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'), 0.12)
    def waitstr(s, t=25):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False
    def have(s):
        return s in ''.join(acc)

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    sendcmd('run dlltest.exe')
    if not waitstr('d8: start', 20):
        print('FAIL - dlltest no arranca'); qemu.terminate(); return

    if not waitstr('d8: coinit=0', 10):
        print('FAIL - CoInitialize'); qemu.terminate(); return
    check('CoInitialize/CoUninitialize (OLE32)', True)
    if not waitstr('d8: couninit ok', 10):
        print('FAIL - sin CoUninitialize'); qemu.terminate(); return

    if not waitstr('d8: strstri=6', 10):
        print('FAIL - StrStrI'); qemu.terminate(); return
    check('StrStrI case-insensitive (SHLWAPI)', True)
    if not waitstr('d8: strcmpi=0', 10):
        print('FAIL - StrCmpI'); qemu.terminate(); return
    check('StrCmpI case-insensitive (SHLWAPI)', True)

    if not waitstr('d8: pathfile_mp=1', 10):
        print('FAIL - PathFileExists metapad'); qemu.terminate(); return
    if not waitstr('pathfile_no=0', 10):
        print('FAIL - PathFileExists noexiste'); qemu.terminate(); return
    check('PathFileExists (SHLWAPI)', True)
    if not waitstr('d8: findname=archivo.txt', 10):
        print('FAIL - PathFindFileName'); qemu.terminate(); return
    check('PathFindFileName (SHLWAPI)', True)
    if not waitstr('d8: rmspec=1', 10):
        print('FAIL - PathRemoveFileSpec'); qemu.terminate(); return
    if not waitstr('dir=C:\\dirs\\sub', 10):
        print('FAIL - PathRemoveFileSpec dir'); qemu.terminate(); return
    check('PathRemoveFileSpec (SHLWAPI)', True)

    if not waitstr('d8: openprinter=1', 10):
        print('FAIL - OpenPrinterA'); qemu.terminate(); return
    check('OpenPrinterA/StartDocPrinterA (WINSPOOL)', True)
    if not waitstr('d8: writeprinter=1 written=16', 10):
        print('FAIL - WritePrinter'); qemu.terminate(); return
    check('WritePrinter acumula 16 bytes (WINSPOOL)', True)
    if not waitstr('d8: enddoc=1', 10):
        print('FAIL - EndDocPrinter'); qemu.terminate(); return
    if not waitstr('d8: closeprinter=1', 10):
        print('FAIL - ClosePrinter'); qemu.terminate(); return
    check('EndDocPrinter/ClosePrinter (WINSPOOL)', True)

    if not waitstr('d8: fin', 10):
        print('FAIL - sin fin'); qemu.terminate(); return

    # print.txt debe existir y contener "Hola impresora!"
    sendcmd('cat print.txt')
    if not waitstr('Hola impresora!', 15):
        print('FAIL - print.txt vacio o sin contenido')
        qemu.terminate(); return
    check('print.txt volcado al FS con el texto impreso', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)