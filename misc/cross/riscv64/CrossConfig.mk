KERNEL_CXX_FLAGS += -mcmodel=medany -DNP_INCLUDE_LIMINE_BOOTSTRAP

KERNEL_LD_FLAGS +=

ARCH_DEFAULT_TARGET = binaries

QEMU_BASE = qemu-system-$(CPU_ARCH) -machine virt -cpu rv64 -smp 4 -m 512M \
	-serial mon:stdio -kernel $(KERNEL_FULL_FILEPATH) -device virtio-gpu-device
QEMU_KVM = 
QEMU_NO_KVM =
QEMU_DEBUG = -s -S -no-reboot -no-shutdown
QEMU_UEFI =
