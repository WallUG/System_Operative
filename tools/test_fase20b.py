import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'

passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

# --- Sesion 1: crear un archivo 'abc' (sin punto) en el disco ---
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
def sendcmd(cmd):
    for ch in cmd+'\n':
        hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'), 0.15)

def cancel_autoboot():
    """Fase 22: si hay autoboot, lo cancela con una tecla (se descarta)."""
    d = time.time()+8
    while time.time() < d:
        if 'Autoboot:' in ''.join(acc): break
        time.sleep(0.2)
    if 'Autoboot:' in ''.join(acc):
        hmp('sendkey x', 0.3)
        d = time.time()+4
        while time.time() < d:
            if 'Autoboot cancelado' in ''.join(acc): break
            time.sleep(0.2)
def waitstr(s, t=8):
    d=time.time()+t
    while time.time()<d:
        if s in ''.join(acc): return True
        time.sleep(0.2)
    return False
d=time.time()+60
while time.time()<d:

    cancel_autoboot()
    if 'myos>' in ''.join(acc[-200:]): break
    time.sleep(0.3)
time.sleep(1)
sendcmd('write abc'); waitstr('escrito')
sendcmd('flush'); waitstr('flusheado')
log1=''.join(acc)
check('creado archivo abc', 'escrito' in log1)
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

# --- Sesion 2: metapad Guardar Como con 'abc' (existe) -> confirmacion ---
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
def waitstr(s, t=8):
    d=time.time()+t
    while time.time()<d:
        if s in ''.join(acc): return True
        time.sleep(0.2)
    return False
d=time.time()+60
while time.time()<d:
    if 'myos>' in ''.join(acc[-200:]): break
    time.sleep(0.3)
time.sleep(1)
for ch in 'run metapad.exe\n':
    hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'))
time.sleep(12)
# Guardar Como (menu File, item Save As = down x4)
hmp('sendkey alt-f'); time.sleep(1)
for _ in range(4): hmp('sendkey down'); time.sleep(0.3)
hmp('sendkey ret'); time.sleep(3)   # GetSaveFileNameA
time.sleep(2)
# el dialogo Save As muestra el nombre inicial; seleccionamos 'abc' tipeando
for ch in 'abc': hmp('sendkey '+ch); time.sleep(0.2)
time.sleep(1)
# Enter -> 'abc' existe -> debe mostrar confirmacion, NO CreateFileA aun
hmp('sendkey ret'); time.sleep(2)
logA = ''.join(acc)
check('Enter con archivo existente NO crea aun', '[k32] CreateFileA' not in logA)
def shot(p):
    s=mon_sock()
    if s: s.sendall(f'screendump {p}\n'.encode()); time.sleep(1.0); s.close()
shot('/tmp/opencode/confirm_abc.ppm'); time.sleep(1)
# responder No (n) -> no debe crear
hmp('sendkey n'); time.sleep(2)
logB = ''.join(acc)
check('Confirmacion + No no crea archivo', '[k32] CreateFileA' not in logB)
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

n = sum(1 for c in passed if c)
print(f"=== RESUMEN {n}/{len(passed)} PASS ===")
sys.exit(0 if n==len(passed) else 1)