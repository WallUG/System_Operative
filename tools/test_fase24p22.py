#!/usr/bin/env python3
"""Fase 24-P2.2: CreateThread con scheduler preemptivo.

thrtest.exe crea un hilo que incrementa un contador global 1000000
veces y termina con ExitThread; el hilo principal hace busy-wait hasta
que el contador llega al objetivo. Si el timer no preemptara al hilo
principal y programara el hilo, el busy-wait no retornaria jamas.
Esperamos en serial: "thr: CreateThread handle=", "thr: hilo termina,
counter=1000000", "thr: main ve counter=1000000 ran=1", "thr: fin ok=1",
exit 0."""
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

    sendcmd('run thrtest.exe')
    if not waitstr('thr: start', 20):
        print('FAIL - thrtest no arranca'); qemu.terminate(); return
    if not waitstr('thr: CreateThread handle=', 10):
        print('FAIL - CreateThread no devolvio handle'); qemu.terminate(); return
    check('CreateThread devuelve handle + tid', True)
    if not waitstr('thr: hilo termina, counter=1000000', 20):
        print('FAIL - el hilo no termino (no preempto el timer)')
        print('=== SERIAL ==='); print(''.join(acc)[-2000:])
        qemu.terminate(); return
    check('hilo corre preemptido y llega al objetivo', True)
    if not waitstr('thr: main ve counter=1000000 ran=1', 10):
        print('FAIL - main no ve el contador'); qemu.terminate(); return
    check('main ve el contador compartido completado', True)
    if not waitstr('thr: fin ok=1', 10):
        print('FAIL - fin != 1'); qemu.terminate(); return
    if not waitstr('exit:0', 10):
        print('FAIL - exit != 0'); qemu.terminate(); return
    check('thrtest termina con exit 0 (proceso + hilo limpios)', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)