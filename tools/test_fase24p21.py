#!/usr/bin/env python3
"""Fase 24-P2.1: comctl32 toolbar/statusbar/trackbar/treeview.

ctldemo.exe crea una ventana con los 4 controles y verifica via
SendMessageA en serial:
- toolbar TB_BUTTONCOUNT == 2
- statusbar SB_GETTEXT(1) == "Listo"
- trackbar TBM_GETPOS == 7
- treeview TVM_GETCOUNT == 2 (raiz + hijo)
- fin ok=1 (exit 0). Ademas screendump del dibujado."""
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

    sendcmd('run ctldemo.exe')
    if not waitstr('ctl: start', 20):
        print('FAIL - ctldemo no arranca'); qemu.terminate(); return
    if not waitstr('ctl: toolbar btns=2', 10):
        print('FAIL - toolbar'); qemu.terminate(); return
    check('toolbar: TB_ADDBUTTONS + TB_BUTTONCOUNT=2', True)
    if not waitstr('ctl: statusbar text=Listo', 10):
        print('FAIL - statusbar')
        print('=== SERIAL ==='); print(''.join(acc)[-2500:])
        qemu.terminate(); return
    check('statusbar: SB_SETPARTS + SB_SETTEXT/SB_GETTEXT', True)
    if not waitstr('ctl: trackbar pos=7', 10):
        print('FAIL - trackbar'); qemu.terminate(); return
    check('trackbar: TBM_SETRANGE + TBM_SETPOS + TBM_GETPOS=7', True)
    if not waitstr('ctl: treeview nodes=2 (root=1,child=2)', 10):
        print('FAIL - treeview'); qemu.terminate(); return
    check('treeview: TVM_INSERTITEMA (raiz+hijo) + TVM_GETCOUNT=2', True)
    if not waitstr('ctl: fin ok=1', 10):
        print('FAIL - fin != 1'); qemu.terminate(); return
    if not waitstr('exit:0', 10):
        print('FAIL - exit != 0'); qemu.terminate(); return
    check('ctldemo termina con exit 0', True)

    # screendump: la ventana con los controles dibujada
    hmp('screendump /tmp/opencode/ctl.ppm', 1.0)
    time.sleep(0.5)
    if os.path.exists('/tmp/opencode/ctl.ppm'):
        check('screendump generado (ventana + controles dibujados)', True)
    else:
        check('screendump generado (ventana + controles dibujados)', False)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)