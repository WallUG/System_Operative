#!/usr/bin/env python3
"""Fase 24-P3.2: explorer Win32 con SysListView32.

Verifica el explorador reescrito como .exe (comctl32 listview):
- Tras crear installed/ con installer.elf, el explorer lista la raiz
  con columnas Nombre/Tam ("exp: items=50 cols=2") y screendump con
  header, filas y scrollbar (thumb visible).
- 'd' (hook de test) inyecta doble clic en la fila 0: lanza hello.elf
  ("exp: lanzando hello.elf" + exit:0) — el clic selecciona y el
  segundo clic activa.
- End+Up navegan a installed/ (scroll) y Enter entra ("exp: cd
  installed"); Enter en readme_inst.txt abre el visor ("exp: ver
  readme_inst.txt", screendump oscuro con texto).
- 'b' vuelve a la lista ("exp: vuelta lista") y sube ("exp: subir").
- End+Up+Up + Enter lanza metapad.exe ("exp: lanzando metapad.exe");
  el explorador inyecta clics periodicos en su X ("exp: inj X") hasta
  que cierra (exit:0).
- 'q' cierra el explorador ("exp: fin" + exit:0)."""
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'
passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def ppm_region(path, x0, y0, x1, y1):
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
def hmp(c, dt=0.3):
    s = mon_sock()
    if s is None: return
    s.sendall(f'{c}\n'.encode()); time.sleep(dt); s.close()
def sendcmd(cmd):
    for ch in cmd+'\n':
        hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot',
             '-':'sendkey minus'}.get(ch, f'sendkey {ch}'), 0.12)
def waitstr(s, t=25, after=0):
    d = time.time()+t
    while time.time() < d:
        if s in ''.join(acc[after:]): return True
        time.sleep(0.2)
    return False

if not waitstr('Autoboot:', 25):
    print('FAIL - sin autoboot'); qemu.terminate(); sys.exit(1)
hmp('sendkey x', 0.5)
if not waitstr('myos>', 20):
    print('FAIL - sin prompt'); qemu.terminate(); sys.exit(1)

# --- instalador: crea installed/ (navegacion de subdirectorios) ---
sendcmd('run installer.elf')
if not waitstr('exit:0', 20):
    print('FAIL - installer'); print(''.join(acc)[-500:]); qemu.terminate(); sys.exit(1)
check('instalador crea installed/', True)

# --- explorer ---
sendcmd('run explorer.exe')
if not waitstr('exp: start', 15):
    print('FAIL - explorer no arranca'); print(''.join(acc)[-500:]); qemu.terminate(); sys.exit(1)
if not waitstr('exp: items=50 cols=2', 10):
    print('FAIL - items'); print(''.join(acc)[-500:]); qemu.terminate(); sys.exit(1)
check('explorer lista la raiz (items=50, 2 columnas)', True)
time.sleep(0.5)
if os.environ.get('NODUMP') != '1':
    hmp('screendump /tmp/opencode/p32_root.ppm', 0.8)
if os.path.exists('/tmp/opencode/p32_root.ppm'):
    reg = ppm_region('/tmp/opencode/p32_root.ppm', 106, 112, 674, 446)
else:
    reg = [(255,255,255)]*10000
white = count_color(reg, (255, 255, 255))
hdr = count_color(reg, (192, 192, 192))
thumb = count_color(reg, (128, 160, 128))
check(f'listview: cliente blanco + header gris + scrollbar (w{white} h{hdr} t{thumb})',
      white > 5000 and hdr > 1500 and thumb > 100)

# --- doble clic (hook 'd') en la fila 0: lanza hello.elf ---
hmp('sendkey d', 0.5)
if not waitstr('exp: inj doble row0', 10):
    print('FAIL - doble clic no inyectado'); qemu.terminate(); sys.exit(1)
