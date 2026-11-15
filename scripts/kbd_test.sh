#!/usr/bin/env bash
# kbd_test.sh — automated PS/2 keyboard self-test via QEMU monitor sendkey.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# Boots the ISO, waits for the kernel's 4s kbd test window, injects
# h, i, shift-h, caps_lock, a, then reports the self-test verdict from
# the serial log. QEMU-only (a USB keyboard on real hardware will not
# appear as PS/2).

set -u
TOOLS=/home/z/my-project/.tools/root
export PATH="$TOOLS/usr/bin:$TOOLS/usr/sbin:$PATH"
export LD_LIBRARY_PATH="$TOOLS/usr/lib/x86_64-linux-gnu:$TOOLS/usr/lib"
SHARE=$TOOLS/usr/share/qemu
cd "$(dirname "$0")/.."

rm -f build/serial.log build/mon.sock
timeout 90 qemu-system-x86_64 -L "$SHARE" -machine q35 -m 512M \
    -cdrom vesper.iso -serial file:build/serial.log -display none -no-reboot \
    -monitor unix:build/mon.sock,server=on,wait=off > /dev/null 2>&1 &
QPID=$!

for i in $(seq 1 12); do
    sleep 2
    grep -aq "kbd test window" build/serial.log 2>/dev/null && break
done

python3 - <<'PYEOF'
import socket, time
s = socket.socket(socket.AF_UNIX)
s.connect('build/mon.sock')
time.sleep(0.3)
try: s.recv(65536)
except Exception: pass
for key in ['h', 'i', 'shift-h', 'caps_lock', 'a']:
    s.sendall(f"sendkey {key}\n".encode())
    time.sleep(0.7)
    try:
        s.settimeout(0.2); s.recv(65536)
    except Exception: pass
s.close()
PYEOF

for i in $(seq 1 15); do
    sleep 2
    grep -aq "M2 self-test" build/serial.log 2>/dev/null && break
done
kill $QPID 2>/dev/null; wait $QPID 2>/dev/null

grep -a "key:\|selftest: kbd" -A7 build/serial.log | tr -c '[:print:]\n' ' '
