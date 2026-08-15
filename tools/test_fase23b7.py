#!/usr/bin/env python3
"""Fase 23-B7: CreateWindowExA extendido (MoveWindow, GetWindowRect,
WM_MOVE, WM_SETTEXT, WM_SIZE).

movetest.exe crea una ventana en (60,60) y luego:
- MoveWindow(160,120) -> el wndproc recibe WM_MOVE con x=160 y=120
  y la ventana se mueve en pantalla (screendump: titulo en y=120)
- SetWindowTextA -> el wndproc recibe WM_SETTEXT y el titulo cambia
- GetWindowRect -> rect=160,120,560,420
- PostQuitMessage + message loop -> fin limpio"""
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

    sendcmd('run movetest.exe')
    if not waitstr('mv: start', 20):
        print('FAIL - movetest no arranca'); qemu.terminate(); return
    if not waitstr('mv: WM_MOVE x=160 y=120', 15):
        print('FAIL - sin WM_MOVE'); qemu.terminate(); return
    check('MoveWindow envia WM_MOVE con las coords nuevas (160,120)', True)
    time.sleep(1)

    if not waitstr('mv: esperando screendump', 10):
        print('FAIL - sin espera de screendump'); qemu.terminate(); return
    hmp('screendump /tmp/opencode/b7_ppm', 0.5)
    # la ventana se movio: el cliente blanco (396x278) debe estar en la
    # posicion nueva (x 164..556, y 142..418) y NO en la original
    # (x 62..454, y 82..358), que muestra la consola negra
    d = open('/tmp/opencode/b7_ppm','rb').read()
    px = d.split(b'\n',3)[3]
    w = int(d.split(b'\n',3)[1].split()[0])
    def frame_gray(x0, y0, x1, y1):
        # marco gris (192,192,192): solo lo dibujan las ventanas (la
        # consola es negra con texto); se cuenta el borde del rect
        n = 0
        for yy in range(y0, y1, 2):
            for xx in range(x0, x1, 2):
                o = (yy*w+xx)*3
                if px[o:o+3] == bytes((192,192,192)): n += 1
        return n
    at_new = frame_gray(160, 120, 561, 421)
    at_old = frame_gray(60, 60, 141, 101)   # esquina superior izq vieja
    check('la ventana se movio en pantalla (marco gris en la nueva posicion)',
          at_new > 300 and at_old < 20)

    if not waitstr('mv: WM_SETTEXT', 10):
        print('FAIL - sin WM_SETTEXT'); qemu.terminate(); return
    check('SetWindowTextA envia WM_SETTEXT al wndproc', True)

    if not waitstr('mv: rect=160,120,560,420', 10):
        print('FAIL - GetWindowRect mal'); qemu.terminate(); return
    check('GetWindowRect devuelve 160,120,560,420', True)

    if not waitstr('mv: fin', 10):
        print('FAIL - sin fin'); qemu.terminate(); return
    check('PostQuitMessage + loop terminan limpio', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)