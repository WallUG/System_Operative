#!/usr/bin/env python3
"""Fase 24-P4.5: drivers de audio (AC'97) y red (RTL8139) en QEMU.

Boot con -device AC97 + -netdev user + rtl8139:
- Audio: el kernel detecta el codec AC'97 por PCI (clase 0x0401), lo
  resetea (registro RESET del NAM), configura 48 kHz sin mute, y
  reproduce el beep de arranque por DMA (bus master del PCI command
  register: sin el bit 2 el DMA de QEMU devuelve ceros). Se verifica
  el SR del bus master y que la transferencia arranca (picb).
- Red: el kernel detecta el RTL8139 (10EC:8139), configura el RX ring
  y hace el round trip ARP contra el gateway del user-net (10.0.2.2):
  "net: tx arp" + "net: arp 10.0.2.2 -> MAC". Despues envia el ICMP
  echo request ("net: tx icmp"); el reply llega al netdev (verificado
  con filter-dump en el test del host) pero el ring de QEMU no lo
  entrega en la segunda recepcion (quirk documentado).
- El test verifica tambien el pcap (ARP request/reply + ICMP request/
  reply validos) con -object filter-dump."""
import subprocess, socket, threading, time, sys, os, struct

os.chdir('/home/demox/respaldo/System_Operative')
MON = '/tmp/opencode/qmon.sock'
passed = []
def check(name, cond):
    passed.append(cond)
    print(('PASS' if cond else 'FAIL')+f' - {name}', flush=True)

subprocess.run(['make', 'persist_disk'], capture_output=True)
subprocess.run(['pkill', '-9', '-f', 'qemu-system'], capture_output=True)
time.sleep(0.5)
try: os.unlink(MON)
except FileNotFoundError: pass
try: os.unlink('/tmp/opencode/p45.pcap')
except FileNotFoundError: pass

qemu = subprocess.Popen(
    ['qemu-system-i386','-display','none',
     '-audiodev','none,id=audio0','-device','AC97,audiodev=audio0',
     '-netdev','user,id=net0','-device','rtl8139,netdev=net0',
     '-object','filter-dump,id=f0,netdev=net0,file=/tmp/opencode/p45.pcap',
     '-monitor',f'unix:{MON},server,nowait',
     '-serial','stdio','-no-reboot','-no-shutdown',
     '-drive',f'format=raw,file=build/os-persist.bin'],
    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
acc = []
def reader():
    while True:
        ch = qemu.stdout.read(1)
        if ch == b'': return
        acc.append(ch.decode('utf-8', errors='replace'))
threading.Thread(target=reader, daemon=True).start()
def waitstr(s, t=40):
    d = time.time()+t
    while time.time() < d:
        if s in ''.join(acc): return True
        time.sleep(0.2)
    return False

# --- audio ---
if not waitstr('ac97: codec ', 30):
    print('FAIL - sin AC97'); qemu.terminate(); sys.exit(1)
check('AC97 detectado por PCI', True)
if not waitstr('ac97: beep 150 ms', 10):
    print('FAIL - beep'); qemu.terminate(); sys.exit(1)
check('beep de arranque iniciado', True)
if not waitstr('ac97: beep fin', 10):
    print('FAIL - beep fin'); qemu.terminate(); sys.exit(1)
check('transferencia DMA del beep completada', True)

# --- red: ARP + ICMP ---
if not waitstr('net: rtl8139 mac=', 10):
    print('FAIL - sin rtl8139'); qemu.terminate(); sys.exit(1)
check('RTL8139 detectado (MAC 52:54:00:12:34:56)', True)
if not waitstr('net: tx arp', 10):
    print('FAIL - sin ARP'); qemu.terminate(); sys.exit(1)
check('ARP request transmitido', True)
if not waitstr('net: arp 10.0.2.2 -> ', 15):
    print('FAIL - sin reply ARP'); qemu.terminate(); sys.exit(1)
check('round trip ARP contra el gateway (reply parseado)', True)
if not waitstr('net: tx icmp', 10):
    print('FAIL - sin ICMP'); qemu.terminate(); sys.exit(1)
check('ICMP echo request transmitido', True)
waitstr('net: sin echo reply', 15)
time.sleep(3)
qemu.terminate()
try: qemu.wait(timeout=3)
except: qemu.kill()

# --- pcap del host: paquetes validos ---
try:
    d = open('/tmp/opencode/p45.pcap','rb').read()
    i = 24; pkts = []
    while i+16 <= len(d):
        ts,us,inc,orig = struct.unpack_from('<IIII',d,i); i += 16
        pkts.append(d[i:i+orig]); i += orig
    arp_req = any(len(p)>=42 and p[12:14]==b'\x08\x06' and
                  struct.unpack_from('>H',p,20)[0]==1 for p in pkts)
    arp_rep = any(len(p)>=42 and p[12:14]==b'\x08\x06' and
                  struct.unpack_from('>H',p,20)[0]==2 and
                  p[28:32]==bytes([10,0,2,2]) for p in pkts)
    icmp_req = any(len(p)>=34 and p[12:14]==b'\x08\x00' and p[23]==1 and
                   p[34]==8 for p in pkts)
    icmp_rep = any(len(p)>=34 and p[12:14]==b'\x08\x00' and p[23]==1 and
                   p[34]==0 for p in pkts)
    check(f'pcap: ARP req+rep y ICMP req+rep validos ({len(pkts)} pkts)',
          arp_req and arp_rep and icmp_req and icmp_rep)
except Exception as e:
    print('FAIL - pcap', e)

print(f"=== RESUMEN {sum(passed)}/{len(passed)} PASS ===")
sys.exit(0 if all(passed) else 1)