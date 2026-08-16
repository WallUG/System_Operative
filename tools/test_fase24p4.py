#!/usr/bin/env python3
"""Fase 24-P4: repintado de dialogos, aceleradores (formato 8 B), menu
bar limpio y botones de titulo (X/minimizar/maximizar).

Sesion A (shell): messagebox.exe + metapad con Ctrl+S (dialogo de
Guardar de comdlg32); al cerrar el dialogo con Enter el rect se
restaura (SYS_REDRAW_RECT 42) sin mover la ventana.

Sesion B (explorer): secuencia de botones ('t' arma, 'm' lanza
metapad): clic MAX -> pantalla completa, clic restaurar -> tamano
original, pausa, y clics en el X hasta cerrar metapad (exit:0).

Sesion C (desktop): MIN inyectado por el escritorio ('m' + 'd'),
ventana oculta, hook 'r' restaura via SYS_WINFIND/WINVIS (como el
taskbar), y el X periodico la cierra."""
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def ppm_colors(path):
    d = open(path, 'rb').read().split(b'\n', 3)[3]
    out = {}
    for y in range(0, 600):
        b = y * 800 * 3
        for x in range(0, 800):
            i = b + x * 3
            p = (d[i], d[i + 1], d[i + 2])
            out[p] = out.get(p, 0) + 1
    return out

subprocess.run(['make', 'persist_disk'], capture_output=True)
subprocess.run(['pkill', '-9', '-f', 'qemu-system'], capture_output=True)
time.sleep(0.5)
try: os.unlink(MON)
except FileNotFoundError: pass

def boot():
    qemu = subprocess.Popen(
        ['qemu-system-i386','-display','none','-monitor',f'unix:{MON},server,nowait',
         '-serial','stdio','-no-reboot','-no-shutdown',
         '-drive',f'format=raw,file=build/os-persist.bin'],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
    acc = []
    def reader():
        while True:
            ch = qemu.stdout.read(1)
            if ch == b'': return
            acc.append(ch.decode('utf-8', errors='replace'))
    threading.Thread(target=reader, daemon=True).start()
    def hmp(c, dt=0.3):
        for _ in range(200):
            try:
                s = socket.socket(socket.AF_UNIX); s.settimeout(10)
                s.connect(MON); s.recv(4096)
                break
            except OSError: time.sleep(0.1)
        else: return
        s.sendall(f'{c}\n'.encode()); time.sleep(dt); s.close()
    def waitstr(s, t=25):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False
    def snap(name):
        hmp(f'screendump /tmp/opencode/{name}.ppm', 0.8)
    def sendcmd(cmd):
        for ch in cmd+'\n':
            hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot',
                 '-':'sendkey minus'}.get(ch, f'sendkey {ch}'), 0.12)
    return qemu, acc, hmp, waitstr, snap, sendcmd

# ============ SESION A: shell (dialogos + repintado) ============
qemu, acc, hmp, waitstr, snap, sendcmd = boot()
if not waitstr('Autoboot:', 25):
    print('FAIL - sin autoboot'); qemu.terminate(); sys.exit(1)
hmp('sendkey x', 0.5)
if not waitstr('myos>', 20):
    print('FAIL - sin prompt'); qemu.terminate(); sys.exit(1)

# --- messagebox: OK y pantalla limpia (la shell espera su exit) ---
sendcmd('run messagebox.exe')
time.sleep(6)
hmp('sendkey ret', 0.4)   # OK del MessageBox
if not waitstr('exit:0', 40):
    print('FAIL - messagebox'); qemu.terminate(); sys.exit(1)
check('messagebox OK + repintado (exit:0)', True)

# --- metapad: Ctrl+S abre el dialogo de Guardar (acelerador 8 B) ---
sendcmd('run metapad.exe')
if not waitstr('LoadAcceleratorsA', 30):
    print('FAIL - metapad no arranca'); qemu.terminate(); sys.exit(1)
time.sleep(8)
hmp('sendkey ctrl-s', 0.5)
if not waitstr('[cdlg] GetSaveFileNameA dialog', 15):
    print('FAIL - Ctrl+S no abre el dialogo'); qemu.terminate(); sys.exit(1)
check('Ctrl+S abre el dialogo de Guardar (acelerador 8 B)', True)
time.sleep(3)
snap('p4_svdlg')
c = ppm_colors('/tmp/opencode/p4_svdlg.ppm')
dark = c.get((16, 48, 16), 0)   # COLOR_BG del dialogo comdlg
check(f'dialogo visible en pantalla (x{dark})', dark > 10000)
hmp('sendkey ret', 0.4)
time.sleep(6)
snap('p4_svafter')
c = ppm_colors('/tmp/opencode/p4_svafter.ppm')
left = c.get((16, 48, 16), 0)
check(f'dialogo cerrado: pantalla restaurada sin arrastrar (x{left})',
      left < 500)
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

