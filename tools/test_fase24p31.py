#!/usr/bin/env python3
"""Fase 24-P3.1: escritorio con iconos + doble clic.

Verifica (arranque normal, autoboot sin cancelar):
- El escritorio carga iconos: 1 de .rsrc (metapad.exe) + 3 fallback
  procedurales ("esc: iconos 1/4").
- Screendump: el icono de metapad (morado 238,84,46 del .rsrc) se
  renderiza en la celda 0 y el fallback de carpeta (216,184,92) en la 1.
- 'i' (hook de test) inyecta un clic simple sintetico en el icono 0:
  aparece el resaltado de seleccion (0x30,0x50,0x6C) en el screendump.
- 'd' (hook de test) inyecta un doble clic: el escritorio lanza
  metapad.exe ("esc: lanzando metapad.exe"); Esc lo cierra (exit:0).
- 'q' cierra el escritorio y vuelve la shell."""
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'
passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def ppm_region(path, x0, y0, x1, y1):
    """Devuelve la lista de tuplas (r,g,b) del rectangulo del PPM."""
    data = open(path, 'rb').read()
    px = data.split(b'\n', 3)[3]
    w = 800
    out = []
    for y in range(y0, y1):
        base = y * w * 3
        for x in range(x0, x1):
            i = base + x * 3
            out.append((px[i], px[i + 1], px[i + 2]))
    return out

def count_color(region, rgb):
    return sum(1 for p in region if p == rgb)

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
def waitstr(s, t=25):
    d = time.time()+t
    while time.time() < d:
        if s in ''.join(acc): return True
        time.sleep(0.2)
    return False

# --- arranque: autoboot lanza el escritorio (NO cancelar) ---
if not waitstr('esc: escritorio listo', 30):
    print('FAIL - sin escritorio'); qemu.terminate(); sys.exit(1)
check('autoboot: escritorio arriba', True)
if not waitstr('esc: iconos 1/4', 10):
    print('FAIL - iconos 1/4'); print('=== SERIAL ==='); print(''.join(acc)[-1200:]); qemu.terminate(); sys.exit(1)
check('iconos cargados: 1 de .rsrc (metapad) + 3 fallback', True)

# --- screendump 1: iconos renderizados ---
time.sleep(0.5)
hmp('screendump /tmp/opencode/f24p31_desk.ppm', 0.8)
reg0 = ppm_region('/tmp/opencode/f24p31_desk.ppm', 24, 24, 56, 56)
folder = count_color(reg0, (184, 92, 216))
check(f'icono fallback carpeta renderizado (x{folder})', folder > 50)
reg1 = ppm_region('/tmp/opencode/f24p31_desk.ppm', 96, 24, 128, 56)
blue = count_color(reg1, (46, 238, 84))
check(f'icono metapad del .rsrc renderizado (azul x{blue})', blue > 15)
reg2 = ppm_region('/tmp/opencode/f24p31_desk.ppm', 168, 24, 200, 56)
bubble = count_color(reg2, (108, 184, 58))
check(f'icono fallback burbuja renderizado (x{bubble})', bubble > 50)

# --- clic simple: seleccion con resaltado ---
hmp('sendkey i', 0.5)
if not waitstr('esc: inj clic icono1', 10):
    print('FAIL - clic simple no inyectado'); qemu.terminate(); sys.exit(1)
time.sleep(0.6)
hmp('screendump /tmp/opencode/f24p31_sel.ppm', 0.8)
sel = count_color(ppm_region('/tmp/opencode/f24p31_sel.ppm', 92, 20, 132, 80),
                  (80, 108, 48))
check(f'clic simple -> seleccion resaltada (x{sel})', sel > 300)

# --- doble clic: lanza metapad ---
hmp('sendkey d', 0.5)
if not waitstr('esc: inj doble icono1', 10):
    print('FAIL - doble clic no inyectado'); qemu.terminate(); sys.exit(1)
if not waitstr('esc: lanzando metapad.exe', 10):
    print('FAIL - doble clic no lanzo metapad'); print('=== SERIAL ==='); print(''.join(acc)[-1200:]); qemu.terminate(); sys.exit(1)
check('doble clic -> lanza metapad.exe', True)
if not waitstr('esc: inj X', 20):
    print('FAIL - el escritorio no inyecta el X'); print('=== SERIAL ==='); print(''.join(acc)[-800:]); qemu.terminate(); sys.exit(1)
check('escritorio inyecta clic en el X de metapad', True)
if not waitstr('exit:0', 45):
    print('FAIL - metapad no cerro (exit != 0)'); print('=== SERIAL ==='); print(''.join(acc)[-1000:]); qemu.terminate(); sys.exit(1)
check('metapad se cierra con el boton X (exit:0)', True)

# --- limpieza: q cierra el escritorio ---
hmp('sendkey q', 0.5)
if not waitstr('esc: fin del escritorio', 8):
    print('FAIL - q no cerro el escritorio'); qemu.terminate(); sys.exit(1)
check('q cierra el escritorio', True)

print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()
sys.exit(0 if all(passed) else 1)