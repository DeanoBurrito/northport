KERNEL_CXX_FLAGS += -march=68040
DRIVER_CXX_FLAGS += -march=68040
DRIVE_C_FLAGS += -march=68040
SYSLIB_CXX_FLAGS += -march=68040

KERNEL_LD_FLAGS +=

ARCH_DEFAULT_TARGET = m68k-loader

QEMU_BASE = qemu-system-m68k -machine virt -cpu m68040 -m 256M \
	-serial mon:stdio -kernel $(LOADER_FULL_FILEPATH)
QEMU_NO_KVM =
QEMU_KVM += $(QEMU_NO_KVM)
QEMU_DEBUG = -s -S -no-reboot -no-shutdown

LOADER_KERNEL_FILEPATH = kernel/arch/m68k/loader/build
export LOADER_FULL_FILEPATH = $(PROJ_ROOT_DIR)/kernel/$(BUILD_DIR)/northport-loader-m68k.elf

m68k-loader: binaries
	$(LOUD)-rm kernel/$(BUILD_DIR)/kernel.elf
	$(LOUD)mkdir -p $(LOADER_KERNEL_FILEPATH)
	$(LOUD)cp $(KERNEL_FULL_FILEPATH) $(LOADER_KERNEL_FILEPATH)/kernel.elf
	$(LOUD)cd kernel/arch/m68k/loader/; $(MAKE) all $(SUBMAKE_FLAGS)
	@printf "$(C_CYAN)[Build]$(C_RST) M68K loader built @ $(LOADER_FULL_FILEPATH)\r\n"

.PHONY: attach-loader
attach-loader:
	$(LOUD)gdb $(LOADER_FULL_FILEPATH) -ex "target remote :1234"

