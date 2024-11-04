# User-facing config file.
include Config.mk
# Translate Config.mk variables into build flags.
include misc/BuildPrep.mk
# Things to make the output nicer on the eyes
include misc/Formatting.mk
# Help text, provides `make help` target which runs by default
include misc/HelpText.mk

# Toolchain selection
ifeq ($(TOOLCHAIN), gcc)
	export X_CXX_BIN = $(TOOLCHAIN_PREFIX)$(ARCH_TARGET)-g++
	export X_AS_BIN = $(TOOLCHAIN_PREFIX)$(ARCH_TARGET)-as
	export X_LD_BIN = $(TOOLCHAIN_PREFIX)$(ARCH_TARGET)-ld
	export X_AR_BIN = $(TOOLCHAIN_PREFIX)$(ARCH_TARGET)-ar
	export KERNEL_AS_FLAGS = 
else ifeq ($(TOOLCHAIN), clang)
	export X_CXX_BIN = clang++ --target=$(ARCH_TARGET)
	export X_AS_BIN = clang --target=$(ARCH_TARGET)
	export X_LD_BIN = ld.lld
	export X_AR_BIN = llvm-ar
	export KERNEL_AS_FLAGS = -c
else
$(error "Unknown toolchain: $(TOOLCHAIN), build aborted.")
endif

# Setting up paths for development files
ifeq ($(USE_DEVEL_CACHE), yes)
	include misc/DevelCache.mk
else
	export LIMINE_DIR
	export OVMF_FILE
endif

# Arch-specific flags + targets.
include misc/cross/$(CPU_ARCH)/CrossConfig.mk

# Platform-agnostic compiler and linker flags for the kernel and drivers
export KERNEL_CXX_FLAGS += -Wall -Wextra -fstack-protector-strong -fPIE \
	-fno-omit-frame-pointer -ffreestanding -fvisibility=hidden \
	-std=c++17 -fno-rtti -fno-exceptions -fsized-deallocation -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -Iinclude -DNP_KERNEL \
	-I$(PROJ_ROOT_DIR)/libs/np-syslib/include
export KERNEL_LD_FLAGS += -L$(LIBS_OUTPUT_DIR) -lknp-syslib \
	-nostdlib -zmax-page-size=0x1000 -static -pie
export SYSLIB_CXX_FLAGS += -fvisibility=default -fPIC
export DRIVER_C_FLAGS += -Wall -Wextra -std=c17 -fno-unwind-tables -fno-asynchronous-unwind-tables \
	-ffreestanding -fPIC -fvisibility=hidden -fno-omit-frame-pointer \
	-I$(PROJ_ROOT_DIR)/kernel/include -I$(PROJ_ROOT_DIR)/libs/np-syslib/include
export DRIVER_CXX_FLAGS += -Wall -Wextra -std=c++17 -fno-rtti -fno-exceptions -fno-unwind-tables \
	-fno-asynchronous-unwind-tables -ffreestanding -fPIC -fvisibility=hidden -fno-omit-frame-pointer \
	-fsized-deallocation \
	-I$(PROJ_ROOT_DIR)/kernel/include -I$(PROJ_ROOT_DIR)/libs/np-syslib/include \
	-I$(PROJ_ROOT_DIR)/libs/np-driverlib/include
export DRIVER_LD_FLAGS += -nostdlib -shared -znorelro \
	-L$(LIBS_OUTPUT_DIR) --exclude-libs ALL -lknp-syslib -lnp-driverlib \
	-T$(PROJ_ROOT_DIR)/misc/cross/$(CPU_ARCH)/Driver.lds

export BUILD_DIR = build
export CPU_ARCH
export TOOLCHAIN
export ARCH_TARGET = $(CPU_ARCH)-elf
export KERNEL_BOOT_PROTOCOL

PROJ_DIR_INITDISK = initdisk
PROJ_DIR_KERNEL = kernel
PROJ_DIR_LIBS = libs
PROJ_DIR_DRIVERS = drivers
PROJ_DIR_DOCS = docs/manual

