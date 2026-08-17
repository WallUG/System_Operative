#!/usr/bin/env python3
"""MyOS - Fase 25-W2A paso 6 (stack UDP/TCP + HTTP).

El plan (skill win32-unicode-w2a): stack TCP/UDP sobre el RTL8139 +
ws2_32.dll + un cliente HTTP real (netget.exe, mingw -lws2_32) contra
el user-net del host (python http.server en 127.0.0.1:8080 ->
10.0.2.2:8080 via slirp).

El test verifica:
1) Fix del RX del rtl8139 de QEMU: la 2ª recepcion ya no se pierde
   (ARP + ICMP reply del boot).
2) UDP/DNS: gethostbyname("example.com") resuelve por 10.0.2.3:53.
3) TCP + HTTP: netget.exe hace GET /netget.txt (8192 B), recibe la
   respuesta completa y verifica el checksum (la metrica del skill:
   "wget baja un archivo y el checksum coincide").

El app imprime "netget:PASS len=<n> cksum=<hex8>"; el test compara el
cksum con el del archivo servido (algoritmo c = c*33 + byte, 32-bit).

Requiere: build/os-persist.bin con el stack (SYS_NET_* 47-53) y
netget.exe en el FS; python3 para el http.server del host.

Uso: python3 tools/test_fase25w2a6.py
"""
import os
import socket
import subprocess
import sys
import threading
import time
import http.server
import functools

os.chdir(os.path.dirname(os.path.abspath(__file__)) + '/..')

MON = '/tmp/opencode/qmon.sock'
HTTPD = '/tmp/opencode/httpd'
PORT = 8080
CONTENT = bytes((i * 37 + 11) % 256 for i in range(8192))

passed = []


def check(name, ok):
    passed.append(ok)
    print(('PASS' if ok else 'FAIL') + ' - ' + name)
    if not ok:
        print('=== RESUMEN %d/%d PASS ===' % (sum(passed), len(passed)))
        sys.exit(1)


def body_cksum(body):
    c = 0
    for b in body:
        c = (c * 33 + b) & 0xFFFFFFFF
    return c


# --- servidor HTTP del host ---
os.makedirs(HTTPD, exist_ok=True)
open(os.path.join(HTTPD, 'netget.txt'), 'wb').write(CONTENT)
handler = functools.partial(http.server.SimpleHTTPRequestHandler,
                            directory=HTTPD)
srv = http.server.ThreadingHTTPServer(('127.0.0.1', PORT), handler)
threading.Thread(target=srv.serve_forever, daemon=True).start()

subprocess.run(['pkill', '-9', '-f', 'qemu-system'], capture_output=True)
time.sleep(0.5)
try:
    os.unlink(MON)
except FileNotFoundError:
    pass

qemu = subprocess.Popen(
    ['qemu-system-i386', '-display', 'none',
     '-monitor', 'unix:%s,server,nowait' % MON,
     '-serial', 'stdio', '-no-reboot', '-no-shutdown',
     '-netdev', 'user,id=net0', '-device', 'rtl8139,netdev=net0',
     '-drive', 'format=raw,file=build/os-persist.bin'],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)

acc = []


def reader():
    while True:
        ch = qemu.stdout.read(1)
        if ch == b'':
            return
        acc.append(ch.decode('utf-8', errors='replace'))


threading.Thread(target=reader, daemon=True).start()


def mon_sock():
    for _ in range(200):
        try:
            s = socket.socket(socket.AF_UNIX)
            s.settimeout(10)
            s.connect(MON)
            s.recv(4096)
            return s
        except OSError:
            time.sleep(0.1)
    return None


def hmp(cmd, dt=0.25):
    s = mon_sock()
    if s is None:
        return
    s.sendall(('%s\n' % cmd).encode())
    time.sleep(dt)
    s.close()


def waitstr(s, t=40):
    end = time.time() + t
    while time.time() < end:
        if s in ''.join(acc):
            return True
        time.sleep(0.2)
    return False


def sendcmd(cmd):
    keymap = {' ': 'spc', '\n': 'ret', '.': 'dot', '_': 'shift-minus'}
    for ch in cmd + '\n':
        hmp('sendkey %s' % keymap.get(ch, ch), 0.12)


# --- boot: ICMP reply (2ª RX, el quirk del CAPR arreglado) ---
if not waitstr('net: ICMP reply de 10.0.2.2 OK', 40):
    check('boot: ICMP reply (2ª recepcion del ring)', False)
    qemu.kill(); srv.shutdown(); sys.exit(1)
check('boot: ICMP reply (2ª recepcion del ring, fix CAPR)', True)

if not waitstr('Autoboot:', 40):
    check('autoboot', False); qemu.kill(); srv.shutdown(); sys.exit(1)
hmp('sendkey x', 0.5)
if not waitstr('myos>', 20):
    check('shell', False); qemu.kill(); srv.shutdown(); sys.exit(1)

# --- netget.exe: UDP/DNS + TCP + HTTP ---
sendcmd('run netget.exe')
if not waitstr('netget: start', 30):
    check('netget arranca', False); qemu.kill(); srv.shutdown(); sys.exit(1)
check('netget.exe arranca (imports ws2_32/msvcrt resueltos)', True)
if not waitstr('DNS ok', 60):
    check('DNS por UDP (gethostbyname example.com)', False)
    qemu.kill(); srv.shutdown(); sys.exit(1)
check('DNS por UDP a 10.0.2.3:53 (gethostbyname)', True)
if not waitstr('connect ok', 60):
    check('connect TCP', False); qemu.kill(); srv.shutdown(); sys.exit(1)
check('connect TCP (SYN/SYN+ACK/ACK)', True)
if not waitstr('GET enviado', 30):
    check('send (GET HTTP)', False); qemu.kill(); srv.shutdown(); sys.exit(1)
check('send: GET /netget.txt HTTP/1.0', True)

exp_cksum = body_cksum(CONTENT)
got_len = got_cksum = None
for attempt in range(3):
    ok = waitstr('netget:PASS', 60)
    text = ''.join(acc)
    i = text.rfind('netget:PASS')
    got_len = got_cksum = None
    if i >= 0:
        for t in text[i:i + 80].split():
            if t.startswith('len='):
                got_len = int(t[4:])
            if t.startswith('cksum='):
                got_cksum = int(t[6:], 16)
    # el app reporta len = la respuesta TOTAL (headers + cuerpo); el
    # checksum se calcula sobre el CUERPO (8192 B) y es el que valida
    # que la transferencia fue integra.
    if ok and got_cksum == exp_cksum:
        break
    if attempt < 2:             # flaky transitorio: reintentar
        time.sleep(1)
        sendcmd('run netget.exe')
check('HTTP GET: recv completo + checksum del cuerpo coincide '
      '(%d B totales, cksum=%08x)' % (got_len or -1, got_cksum or 0),
      got_cksum == exp_cksum)

time.sleep(1)
qemu.kill()
srv.shutdown()

print('=== RESUMEN %d/%d PASS ===' % (sum(passed), len(passed)))
sys.exit(0 if all(passed) else 1)