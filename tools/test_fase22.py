#!/usr/bin/env python3
# MyOS - tools/test_fase22.py
# Fase 22: arranque tipo Windows.
# 1. Barra de carga visible en un screendump temprano del boot.
# 2. Autoboot: sin teclas, el escritorio se lanza solo (shell por debajo).
# 3. 'q' cierra el desktop y vuelve el prompt de la shell.
# 4. bootgui off + flush desactiva el autoboot en el siguiente boot.
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'

passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

subprocess.run(['make','persist_disk'], check=True, capture_output=True)
try: os.unlink(MON)
except FileNotFoundError: pass

qemu = subprocess.Popen(
    ['qemu-system-i386','-display','none','-monitor',f'unix:{MON},server,nowait',
     '-serial','stdio','-no-reboot','-no-shutdown','-drive',f'format=raw,file={WORK}'],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0, text=True)
acc=[]
def reader():
    while True:
        ch=qemu.stdout.read(1)
        if ch=='': return
        acc.append(ch); sys.stdout.write(ch); sys.stdout.flush()
threading.Thread(target=reader, daemon=True).start()
def mon_sock():
    for _ in range(150):
        try:
            s=socket.socket(socket.AF_UNIX); s.settimeout(10); s.connect(MON); s.recv(4096); return s
        except OSError: time.sleep(0.1)
    return None
def hmp(c, dt=0.3):
    s=mon_sock()
    if s is None: return
    s.sendall(f'{c}\n'.encode()); time.sleep(dt); s.close()
def waitstr(s, t=15):
    d=time.time()+t
    while time.time()<d:
        if s in ''.join(acc): return True
        time.sleep(0.2)
    return False

# --- 1) barra de carga: screendump temprano (durante el boot) ---
time.sleep(0.3)
hmp('screendump /tmp/opencode/f22_bar.ppm', 0.8)
print("barra capturada", flush=True)

# --- 2) autoboot: no tocar teclas; el escritorio debe arrancar solo ---
ok = waitstr('esc: escritorio listo', 20)
check('autoboot lanza el escritorio solo', ok)
hmp('screendump /tmp/opencode/f22_desk.ppm', 0.8)

# --- 3) 'q' cierra el desktop y vuelve la shell ---
hmp('sendkey q', 0.5)
ok = waitstr('esc: fin del escritorio', 6)
check('q cierra el escritorio', ok)
ok = waitstr('myos>', 8)
check('la shell sigue viva por debajo', ok)

# --- 4) bootgui off + flush -> siguiente boot sin autoboot ---
def sendline(s):
    for ch in s:
        hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'), 0.2)
waitstr('esc: fin del escritorio', 6)
time.sleep(2.5)                  # espera a que la shell vuelva al read_line
sendline('bootgui off\n'); time.sleep(1)
sendline('flush\n'); time.sleep(2)
check('bootgui off persistido', 'OFF' in ''.join(acc[-400:]))

qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

# --- reboot con bootgui off ---
try: os.unlink(MON)
except FileNotFoundError: pass
qemu = subprocess.Popen(
    ['qemu-system-i386','-display','none','-monitor',f'unix:{MON},server,nowait',
     '-serial','stdio','-no-reboot','-no-shutdown','-drive',f'format=raw,file={WORK}'],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0, text=True)
acc=[]
threading.Thread(target=reader, daemon=True).start()
ok = waitstr('myos>', 20)
time.sleep(4)
check('sin autoboot: prompt directo', ok and 'Autoboot:' not in ''.join(acc))
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

# --- 5) verificar la barra en el ppm ---
def ppm_color_count(path, rgb):
    try:
        data=open(path,'rb').read()
    except FileNotFoundError:
        return 0
    px=data.split(b'\n',3)[3]
    w=800
    c=0
    for y in range(0,600,4):
        base=y*w*3
        for x in range(0,800,4):
            i=base+x*3
            if px[i]==rgb[0] and px[i+1]==rgb[1] and px[i+2]==rgb[2]:
                c+=1
    return c

azul = ppm_color_count('/tmp/opencode/f22_bar.ppm', (0x20,0x4A,0x80))
verde = ppm_color_count('/tmp/opencode/f22_bar.ppm', (0x30,0xB0,0x00))
check('barra de carga visible (fondo azul)', azul > 10000)
check('barra de carga con progreso verde', verde > 300)

print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")