export INITDISK_FULL_FILEPATH = $(abspath $(PROJ_DIR_INITDISK)/$(BUILD_DIR)/northport-initdisk.tar)
export KERNEL_FILENAME = northport-kernel-$(CPU_ARCH).elf
export KERNEL_FULL_FILEPATH = $(abspath $(PROJ_DIR_KERNEL)/$(BUILD_DIR)/$(KERNEL_FILENAME))
export LIBS_OUTPUT_DIR = $(abspath $(PROJ_DIR_LIBS)/$(BUILD_DIR))
export DRIVERS_OUTPUT_DIR = $(abspath $(PROJ_DIR_DRIVERS)/$(BUILD_DIR))
export SUBMAKE_FLAGS = --no-print-directory -j $(shell nproc)
export PROJ_ROOT_DIR = $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))

ISO_TEMP_DIR = iso/build
ISO_FILENAME = iso/northport-$(CPU_ARCH).iso
export ISO_FULL_FILEPATH = $(abspath $(ISO_FILENAME))

.PHONY: help
help: help-text

.PHONY: all
all: $(ARCH_DEFAULT_TARGET)

.PHONY: options
options:
	@printf "$(C_CYAN)Toolchain:$(C_RST) $(TOOLCHAIN), $(C_CYAN)Arch:$(C_RST)\
	 $(CPU_ARCH), $(C_CYAN)Boot Protocol:$(C_RST) $(KERNEL_BOOT_PROTOCOL), $(C_CYAN)Quiet:$(C_RST) $(QUIET_BUILD)\r\n"
	@printf "$(C_CYAN)Kernel C++ flags:$(C_RST) $(KERNEL_CXX_FLAGS)\r\n"
	@printf "$(C_CYAN)Kernel LD flags:$(C_RST) $(KERNEL_LD_FLAGS)\r\n"

.PHONY: binaries
binaries: options
	$(LOUD)cd $(PROJ_DIR_LIBS)/np-syslib; $(MAKE) all-kernel $(SUBMAKE_FLAGS)
	$(LOUD)cd $(PROJ_DIR_LIBS); $(MAKE) all $(SUBMAKE_FLAGS)
	$(LOUD)cd $(PROJ_DIR_KERNEL); $(MAKE) all $(SUBMAKE_FLAGS)
	$(LOUD)cd $(PROJ_DIR_DRIVERS); $(MAKE) all $(SUBMAKE_FLAGS)
	$(LOUD)cd $(PROJ_DIR_INITDISK); $(MAKE) all $(SUBMAKE_FLAGS)
ifeq ($(COUNT_TODOS), yes)
	@printf "$(C_CYAN)[Build]$(C_RST) $(shell grep -o "TODO:" $$(find -name "*.cpp"\
	 -o -name "*.h" -o -name "*.S") | wc -l) TODOs found in code, $(shell grep -o \
	 "TODO:" $$(find -name "*.tex" -o -name "*.md") | wc -l) TODOs found in docs.\r\n"
endif

#Builds a bootable (via uefi) iso, suitable for any platforms that support UEFI.
.PHONY: iso
iso: binaries $(LIMINE_DIR)
	$(LOUD)mkdir -p $(ISO_TEMP_DIR)
	$(LOUD)cp misc/cross/$(CPU_ARCH)/limine.cfg $(ISO_TEMP_DIR)
	$(LOUD)cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_TEMP_DIR)
	$(LOUD)mkdir -p $(ISO_TEMP_DIR)/EFI/BOOT
	$(LOUD)cp $(LIMINE_DIR)/$(UEFI_BOOT_NAME) $(ISO_TEMP_DIR)/EFI/BOOT/
	$(LOUD)cp $(INITDISK_FULL_FILEPATH) $(ISO_TEMP_DIR)
	$(LOUD)cp $(KERNEL_FULL_FILEPATH) $(ISO_TEMP_DIR)
	$(LOUD)xorriso -as mkisofs --efi-boot limine-uefi-cd.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label $(ISO_TEMP_DIR) -o \
		$(ISO_FULL_FILEPATH) $(LOUD_REDIRECT)
	$(LOUD)rm -r $(ISO_TEMP_DIR)
	@printf "$(C_CYAN)[Build]$(C_RST) Bootable iso generated @ $(ISO_FULL_FILEPATH)\r\n"
	@printf "$(C_CYAN)[Build]$(C_RST) If qemu is installed, try it out with 'make run'!\r\n"

