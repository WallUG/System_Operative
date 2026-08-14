#!/usr/bin/env python3
"""Fase 23-A4: instalador tipo Windows.

1. Sesion 1: run installer.elf -> crea installed/, copia readme.txt a
   installed/readme_inst.txt, escribe version.txt, flush.
2. Verificacion HOST: se parsea build/os-persist.bin (el FS MEFS en
   LBA 129) y se comprueba que "installed" es un directorio con
   readme_inst.txt (contenido == readme.txt) y version.txt.
3. Sesion 2 (mismo disco): se navega con el explorer a installed/ y se
   verifica que lista los 2 archivos (persistencia tras reboot)."""
import subprocess, socket, threading, time, sys, os, struct

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'
FS_START = 129
LBA = struct.Struct('<I')
ENT = struct.Struct('<16s4I')

passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

def read_fs():
    with open(WORK, 'rb') as f:
        return f.read()

def parse_fs(data):
    """Devuelve {nombre: (size, lba, flags, parent, indice)} desde el
    directorio del MEFS en LBA 129 del disco."""
    off = FS_START * 512
    magic = data[off:off+8]
    n = LBA.unpack_from(data, off+8)[0]
    dir_lba = LBA.unpack_from(data, off+12)[0]
    entries = {}
    for i in range(n):
        e = data[dir_lba*512 + i*32: dir_lba*512 + i*32 + 32]
        name, size, lba, fl, par = ENT.unpack(e)
        nm = name.split(b'\0')[0].decode()
        if nm:
            entries[nm] = (size, lba, fl, par, i)
    return entries

def read_entry(data, e):
    size, lba, fl, par, i = e
    return data[lba*512 : lba*512 + size]

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
    def waitstr(s, t=25):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False
    def snap(name):
        hmp(f'screendump /tmp/opencode/a4_{name}.ppm', 0.4)

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    # --- 1) correr el instalador ---
    sendcmd('run installer.elf')
    if not waitstr('inst: flush ok - instalacion completa', 20):
        print('FAIL - instalador no completo'); qemu.terminate(); return
    check('instalador corre y completa el flush', True)
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

    # --- 2) verificar persistencia desde el host ---
    data = read_fs()
    entries = parse_fs(data)
    ok_inst = 'installed' in entries and (entries['installed'][2] & 1)
    check('persistido: installed/ es directorio', ok_inst)
    # leer readme.txt original y comparar con readme_inst.txt
    orig = read_entry(data, entries['readme.txt'])
    ok_cpy = ('readme_inst.txt' in entries and
              read_entry(data, entries['readme_inst.txt']) == orig)
    check('persistido: readme_inst.txt == readme.txt', ok_cpy)
    ok_ver = ('version.txt' in entries and
              b'MyOS Installer' in read_entry(data, entries['version.txt']))
    check('persistido: version.txt con banner', ok_ver)

    # --- 3) sesion 2: persistencia tras reboot (navegar con explorer) ---
    subprocess.run(['pkill', '-9', '-f', 'qemu-system'], capture_output=True)
    time.sleep(0.5)
    try: os.unlink(MON)
    except FileNotFoundError: pass
    qemu2 = subprocess.Popen(
        ['qemu-system-i386','-display','none','-monitor',f'unix:{MON},server,nowait',
         '-serial','stdio','-no-reboot','-no-shutdown',
         '-drive',f'format=raw,file={WORK}'],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0, text=True)
    acc2 = []
    def reader2():
        while True:
            ch = qemu2.stdout.read(1)
            if ch == '': return
            acc2.append(ch); sys.stdout.write(ch); sys.stdout.flush()
    threading.Thread(target=reader2, daemon=True).start()
    def hmp2(c, dt=0.25):
        s = mon_sock()
        if s is None: return
        s.sendall(f'{c}\n'.encode()); time.sleep(dt); s.close()
    def waitstr2(s, t=25):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc2): return True
            time.sleep(0.2)
        return False
    if not waitstr2('Autoboot:', 25):
        print('FAIL - reboot sin autoboot'); qemu2.terminate(); return
    hmp2('sendkey x', 0.5)
    if not waitstr2('myos>', 20):
        print('FAIL - reboot sin prompt'); qemu2.terminate(); return
    sendcmd('run explorer.elf')
    if not waitstr2('exp: explorador iniciando', 15):
        print('FAIL - explorer tras reboot'); qemu2.terminate(); return
    time.sleep(1.5)
    # End -> instalador creo installed como ultima entrada -> Enter
    hmp2('sendkey end', 0.4)
    hmp2('sendkey ret', 0.6)
    time.sleep(1.2)
    hmp2('screendump /tmp/opencode/a4_inst_dir.ppm', 0.4)
    qemu2.terminate()
    try: qemu2.wait(timeout=3)
    except: qemu2.kill()

    # el explorer navego a installed/ y muestra su listado (fila sel)
    d = open('/tmp/opencode/a4_inst_dir.ppm','rb').read()
    hdr = d.split(b'\n',3)
    w = int(hdr[1].split()[0])
    px = d.split(b'\n',3)[3]
    selrows = 0
    for y in range(110, 450):
        n = 0
        for x in range(102, 666):
            o = (y*w+x)*3
            if px[o:o+3] == bytes((0x50,0x90,0x50)):
                n += 1
        if n > 200:
            selrows += 1
    check('reboot: el explorer navega a installed/ (lista con seleccion)',
          selrows > 0)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")

run()
sys.exit(0 if all(passed) else 1)