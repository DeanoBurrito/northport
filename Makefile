#toolchain selection (valid options are gcc or clang)
TOOLCHAIN_DEFAULT = clang

ifeq ($(TOOLCHAIN_DEFAULT), gcc)
	TOOLCHAIN_DIR = ../cross-tools
	CXX_BIN = $(ARCH_TARGET)-g++
	ASM_BIN = $(ARCH_TARGET)-as
	LD_BIN = $(ARCH_TARGET)-ld
	AR_BIN = $(ARCH_TARGET)-ar
	export CXX = $(abspath $(TOOLCHAIN_DIR)/bin/$(CXX_BIN))
	export ASM = $(abspath $(TOOLCHAIN_DIR)/bin/$(ASM_BIN))
	export LD = $(abspath $(TOOLCHAIN_DIR)/bin/$(LD_BIN))
	export AR = $(abspath $(TOOLCHAIN_DIR)/bin/$(AR_BIN))
	export GLOBAL_ASM_FLAGS = --64
	LIMINE_DIR = $(TOOLCHAIN_DIR)/limine
else ifeq ($(TOOLCHAIN_DEFAULT), clang)
	export CXX = clang++ --target=$(ARCH_TARGET)
	export ASM = clang --target=$(ARCH_TARGET)
	export LD = ld.lld
	export AR = llvm-ar
	export GLOBAL_ASM_FLAGS = -c
	LIMINE_DIR = ../cross-tools/limine
endif

#top-level projects
PROJ_BOOTLOADER_ROOT_DIR = boot
PROJ_INITDISK_DIR = initdisk
PROJ_KERNEL_DIR = kernel
PROJ_SYSLIB_DIR = syslib

SYSLIB_FILENAME = libsyslib.a
export SYSLIB_INCLUDE_DIR = $(abspath $(PROJ_SYSLIB_DIR)/include)
export SYSLIB_FULL_FILEPATH = $(abspath $(PROJ_SYSLIB_DIR)/$(BUILD_DIR)/$(SYSLIB_FILENAME))

INITDISK_FILENAME = northport-initdisk.tar
export INITDISK_FULL_FILEPATH = $(abspath $(PROJ_INITDISK_DIR)/$(BUILD_DIR)/$(INITDISK_FILENAME))

export KERNEL_FILENAME = northport-kernel-$(CPU_ARCH).elf
export KERNEL_FULL_FILEPATH = $(abspath $(PROJ_KERNEL_DIR)/$(BUILD_DIR)/$(KERNEL_FILENAME))

#build config
INCLUDE_DEBUG_INFO = true
OPTIMIZATION_FLAGS = -O0
DISABLE_SMP = false
ENABLE_DEBUGCON_LOGGING = true
ENABLE_FRAMEBUFFER_LOGGING = false

export CPU_ARCH = x86_64
export ARCH_TARGET = $(CPU_ARCH)-elf
export BUILD_DIR = build

ISO_FILENAME = iso/northport-os-$(CPU_ARCH).iso
ISO_WORKING_DIR = iso/$(BUILD_DIR)
export ISO_TARGET = $(abspath $(ISO_FILENAME))
LIMINE_CFG = misc/limine.cfg

SUBMAKE_FLAGS = --no-print-directory

#these are modified by build prep (based on options above)
export CXX_DEBUG_FLAGS = 
include BuildPrep.mk
include Run.mk
export CXX_GLOBAL_FLAGS = $(OPTIMIZATION_FLAGS) $(CXX_DEBUG_FLAGS)

.PHONY: all build-all iso clean run debug create-toolchain validate-toolchain

all: iso

build-all: prep-build-env
	@echo "Starting northport full build ..."
	@cd $(PROJ_SYSLIB_DIR); make all $(SUBMAKE_FLAGS);
	@cd $(PROJ_INITDISK_DIR); make all $(SUBMAKE_FLAGS);
	@cd $(PROJ_KERNEL_DIR); make all $(SUBMAKE_FLAGS);
	@echo "Build done!"

iso: build-all
	@echo "Creating bootable iso ..."
	@mkdir -p $(ISO_WORKING_DIR)
	@cp $(LIMINE_CFG) $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine-cd.bin $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine.sys $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine-eltorito-efi.bin $(ISO_WORKING_DIR)
	@cp $(KERNEL_FULL_FILEPATH) $(ISO_WORKING_DIR)
	@cp $(INITDISK_FULL_FILEPATH) $(ISO_WORKING_DIR)
	@xorriso -as mkisofs -b limine-cd.bin -no-emul-boot -boot-load-size 4 \
		-boot-info-table --efi-boot limine-eltorito-efi.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label $(ISO_WORKING_DIR) -o $(ISO_TARGET)
	@$(LIMINE_DIR)/limine-install $(ISO_TARGET)
	@rm -r $(ISO_WORKING_DIR)
	@echo "Done! Bootable iso located at $(ISO_TARGET)."
	@echo "If qemu is installed, try it out with 'make run'."

clean:
	@echo "Cleaning build directories ..."
	@-cd $(PROJ_SYSLIB_DIR); make clean $(SUBMAKE_FLAGS);
	@-cd $(PROJ_INITDISK_DIR); make clean $(SUBMAKE_FLAGS);
	@-cd $(PROJ_KERNEL_DIR); make clean $(SUBMAKE_FLAGS);
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
	@if [ -d "$(LIMINE_DIR)" ] ; then echo "Found limine."; else echo "Unable to locate limine."; fi

prep-build-env:
	@mkdir -p $(shell dirname $(ISO_TARGET))
