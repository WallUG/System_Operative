#!/usr/bin/env python3
"""Fase 24-P1.4: kernel32 de arranque.

bootpaths.exe verifica en serial:
- GetTempPathA -> C:\\TEMP
- GetWindowsDirectoryA -> C:\\WINDOWS
- GetSystemDirectoryA -> C:\\WINDOWS\\System32
- GetVersionExA -> 6.1
- GetFileInformationByHandle(readme.txt) -> size>0
- fin ok=1 (exit 0)."""
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

    sendcmd('run bootpaths.exe')
    if not waitstr('boot: temp=C:\\TEMP', 20):
        print('FAIL - GetTempPathA'); qemu.terminate(); return
    check('GetTempPathA -> C:\\TEMP', True)
    if not waitstr('boot: win=C:\\WINDOWS', 10):
        print('FAIL - GetWindowsDirectoryA'); qemu.terminate(); return
    check('GetWindowsDirectoryA -> C:\\WINDOWS', True)
    if not waitstr('boot: sys=C:\\WINDOWS\\System32', 10):
        print('FAIL - GetSystemDirectoryA'); qemu.terminate(); return
    check('GetSystemDirectoryA -> C:\\WINDOWS\\System32', True)
    if not waitstr('boot: ver=6.1', 10):
        print('FAIL - GetVersionExA'); qemu.terminate(); return
    check('GetVersionExA -> 6.1', True)
    if not waitstr('boot: size=', 10):
        print('FAIL - GetFileInformationByHandle'); qemu.terminate(); return
    check('GetFileInformationByHandle -> size>0', True)
    if not waitstr('boot: fin ok=1', 10):
        print('FAIL - fin != 1'); qemu.terminate(); return
    if not waitstr('exit:0', 10):
        print('FAIL - exit != 0'); qemu.terminate(); return
    check('bootpaths termina con exit 0', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)