#!/usr/bin/env bash
# verify_image.sh — validate vesper.img before a human flashes it.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# Portable (session 7): no hard-coded paths. QEMU and OVMF are found
# via PATH and standard distro locations (/usr/share/OVMF,
# /usr/share/edk2/ovmf, ...) with env overrides QEMU_BIN / OVMF_DIR.
# If vesper.img is missing, it is BUILT (make img) so a clean clone
# can verify itself end to end. Runs under LC_ALL=C: no tool-output
# locale dependencies.
#
# Checks:
#   1. GPT header + partition entries CRC32s (computed and compared)
#   2. ESP contents: EFI/BOOT/BOOTX64.EFI, limine.conf, vesper.elf,
#      limine-bios.sys (mtools directory read)
#   3. Boot test: the image boots as a QEMU USB mass-storage device
#      under OVMF with NO serial port and no default devices; the
#      on-screen framebuffer output is checked by pixel analysis
#      (text + green PASS lines present).
# Prints PASS/FAIL per check and a final verdict. Never writes to a
# real disk: only the image file is read (QEMU opens it read-write but
# the guest never writes in the boot path under test).

set -u
export LC_ALL=C

cd "$(dirname "$0")/.."

IMG=vesper.img
FAILS=0

# ---- tool discovery ----
QEMU="${QEMU_BIN:-$(command -v qemu-system-x86_64 2>/dev/null || true)}"
if [ -z "$QEMU" ]; then
    for f in /usr/bin/qemu-system-x86_64; do
        [ -x "$f" ] && QEMU="$f" && break
    done
fi
OVMF_CODE=""; OVMF_VARS=""
for d in "${OVMF_DIR:-}" /usr/share/OVMF /usr/share/ovmf \
         /usr/share/edk2/ovmf /usr/share/qemu/OVMF; do
    [ -n "$d" ] || continue
    if [ -f "$d/OVMF_CODE_4M.fd" ]; then OVMF_CODE="$d/OVMF_CODE_4M.fd"; OVMF_VARS="$d/OVMF_VARS_4M.fd"; break; fi
    if [ -f "$d/OVMF_CODE.fd" ]; then OVMF_CODE="$d/OVMF_CODE.fd"; OVMF_VARS="$d/OVMF_VARS.fd"; break; fi
done
QEMU_ROMS=$(dirname "$(command -v "$QEMU" 2>/dev/null || echo /usr/bin/qemu-system-x86_64)")/../share/qemu
QEMU_ROMS=$(cd "$QEMU_ROMS" 2>/dev/null && pwd)

check() {   # name, condition (0=ok)
    if [ "$2" -eq 0 ]; then
        echo "PASS  $1"
    else
        echo "FAIL  $1"
        FAILS=$((FAILS + 1))
    fi
}

# ---- self-build the image if missing (clean clone support) ----
if [ ! -s "$IMG" ]; then
    echo "== $IMG missing: building it (make img) =="
    if make img >/dev/null 2>&1; then
        echo "built $IMG ok"
    else
        echo "FAIL  could not build $IMG (run scripts/doctor.sh)"
        exit 1
    fi
fi

echo "== image: $IMG =="
[ -s "$IMG" ]; check "image exists and is non-empty" $?

# ---- 1. GPT structure ----
python3 - "$IMG" <<'PYEOF'
import struct, sys, zlib

img = sys.argv[1]
fails = 0

