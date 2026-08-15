#!/usr/bin/env python3
"""Fase 23-A2: scrollbar del explorer + Home/End/PgUp/PgDn.

Genera un FS con la raiz (~28 archivos, mas de las 21 filas visibles) y
un subdirectorio 'apps/' con 35 archivos, arranca el explorer y verifica:
- scrollbar visible (thumb en la franja derecha) en la raiz
- End baja la seleccion y el thumb al fondo; Home lo sube al tope
- Enter navega a 'apps/' (35 entradas) y las teclas Home/End/PgUp/PgDn
  mueven sel y el thumb (se mide el centro del thumb en el screendump)
- PgDn/PgUp saltan por pagina (21 filas)"""
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'

passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

THUMB = (0x80, 0xA0, 0x80)   # COLOR_THUMB 0x008080A0 en el PPM (bytes LE)
TRACK = (0x30, 0x50, 0x30)   # COLOR_SB 0x00303050
ROW   = (0x50, 0x90, 0x50)   # COLOR_ROW 0x00505090 (fila seleccionada)

def make_fs():
    """Regenera build/os-persist.bin con la raiz normal + subdir apps/
    con 35 archivos (patron del Makefile persist_disk + makefs --dir)."""
    subprocess.run(['make', '-s', 'os-image.bin'], check=True)
    # lista exacta de archivos del FS: la del target fs.bin del Makefile
    out = subprocess.check_output(['make', '-s', '-n', '-B', 'build/fs.bin'], text=True)
    lines = [l for l in out.splitlines() if 'makefs.py' in l]
    root = [t for l in lines for t in l.split()
            if t.endswith(('.elf', '.exe', '.txt'))]
    os.makedirs('/tmp/opencode/a2files', exist_ok=True)
    a2 = []
    for i in range(35):
        p = f'/tmp/opencode/a2files/app{i:02d}.dat'
        with open(p, 'wb') as f:
            f.write(b'data ' + str(i).encode() + b'\n' * 20)
        a2.append(p)
    fs = 'build/fs_a2.bin'
    subprocess.run(['python3', 'tools/makefs.py'] + root +
                   ['--dir', 'apps:' + ','.join(a2),
                    '-c', '2000', '-o', fs], check=True)
    subprocess.run(['truncate', '-s', str(2000 * 512), fs], check=True)
    with open(WORK, 'wb') as f:
        for part in ['build/boot.bin', 'build/kernel.bin', fs]:
            f.write(open(part, 'rb').read())
    subprocess.run(['truncate', '-s', str(16384 * 512), WORK], check=True)

def read_ppm(path):
    with open(path, 'rb') as f:
        assert f.readline().strip() == b'P6'
        w, h = map(int, f.readline().split())
        f.readline()          # maxval
        return f.read(), w, h

def thumb_center(path):
    """Centro (x,y) del thumb: pico de pixeles THUMB en la columna del
    scrollbar (x 666..677), filas y 110..448. Devuelve (-1,-1) si no hay."""
    data, w, h = read_ppm(path)
    best_x, best_y = -1, -1
    best_n = 0
    for x in range(666, 678):
        n = y = 0
        for yy in range(110, 448):
            o = (yy * w + x) * 3
            if (data[o], data[o+1], data[o+2]) == THUMB:
                n += 1
                y += yy
        if n > best_n:
            best_n, best_x, best_y = n, x, y // n
    return (best_x, best_y) if best_n else (-1, -1)

def run():
    make_fs()
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
    def hmp(c, dt=0.2):
        s = mon_sock()
        if s is None: return
        s.sendall(f'{c}\n'.encode()); time.sleep(dt); s.close()
    def sendcmd(cmd):
        for ch in cmd+'\n':
            hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'), 0.12)
    def waitstr(s, t=10):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False
    def snap(name):
        hmp(f'screendump /tmp/opencode/a2_{name}.ppm', 0.4)

    # autoboot -> shell (cancelar con una tecla)
    if not waitstr('Autoboot:', 15):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.4)
    if not waitstr('myos>', 15):
        print('FAIL - sin prompt'); qemu.terminate(); return
    sendcmd('run explorer.elf')
    if not waitstr('exp: explorador iniciando', 10):
        print('FAIL - explorer no arranca'); qemu.terminate(); return
    time.sleep(1.2)
    snap('root0')

    cx, cy, cw, ch = 102, 90, 576, 358   # cliente (ventana en 100,70)

    # 1) scrollbar visible en la raiz (29 entradas > 21 visibles)
    bx, by0 = thumb_center('/tmp/opencode/a2_root0.ppm')
    check('scrollbar visible en la raiz (thumb en x 666..677)',
          bx >= 666 and bx <= 677)
    check('thumb al inicio (arriba)', by0 >= 110 and by0 <= 250)

    # 2) End en la raiz: sel=28 (apps, ultima) -> thumb baja
    hmp('sendkey end', 0.4)
    snap('root1')
    bx, by1 = thumb_center('/tmp/opencode/a2_root1.ppm')
    check('End en la raiz baja el thumb', by1 > by0 + 50)

    # 3) Enter: navega al subdir apps/ (35 entradas)
    hmp('sendkey ret', 0.6)
    time.sleep(0.5)
    snap('apps0')
    bx, bya = thumb_center('/tmp/opencode/a2_apps0.ppm')
    check('navego a apps/ (35 entradas, thumb visible)', bx >= 666)

    # 4) End en apps: sel=34 -> scroll_off=14, thumb al fondo
    hmp('sendkey end', 0.4)
    snap('apps1')
    bx, bye = thumb_center('/tmp/opencode/a2_apps1.ppm')
    check('End en apps: thumb al fondo', bye > bya + 80)

    # 5) Home: sel=0 -> thumb arriba (cerca del tope del track)
    hmp('sendkey home', 0.4)
    snap('apps2')
    bx, byh = thumb_center('/tmp/opencode/a2_apps2.ppm')
    check('Home: thumb al tope', byh >= 110 and byh <= 250)

    # 6) 25 x down: sel=24 -> thumb baja un poco
    for _ in range(25):
        hmp('sendkey down', 0.12)
    snap('apps3')
    bx, byd = thumb_center('/tmp/opencode/a2_apps3.ppm')
    check('25x down mueve el thumb', byd > byh + 20 and byd < bye - 20)

    # 7) PgDn (sel +=21 -> 34) -> thumb abajo; PgUp -> sube
    hmp('sendkey pgdn', 0.4)
    snap('apps4')
    bx, byp = thumb_center('/tmp/opencode/a2_apps4.ppm')
    check('PgDn: thumb al fondo', byp > byd + 30)
    hmp('sendkey pgup', 0.4)
    hmp('sendkey pgup', 0.4)
    snap('apps5')
    bx, byq = thumb_center('/tmp/opencode/a2_apps5.ppm')
    check('PgUp x2: thumb vuelve al tope', abs(byq - byh) <= 15)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)
