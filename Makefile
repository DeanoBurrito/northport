# User-facing config file.
include Config.mk
# Translate Config.mk variables into build flags.
include misc/BuildPrep.mk

# Toolchain selection
ifeq ($(TOOLCHAIN), gcc)
	export X_CXX_BIN = $(abspath $(TOOLCHAIN_PREFIX)/$(ARCH_TARGET)-g++) --sysroot=$(abspath $(TOOLCHAIN_SYSROOT))
	export X_AS_BIN = $(abspath $(TOOLCHAIN_PREFIX)/$(ARCH_TARGET)-as)
	export X_LD_BIN = $(abspath $(TOOLCHAIN_PREFIX)/$(ARCH_TARGET)-ld)
	export X_AR_BIN = $(abspath $(TOOLCHAIN_PREFIX)/$(ARCH_TARGET)-ar)
	export KERNEL_AS_FLAGS = 
else ifeq ($(TOOLCHAIN), clang)
	export X_CXX_BIN = clang++ --target=$(ARCH_TARGET) --sysroot=$(abspath $(TOOLCHAIN_SYSROOT))
	export X_AS_BIN = clang --target=$(ARCH_TARGET)
	export X_LD_BIN = ld.lld
	export X_AR_BIN = llvm-ar
	export KERNEL_AS_FLAGS = -c
else
	UNKNOWN_TOOLCHAIN = true
endif

# Arch-specific flags + targets.
include misc/cross/$(CPU_ARCH)/CrossConfig.mk

# Platform-agnostic compiler and linker flags for the kernel
KERNEL_CXX_FLAGS += -Wall -Wextra -fstack-protector-strong -fno-pic -fno-pie \
	-fno-omit-frame-pointer -ffreestanding -std=c++17 -fno-rtti -fno-exceptions \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -Iinclude
KERNEL_LD_FLAGS += -L$(LIBS_OUTPUT_DIR) -lknp-syslib \
	-nostdlib -zmax-page-size=0x1000 -static --no-dynamic-linker

export KERNEL_CXX_FLAGS
export KERNEL_LD_FLAGS
export BUILD_DIR = build
export ARCH_TARGET = $(CPU_ARCH)-elf

PROJ_DIR_INITDISK = initdisk
PROJ_DIR_KERNEL = kernel
PROJ_DIR_LIBS = libs
PROJ_DIR_DOCS = docs

export INITDISK_FULL_FILEPATH = $(abspath $(PROJ_DIR_INITDISK)/$(BUILD_DIR)/northport-initdisk.tar)
export KERNEL_FILENAME = northport-kernel-$(CPU_ARCH).elf
export KERNEL_FULL_FILEPATH = $(abspath $(PROJ_DIR_KERNEL)/$(BUILD_DIR)/$(KERNEL_FILENAME))
export LIBS_OUTPUT_DIR = $(abspath $(PROJ_DIR_LIBS)/$(BUILD_DIR))
export LIB_COMMON_MK = $(abspath misc/LibCommon.mk)
export SUBMAKE_FLAGS = --no-print-directory -j $(shell nproc)

ISO_TEMP_DIR = iso/build
ISO_FILENAME = iso/northport-$(CPU_ARCH).iso
export ISO_FULL_FILEPATH = $(abspath $(ISO_FILENAME))

.PHONY: all
all: $(ARCH_DEFAULT_TARGET)

.PHONY: binaries
binaries:
ifeq ($(UNKNOWN_TOOLCHAIN), true)
	$(error "Unknown toolchain: $(TOOLCHAIN), build aborted.")
endif
	@echo "Toolchain: $(TOOLCHAIN), Arch: $(CPU_ARCH), Quiet: $(QUIET_BUILD)"
	@echo "Sysroot: $(abspath $(TOOLCHAIN_SYSROOT))"
	@echo "Kernel C++ flags: $(KERNEL_CXX_FLAGS)"
	@echo "Kernel LD flags: $(KERNEL_LD_FLAGS)"
	@echo
	$(LOUD)cd $(PROJ_DIR_LIBS)/np-syslib; $(MAKE) all-kernel $(SUBMAKE_FLAGS)
	$(LOUD)cd $(PROJ_DIR_LIBS); $(MAKE) all $(SUBMAKE_FLAGS)
	$(LOUD)cd $(PROJ_DIR_KERNEL); $(MAKE) all $(SUBMAKE_FLAGS)
	$(LOUD)cd $(PROJ_DIR_INITDISK); $(MAKE) all $(SUBMAKE_FLAGS)

.PHONY: iso
iso: binaries
	$(LOUD)mkdir -p $(ISO_TEMP_DIR)
	$(LOUD)cp misc/cross/$(CPU_ARCH)/limine.cfg $(ISO_TEMP_DIR)
	$(LOUD)cp $(LIMINE_DIR)/limine-cd.bin $(ISO_TEMP_DIR)
	$(LOUD)cp $(LIMINE_DIR)/limine-cd-efi.bin $(ISO_TEMP_DIR)
	$(LOUD)cp $(LIMINE_DIR)/limine.sys $(ISO_TEMP_DIR)
	$(LOUD)cp $(INITDISK_FULL_FILEPATH) $(ISO_TEMP_DIR)
	$(LOUD)cp $(KERNEL_FULL_FILEPATH) $(ISO_TEMP_DIR)
	$(LOUD)xorriso -as mkisofs -b limine-cd.bin -no-emul-boot -boot-load-size 4 \
		-boot-info-table --efi-boot limine-cd-efi.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label $(ISO_TEMP_DIR) -o $(ISO_FULL_FILEPATH) $(LOUD_REDIRECT)
	$(LOUD)$(LIMINE_DIR)/limine-deploy $(ISO_FULL_FILEPATH) $(LOUD_REDIRECT)
	$(LOUD)rm -r $(ISO_TEMP_DIR)
	@echo "Bootable iso generated @ $(ISO_FULL_FILEPATH)"
	@echo "If qemu is installed, try it out with 'make run'!"

.PHONY: clean
clean:
	$(LOUD)-cd $(PROJ_DIR_LIBS)/np-syslib; $(MAKE) clean-kernel $(SUBMAKE_FLAGS)
	$(LOUD)-cd $(PROJ_DIR_LIBS); $(MAKE) clean $(SUBMAKE_FLAGS)
	$(LOUD)-cd $(PROJ_DIR_KERNEL); $(MAKE) clean $(SUBMAKE_FLAGS)

.PHONY: docs
docs:
	$(LOUD)cd $(PROJ_DIR_DOCS); $(MAKE) all $(SUBMAKE_FLAGS)

# Optional, run/debug/attach targets and qemu interactions.
include misc/RunDebug.mk