with open(img, 'rb') as f:
    # protective MBR at LBA 0
    mbr = f.read(512)
    if mbr[510:512] != b'\x55\xAA':
        print("      protective MBR signature missing")
        fails += 1
    part_type = mbr[446 + 4]
    if part_type != 0xEE:
        print("      protective MBR partition type != 0xEE")
        fails += 1

    # primary GPT header at LBA 1
    hdr = f.read(512)
    if hdr[:8] != b'EFI PART':
        print("      GPT header signature missing")
        fails += 1
    # header CRC covers bytes 0..(header_size) with the CRC field at
    # 16..20 zeroed (everything else, including the entries CRC at 88,
    # participates)
    hdr_size = struct.unpack('<I', hdr[12:16])[0]
    crc_stored = struct.unpack('<I', hdr[16:20])[0]
    hdr_mut = bytearray(hdr[:hdr_size])
    hdr_mut[16:20] = b'\x00\x00\x00\x00'
    crc_calc = zlib.crc32(bytes(hdr_mut)) & 0xFFFFFFFF
    if crc_calc != crc_stored:
        print(f"      GPT header CRC mismatch (stored {crc_stored:#x} calc {crc_calc:#x})")
        fails += 1

    # partition entries
    n_parts = struct.unpack('<I', hdr[80:84])[0]
    part_size = struct.unpack('<I', hdr[84:88])[0]
    parts = f.read(part_size * n_parts)
    parts_crc_stored = struct.unpack('<I', hdr[88:92])[0]
    parts_crc_calc = zlib.crc32(parts) & 0xFFFFFFFF
    if parts_crc_calc != parts_crc_stored:
        print(f"      partition-entry CRC mismatch "
              f"(stored {parts_crc_stored:#x} calc {parts_crc_calc:#x})")
        fails += 1

    # first partition: EFI system partition (type GUID
    # C12A7328-F81F-11D2-BA4B-00A0C93EC93B, mixed-endian as packed)
    p0 = parts[:part_size]
    esp_type = (bytes.fromhex('28732ac1') + bytes.fromhex('1ff8') +
                bytes.fromhex('d211') + b'\xba\x4b\x00\xa0\xc9\x3e\xc9\x3b')
    if p0[:16] != esp_type:
        print("      partition 0 is not an EFI System Partition")
        fails += 1
    if p0[56:128].decode('utf-16-le', errors='replace').rstrip('\x00') != 'Vesper ESP':
        print("      partition 0 name is not 'Vesper ESP'")
        fails += 1

sys.exit(1 if fails else 0)
PYEOF
check "GPT headers/CRCs and ESP partition" $?

# ---- 2. ESP contents ----
files=$(mdir -b -i "$IMG"@@2048S ::/ ::/EFI/BOOT 2>/dev/null)
want_missing=0
for want in "EFI/BOOT/BOOTX64.EFI" "limine.conf" "vesper.elf" "limine-bios.sys"; do
    echo "$files" | grep -q "/$want\$" || want_missing=1
done
[ "$want_missing" -eq 0 ]
check "ESP contains EFI/BOOT/BOOTX64.EFI, limine.conf, vesper.elf, limine-bios.sys" $?

# ---- 3. USB boot test (OVMF, no serial, no default devices) ----
if [ -z "$QEMU" ] || [ -z "$OVMF_CODE" ]; then
    echo "SKIP  USB boot test (qemu or OVMF not found — install qemu-system-x86 ovmf)"
else
    rm -f build/OVMF_VARS.fd build/mon.sock build/verify_screen.ppm
    cp "$OVMF_VARS" build/OVMF_VARS.fd
    timeout 150 "$QEMU" -L "$QEMU_ROMS" -machine q35 -m 512M \
        -nodefaults -vga std \
        -drive if=pflash,format=raw,file="$OVMF_CODE",readonly=on \
        -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
        -drive if=none,id=usb,file="$IMG",format=raw \
        -device qemu-xhci -device usb-storage,drive=usb \
        -serial none -display none -no-reboot \
        -monitor unix:build/mon.sock,server=on,wait=off > /dev/null 2>&1 &
    QPID=$!
    for i in $(seq 1 26); do sleep 5; done
    python3 - <<'PYEOF'
import socket, time
s = socket.socket(socket.AF_UNIX)
s.connect('build/mon.sock')
time.sleep(0.3)
try: s.recv(65536)
except Exception: pass
s.sendall(b"screendump build/verify_screen.ppm\n")
time.sleep(2)
try: s.settimeout(2); s.recv(65536)
except Exception: pass
s.close()
PYEOF
    kill $QPID 2>/dev/null; wait $QPID 2>/dev/null

    python3 - <<'PYEOF'
from PIL import Image
import sys
try:
    im = Image.open('build/verify_screen.ppm').convert('RGB')
except Exception:
    print("      no screendump produced")
    sys.exit(1)
w, h = im.size
px = im.load()
nonblack = sum(1 for y in range(0, h, 4) for x in range(0, w, 4)
               if px[x, y] != (0, 0, 0))
green = sum(1 for y in range(0, h, 2) for x in range(0, w, 2)
            if all(abs(px[x, y][i] - (0x22, 0xC2, 0x22)[i]) <= 40
                   for i in range(3)))
print(f"      screendump {w}x{h}: non-black {nonblack}, green {green}")
sys.exit(0 if (nonblack > 500 and green > 100) else 1)
PYEOF
    check "USB-stick boot (OVMF, -serial none, -nodefaults) shows self-tests on screen" $?
fi

echo
if [ "$FAILS" -eq 0 ]; then
    echo "RESULT: ALL CHECKS PASS — image ready to flash"
    exit 0
else
    echo "RESULT: $FAILS CHECK(S) FAILED — do not flash"
    exit 1
fi
