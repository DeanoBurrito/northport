KERNEL_CXX_FLAGS += -march=68040
DRIVER_CXX_FLAGS += -march=68040
DRIVE_C_FLAGS += -march=68040
SYSLIB_CXX_FLAGS += -march=68040

KERNEL_LD_FLAGS +=

ARCH_DEFAULT_TARGET = binaries

QEMU_BASE = qemu-system-m68k -machine virt -cpu m68040 -m 256M \
	-serial mon:stdio -kernel $(KERNEL_FULL_FILEPATH)
QEMU_NO_KVM =
QEMU_KVM += $(QEMU_NO_KVM)
QEMU_DEBUG = -s -S -no-reboot -no-shutdown
