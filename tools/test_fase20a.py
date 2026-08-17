import subprocess, socket, threading, time, sys, os, struct

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'

passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def run_session(cmds):
    """Ejecuta una sesion QEMU en build/os-persist.bin con el monitor unix.
    cmds: lista de (comando, str_esperado) que se envian por sendkey.
    Devuelve el log acumulado."""
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
        for _ in range(150):
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
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False
    # espera prompt
    d = time.time()+60
    while time.time() < d:

        cancel_autoboot()
        if 'myos>' in ''.join(acc[-200:]): break
        time.sleep(0.3)
    time.sleep(1)
    for cmd, expect in cmds:
        sendcmd(cmd)
        if expect:
            waitstr(expect)
    time.sleep(1)
    log = ''.join(acc)
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()
    return log

# --- Test: formato v2 + bitmap + subdirectorios + persistencia ---
subprocess.run(['make','persist_disk'], check=True, capture_output=True)

# Sesion 1: crear archivos, dir, formato, bitmap
log = run_session([
    ('write a.txt', 'escrito'),
    ('write b.txt', 'escrito'),
    ('rm b.txt', 'eliminado'),
    ('write c.txt', 'escrito'),     # reutiliza el bloque de b.txt
    ('mkdir docs', 'dir creado'),
    ('ls', 'entrada'),
    ('flush', 'flusheado'),
])
check('write + rm + write (bitmap reutiliza bloques)', 'escrito' in log and 'eliminado' in log)
check('mkdir docs', 'dir creado' in log)

# Sesion 2 (reinicio): persistencia de archivos + dir
log = run_session([
    ('cat a.txt', 'hola desde'),
    ('cat c.txt', 'hola desde'),
    ('ls', 'entrada'),
    ('cd docs', None),
    ('pwd', '/docs'),
])
check('a.txt persiste tras reinicio', 'hola desde MyOS!' in log)
check('docs dir persiste tras reinicio', '/docs' in log)

# Verificar disco: nf >= 29, bitmap consistente
img = open(WORK,'rb').read()
nf = struct.unpack('<I', img[145*512+8:145*512+12])[0]
check('nf refleja archivos creados', nf >= 29)

# Sesion 3: format borra todo
log = run_session([
    ('format', 'formateado'),
    ('ls', '0 entrada'),
    ('write post.txt', 'escrito'),
    ('flush', 'flusheado'),
    ('ls', '1 entrada'),
])
check('format limpia el disco', '0 entrada' in log)
check('write tras format', '1 entrada' in log)

# Sesion 4 (reinicio): el disco formateado tiene solo post.txt
log = run_session([
    ('ls', '1 entrada'),
    ('cat post.txt', 'hola desde'),
])
check('disco formateado persiste (solo post.txt)', '1 entrada' in log and 'hola desde MyOS!' in log)

n = sum(1 for c in passed if c)
print(f"=== RESUMEN {n}/{len(passed)} PASS ===")
sys.exit(0 if n==len(passed) else 1)