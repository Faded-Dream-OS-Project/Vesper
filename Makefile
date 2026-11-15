# Vesper kernel Makefile
# Copyright (c) Faded Dream OS Project. All rights reserved.
# Source is viewable but may not be copied, modified, or redistributed
# without permission.

# ---- toolchain ----
# The sandbox has no x86_64-elf cross gcc; the host gcc with explicit
# freestanding flags produces position-precise ELF objects for the
# kernel as long as we avoid referencing the host libc. Target is
# x86_64, code model kernel (higher half).
CC      := gcc
LD      := ld
OBJCOPY := objcopy

# Local tool prefix (QEMU etc. unpacked from .debs without root).
TOOLS   := $(HOME)/my-project/.tools/root
QEMU    := $(TOOLS)/usr/bin/qemu-system-x86_64
QEMU_IMG:= $(TOOLS)/usr/bin/qemu-img
OVMF    := $(TOOLS)/usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS := $(TOOLS)/usr/share/OVMF/OVMF_VARS_4M.fd
MKFS    := $(TOOLS)/usr/sbin/mkfs.vfat
LIMINE  := thirdparty/limine/limine

# Allow external override (e.g. a real x86_64-elf-gcc on another host).
ifeq ($(shell command -v x86_64-elf-gcc >/dev/null 2>&1 && echo yes),yes)
CC := x86_64-elf-gcc
LD := x86_64-elf-ld
OBJCOPY := x86_64-elf-objcopy
endif

KERNEL  := vesper.elf
ISO     := vesper.iso
IMG     := vesper.img

KERNEL_SRCS := $(wildcard kernel/src/*.c)
ASM_SRCS    := kernel/src/isr_stubs.S kernel/src/gdt_flush.S
KERNEL_OBJS := $(patsubst kernel/src/%.c,build/%.o,$(KERNEL_SRCS)) \
               build/isr_stubs.o build/gdt_flush.o

CFLAGS := -std=c17 -Wall -Wextra -ffreestanding -nostdlib \
          -fno-stack-protector -fno-pic -fno-pie -mno-red-zone \
          -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel \
          -O2 -g -fno-omit-frame-pointer \
          -Ikernel/include -Ithirdparty/limine \
          -Wno-override-init

LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.ld

.PHONY: all clean run iso img $(KERNEL_SRCS)

all: $(KERNEL)

build/%.o: kernel/src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/isr_stubs.o: kernel/src/isr_stubs.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/gdt_flush.o: kernel/src/gdt_flush.S | build
	$(CC) $(CFLAGS) -c $< -o $@

kernel/src/isr_stubs.S: scripts/gen_isr_stubs.py
	python3 scripts/gen_isr_stubs.py $@

build:
	mkdir -p build

$(KERNEL): $(KERNEL_OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o $@

# ---- host tool (Limine) ----
$(LIMINE): thirdparty/limine/limine.c
	$(MAKE) -C thirdparty/limine

# ---- ISO (UEFI + BIOS hybrid from Limine blobs) ----
iso: $(ISO)

$(ISO): $(KERNEL) $(LIMINE) limine.conf
	mkdir -p build/iso_root
	cp $(KERNEL) build/iso_root/
	cp limine.conf build/iso_root/
	cp thirdparty/limine/limine-bios-cd.bin build/iso_root/
	cp thirdparty/limine/limine-uefi-cd.bin build/iso_root/
	cp thirdparty/limine/limine-bios.sys build/iso_root/
	xorriso -as mkisofs -R -r -J -b limine-bios-cd.bin -no-emul-boot \
	    -boot-load-size 4 -boot-info-table -hfsplus \
	    -apm-block-size 2048 --efi-boot limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    build/iso_root -o $(ISO)
	$(LIMINE) bios-install $(ISO)

# ---- disk image (GPT + ESP, for USB sticks and QEMU UEFI) ----
img: $(IMG)

$(IMG): $(KERNEL) $(LIMINE) limine.conf
	qemu-img create -f raw $(IMG) 64M
	$(TOOLS)/usr/sbin/mkfs.vfat -n VESPER $(IMG)
	mmd -i $(IMG) ::/EFI ::/EFI/BOOT
	mcopy -i $(IMG) thirdparty/limine/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $(IMG) limine.conf ::/limine.conf
	mcopy -i $(IMG) $(KERNEL) ::/vesper.elf
	$(LIMINE) uefi-install $(IMG) || $(LIMINE) bios-install $(IMG)

# ---- QEMU ----
QEMU_ARGS := -machine q35 -m 512M \
    -drive if=pflash,format=raw,file=$(OVMF),readonly=on \
    -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
    -drive file=$(ISO),format=raw,media=disk \
    -serial file:build/serial.log -display none -no-reboot

build/OVMF_VARS.fd: | build
	cp $(OVMF_VARS) build/OVMF_VARS.fd

run: $(ISO) build/OVMF_VARS.fd
	$(QEMU) $(QEMU_ARGS) &
	@sleep 5 && kill %1 2>/dev/null; true

.PHONY: run

clean:
	rm -rf build $(KERNEL) $(ISO) $(IMG)

# ---- gitignore-style helpers ----
$(KERNEL_SRCS): ;

.PHONY: help
help:
	@echo "targets: all (vesper.elf), iso, img, run, clean"