# ============ SESION B: explorer (MAX/restaurar/X) ============
qemu, acc, hmp, waitstr, snap, sendcmd = boot()
if not waitstr('Autoboot:', 25):
    print('FAIL - sin autoboot B'); qemu.terminate(); sys.exit(1)
hmp('sendkey x', 0.5)
if not waitstr('myos>', 20):
    print('FAIL - sin prompt B'); qemu.terminate(); sys.exit(1)
sendcmd('run explorer.exe')
if not waitstr('exp: items=', 15):
    print('FAIL - explorer'); qemu.terminate(); sys.exit(1)
time.sleep(2)
hmp('sendkey t', 0.5)
if not waitstr('exp: secuencia botones armada', 10):
    print('FAIL - hook t'); qemu.terminate(); sys.exit(1)
check('hook t arma la secuencia de botones', True)
hmp('sendkey m', 0.5)
if not waitstr('exp: lanzando metapad.exe', 25):
    print('FAIL - no lanzo metapad'); qemu.terminate(); sys.exit(1)
if not waitstr('exp: inj MAX', 30):
    print('FAIL - sin MAX'); qemu.terminate(); sys.exit(1)
check('clic MAX inyectado', True)
time.sleep(2)
snap('p4_max')
c = ppm_colors('/tmp/opencode/p4_max.ppm')
white = c.get((255, 255, 255), 0)
check(f'maximizado: cliente blanco cubre la pantalla (x{white})',
      white > 250000)
if not waitstr('exp: inj rest', 15):
    print('FAIL - sin rest'); qemu.terminate(); sys.exit(1)
time.sleep(2)
snap('p4_rest')
c = ppm_colors('/tmp/opencode/p4_rest.ppm')
white = c.get((255, 255, 255), 0)
check(f'restaurado: ventana a tamano original (x{white})',
      100000 < white < 300000)
# el X llega tras la pausa de 25 s (x_skip) y cierra metapad
if not waitstr('exp: inj X', 40):
    print('FAIL - sin X'); qemu.terminate(); sys.exit(1)
check('X periodico inyectado tras la pausa', True)
if not waitstr('exit:0', 30):
    print('FAIL - metapad no cerro'); qemu.terminate(); sys.exit(1)
check('X cierra metapad (exit:0)', True)
hmp('sendkey q', 0.5)
if not waitstr('exp: fin', 15):
    print('FAIL - explorer no cerro'); qemu.terminate(); sys.exit(1)
check('q cierra el explorador', True)
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

# ============ SESION C: desktop (MIN + restaurar) ============
qemu, acc, hmp, waitstr, snap, sendcmd = boot()
if not waitstr('Autoboot:', 25):
    print('FAIL - sin autoboot C'); qemu.terminate(); sys.exit(1)
time.sleep(6)   # autoboot: desktop
if not waitstr('esc: escritorio listo', 20):
    print('FAIL - desktop no arranco'); qemu.terminate(); sys.exit(1)
hmp('sendkey m', 0.5)   # hook: MIN antes del X
hmp('sendkey d', 0.5)   # hook: doble clic icono 1 = metapad
if not waitstr('esc: lanzando metapad', 30):
    print('FAIL - desktop no lanzo metapad'); qemu.terminate(); sys.exit(1)
if not waitstr('LoadAcceleratorsA', 40):
    print('FAIL - metapad (C)'); qemu.terminate(); sys.exit(1)
if not waitstr('esc: inj MIN', 30):
    print('FAIL - sin MIN'); qemu.terminate(); sys.exit(1)
check('MIN inyectado por el escritorio', True)
time.sleep(2)
snap('p4_bmin')
c = ppm_colors('/tmp/opencode/p4_bmin.ppm')
check('metapad minimizado (ventana oculta)', c.get((0, 136, 0), 0) < 200)
hmp('sendkey r', 0.5)   # hook: restaurar app 1 = METAPAD
if not waitstr('esc: restaurando ventana', 20):
    print('FAIL - no restauro'); qemu.terminate(); sys.exit(1)
check('restaura la ventana minimizada', True)
time.sleep(1)
snap('p4_bres')
c = ppm_colors('/tmp/opencode/p4_bres.ppm')
check('metapad visible de nuevo', c.get((0, 136, 0), 0) > 5000)
if not waitstr('esc: inj X', 20):
    print('FAIL - sin X'); qemu.terminate(); sys.exit(1)
if not waitstr('exit:0', 45):
    print('FAIL - metapad no cerro'); qemu.terminate(); sys.exit(1)
check('X cierra metapad', True)
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
sys.exit(0 if all(passed) else 1)