#Builds an iso that can be booted via bios or uefi, for x86_64 only.
.PHONY: iso-hybrid
iso-hybrid: binaries $(LIMINE_DIR)
	$(LOUD)mkdir -p $(ISO_TEMP_DIR)
	$(LOUD)cp misc/cross/$(CPU_ARCH)/limine.cfg $(ISO_TEMP_DIR)
	$(LOUD)cp $(LIMINE_DIR)/limine-uefi-cd.bin $(ISO_TEMP_DIR)
	$(LOUD)cp $(LIMINE_DIR)/limine-bios-cd.bin $(ISO_TEMP_DIR)
	$(LOUD)cp $(LIMINE_DIR)/limine-bios.sys $(ISO_TEMP_DIR)
	$(LOUD)mkdir -p $(ISO_TEMP_DIR)/EFI/BOOT
	$(LOUD)cp $(LIMINE_DIR)/$(UEFI_BOOT_NAME) $(ISO_TEMP_DIR)/EFI/BOOT/
	$(LOUD)cp $(INITDISK_FULL_FILEPATH) $(ISO_TEMP_DIR)
	$(LOUD)cp $(KERNEL_FULL_FILEPATH) $(ISO_TEMP_DIR)
	$(LOUD)xorriso -as mkisofs -b limine-bios-cd.bin -no-emul-boot -boot-load-size 4 \
		-boot-info-table --efi-boot limine-uefi-cd.bin -efi-boot-part \
		--efi-boot-image --protective-msdos-label $(ISO_TEMP_DIR) -o \
		$(ISO_FULL_FILEPATH) $(LOUD_REDIRECT)
	$(LOUD)$(LIMINE_DIR)/limine bios-install $(ISO_FULL_FILEPATH) $(LOUD_REDIRECT)
	$(LOUD)rm -r $(ISO_TEMP_DIR)
	@printf "$(C_CYAN)[Build]$(C_RST) Bootable iso (hybrid uefi + bios) generated @ $(ISO_FULL_FILEPATH)\r\n"
	@printf "$(C_CYAN)[Build]$(C_RST) If qemu is installed, try it out with 'make run'!\r\n"

.PHONY: clean
clean:
	$(LOUD)-cd $(PROJ_DIR_LIBS)/np-syslib; $(MAKE) clean-kernel $(SUBMAKE_FLAGS)
	$(LOUD)-cd $(PROJ_DIR_LIBS); $(MAKE) clean $(SUBMAKE_FLAGS)
	$(LOUD)-cd $(PROJ_DIR_KERNEL); $(MAKE) clean $(SUBMAKE_FLAGS)
	$(LOUD)-cd $(PROJ_DIR_DRIVERS); $(MAKE) clean $(SUBMAKE_FLAGS)
	$(LOUD)-cd $(PROJ_DIR_INITDISK); $(MAKE) clean $(SUBMAKE_FLAGS)

.PHONY: docs
docs:
	$(LOUD)cd $(PROJ_DIR_DOCS); $(MAKE) all $(SUBMAKE_FLAGS)

.PHONY: docs-clean
docs-clean:
	$(LOUD)cd $(PROJ_DIR_DOCS); $(MAKE) clean $(SUBMAKE_FLAGS)

# Optional, run/debug/attach targets and qemu interactions.
include misc/RunDebug.mk
