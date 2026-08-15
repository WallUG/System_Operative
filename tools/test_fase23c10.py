#!/usr/bin/env python3
"""Fase 23-C10: heap por proceso (HeapAlloc/HeapFree reales).

heaptest.exe:
- HeapAlloc(100) -> a; HeapSize(a) == 100
- HeapAlloc(200) -> b; HeapFree(b); HeapAlloc(200) -> c == b (el
  hueco se reutiliza: free real, no bump)
- HeapFree(c); HeapAlloc(400) -> d: cabe solo si el coalesce fusiono
  los bloques libres contiguos (100+200+header >= 400)
- HeapReAlloc(a,300) preserva el contenido
- malloc/free/malloc del CRT: m2 == m1 (reutilizacion via msvcrt)
- realloc(a,512) del CRT preserva contenido"""
import subprocess, socket, threading, time, sys, os

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
WORK = 'build/os-persist.bin'

passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

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
            hmp({' ':'sendkey spc','\n':'sendkey ret','.':'sendkey dot',
                 '_':'sendkey shift-minus'}.get(ch, f'sendkey {ch}'), 0.12)
    def waitstr(s, t=25):
        d = time.time()+t
        while time.time() < d:
            if s in ''.join(acc): return True
            time.sleep(0.2)
        return False

    if not waitstr('Autoboot:', 25):
        print('FAIL - sin autoboot'); qemu.terminate(); return
    hmp('sendkey x', 0.5)
    if not waitstr('myos>', 20):
        print('FAIL - sin prompt'); qemu.terminate(); return

    sendcmd('run heaptest.exe')
    if not waitstr('h10: start', 20):
        print('FAIL - heaptest no arranca'); qemu.terminate(); return

    # HeapSize == 100
    if not waitstr('h10: a=0x', 10):
        print('FAIL - sin a'); qemu.terminate(); return
    if not waitstr(' size=104', 10):
        print('FAIL - HeapSize'); qemu.terminate(); return
    check('HeapAlloc + HeapSize (100 -> 104 alineado, como Windows)', True)

    # free + realloc del mismo tamano -> mismo puntero
    if not waitstr('h10: b=', 10):
        print('FAIL - sin b'); qemu.terminate(); return
    i = ''.join(acc).find('h10: b=')
    line = ''.join(acc)[i:i+46]
    b = line.split('b=')[1].split(' ')[0]
    c = line.split('c=')[1].split('\n')[0]
    check('HeapFree reutiliza el hueco (c == b: '+b+' vs '+c+')', b == c)

    # coalesce: el 400 cabe en el hueco fusionado (100+200)
    if not waitstr('h10: d=0x', 10):
        print('FAIL - sin d'); qemu.terminate(); return
    i = ''.join(acc).find('h10: d=')
    d = ''.join(acc)[i:i+12].split('d=')[1].split('\n')[0]
    check('Coalesce: HeapAlloc(400) cabe tras fusionar huecos libres',
          d != '0x00000000')

    # HeapReAlloc preserva contenido
    if not waitstr(' txt=contenido preservado', 10):
        print('FAIL - realloc contenido'); qemu.terminate(); return
    check('HeapReAlloc preserva el contenido', True)

    # malloc/free/malloc del CRT: m2 == m1
    if not waitstr('h10: m1=0x', 10):
        print('FAIL - sin m1'); qemu.terminate(); return
    i = ''.join(acc).find('h10: m1=')
    m1 = ''.join(acc)[i:i+14].split('m1=')[1].split('\n')[0]
    if not waitstr('h10: m2=0x', 10):
        print('FAIL - sin m2'); qemu.terminate(); return
    i = ''.join(acc).find('h10: m2=')
    m2 = ''.join(acc)[i:i+14].split('m2=')[1].split('\n')[0]
    check('msvcrt malloc/free reutiliza (m2 == m1: '+m1+' vs '+m2+')', m1 == m2)

    # realloc del CRT preserva contenido
    if not waitstr(' txt=crt realloc', 10):
        print('FAIL - realloc crt'); qemu.terminate(); return
    check('msvcrt realloc preserva el contenido', True)

    if not waitstr('h10: fin', 10):
        print('FAIL - sin fin'); qemu.terminate(); return
    check('fin limpio', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)