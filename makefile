#toolchain selection
TOOLCHAIN_DIR = ../cross-tools
CXX_BIN = $(ARCH_TARGET)-g++
ASM_BIN = $(ARCH_TARGET)-as
LD_BIN = $(ARCH_TARGET)-ld
AR_BIN = $(ARCH_TARGET)-ar
export TOOLS_DIR = $(abspath $(TOOLCHAIN_DIR))
export CXX = $(abspath $(TOOLCHAIN_DIR)/bin/$(CXX_BIN))
export ASM = $(abspath $(TOOLCHAIN_DIR)/bin/$(ASM_BIN))
export LD = $(abspath $(TOOLCHAIN_DIR)/bin/$(LD_BIN))
export AR = $(abspath $(TOOLCHAIN_DIR)/bin/$(AR_BIN))
LIMINE_DIR = $(TOOLCHAIN_DIR)/limine

#top-level projects
PROJ_BOOTLOADER_ROOT_DIR = boot
PROJ_INITDISK_DIR = initdisk
PROJ_KERNEL_DIR = kernel
PROJ_SYSLIB_DIR = syslib

SYSLIB_FILENAME = libsyslib.a
export SYSLIB_INCLUDE_DIR = $(abspath $(PROJ_SYSLIB_DIR)/include)
export SYSLIB_FULL_FILEPATH = $(abspath $(PROJ_SYSLIB_DIR)/$(BUILD_DIR)/$(SYSLIB_FILENAME))

INITDISK_FILENAME = initdisk.tar
export INITDISK_FULL_FILEPATH = $(abspath $(PROJ_INITDISK_DIR)/$(BUILD_DIR)/$(INITDISK_FILENAME))

export KERNEL_FILENAME = kernel-$(CPU_ARCH).elf
export KERNEL_FULL_FILEPATH = $(abspath $(PROJ_KERNEL_DIR)/$(BUILD_DIR)/$(KERNEL_FILENAME))

#build config
INCLUDE_DEBUG_INFO = 1
OPTIMIZATION_FLAGS = -O0
export CPU_ARCH = x86_64
export ARCH_TARGET = $(CPU_ARCH)-elf
export BUILD_DIR = build

ISO_FILENAME = iso/northport-os-$(CPU_ARCH).iso
ISO_WORKING_DIR = iso/$(BUILD_DIR)
export ISO_TARGET = $(abspath $(ISO_FILENAME))
LIMINE_CFG = misc/limine.cfg

ifeq ($(INCLUDE_DEBUG_INFO), 1)
	export CXX_DEBUG_FLAGS = -g
else
	export CXX_DEBUG_FLAGS =
endif
export CXX_GLOBAL_FLAGS = $(OPTIMIZATION_FLAGS) $(CXX_DEBUG_FLAGS)

include run.mk

.PHONY: all build-all iso clean run debug create-toolchain validate-toolchain

all: iso

build-all: prep-build-env
	@echo "Starting northport full build ..."
	@cd $(PROJ_SYSLIB_DIR); make all;
	@cd $(PROJ_INITDISK_DIR); make all;
	@cd $(PROJ_KERNEL_DIR); make all;
	@echo "Build done!"

iso: build-all
	@echo "Creating bootable iso ..."
	@mkdir -p $(ISO_WORKING_DIR)
	@cp $(LIMINE_CFG) $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine-cd.bin $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine.sys $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine-eltorito-efi.bin $(ISO_WORKING_DIR)
	@cp $(KERNEL_FULL_FILEPATH) $(ISO_WORKING_DIR)
	@xorriso -as mkisofs -b limine-cd.bin -no-emul-boot -boot-load-size 4 \
		-boot-info-table --efi-boot limine-eltorito-efi.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label $(ISO_WORKING_DIR) -o $(ISO_TARGET)
	@$(LIMINE_DIR)/limine-install $(ISO_TARGET)
	@rm -r $(ISO_WORKING_DIR)
	@echo "Done! Bootable iso located at $(ISO_TARGET)."
	@echo "If qemu is installed, try it out with 'make run'."

clean:
	@echo "Cleaning build directories ..."
	@-cd $(PROJ_SYSLIB_DIR); make clean;
	@-cd $(PROJ_INITDISK_DIR); make clean;
	@-cd $(PROJ_KERNEL_DIR); make clean;
	@-rm -r iso
	@echo "Cleaning done!"

#run and debug arent necessary for building, and are from the run.mk file

create-toolchain:
	@scripts/create-toolchain.sh

validate-toolchain:
	@echo "Checking for C++ compiler"
	@which $(CXX) || echo "Could not locate architecture specific C++ compiler."
	@echo "Checking for assembler"
	@which $(ASM) || echo "Could not locate architecture specific assembler."
	@echo "Checking for linker"
	@which $(LD) || echo "Could not locate architecture specific linker."
	@echo "Checking for limine install"
	@if [ -d "$(TOOLS_DIR)/limine" ] ; then echo "Found limine."; else echo "Unable to locate limine."; fi

prep-build-env:
	@mkdir -p $(shell dirname $(ISO_TARGET))
