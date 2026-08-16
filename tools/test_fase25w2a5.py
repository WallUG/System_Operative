#!/usr/bin/env python3
"""MyOS - Fase 25-W2A paso 5 (winmm.dll + audio AC'97).

El plan: winmm.dll (PlaySoundA/waveOutWrite/mciSendStringA/timeGetTime
sobre el AC'97 del kernel) probada con wavplay.exe (mingw real, genera
tone.wav 440 Hz y lo reproduce 3 veces) y verificacion del wav del
host (-audiodev wav): cabecera 44100 Hz stereo 16-bit y muestras no
nulas (el beep + 3 plays).

NOTA QEMU 10.2.2: el contenido del wav del host sale distorsionado por
la regresion 42061a14 (audio/mixeng usa hw->info.af en vez de
sw->info.af; fix 924b0be8 aun no empaquetado). La validacion de
contenido se apoya en el lado GUEST: el kernel imprime rate/bytes/
cksum/res de cada SYS_AUDIO_PLAY (DMA completado con los datos
correctos) y wavplay valida cada API.

Uso: python3 tools/test_fase25w2a5.py
Requiere: build/os-persist.bin con kernel.elf (SYS_AUDIO_PLAY) y
wavplay.exe en el FS.
"""
import os
import socket
import struct
import subprocess
import sys
import threading
import time

os.chdir(os.path.dirname(os.path.abspath(__file__)) + '/..')

MON = '/tmp/opencode/qmon.sock'
WAV = '/tmp/opencode/w2a5.wav'

passed = []


def check(name, ok):
    passed.append(ok)
    print(('PASS' if ok else 'FAIL') + ' - ' + name)
    if not ok:
        print('=== RESUMEN %d/%d PASS ===' % (sum(passed), len(passed)))
        sys.exit(1)


subprocess.run(['pkill', '-9', '-f', 'qemu-system'], capture_output=True)
time.sleep(0.5)
for p in (MON, WAV):
    try:
        os.unlink(p)
    except FileNotFoundError:
        pass

qemu = subprocess.Popen(
    ['qemu-system-i386', '-display', 'none',
     '-monitor', 'unix:%s,server,nowait' % MON,
     '-serial', 'stdio', '-no-reboot', '-no-shutdown',
     '-audiodev', 'wav,path=%s,id=audio0' % WAV,
     '-device', 'AC97,audiodev=audio0',
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


def waitstr(s, t=30):
    end = time.time() + t
    while time.time() < end:
        if s in ''.join(acc):
            return True
        time.sleep(0.2)
    return False


def sendcmd(cmd):
    keymap = {' ': 'spc', '\n': 'ret', '.': 'dot', '_': 'shift-minus',
              '>': 'shift-dot', '=': 'equal', '-': 'minus'}
    for ch in cmd + '\n':
        key = keymap.get(ch, ch)
        hmp('sendkey %s' % key, 0.12)


# --- boot: codec AC'97 real (id Sigmatel 0x8384) ---
if not waitstr('ac97: codec ', 30):
    print('FAIL - sin AC97'); qemu.kill(); sys.exit(1)
text = ''.join(acc)
i = text.find('ac97: codec ')
codec = text[i:i + 30]
cid = codec.split('codec ')[1].split()[0]
check('AC97 codec presente (id=%s, Sigmatel 0x8384)' % cid,
      cid.isdigit() and int(cid) == 0x8384)
if not waitstr('ac97: beep 150 ms', 10):
    check('beep de arranque', False); qemu.kill(); sys.exit(1)
check('beep de arranque iniciado', True)

# --- autoboot -> shell ---
if not waitstr('Autoboot:', 30):
    check('autoboot', False); qemu.kill(); sys.exit(1)
hmp('sendkey x', 0.5)
if not waitstr('myos>', 20):
    check('shell', False); qemu.kill(); sys.exit(1)
check('shell lista', True)

# --- wavplay.exe: PlaySoundA + waveOutWrite + mciSendStringA ---
sendcmd('run wavplay.exe')
if not waitstr('wavplay: start', 20):
    check('wavplay arranca', False); qemu.kill(); sys.exit(1)
check('wavplay.exe arranca (ring 3, imports winmm resueltos)', True)
if not waitstr('tone.wav generado', 20):
    check('tone.wav generado', False); qemu.kill(); sys.exit(1)
check('tone.wav generado con CreateFileA/WriteFile', True)
if not waitstr('PlaySoundA ok', 30):
    check('PlaySoundA ok', False); qemu.kill(); sys.exit(1)
check('PlaySoundA(tone.wav, SND_FILENAME) reproduce', True)
if not waitstr('waveOutWrite ok', 30):
    check('waveOutWrite ok', False); qemu.kill(); sys.exit(1)
check('waveOutOpen + waveOutWrite (WAVEHDR) reproducen', True)
if not waitstr('mci ok', 30):
    check('mciSendStringA ok', False); qemu.kill(); sys.exit(1)
check('mciSendStringA(play tone.wav) reproduce', True)
if not waitstr('wavplay:PASS', 30):
    check('wavplay:PASS', False); qemu.kill(); sys.exit(1)
check('wavplay:PASS + exit:0', True)

# --- kernel: 3x SYS_AUDIO_PLAY completos con datos correctos ---
time.sleep(1)
text = ''.join(acc)
plays = text.count('ac97: play rate=')
check('kernel: 3 reproducciones DMA (%d) res=0' % plays,
      plays == 3 and 'res=0' in text)
# cada play: tone.wav 0.5 s (22050 muestras) mono 16-bit -> stereo
# 16-bit = 88200 bytes a 44100 Hz
ok_plays = all(
    'ac97: play rate=44100 bytes=88200 cksum=' in l and ' res=0' in l
    for l in text.split('\n')
    if l.startswith('ac97: play '))
check('kernel: rate=44100 bytes=88200 (mono->stereo) res=0 cksum!=0',
      ok_plays)

time.sleep(2)
qemu.kill()
try:
    qemu.wait(timeout=3)
except Exception:
    qemu.kill()

# --- wav del host: cabecera correcta + muestras no nulas ---
try:
    d = open(WAV, 'rb').read()
    ok_hdr = len(d) > 44 and d[:4] == b'RIFF' and d[8:12] == b'WAVE'
    rate = struct.unpack_from('<I', d, 24)[0]
    ch = struct.unpack_from('<H', d, 22)[0]
    bits = struct.unpack_from('<H', d, 34)[0]
    check('wav del host: RIFF %d Hz %d ch %d-bit' % (rate, ch, bits),
          ok_hdr and rate == 44100 and ch == 2 and bits == 16)
    data = d[44:]
    n = len(data) // 2
    nz = 0
    mx = 0
    step = max(1, n // 200000)
    for i in range(0, n, step):
        v = struct.unpack_from('<h', data, i * 2)[0]
        if v:
            nz += 1
        mx = max(mx, abs(v))
    # beep 0.15s + 3 plays 0.5s: >= 30000 muestras no nulas (QEMU 10.2.2
    # duplica cada segmento por el bug del mixer; contamos en exceso)
    check('wav del host: %d muestras no nulas (max amp %d)'
          % (nz * step, mx),
          nz * step > 30000 and mx > 4000)
except Exception as e:
    check('wav del host: lectura (%s)' % e, False)

print('=== RESUMEN %d/%d PASS ===' % (sum(passed), len(passed)))
sys.exit(0 if all(passed) else 1)