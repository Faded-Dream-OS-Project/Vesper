#!/usr/bin/env python3
# make_gpt.py — create a raw disk image with a protective MBR + GPT and
# one EFI System Partition, then leave FAT32 formatting to mtools.
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.
#
# Usage: make_gpt.py <image> <size_bytes> <esp_start_lba> <esp_sectors>
#
# Entry 0 = BIOS boot partition (LBA 34..esp_start_lba-1), entry 1 = EFI
# System Partition at esp_start_lba for esp_sectors.
#
# Layout: LBA 0 protective MBR, 1 GPT header, 2-33 partition entries,
# BIOS boot partition at 34, ESP at esp_start_lba.

import struct
import sys

def crc32(data):
    # Standard reflected CRC-32 (no lookup table, bit by bit).
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
    return crc ^ 0xFFFFFFFF

def main():
    image, size, start_lba, esp_sectors = (
        sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))
    sector = 512
    total_lba = size // sector

    # Partition usable range: LBA 34 .. total_lba-34 (backup GPT occupies
    # the last 33 LBAs: last_lba-32..-1 entries, last_lba header).
    img = bytearray(size)

    # --- protective MBR (LBA 0) ---
    mbr = bytearray(512)
    # partition entry: type 0xEE, spans LBA 1..min(total-1, 0xFFFFFFFF)
    struct.pack_into('<B3sB3sB3s', mbr, 446,
                     0, b'\x00\x00\x00', 0xEE, b'\x00\x00\x00',
                     0, b'\xff\xff\xff')
    # Protective entry: type 0xEE spanning the whole disk from LBA 1
    # (spec-correct; some firmware rejects 0xEE entries not starting at 1).
    # Limine's bios-install check (start >= 63) is bypassed with --force.
    struct.pack_into('<II', mbr, 454, 1, min(total_lba - 1, 0xFFFFFFFF))
    mbr[510:512] = b'\x55\xaa'
    img[0:512] = mbr

    # --- GPT header (LBA 1) ---
    gpt = bytearray(92)
    gpt[0:8] = b'EFI PART'
    struct.pack_into('<II', gpt, 8, 0x00010000, 92)      # rev, hdr size
    struct.pack_into('<II', gpt, 16, 0, 0)               # crc (later)
    struct.pack_into('<QQ', gpt, 24, 1, total_lba - 1)   # this/backup header LBA
    struct.pack_into('<QQ', gpt, 40, 34, total_lba - 34)  # part usable lba range
    gpt[56:72] = bytes.fromhex('a5f1c3d4b2e4f6a7' '0809101112131415')
    struct.pack_into('<QII', gpt, 72, 2, 128, 128)       # entries lba/count/size

    # --- partition entries (LBA 2..) ---
    entries = bytearray(128 * 128)   # 128 entries
    # EFI System Partition GUID: C12A7328-F81F-11D2-BA4B-00A0C93EC93B
    part_type = (bytes.fromhex('28732ac1') + bytes.fromhex('1ff8') +
                 bytes.fromhex('d211') + b'\xba\x4b\x00\xa0\xc9\x3e\xc9\x3b')
    part_guid = bytes.fromhex('b2e5a7c9d3f1b6e4f7a8090a1b2c3d4e')
    e = bytearray(128)
    e[0:16] = part_type
    e[16:32] = part_guid
    struct.pack_into('<QQ', e, 32, start_lba, start_lba + esp_sectors - 1)
    struct.pack_into('<Q', e, 48, 1 << 60)              # attrs: bit 60 legacy bootable
    e[56:128] = 'Vesper ESP'.encode('utf-16-le').ljust(72, b'\x00')
    entries[0:128] = e

    # --- entry 1: BIOS boot partition (34 .. esp_start_lba-1) ---
    # type GUID 21686148-6449-6E6F-744E-656564454649
    bb_type = (bytes.fromhex('48616821') + bytes.fromhex('4964') +
               bytes.fromhex('6f6e') + bytes.fromhex('744e6565') +
               b'\x64\x45\x46\x49')
    bb = bytearray(128)
    bb[0:16] = bb_type
    bb[16:32] = bytes.fromhex('c3d2e1f0a1b2c3d4e5f60718293a4b5c')
    struct.pack_into('<QQ', bb, 32, 34, start_lba - 1)
    bb[56:128] = 'BIOS boot'.encode('utf-16-le').ljust(72, b'\x00')
    entries[128:256] = bb

    entries_crc = crc32(entries)

    # Per spec: header CRC (offset 16) covers bytes 0..91 (header size
    # field, 92 bytes) with its own field zeroed; entries CRC at 88.
    struct.pack_into('<I', gpt, 88, entries_crc)
    struct.pack_into('<I', gpt, 16, 0)
    hdr_crc = crc32(bytes(gpt))
    struct.pack_into('<I', gpt, 16, hdr_crc)

    img[512:1024] = gpt
    img[1024:1024 + len(entries)] = entries

    # --- backup GPT: entries at LBAs (total-33)..(total-2), header at total-1 ---
    img[(total_lba - 33) * 512 : (total_lba - 1) * 512] = entries
    backup_hdr = bytearray(gpt)
    struct.pack_into('<QQ', backup_hdr, 24, total_lba - 1, 1)  # swap hdr LBAs
    struct.pack_into('<QII', backup_hdr, 72, total_lba - 33, 128, 128)
    struct.pack_into('<I', backup_hdr, 16, 0)
    struct.pack_into('<I', backup_hdr, 16, crc32(bytes(backup_hdr)))
    img[(total_lba - 1) * 512 : total_lba * 512] = backup_hdr

    with open(image, 'wb') as f:
        f.write(img)
    print(f"GPT written: {image} size={size} esp_lba={start_lba} "
          f"esp_sectors={esp_sectors} entries_crc={entries_crc:08x} "
          f"hdr_crc={hdr_crc:08x}")

if __name__ == '__main__':
    main()
