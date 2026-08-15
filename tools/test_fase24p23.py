#!/usr/bin/env python3
"""Fase 24-P2.3: GDI blits (BitBlt/StretchBlt) + clipboard.

blitclip.exe (gdi32/user32) verifica en serial:
- GetPixel(10,10)=0x000000FF (rojo) tras Rectangle.
- BitBlt de un memoria DC verde al cliente -> GetPixel(100,100)=0x0000FF00.
- StretchBlt 10x10 rojo a 40x40 -> GetPixel(200,200)=0x000000FF.
- Clipboard: SetClipboardData/GetClipboardData round-trip ('hola clipboard').
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

    sendcmd('run blitclip.exe')
    if not waitstr('blit: start', 20):
        print('FAIL - blitclip no arranca'); qemu.terminate(); return
    if not waitstr('blit: px(20,20)=0x00FF0000', 10):
        print('FAIL - GetPixel/rojo interior'); print('=== SERIAL ==='); print(''.join(acc)[-1200:]); qemu.terminate(); return
    check('GetPixel lee el interior rojo (px_disp 0x0000FF00)', True)
    if not waitstr('blit: px(100,100)=0x0000FF00', 10):
        print('FAIL - BitBlt del mem DC verde'); qemu.terminate(); return
    check('BitBlt (mem DC verde -> cliente) con GetPixel=verde', True)
    if not waitstr('blit: px(200,200)=0x00FF0000', 10):
        print('FAIL - StretchBlt'); qemu.terminate(); return
    check('StretchBlt (10x10 -> 40x40) rojo', True)
    if not waitstr('blit: clip disponible', 10):
        print('FAIL - IsClipboardFormatAvailable'); qemu.terminate(); return
    check('clipboard: IsClipboardFormatAvailable(CF_TEXT)', True)
    if not waitstr("blit: clip texto='hola clipboard'", 10):
        print('FAIL - GetClipboardData'); print('=== SERIAL ==='); print(''.join(acc)[-800:]); qemu.terminate(); return
    check('clipboard: Set/GetClipboardData round-trip', True)
    if not waitstr('blit: fin ok=1', 10):
        print('FAIL - fin != 1'); print('=== SERIAL ==='); print(''.join(acc)[-900:]); qemu.terminate(); return
    if not waitstr('exit:0', 10):
        print('FAIL - exit != 0'); qemu.terminate(); return
    check('blitclip termina con exit 0', True)

    print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
    qemu.terminate()
    try: qemu.wait(timeout=3)
    except: qemu.kill()

run()
sys.exit(0 if all(passed) else 1)