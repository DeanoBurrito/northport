include Config.mk
include misc/BuildPrep.mk
include misc/Formatting.mk
include misc/HelpText.mk

KERNEL_CXX_FLAGS += -Wall -Wextra -fstack-protector-strong -fPIE -ffreestanding \
	-fno-omit-frame-pointer -fvisibility=hidden -fno-asynchronous-unwind-tables \
	-std=c++17 -fno-rtti -fno-exceptions -fsized-deallocation -fno-unwind-tables \
	-Ikernel/include -DNPK_HAS_KERNEL -Ilibs/np-syslib/include -ffunction-sections \
	-fdata-sections
KERNEL_LD_FLAGS += -zmax-page-size=0x1000 -static -pie -nostdlib --gc-sections

BUILD_DIR = .build
VENDOR_CACHE_DIR = .cache
ARCH_DIR = arch/$(CPU_ARCH)
KERNEL_TARGET = $(BUILD_DIR)/npk.elf
BUILD_TARGETS = $(KERNEL_TARGET) $(INITDISK_TARGET)
ISO_BUILD_DIR = $(BUILD_DIR)/iso
ISO_TARGET = $(BUILD_DIR)/northport-live-$(CPU_ARCH).iso

ARCH_TARGET = $(CPU_ARCH)-elf
KERNEL_VER_MAJOR = 0
KERNEL_VER_MINOR = 5
KERNEL_VER_REVISION = 0
KERNEL_CXX_FLAGS_HASH = $(strip $(shell echo $(KERNEL_CXX_FLAGS) | sha256sum | cut -d " " -f1))

LIMINE_BINARIES = $(VENDOR_CACHE_DIR)/limine

.PHONY: help
help: help-text

include misc/cross/$(CPU_ARCH).mk
include kernel/arch/$(CPU_ARCH)/Arch.mk
include libs/np-syslib/Local.mk
include kernel/Local.mk
include initdisk/Local.mk
include tests/Local.mk

all: $(ARCH_DEFAULT_TARGET)

.PHONY: clean
clean:
	$(LOUD)-rm $(BUILD_DIR) -r $(LOUD_REDIRECT)
	@printf "$(C_CYAN)[Build]$(C_RST) Removing build artefacts in: $(BUILD_DIR)\n"

.PHONY: clean-cache
clean-cache:
	$(LOUD)-rm $(VENDOR_CACHE_DIR) -rf $(LOUD_REDIRECT)
	@printf "$(C_YELLOW)[Cache]$(C_RST) Removing vendor artefacts in: $(VENDOR_CACHE_DIR)\n"

.PHONY: options
options:
	@printf "$(C_CYAN)Toolchain:$(C_RST) $(TOOLCHAIN), $(C_CYAN)Arch:$(C_RST)\
	 $(CPU_ARCH), $(C_CYAN)Boot Protocol:$(C_RST) $(KERNEL_BOOT_PROTOCOL), $(C_CYAN)Quiet:$(C_RST) $(QUIET_BUILD)\n"
	@printf "$(C_CYAN)Kernel C++ flags:$(C_RST) $(KERNEL_CXX_FLAGS)\n"
	@printf "$(C_CYAN)Kernel LD flags:$(C_RST) $(KERNEL_LD_FLAGS)\n"
ifeq ($(COUNT_TODOS), yes)
	@printf "$(C_CYAN)TODOs:$(C_RST) $(shell grep -o "TODO:" $$(find -name "*.cpp"\
	 -o -name "*.h" -o -name "*.S") | wc -l) in code, $(shell grep -o \
	 "TODO:" $$(find -name "*.tex" -o -name "*.md") | wc -l) in docs.\n"
endif

.PHONY: run
run: $(ARCH_DEFAULT_TARGET) $(QEMU_FW_FILE)
	$(LOUD)$(QEMU_BASE) $(QEMU_ACCEL)

.PHONY: run-noaccel
run-noaccel: $(ARCH_DEFAULT_TARGET) $(QEMU_FW_FILE) 
	$(LOUD)$(QEMU_BASE) $(QEMU_NO_ACCEL)

.PHONY: debug
debug: $(ARCH_DEFAULT_TARGET) $(QEMU_FW_FILE)
	$(LOUD)$(QEMU_BASE) $(QEMU_NO_ACCEL) $(QEMU_DEBUG)

.PHONY: attach
attach:
	$(LOUD)gdb $(KERNEL_TARGET) -ex "target remote :1234"

binaries: options $(BUILD_TARGETS)

$(BUILD_DIR)/limine.conf: Config.mk
	$(LOUD)$(X_CXX_BIN) $(KERNEL_CXX_FLAGS) -xc++ -E -P misc/loader-config/limine.conf -o $@

limine-iso-prep: binaries $(LIMINE_BINARIES) $(BUILD_DIR)/limine.conf
	$(LOUD)mkdir -p $(BUILD_DIR)/iso
	$(LOUD)cp $(BUILD_DIR)/limine.conf $(ISO_BUILD_DIR)
	$(LOUD)cp $(LIMINE_BINARIES)/limine-uefi-cd.bin $(ISO_BUILD_DIR)
	$(LOUD)mkdir -p $(ISO_BUILD_DIR)/EFI/BOOT
	$(LOUD)cp $(LIMINE_BINARIES)/$(UEFI_BOOT_NAME) $(ISO_BUILD_DIR)/EFI/BOOT/
	$(LOUD)cp $(INITDISK_TARGET) $(ISO_BUILD_DIR)
	$(LOUD)cp $(KERNEL_TARGET) $(ISO_BUILD_DIR)

limine-iso: limine-iso-prep
	$(LOUD)xorriso -as mkisofs --efi-boot limine-uefi-cd.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label $(ISO_BUILD_DIR) -o \
		$(ISO_TARGET) $(LOUD_REDIRECT)
	$(LOUD)rm -r $(ISO_BUILD_DIR)
	@printf "$(C_CYAN)[Build]$(C_RST) Bootable iso generated @ $(ISO_TARGET)\n"
	@printf "$(C_CYAN)[Build]$(C_RST) If qemu is installed, try it out with 'make run'!\n"

$(LIMINE_BINARIES):
	$(LOUD)-rm -rf $(LIMINE_BINARIES)
	$(LOUD)git clone https://github.com/limine-bootloader/limine.git \
		--branch=v8.x-binary --depth 1 $(LIMINE_BINARIES)
	$(LOUD)cd $(LIMINE_BINARIES); make all
	@printf "$(C_YELLOW)[Cache]$(C_RST) Limine repo cloned to local cache.\r\n"

