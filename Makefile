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
BUILD_ID_HDR := kernel/include/build_id.h

# Build identity (git hash + dirty + compiler version) stamped into a
# generated header on every build; the kernel prints it as the first
# boot line so a photo identifies the exact build. Not committed.
$(BUILD_ID_HDR): FORCE
	@bash scripts/gen_build_id.sh $(CC) $@

.PHONY: FORCE
FORCE:

.PHONY: build-id
build-id: $(BUILD_ID_HDR)

KERNEL_SRCS := $(wildcard kernel/src/*.c)
ASM_SRCS    := kernel/src/isr_stubs.S kernel/src/gdt_flush.S \
               kernel/src/sched_switch_to.S kernel/src/syscall_entry.S \
               kernel/src/init_blob.S
KERNEL_OBJS := $(patsubst kernel/src/%.c,build/%.o,$(KERNEL_SRCS)) \
               build/isr_stubs.o build/gdt_flush.o build/sched_switch_to.o \
               build/syscall_entry.o build/init_blob.o

# objects depend on the generated build id (hash changes = relink)
$(KERNEL_OBJS): $(BUILD_ID_HDR)

CFLAGS := -std=c17 -Wall -Wextra -ffreestanding -nostdlib \
          -fno-stack-protector -fno-pic -fno-pie -mno-red-zone \
          -mno-sse -mno-mmx -mno-3dnow -mcmodel=kernel \
          -O2 -g -fno-omit-frame-pointer \
          -Ikernel/include -Ithirdparty/limine \
          -Wno-override-init

LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T kernel/linker.ld

.PHONY: all clean run iso img $(KERNEL_SRCS)

all: $(KERNEL)

# print the compiler actually in use on every build (Task: portable
# build; a photo/console mismatch is debuggable only if we know it)
.PHONY: cc-info
cc-info:
	@echo "CC      : $(CC) ($$($(CC) -dumpfullversion -dumpversion 2>/dev/null || $(CC) -dumpversion))"
	@echo "LD      : $(LD)"

build/%.o: kernel/src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/isr_stubs.o: kernel/src/isr_stubs.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/gdt_flush.o: kernel/src/gdt_flush.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/sched_switch_to.o: kernel/src/sched_switch_to.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/syscall_entry.o: kernel/src/syscall_entry.S | build
	$(CC) $(CFLAGS) -c $< -o $@

build/init_blob.o: kernel/src/init_blob.S build/init.bin | build
	$(CC) $(CFLAGS) -c $< -o $@

build/init.bin: user/init.c scripts/build_init.sh | build
	bash scripts/build_init.sh

.PHONY: init

kernel/src/isr_stubs.S: scripts/gen_isr_stubs.py
	python3 scripts/gen_isr_stubs.py $@

build:
	mkdir -p build

$(KERNEL): $(KERNEL_OBJS) kernel/linker.ld
	@echo "CC      : $(CC) ($$($(CC) -dumpfullversion -dumpversion 2>/dev/null || $(CC) -dumpversion))  LD: $(LD)"
	$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o $@

# ---- host tool (Limine) ----
$(LIMINE): thirdparty/limine/limine.c
	$(MAKE) -C thirdparty/limine

# ---- ISO (UEFI + BIOS hybrid from Limine blobs) ----
iso: $(ISO)

CONF ?= limine.conf

$(ISO): $(KERNEL) $(LIMINE) $(CONF)
	mkdir -p build/iso_root
	cp $(KERNEL) build/iso_root/
	cp $(CONF) build/iso_root/limine.conf
	cp thirdparty/limine/limine-bios-cd.bin build/iso_root/
	cp thirdparty/limine/limine-uefi-cd.bin build/iso_root/
	cp thirdparty/limine/limine-bios.sys build/iso_root/
	xorriso -as mkisofs -R -r -J -b limine-bios-cd.bin -no-emul-boot \
	    -boot-load-size 4 -boot-info-table -hfsplus \
	    -apm-block-size 2048 --efi-boot limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    build/iso_root -o $(ISO)
	$(LIMINE) bios-install $(ISO)

# ---- disk image (GPT + FAT32 ESP, for USB sticks and QEMU UEFI) ----
# Layout: protective MBR + GPT (scripts/make_gpt.py), one EFI System
# Partition at LBA 2048, FAT32, holding BOOTX64.EFI, limine.conf and
# the kernel. Limine stage is installed for UEFI; BIOS stage sits in
# the ESP as limine-bios.sys with the MBR patched by bios-install.
IMG_SZ_MB := 64
ESP_LBA   := 2048
# ESP ends at total-34 (last LBA - 33 for backup GPT entries, -1 header)
MTOOLS := env MTOOLS_SKIP_CHECK=1

img: $(IMG)

$(IMG): $(KERNEL) $(LIMINE) limine.conf scripts/make_gpt.py | build
	@esp_secs=$$(( $$(stat -c%s $(IMG) 2>/dev/null || echo 0) )); \
	esp_secs=$$(( IMG_SZ_MB_HACK )); true
	python3 scripts/make_gpt.py $(IMG) $$(( $(IMG_SZ_MB) * 1024 * 1024 )) $(ESP_LBA) $$(( $(IMG_SZ_MB) * 1024 * 1024 / 512 - $(ESP_LBA) - 34 ))
	$(MTOOLS) mformat -i $(IMG)@@$(ESP_LBA)S -c 1 -H $(ESP_LBA) -T $$(( $(IMG_SZ_MB) * 1024 * 1024 / 512 - $(ESP_LBA) - 34 )) -n VESPER ::
	$(MTOOLS) mmd -i $(IMG)@@$(ESP_LBA)S ::/EFI ::/EFI/BOOT
	$(MTOOLS) mcopy -i $(IMG)@@$(ESP_LBA)S thirdparty/limine/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	$(MTOOLS) mcopy -i $(IMG)@@$(ESP_LBA)S limine.conf ::/limine.conf
	$(MTOOLS) mcopy -i $(IMG)@@$(ESP_LBA)S $(KERNEL) ::/vesper.elf
	$(MTOOLS) mcopy -i $(IMG)@@$(ESP_LBA)S thirdparty/limine/limine-bios.sys ::/limine-bios.sys
	$(LIMINE) bios-install --force $(IMG) 2 --uninstall-data-file=$(IMG).uninstall
	truncate -s $$(( $(IMG_SZ_MB) * 1024 * 1024 )) $(IMG)

# ---- QEMU ----
QEMU_ROMS := $(TOOLS)/usr/share/qemu
QEMU_ARGS := -L $(QEMU_ROMS) -machine q35 -m 512M \
    -drive if=pflash,format=raw,file=$(OVMF),readonly=on \
    -drive if=pflash,format=raw,file=build/OVMF_VARS.fd \
    -cdrom $(ISO) \
    -serial file:build/serial.log -display none -no-reboot

build/OVMF_VARS.fd: $(OVMF_VARS) | build
	cp $(OVMF_VARS) build/OVMF_VARS.fd

# `make run`: boot the ISO under OVMF (UEFI) in QEMU, one command.
# Runs in the foreground with a timeout so CI sandboxes never hang;
# serial output is captured to build/serial.log and dumped on exit.
RUN_SECS ?= 30
run: $(ISO) build/OVMF_VARS.fd
	@rm -f build/serial.log
	@echo ">> qemu (UEFI/OVMF) starting, $(RUN_SECS)s budget; serial -> build/serial.log"
	@timeout $(RUN_SECS) $(QEMU) $(QEMU_ARGS) || test $$? -eq 124
	@echo ">> serial output:"
	@cat build/serial.log | tr -c '[:print:]\n' ' ' | grep -a . | tail -n 30

.PHONY: run

clean:
	rm -rf build $(KERNEL) $(ISO) $(IMG)

# ---- gitignore-style helpers ----
$(KERNEL_SRCS): ;

.PHONY: help
help:
	@echo "targets: all (vesper.elf), iso, img, run, clean"
