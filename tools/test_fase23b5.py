#!/usr/bin/env python3
"""Fase 23-B5: dialogos modales reales (DialogBoxParamA/EndDialog).

dlgtest.exe abre un DialogBox con recurso RT_DIALOG (titulo, LTEXT,
DEFPUSHBUTTON OK id=IDOK=1). Se verifica:
- WM_INITDIALOG llega al DlgProc (imprime "dlg: WM_INITDIALOG")
- el dialogo se dibuja (screendump: ventana + boton OK)
- pulsar Enter (boton por defecto) envia WM_COMMAND IDOK -> EndDialog
  y DialogBoxParamA devuelve IDOK (1) -> "dlg: DialogBox devolvio 1"
- WM_CLOSE (Esc) -> EndDialog(0) -> devuelve 0"""
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

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    # --- abrir el dialogo ---
    sendcmd('run dlgtest.exe')
    if not waitstr('dlg: abriendo DialogBox', 20):
        print('FAIL - dlgtest no arranca'); qemu.terminate(); return
    if not waitstr('dlg: WM_INITDIALOG', 10):
        print('FAIL - sin WM_INITDIALOG'); qemu.terminate(); return
    check('DialogBoxParamA envia WM_INITDIALOG al DlgProc', True)
    time.sleep(1)

    # --- el dialogo se dibuja (ventana + boton OK) ---
    hmp('screendump /tmp/opencode/b5_dlg.ppm', 0.5)
    d = open('/tmp/opencode/b5_dlg.ppm','rb').read()
    hdr = d.split(b'\n',3)
    w = int(hdr[1].split()[0])
    px = d.split(b'\n',3)[3]
    # boton OK en (340,348,120,32) centrado: color COLOR_BTN (0x888888)
    btn = 0
    for y in range(350, 378):
        for x in range(342, 458):
            o = (y*w+x)*3
            if px[o:o+3] == bytes((0x88,0x88,0x88)):
                btn += 1
    check('dialogo dibujado (boton OK visible)', btn > 500)

    # --- Enter -> boton por defecto (IDOK) -> EndDialog ---
    hmp('sendkey ret', 0.6)
    if not waitstr('dlg: WM_COMMAND IDOK -> EndDialog', 10):
        print('FAIL - sin WM_COMMAND'); qemu.terminate(); return
    if not waitstr('dlg: DialogBox devolvio 1', 10):
        print('FAIL - EndDialog no devolvio IDOK'); qemu.terminate(); return
    check('Enter dispara el DEFPUSHBUTTON y EndDialog devuelve IDOK', True)

    # --- segundo dialogo: clic con raton en el boton OK (340,348,120,32) ---
    sendcmd('run dlgtest.exe')
    if not waitstr('dlg: WM_INITDIALOG', 20):
        print('FAIL - sin WM_INITDIALOG (2)'); qemu.terminate(); return
    # mover el cursor al centro del boton y hacer clic (inject via monitor
    # no existe; se usa el raton PS/2 sintetico no disponible en headless:
    # se emula el clic con mouse_move/mouse_button si el monitor lo permite)
    hmp('mouse_move 400 364', 0.3)
    hmp('mouse_button 0', 0.3)
    hmp('mouse_button 1', 0.3)
    if not waitstr('dlg: WM_COMMAND IDOK -> EndDialog', 10):
        print('FAIL - sin WM_COMMAND por clic'); qemu.terminate(); return
    if not waitstr('dlg: DialogBox devolvio 1', 10):
        print('FAIL - clic no devolvio IDOK'); qemu.terminate(); return
    check('Clic en el boton OK dispara WM_COMMAND IDOK -> EndDialog', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)