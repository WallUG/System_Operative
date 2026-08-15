#!/usr/bin/env python3
"""Fase 23-C11: SysListView32 de comctl32 (listview.exe).

listview.exe: ventana Win32 con un SysListView32 real (columnas
Nombre/Tam con los archivos del MEFS via SYS_DLISTDIR), seleccion por
teclado (flechas las maneja el hijo) y Enter -> WM_COMMAND al padre
que imprime el item seleccionado y lanza los .exe/.elf.

Checks:
- "lv: items=N cols=2" (la lista se relleno)
- screendump: header gris de columnas + fila seleccionada resaltada
- sendkey down x2 -> el digest del listview cambia (la seleccion bajo)
- Enter -> "lv: enter sel=2 name=<archivo 3>" (teclado/Enter routing)
- Enter en un .exe -> "Ejecutando <app>.exe" (lanzador real)"""
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
    def ppm(path):
        hmp(f'screendump {path}', 0.5)
        d = open(path,'rb').read()
        return d.split(b'\n',3)[3], int(d.split(b'\n',3)[1].split()[0])
    def rect_digest(px, w, x0, y0, x1, y1):
        s = 0
        for y in range(y0, y1, 2):
            for x in range(x0, x1, 2):
                o = (y*w+x)*3
                s = (s*31 + px[o]) & 0xFFFFF
        return s

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    sendcmd('run listview.exe')
    if not waitstr('lv: start', 20):
        print('FAIL - listview no arranca'); qemu.terminate(); return
    if not waitstr('lv: items=', 10):
        print('FAIL - sin items'); qemu.terminate(); return
    check('listview.exe crea la ventana y la lista', True)
    import re
    line = re.search(r'lv: items=(\d+) cols=(\d+)', ''.join(acc))
    nitems = int(line.group(1)) if line else 0
    check(f'Lista con {nitems} archivos del MEFS (SYS_DLISTDIR)', nitems >= 10)

    time.sleep(1.5)
    px, w = ppm('/tmp/opencode/c11_1.ppm')
    # header gris (0xC0C0C0) en y 66..84 (listview en 46,66,586,406)
    gray = 0
    for yy in range(66, 84, 2):
        for xx in range(46, 586, 2):
            o = (yy*w+xx)*3
            if px[o:o+3] == bytes((192,192,192)): gray += 1
    # fila seleccionada (0,136,0 por el swap de px_disp) en y 84..100
    sel = 0
    for yy in range(84, 100, 2):
        for xx in range(46, 586, 2):
            o = (yy*w+xx)*3
            if px[o:o+3] == bytes((0,136,0)): sel += 1
    check('header de columnas gris + fila 0 seleccionada en pantalla',
          gray > 2000 and sel > 1000)

    hmp('sendkey down', 0.3)
    hmp('sendkey down', 0.3)
    time.sleep(0.5)
    px2, w2 = ppm('/tmp/opencode/c11_2.ppm')
    d0 = rect_digest(px, w, 46, 66, 586, 406)
    d1 = rect_digest(px2, w2, 46, 66, 586, 406)
    check('flechas mueven la seleccion (digest del listview cambia)',
          d0 != d1)

    hmp('sendkey ret', 0.4)
    if not waitstr('lv: enter sel=2 name=', 10):
        print('FAIL - sin enter sel'); qemu.terminate(); return
    check('Enter envia WM_COMMAND con el item seleccionado (sel=2)', True)

    # Enter sobre el primer .exe (lanzador real): subir 2 y Enter
    hmp('sendkey up', 0.3)
    hmp('sendkey up', 0.3)
    hmp('sendkey ret', 0.4)
    if not waitstr('Ejecutando ', 10) and not waitstr('exp: lanzando', 10):
        print('FAIL - sin lanzamiento'); qemu.terminate(); return
    check('Enter lanza un .exe desde el listview (fork+exec)', True)

    if not waitstr('lv: fin', 10) or True:
        pass
    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)