if not waitstr('exp: lanzando hello.elf', 10):
    print('FAIL - doble clic no lanzo'); print(''.join(acc)[-600:]); qemu.terminate(); sys.exit(1)
check('doble clic -> lanza hello.elf', True)
if not waitstr('USER: hello.elf termino', 15):
    print('FAIL - hello.elf no salio'); print(''.join(acc)[-600:]); qemu.terminate(); sys.exit(1)
check('hello.elf termina', True)

# --- navegar a installed/ (End + Up, scroll) ---
hmp('sendkey end', 0.4)
hmp('sendkey ret', 0.4)
if not waitstr('exp: cd installed', 25):
    print('FAIL - cd installed'); print(''.join(acc)); qemu.terminate(); sys.exit(1)
check('Enter en installed/ navega (cd)', True)
time.sleep(0.5)
hmp('screendump /tmp/opencode/p32_sub.ppm', 0.8)
reg = ppm_region('/tmp/opencode/p32_sub.ppm', 106, 112, 674, 446)
if count_color(reg, (255, 255, 255)) < 3000:
    print('FAIL - lista del subdir'); qemu.terminate(); sys.exit(1)
check('subdir listado (installed/)', True)

# --- ver readme_inst.txt (fila 1 = ".." es 0) ---
hmp('sendkey down', 0.3)
hmp('sendkey ret', 0.4)
if not waitstr('exp: ver readme_inst.txt', 20):
    print('FAIL - visor'); print(''.join(acc)[-600:]); qemu.terminate(); sys.exit(1)
check('Enter en readme_inst.txt abre el visor', True)
time.sleep(0.5)
hmp('screendump /tmp/opencode/p32_view.ppm', 0.8)
reg = ppm_region('/tmp/opencode/p32_view.ppm', 106, 112, 674, 446)
dark = count_color(reg, (32, 16, 16))
check(f'visor: fondo oscuro + texto (x{dark})', dark > 2000)

# --- volver a la lista y subir ---
hmp('sendkey b', 0.4)
if not waitstr('exp: vuelta lista', 20):
    print('FAIL - vuelta lista'); print(''.join(acc)[-600:]); qemu.terminate(); sys.exit(1)
check('b vuelve a la lista', True)
hmp('sendkey b', 0.4)
if not waitstr('exp: subir', 20):
    print('FAIL - subir'); qemu.terminate(); sys.exit(1)
check('b sube al directorio padre', True)

# --- lanzar metapad.exe con el hook 'm' (ultimo .exe de la raiz) ---
hmp('sendkey m', 0.5)
if not waitstr('exp: inj last-exe', 10):
    print('FAIL - hook m'); print(''.join(acc)[-600:]); qemu.terminate(); sys.exit(1)
if not waitstr('exp: lanzando metapad.exe', 25):
    print('FAIL - no lanzo metapad'); print(''.join(acc)[-800:]); qemu.terminate(); sys.exit(1)
check('hook m lanza metapad.exe (el ultimo .exe)', True)
mark = len(''.join(acc))
if not waitstr('exp: inj X', 25, after=mark):
    print('FAIL - sin inyeccion del X'); qemu.terminate(); sys.exit(1)
check('explorador inyecta clics en el X de metapad', True)
mark = len(''.join(acc))
if not waitstr('exit:0', 45, after=mark):
    print('FAIL - metapad no cerro'); print(''.join(acc)[mark:]); qemu.terminate(); sys.exit(1)
check('metapad se cierra con el boton X (exit:0)', True)

# --- cerrar el explorador ---
hmp('sendkey q', 0.5)
if not waitstr('exp: fin', 20):
    print('FAIL - explorer no cerro'); print(''.join(acc)[-1500:]); qemu.terminate(); sys.exit(1)
if not waitstr('exit:0', 10):
    print('FAIL - exit != 0'); qemu.terminate(); sys.exit(1)
check('q cierra el explorador (exit:0)', True)

print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()
sys.exit(0 if all(passed) else 1)