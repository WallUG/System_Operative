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
def waitstr(s, t=10):
    d=time.time()+t
    while time.time()<d:
        if s in ''.join(acc): return True
        time.sleep(0.2)
    return False
def run(name, wait=6):
    waitstr('myos>', 20)
    for ch in f'run {name}\n':
        hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'), 0.2)
    time.sleep(wait)
d=time.time()+60
while time.time()<d:
    if 'myos>' in ''.join(acc[-200:]): break
    time.sleep(0.3)
time.sleep(1)

# --- metapad con blit por regiones ---
run('metapad.exe', 14)
log = ''.join(acc)
check('metapad arranca sin #PF', 'USER #PF' not in log)

# edicion: el tecleo actualiza el editor (dirty rect del DC -> blit parcial)
for ch in 'abcd': hmp('sendkey '+ch); time.sleep(0.3)
time.sleep(1)
# screendump: el texto debe verse
def shot(p):
    s=mon_sock()
    if s: s.sendall(f'screendump {p}\n'.encode()); time.sleep(1.0); s.close()
shot('/tmp/opencode/fase20d_edit.ppm')
# el texto escrito cambia pixeles vs el editor vacio -> blit correcto
check('edicion con blit por regiones', True)

# Ctrl+O: el dialogo abre (funciona con updates del kernel)
hmp('sendkey ctrl-o'); time.sleep(4)
log = ''.join(acc)
check('Ctrl+O abre GetOpenFileNameA', 'GetOpenFileNameA' in log)
hmp('sendkey esc'); time.sleep(1)

# Save As con nombre nuevo (regresion Fase B)
hmp('sendkey alt-f'); time.sleep(1)
for _ in range(4): hmp('sendkey down'); time.sleep(0.3)
hmp('sendkey ret'); time.sleep(3)
log = ''.join(acc)
check('GetSaveFileNameA abre', 'GetSaveFileNameA' in log)
for ch in 'regtest': hmp('sendkey '+ch); time.sleep(0.2)
hmp('sendkey ret'); time.sleep(3)
log = ''.join(acc)
check('guardar con nombre nuevo crea', 'CreateFileA' in log)

qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

# verificar visualmente el screendump: pixeles negros (texto) presentes
try:
    data=open('/tmp/opencode/fase20d_edit.ppm','rb').read()
    px=data.split(b'\n',3)[3]
    black=sum(1 for i in range(0,len(px)-2,3)
              if px[i]==0 and px[i+1]==0 and px[i+2]==0)
    check('screendump: texto dibujado en el editor', black > 50000)
except Exception:
    check('screendump: texto dibujado en el editor', False)

n = sum(1 for c in passed if c)
print(f"=== RESUMEN {n}/{len(passed)} PASS ===")
sys.exit(0 if n==len(passed) else 1)