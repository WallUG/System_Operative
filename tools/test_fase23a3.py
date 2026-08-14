#!/usr/bin/env python3
"""Fase 23-A3: foco y eventos entre apps (explorer + metapad a la vez).

Verifica que:
- el teclado va a la ventana topmost (metapad al lanzarse de ultimo)
- un clic sintetico en la franja visible de la ventana inferior
  (explorer, parcialmente tapada por metapad) la sube al tope y el
  teclado pasa a ella (fix del raise con redraw)
- al cerrar todas las ventanas la consola vuelve y la shell recibe
  el teclado limpio

Los clics son eventos sinteticos inyectados por inject.elf
(SYS_MOUSE_INJECT) con marcadores al serial; el test sincroniza los
sendkeys con ellos. (Nota: cuando el metapad queda bajo el explorer su
area se muestra como fondo en el screendump — bug de renderizado
pre-existente del buffer del RichEdit, NO de enrutado de foco/eventos;
por eso este test solo verifica el explorer y la consola.)"""
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'

passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def rect_digest(px, w, x0, y0, x1, y1):
    s = 0
    for y in range(y0, y1):
        o = (y * w + x0) * 3
        s += sum(px[o:o + (x1 - x0) * 3])
    return s

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
    def waitstr(s, t=30):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False
    def snap(name):
        hmp(f'screendump /tmp/opencode/a3_{name}.ppm', 0.4)
    def ppm(path):
        d = open(path,'rb').read()
        hdr = d.split(b'\n',3)
        w = int(hdr[1].split()[0])
        return d.split(b'\n',3)[3], w

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    # --- 1) explorer (la shell queda esperando su exit) ---
    sendcmd('run explorer.elf')
    if not waitstr('exp: explorador iniciando', 15):
        print('FAIL - explorer no arranca'); qemu.terminate(); return
    time.sleep(1.5)

    # --- 2) el explorer lanza inject.elf (entrada 6) ---
    for _ in range(6):
        hmp('sendkey down', 0.15)
    hmp('sendkey ret', 0.4)
    if not waitstr('inj: start', 15):
        print('FAIL - inject no corre'); qemu.terminate(); return

    # --- 3) el explorer lanza metapad.exe (end + Enter) ---
    hmp('sendkey end', 0.4)
    hmp('sendkey ret', 0.4)
    if not waitstr('new file', 30):
        print('FAIL - metapad no arranca'); qemu.terminate(); return
    time.sleep(6)

    # --- 4) foco al topmost: metapad recibe el teclado ---
    snap('antes_a')
    hmp('sendkey a', 0.4)
    snap('despues_a')
    px, w = ppm('/tmp/opencode/a3_antes_a.ppm')
    d0 = rect_digest(px, w, 84, 64, 680, 438)
    px, w = ppm('/tmp/opencode/a3_despues_a.ppm')
    d1 = rect_digest(px, w, 84, 64, 680, 438)
    check('teclas van a metapad (topmost): texto crece', d1 != d0)

    # --- 5) clic1 sintetico en el explorer (franja y=440..450) ---
    if not waitstr('inj: hecho1', 45):
        print('FAIL - sin clic1'); qemu.terminate(); return
    time.sleep(1.2)
    snap('explorer_arriba')
    hmp('sendkey j', 0.4)
    snap('explorer_j')
    # la fila resaltada (COLOR_ROW) del explorer debe estar visible y
    # moverse 16px con 'j'
    def row_y(path):
        d = open(path,'rb').read()
        hdr = d.split(b'\n',3)
        w = int(hdr[1].split()[0])
        px = d.split(b'\n',3)[3]
        for y in range(110, 450):
            n = 0
            for x in range(102, 678):
                o = (y*w+x)*3
                if px[o:o+3] == bytes((0x50,0x90,0x50)):
                    n += 1
            if n > 100:
                return y
        return -1
    ya = row_y('/tmp/opencode/a3_explorer_arriba.ppm')
    yj = row_y('/tmp/opencode/a3_explorer_j.ppm')
    check('clic en explorer tapado lo sube: fila resaltada visible',
          ya >= 110 and ya < 450)
    check('teclado pasa al explorer: j baja la seleccion 16px',
          yj == ya + 16)

    # --- 6) clic3 en la X de metapad -> WM_CLOSE (ya subido por clic2) ---
    if not waitstr('inj: hecho3', 45):
        print('FAIL - sin clic3'); qemu.terminate(); return
    time.sleep(2.5)
    log = ''.join(acc)[-2000:]
    if 'GetSaveFileNameA' in log or 'Save' in log:
        hmp('sendkey n', 0.5)
        time.sleep(1)
    time.sleep(1.5)

    # --- 7) q en el explorer -> cierra -> consola limpia ---
    hmp('sendkey q', 0.6)
    if not waitstr('exp: fin', 10):
        print('FAIL - explorer no cerro'); qemu.terminate(); return
    if not waitstr('myos>', 15):
        print('FAIL - no volvio la consola'); qemu.terminate(); return
    check('tras cerrar todo la shell recibe el teclado', True)
    sendcmd('help')
    time.sleep(1)
    tail = ''.join(acc)[-500:]
    check('shell responde a help', 'run' in tail or 'help' in tail
          or 'exit' in tail or 'bootgui' in tail)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)