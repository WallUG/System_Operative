import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = os.path.join(os.getcwd(), 'persist_test.bin')
BASE = os.path.join(os.getcwd(), 'persist_base.bin')

# regenera el disco de trabajo limpio desde os-image.bin (sin relleno:
# el disco raw es el propio os-image, con ~190 sectores libres de margen)
img = open('os-image.bin','rb').read()
open(BASE,'wb').write(img)
open(WORK,'wb').write(img)

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
    for _ in range(120):
        try:
            s = socket.socket(socket.AF_UNIX); s.settimeout(10)
            s.connect(MON); s.recv(4096); return s
        except OSError: time.sleep(0.1)
    return None

def hmp(c, dt=0.35):
    s = mon_sock()
    if s is None: return
    s.sendall(f'{c}\n'.encode()); time.sleep(dt); s.close()

def wait_prompt(timeout=120):
    d = time.time()+timeout
    while time.time() < d:
        if 'myos>' in ''.join(acc[-200:]): return True
        time.sleep(0.3)
    return False

passed = []
def check(name, cond):
    passed.append((name, cond))
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

if not wait_prompt():
    print("NO PROMPT")
    time.sleep(3)   # deja que el arranque termine igualmente
time.sleep(1)

# 1) shell: write + flush (enviadas como sendkey al teclado, que la
#    shell idle lee de PS/2)
print("== shell write ==", flush=True)
for ch in 'write nuevo.txt\n':
    hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'), 0.15)
time.sleep(2)
for ch in 'flush\n':
    hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'), 0.15)
time.sleep(2)
check('shell write/flush sin error', 'escrito' in ''.join(acc))

# 2) writetest.exe escribe saved.txt
print("== writetest.exe ==", flush=True)
for ch in 'run writetest.exe\n':
    hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'))
time.sleep(4)
check('writetest escribe saved.txt', 'saved.txt' in ''.join(acc) or 'escribi' in ''.join(acc))

# 3) metapad: editar + Guardar Como
print("== metapad Guardar Como ==", flush=True)
for ch in 'run metapad.exe\n':
    hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot'}.get(ch, f'sendkey {ch}'))
time.sleep(12)
for ch in 'hola':
    hmp('sendkey '+ch)
time.sleep(1)
hmp('sendkey alt-f'); time.sleep(1)
for _ in range(4): hmp('sendkey down'); time.sleep(0.3)
hmp('sendkey ret'); time.sleep(3)          # Save As
check('metapad abre GetSaveFileNameA', '[cdlg] GetSaveFileNameA dialog' in ''.join(acc))
for ch in 'miapp.txt': hmp('sendkey '+ch)
time.sleep(1)
hmp('sendkey ret'); time.sleep(4)          # guardar
check('metapad llama CreateFileA', '[k32] CreateFileA' in ''.join(acc))
time.sleep(1)

qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

# 4) verificar persistencia física en el disco de trabajo
import struct
fs = open(WORK,'rb').read()[129*512:]
nf = struct.unpack('<I', fs[8:12])[0]
found = [None, None]
for k in range(nf):
    e = fs[512+k*32:512+(k+1)*32]
    name = e[:16].split(b'\0')[0]
    size, lba, _, _ = struct.unpack('<IIII', e[16:32])
    if size > 0:
        if name == b'nuevo.txt': found[0] = (size, lba)
        if name == b'saved.txt': found[1] = (size, lba)
check('nuevo.txt persistio en disco', found[0] is not None)
check('saved.txt persistio en disco', found[1] is not None)

n = sum(1 for _,c in passed if c)
print(f"=== RESUMEN {n}/{len(passed)} PASS ===")
sys.exit(0 if n==len(passed) else 1)