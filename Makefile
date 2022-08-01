#SECTION ---- Toolchain Setup and Build Options ----

#toolchain selection (valid options are gcc or clang)
TOOLCHAIN_DEFAULT = clang

#run options
BOOT_WITH_UEFI = true

#build options
INCLUDE_DEBUG_INFO = true
OPTIMIZATION_FLAGS = -O0
ENABLE_KERNEL_UBSAN = false
ENABLE_DEBUGCON_LOGGING = true
ENABLE_FRAMEBUFFER_LOGGING = false

#configuring toolchain binaries (if default dosnt work)
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
PROJ_INITDISK_DIR = initdisk
PROJ_KERNEL_DIR = kernel
PROJ_LIBS_GLUE_DIR = libs
PROJ_USERLAND_GLUE_DIR = userland

#ENDSECTION

#SECTION ---- Build Paths, Bibraries and Iso Generation ----
export INITDISK_FULL_FILEPATH = $(abspath $(PROJ_INITDISK_DIR)/$(BUILD_DIR)/northport-initdisk.tar)
export KERNEL_FILENAME = northport-kernel-$(CPU_ARCH).elf
export KERNEL_FULL_FILEPATH = $(abspath $(PROJ_KERNEL_DIR)/$(BUILD_DIR)/$(KERNEL_FILENAME))

#where built libraries and userland programs are going to be stored
export LIBS_OUTPUT_DIR = $(abspath libs/$(BUILD_DIR))
export LIBS_DIR = $(abspath libs/)
export LIB_COMMON_MK = $(abspath misc/LibCommon.mk)
export USERLAND_OUTPUT_DIR = $(abspath userland/$(BUILD_DIR))
export USERLAND_COMMON_MK = $(abspath misc/UserlandCommon.mk)

export CPU_ARCH = x86_64
export ARCH_TARGET = $(CPU_ARCH)-elf
export BUILD_DIR = build

ISO_FILENAME = iso/northport-os-$(CPU_ARCH).iso
ISO_WORKING_DIR = iso/$(BUILD_DIR)
export ISO_TARGET = $(abspath $(ISO_FILENAME))

#ENDSECTION

#SECTION ---- Internals ----
LIMINE_CFG = misc/limine.cfg
SUBMAKE_FLAGS = --no-print-directory -j $(shell nproc)

export CXX_GLOBAL_FLAGS = $(OPTIMIZATION_FLAGS) -DNORTHPORT_DEBUG_LOGGING_COLOUR_LEVELS
export CXX_KERNEL_FLAGS = 
#BuildPrep.mk populates the above flags
include BuildPrep.mk

#ENDSECTION

#SECTION ---- GNU Make Targets ----
.PHONY: all build-all iso clean run debug

all: iso

build-all: prep-build-env
	@echo "Starting northport full build ..."
	@cd $(PROJ_LIBS_GLUE_DIR); $(MAKE) all -j $(SUBMAKE_FLAGS);
	@cd $(PROJ_KERNEL_DIR); $(MAKE) all $(SUBMAKE_FLAGS);
	@cd $(PROJ_USERLAND_GLUE_DIR); $(MAKE) all -j $(SUBMAKE_FLAGS);
	@cd $(PROJ_INITDISK_DIR); $(MAKE) all $(SUBMAKE_FLAGS);
	@echo "Build done!"

iso: build-all
	@echo "Generating bootable iso ..."
	@mkdir -p $(ISO_WORKING_DIR)
	@cp $(LIMINE_CFG) $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine-cd.bin $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine-cd-efi.bin $(ISO_WORKING_DIR)
	@cp $(LIMINE_DIR)/limine.sys $(ISO_WORKING_DIR)
	@cp $(KERNEL_FULL_FILEPATH) $(ISO_WORKING_DIR)
	@cp $(INITDISK_FULL_FILEPATH) $(ISO_WORKING_DIR)
	@xorriso -as mkisofs -b limine-cd.bin -no-emul-boot -boot-load-size 4 \
		-boot-info-table --efi-boot limine-cd-efi.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label $(ISO_WORKING_DIR) -o $(ISO_TARGET)
	@$(LIMINE_DIR)/limine-deploy $(ISO_TARGET)
	@rm -r $(ISO_WORKING_DIR)
	@echo "Iso generated @ $(ISO_TARGET)."
	@echo "If qemu is installed, try it out with 'make run'."

clean:
	@echo "Cleaning build directories ..."
	@-cd $(PROJ_INITDISK_DIR); $(MAKE) clean $(SUBMAKE_FLAGS);
	@-cd $(PROJ_LIBS_GLUE_DIR); $(MAKE) clean $(SUBMAKE_FLAGS);
	@-cd $(PROJ_USERLAND_GLUE_DIR); $(MAKE) clean $(SUBMAKE_FLAGS);
	@-cd $(PROJ_KERNEL_DIR); $(MAKE) clean $(SUBMAKE_FLAGS)
	@echo  "Cleaning done!"

prep-build-env:
	@mkdir -p $(shell dirname $(ISO_TARGET))

#run and debug arent necessary for building, separation of concerns and all.
include Run.mk

#ENDSECTION
