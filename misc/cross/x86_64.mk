ifeq ($(TOOLCHAIN), gcc)
	X_CXX_BIN = $(TOOLCHAIN_PREFIX)x86_64-elf-g++
	X_CC_BIN = $(TOOLCHAIN_PREIFX)x86_64-elf-gcc
	X_AS_BIN = $(TOOLCHAIN_PREFIX)x86_64-elf-as
	X_LD_BIN = $(TOOLCHAIN_PREFIX)x86_64-elf-ld
else ifeq ($(TOOLCHAIN), clang)
	X_CXX_BIN = $(TOOLCHAIN_PREFIX)clang++ --target=x86_64-elf
	X_CC_BIN = $(TOOLCHAIN_PREFIX)clang --target=x86_64-elf
	X_AS_BIN = $(TOOLCHAIN_PREFIX)clang --target=x86_64-elf
	X_LD_BIN = $(TOOLCHAIN_PREFIX)ld.lld
	KERNEL_AS_FLAGS += -c
else
$(error "Unsupported toolchain for x86_64: $(TOOLCHAIN)")
endif

X86_64_FLAGS = -mno-red-zone -mno-80387 -mno-mmx -mno-sse -mno-sse2 -mno-3dnow
KERNEL_CXX_FLAGS += $(X86_64_FLAGS)
KERNEL_LD_FLAGS += -ztext --no-relax
DRIVER_CXX_FLAGS += $(X86_64_FLAGS)
DRIVER_C_FLAGS += $(X86_64_FLAGS)

ARCH_DEFAULT_TARGET = limine-iso-hybrid
KERNEL_BOOT_PROTOCOL = limine
UEFI_BOOT_NAME = BOOTX64.EFI
RUN_WITH_BIOS = no

QEMU_BASE = qemu-system-x86_64 -machine q35 \
	-smp 2 -m 128M -cdrom $(ISO_TARGET) \
	-debugcon /dev/stdout -monitor stdio -device virtio-gpu
ifneq ($(RUN_WITH_BIOS), yes)
	QEMU_BASE += -drive if=pflash,format=raw,file=$(QEMU_FW_FILE),readonly=on
endif

QEMU_ACCEL = --enable-kvm -cpu host,+invtsc
QEMU_NO_ACCEL = 
QEMU_DEBUG = -s -S -no-reboot -no-shutdown
QEMU_FW_FILE = $(VENDOR_CACHE_DIR)/ovmf-x86_64.fd
OVMF_DOWNLOAD_URL = https://retrage.github.io/edk2-nightly/bin/RELEASEX64_OVMF.fd

limine-iso-hybrid: limine-iso-prep
	$(LOUD)cp $(LIMINE_BINARIES)/limine-bios-cd.bin $(ISO_BUILD_DIR)
	$(LOUD)cp $(LIMINE_BINARIES)/limine-bios.sys $(ISO_BUILD_DIR)
	$(LOUD)xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
		-boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label $(ISO_BUILD_DIR) -o \
		$(ISO_TARGET) $(LOUD_REDIRECT)
	$(LOUD)$(LIMINE_BINARIES)/limine bios-install $(ISO_TARGET) $(LOUD_REDIRECT)
	$(LOUD)rm -r $(ISO_BUILD_DIR)
	@printf "$(C_CYAN)[Build]$(C_RST) Bootable iso (hybrid uefi + bios) generated @ $(ISO_TARGET)\r\n"
	@printf "$(C_CYAN)[Build]$(C_RST) If qemu is installed, try it out with 'make run'!\r\n"

$(QEMU_FW_FILE):
	$(LOUD)-rm $(QEMU_FW_FILE)
	$(LOUD)mkdir -p $(VENDOR_CACHE_DIR)
	$(LOUD)curl -o $(QEMU_FW_FILE) $(OVMF_DOWNLOAD_URL)
	@printf "$(C_YELLOW)[Cache]$(C_RST)Downloaded OVMF for x86_64 from $(OVMF_DOWNLOAD_URL)\n"
