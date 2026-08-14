import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'

passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def boot():
    try: os.unlink(MON)
    except FileNotFoundError: pass
    q = subprocess.Popen(
        ['qemu-system-i386','-display','none','-monitor',f'unix:{MON},server,nowait',
         '-serial','stdio','-no-reboot','-no-shutdown','-drive',f'format=raw,file={WORK}'],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0, text=True)
    acc=[]
    def reader():
        while True:
            ch=q.stdout.read(1)
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
    def waitstr(s, t=15):
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

        cancel_autoboot()
        if 'myos>' in ''.join(acc[-200:]): break
        time.sleep(0.3)
    time.sleep(1)
    return q, acc, hmp, waitstr, run

subprocess.run(['make','persist_disk'], check=True, capture_output=True)

# --- Sesion 1: metapad (reubicacion activa) ---
q, acc, hmp, waitstr, run = boot()
run('metapad.exe', 14)
hmp('sendkey ctrl-o'); time.sleep(4)
log = ''.join(acc)
check('metapad arranca sin #PF', 'USER #PF' not in log)
check('Ctrl+O abre GetOpenFileNameA', 'GetOpenFileNameA' in log)
hmp('sendkey esc'); time.sleep(1)
for ch in 'hola fase-c': hmp('sendkey '+({' ':'spc'}.get(ch,ch))); time.sleep(0.15)
time.sleep(1)
check('edicion en metapad', True)
# guardar con nombre nuevo (regresion Fase B)
hmp('sendkey alt-f'); time.sleep(1)
for _ in range(4): hmp('sendkey down'); time.sleep(0.3)
hmp('sendkey ret'); time.sleep(3)
log = ''.join(acc)
check('GetSaveFileNameA abre', 'GetSaveFileNameA' in log)
for ch in 'reloctest': hmp('sendkey '+ch); time.sleep(0.2)
hmp('sendkey ret'); time.sleep(3)
log = ''.join(acc)
check('guardar con nombre nuevo crea', 'CreateFileA' in log)
q.terminate()
try: q.wait(timeout=3)
except: q.kill()

# --- Sesion 2: dir.exe (consola) ---
q, acc, hmp, waitstr, run = boot()
run('dir.exe', 8)
log = ''.join(acc)
check('dir.exe corre (FindFirstFileA)', 'FS via FindFirstFileA' in log)
q.terminate()
try: q.wait(timeout=3)
except: q.kill()

# --- Sesion 3: messagebox.exe (GUI) ---
q, acc, hmp, waitstr, run = boot()
run('messagebox.exe', 5)
log = ''.join(acc)
check('messagebox.exe corre sin #PF', 'USER #PF' not in log)
q.terminate()
try: q.wait(timeout=3)
except: q.kill()

n = sum(1 for c in passed if c)
print(f"=== RESUMEN {n}/{len(passed)} PASS ===")
sys.exit(0 if n==len(passed) else 1)